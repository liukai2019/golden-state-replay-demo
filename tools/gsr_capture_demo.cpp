#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "gsr/demo_domain.h"
#include "gsr/demo_exporter.h"

int main(int argc, char** argv) {
  const std::string output_path =
      argc > 1 ? argv[1] : "connected_call.generated.gsr";
  try {
    auto call = std::make_unique<call_demo::CallSession>();
    auto peer = std::make_unique<call_demo::Peer>();
    auto legs = std::make_unique<call_demo::MediaLeg[]>(2);
    auto timer = std::make_unique<call_demo::TimerHandle>();

    call->call_id = 42;
    call->state = call_demo::CallState::kConnected;
    call->peer = peer.get();
    call->active_leg = &legs[1];
    call->refresh_timer = timer.get();
    std::strcpy(peer->uri, "sip:alice@example.test");
    peer->owner = call.get();
    legs[0] = {10, false, call.get(), &legs[1]};
    legs[1] = {20, true, call.get(), &legs[0]};
    timer->token = 9001;

    call_demo::g_service_ready = true;
    call_demo::g_call = call.get();
    call_demo::g_active_leg = &legs[1];

    std::ofstream output(output_path);
    if (!output) {
      throw std::runtime_error("cannot open output file: " + output_path);
    }
    gsr::ExportDemoState(output, {call.get(), peer.get(), legs.get(), 2});
    call_demo::ResetGlobals();
    std::cout << "Wrote GSR/1 fixture to " << output_path << '\n';
    return 0;
  } catch (const std::exception& error) {
    call_demo::ResetGlobals();
    std::cerr << "Capture failed: " << error.what() << '\n';
    return 1;
  }
}
