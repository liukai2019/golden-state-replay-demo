#include "gsr/demo_domain.h"

namespace call_demo {

bool g_service_ready = false;
CallSession* g_call = nullptr;
MediaLeg* g_active_leg = nullptr;

HandlerResult HandleRemoteHold(PlatformApi& platform) {
  if (!g_service_ready || g_call == nullptr || g_active_leg == nullptr) {
    return HandlerResult::kMissingState;
  }
  if (g_call->state != CallState::kConnected) {
    return HandlerResult::kInvalidState;
  }
  if (g_call->active_leg != g_active_leg || g_active_leg->owner != g_call) {
    return HandlerResult::kMissingState;
  }
  if (g_call->refresh_timer != nullptr &&
      !platform.StopTimer(g_call->refresh_timer->token)) {
    return HandlerResult::kPlatformError;
  }

  g_active_leg->active = false;
  g_call->state = CallState::kRemoteHold;
  return HandlerResult::kOk;
}

void ResetGlobals() {
  g_service_ready = false;
  g_call = nullptr;
  g_active_leg = nullptr;
}

bool RecordingPlatformApi::StopTimer(std::int32_t token) {
  stopped_timer_tokens.push_back(token);
  return stop_timer_result;
}

}  // namespace call_demo
