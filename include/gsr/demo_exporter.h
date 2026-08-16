#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "gsr/demo_domain.h"

namespace gsr {

inline constexpr const char* kDemoTypeFingerprint =
    "call-demo-typed-codec-v2";

struct CapturedCallArguments {
  call_demo::SipRuntime* sip_ptr = nullptr;
  call_demo::RegistrationService* service_ptr = nullptr;
  call_demo::SipHandle h_ua = nullptr;
  call_demo::SipHandle h_dialog = nullptr;
  std::int32_t event = 0;
  call_demo::UaAppEvent* ua_event_ptr = nullptr;
  call_demo::AppEvent* event_ptr = nullptr;
};

// Entry phase: writes manifest.json and the before-state records in state.jsonl.
// A production trampoline calls this before forwarding the seven arguments.
void ExportEntryState(const std::string& bundle_directory,
                      const CapturedCallArguments& arguments);

// Exit phase: appends the selected output and spy transcript. If a project keeps
// these expectations in GTest instead, this phase can be omitted.
void AppendExpectedResult(
    const std::string& bundle_directory,
    const call_demo::AppEvent& event_after,
    const std::vector<std::int32_t>& confirmed_dialog_ids);

}  // namespace gsr
