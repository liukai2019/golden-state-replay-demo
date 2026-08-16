#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "gsr/demo_domain.h"
#include "gsr/demo_exporter.h"

namespace {

template <std::size_t N>
void SetText(char (&destination)[N], const char* source) {
  const std::size_t length = std::strlen(source);
  if (length >= N) throw std::runtime_error("demo text is too long");
  std::memcpy(destination, source, length + 1);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string output_directory =
      argc > 1 ? argv[1] : "invite_200_ok.generated";
  try {
    call_demo::SipRuntime sip;
    call_demo::UaContext ua;
    call_demo::Dialog dialog;
    call_demo::UaAppEvent ua_event;
    call_demo::AppEvent app_event;
    call_demo::RecordingExternalApi external;

    sip.config_epoch = 7;
    sip.service.registered = true;
    SetText(sip.service.public_user_id, "sip:user-001@example.test");
    sip.primary_ua = &ua;
    ua.task_id = 17;
    SetText(ua.scratch, "synthetic-transaction-state");
    ua.dialogs.head = &dialog.link;
    ua.dialogs.tail = &dialog.link;
    dialog.dialog_id = 42;
    dialog.confirmed = false;
    SetText(dialog.remote_uri, "sip:peer-001@example.test");

    ua_event.header.ua = &ua;
    ua_event.header.owner = &dialog;
    ua_event.header.type = call_demo::UaEventType::kResponse;
    SetText(ua_event.remote_uri, "sip:peer-001@example.test");
    SetText(ua_event.call_id, "synthetic-call-001");
    app_event.correlation_id = 73;  // proves this is an in/out object
    app_event.call.to[0] = '\0';

    call_demo::g_feature_enabled = true;
    call_demo::g_runtime = &sip;
    call_demo::SetExternalApi(&external);

    const gsr::CapturedCallArguments arguments{
        &sip, &sip.service, &ua, &dialog, 200, &ua_event, &app_event};

    // Equivalent to the trampoline's CaptureBefore hook.
    gsr::ExportEntryState(output_directory, arguments);

    const auto result = call_demo::ProcessSipCallEvent(
        arguments.sip_ptr, arguments.service_ptr, arguments.h_ua,
        arguments.h_dialog, arguments.event, arguments.ua_event_ptr,
        arguments.event_ptr);
    if (result != call_demo::HandlerResult::kOk) {
      throw std::runtime_error("synthetic integration call failed");
    }

    // Equivalent to the optional CaptureAfter hook.
    gsr::AppendExpectedResult(output_directory, app_event,
                              external.confirmed_dialog_ids);
    call_demo::ResetGlobals();
    std::cout << "Wrote sanitized replay bundle to " << output_directory
              << '\n';
    return 0;
  } catch (const std::exception& error) {
    call_demo::ResetGlobals();
    std::cerr << "Capture failed: " << error.what() << '\n';
    return 1;
  }
}
