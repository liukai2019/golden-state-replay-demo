#include <iostream>
#include <stdexcept>
#include <string>

#include "gsr/demo_domain.h"
#include "gsr/replay_loader.h"

namespace {

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string bundle =
      argc > 1 ? argv[1] : "examples/invite_200_ok";
  try {
    auto replay = gsr::ReplayContext::LoadBundle(bundle);
    const auto& args = replay->arguments();

    Require(args.service_ptr == &args.sip_ptr->service,
            "embedded service identity was not restored");
    auto* ua = static_cast<call_demo::UaContext*>(args.h_ua);
    auto* dialog = static_cast<call_demo::Dialog*>(args.h_dialog);
    Require(args.sip_ptr->primary_ua == ua,
            "void handle alias was not restored");
    Require(ua->dialogs.head == &dialog->link,
            "intrusive-list subobject edge was not restored");
    Require(args.event_ptr->correlation_id == 73 &&
                args.event_ptr->call.to[0] == '\0',
            "in/out entry state was not restored");

    call_demo::RecordingExternalApi external;
    call_demo::SetExternalApi(&external);
    Require(call_demo::ProcessSipCallEvent(
                args.sip_ptr, args.service_ptr, args.h_ua, args.h_dialog,
                args.event, args.ua_event_ptr, args.event_ptr) ==
                call_demo::HandlerResult::kOk,
            "real handler rejected replayed state");
    Require(replay->expected_calls().size() == 1 &&
                external.confirmed_dialog_ids.size() == 1 &&
                replay->expected_calls().front().argument ==
                    external.confirmed_dialog_ids.front(),
            "external spy transcript did not match");

    std::string mismatch;
    Require(replay->OracleMatches(&mismatch), "oracle mismatch: " + mismatch);
    std::cout << "PASS: replayed " << replay->manifest().at("scenario")
              << ", preserved subobjects/handles/list edges, matched spy and oracle\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
