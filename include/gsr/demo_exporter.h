#pragma once

#include <cstddef>
#include <iosfwd>

#include "gsr/demo_domain.h"

namespace gsr {

// In production this allocation view is supplied by instrumented allocators.
// It is explicit here so the demo can preserve interior pointers into an array.
struct DemoCaptureView {
  call_demo::CallSession* call = nullptr;
  call_demo::Peer* peer = nullptr;
  call_demo::MediaLeg* legs = nullptr;
  std::size_t leg_count = 0;
};

void ExportDemoState(std::ostream& output, const DemoCaptureView& view);

}  // namespace gsr
