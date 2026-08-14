#pragma once

#include <cstdint>
#include <vector>

namespace call_demo {

enum class CallState : std::int32_t {
  kIdle = 0,
  kConnected = 1,
  kRemoteHold = 2,
};

struct CallSession;

struct Peer {
  char uri[64]{};
  CallSession* owner = nullptr;
};

struct MediaLeg {
  std::int32_t id = 0;
  bool active = false;
  CallSession* owner = nullptr;
  MediaLeg* sibling = nullptr;
};

// A process-local/platform resource. Its address is never serialized; an adapter
// reconstructs it from the stable token stored in the snapshot.
struct TimerHandle {
  std::int32_t token = 0;
};

struct CallSession {
  std::int32_t call_id = 0;
  CallState state = CallState::kIdle;
  Peer* peer = nullptr;
  MediaLeg* active_leg = nullptr;
  TimerHandle* refresh_timer = nullptr;
};

// Demo equivalents of target-module globals discovered from compile_commands.json.
extern bool g_service_ready;
extern CallSession* g_call;
extern MediaLeg* g_active_leg;

class PlatformApi {
 public:
  virtual ~PlatformApi() = default;
  virtual bool StopTimer(std::int32_t token) = 0;
};

enum class HandlerResult {
  kOk,
  kMissingState,
  kInvalidState,
  kPlatformError,
};

HandlerResult HandleRemoteHold(PlatformApi& platform);
void ResetGlobals();

class RecordingPlatformApi final : public PlatformApi {
 public:
  bool StopTimer(std::int32_t token) override;

  bool stop_timer_result = true;
  std::vector<std::int32_t> stopped_timer_tokens;
};

}  // namespace call_demo
