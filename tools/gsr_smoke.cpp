#include <iostream>
#include <stdexcept>
#include <string>

#include "gsr/demo_domain.h"
#include "gsr/replay_loader.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string fixture =
      argc > 1 ? argv[1] : "examples/connected_call.gsr";
  try {
    auto replay = gsr::ReplayContext::LoadFile(fixture);

    Require(call_demo::g_service_ready, "scalar global was not restored");
    Require(call_demo::g_call != nullptr, "g_call was not bound");
    Require(call_demo::g_active_leg != nullptr, "g_active_leg was not bound");
    Require(call_demo::g_call->active_leg == call_demo::g_active_leg,
            "global alias was not preserved");
    Require(call_demo::g_call->peer->owner == call_demo::g_call,
            "peer back-pointer cycle was not preserved");
    Require(call_demo::g_active_leg->sibling->sibling ==
                call_demo::g_active_leg,
            "media-leg cycle/interior pointers were not preserved");
    Require(replay->expected_calls().size() == 1,
            "expected call transcript was not loaded");

    call_demo::RecordingPlatformApi platform;
    Require(call_demo::HandleRemoteHold(platform) ==
                call_demo::HandlerResult::kOk,
            "handler did not accept replayed state");
    Require(platform.stopped_timer_tokens.size() == 1 &&
                platform.stopped_timer_tokens.front() ==
                    replay->expected_calls().front().argument,
            "platform interaction did not match transcript");

    std::string mismatch;
    Require(replay->OracleMatches(&mismatch), "oracle mismatch: " + mismatch);
    std::cout << "PASS: replayed " << replay->metadata().at("scenario")
              << ", preserved aliases/cycles, matched platform transcript and oracle\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
