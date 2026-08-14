#include "gsr/demo_exporter.h"

#include <iomanip>
#include <ostream>
#include <stdexcept>
#include <string>

namespace gsr {
namespace {

std::size_t LegIndex(const DemoCaptureView& view,
                     const call_demo::MediaLeg* leg) {
  if (leg == nullptr) {
    throw std::runtime_error("demo exporter does not expect a null media leg");
  }
  if (leg < view.legs || leg >= view.legs + view.leg_count) {
    throw std::runtime_error("media-leg pointer is outside the registered allocation");
  }
  return static_cast<std::size_t>(leg - view.legs);
}

void Edge(std::ostream& out, const std::string& owner, std::size_t owner_index,
          const std::string& field, const std::string& target,
          std::size_t target_index) {
  out << "edge " << owner << ' ' << owner_index << ' ' << field << ' '
      << target << ' ' << target_index << '\n';
}

}  // namespace

void ExportDemoState(std::ostream& out, const DemoCaptureView& view) {
  if (view.call == nullptr || view.peer == nullptr || view.legs == nullptr ||
      view.leg_count == 0 || view.call->refresh_timer == nullptr) {
    throw std::runtime_error("incomplete demo capture view");
  }

  out << "GSR/1\n"
      << "meta scenario " << std::quoted("connected_call") << "\n"
      << "meta capture_point "
      << std::quoted("CALL_CONNECTED_QUIESCENT") << "\n"
      << "meta schema_hash " << std::quoted("demo-schema-v1") << "\n"
      << "object call CallSession 1\n"
      << "object peer Peer 1\n"
      << "object legs MediaLeg " << view.leg_count << "\n"
      << "external refresh_timer TimerHandle "
      << (view.call->refresh_timer == nullptr ? 0 : view.call->refresh_timer->token)
      << "\n"
      << "global g_service_ready bool "
      << (call_demo::g_service_ready ? "true" : "false") << "\n"
      << "scalar call 0 call_id i32 " << view.call->call_id << "\n"
      << "scalar call 0 state enum "
      << static_cast<std::int32_t>(view.call->state) << "\n"
      << "string peer 0 uri " << std::quoted(view.peer->uri) << "\n";

  for (std::size_t i = 0; i < view.leg_count; ++i) {
    out << "scalar legs " << i << " id i32 " << view.legs[i].id << "\n"
        << "scalar legs " << i << " active bool "
        << (view.legs[i].active ? "true" : "false") << "\n";
  }

  Edge(out, "call", 0, "peer", "peer", 0);
  Edge(out, "call", 0, "active_leg", "legs",
       LegIndex(view, view.call->active_leg));
  Edge(out, "call", 0, "refresh_timer", "refresh_timer", 0);
  Edge(out, "peer", 0, "owner", "call", 0);
  for (std::size_t i = 0; i < view.leg_count; ++i) {
    Edge(out, "legs", i, "owner", "call", 0);
    Edge(out, "legs", i, "sibling", "legs",
         LegIndex(view, view.legs[i].sibling));
  }
  Edge(out, "root", 0, "g_call", "call", 0);
  Edge(out, "root", 0, "g_active_leg", "legs",
       LegIndex(view, call_demo::g_active_leg));

  out << "expect_call stop_timer " << view.call->refresh_timer->token << "\n"
      << "expect call 0 state enum "
      << static_cast<std::int32_t>(call_demo::CallState::kRemoteHold) << "\n"
      << "expect legs " << LegIndex(view, view.call->active_leg)
      << " active bool false\n";
}

}  // namespace gsr
