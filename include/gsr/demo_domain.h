#pragma once

#include <cstdint>
#include <vector>

namespace call_demo {

// The public demo deliberately uses generic names. The shapes are close enough
// to a C-style SIP stack to exercise the replay problems without publishing
// product-specific identifiers or private layouts.
using SipHandle = void*;

enum class UaEventType : std::int32_t {
  kRequest = 1,
  kResponse = 2,
};

struct ListEntry {
  ListEntry* previous = nullptr;
  ListEntry* next = nullptr;
};

struct DialogList {
  ListEntry* head = nullptr;
  ListEntry* tail = nullptr;
};

struct RegistrationService {
  bool registered = false;
  char public_user_id[96]{};
};

struct Dialog {
  // Intrusive list node: UaContext::dialogs points to this subobject, not to
  // the address of Dialog itself.
  ListEntry link;
  std::int32_t dialog_id = 0;
  bool confirmed = false;
  char remote_uri[128]{};
};

struct UaContext {
  ListEntry link;
  std::uint32_t task_id = 0;
  DialogList dialogs;
  char scratch[256]{};
};

struct SipRuntime {
  std::int32_t config_epoch = 0;
  RegistrationService service;
  UaContext* primary_ua = nullptr;
};

struct UaAppEvent {
  struct Header {
    SipHandle ua = nullptr;
    SipHandle owner = nullptr;
    UaEventType type = UaEventType::kRequest;
  } header;
  char remote_uri[128]{};
  char call_id[64]{};
};

// This is an in/out parameter. The caller may pre-populate correlation_id and
// other fields; the function under test fills call.to for this scenario.
struct AppEvent {
  std::int32_t correlation_id = 0;
  struct {
    char to[128]{};
  } call;
};

// Demo equivalents of selected module globals. A real integration uses a
// Clang-generated inventory intersected with an explicit capture policy.
extern bool g_feature_enabled;
extern SipRuntime* g_runtime;

class ExternalApi {
 public:
  virtual ~ExternalApi() = default;
  virtual void NotifyDialogConfirmed(std::int32_t dialog_id) = 0;
};

void SetExternalApi(ExternalApi* api);

enum class HandlerResult {
  kOk,
  kMissingState,
  kWrongEvent,
  kInvalidRelationship,
  kInvalidText,
  kExternalUnavailable,
};

// Sanitized equivalent of a large production call-event handler. Upstream
// dialog/transaction processing already classified this response as belonging
// to INVITE; this entry point only receives response category + status code.
HandlerResult ProcessSipCallEvent(SipRuntime* sip_ptr,
                                  RegistrationService* service_ptr,
                                  SipHandle h_ua, SipHandle h_dialog,
                                  std::int32_t event,
                                  UaAppEvent* ua_event_ptr,
                                  AppEvent* event_ptr);

void ResetGlobals();

class RecordingExternalApi final : public ExternalApi {
 public:
  void NotifyDialogConfirmed(std::int32_t dialog_id) override;
  std::vector<std::int32_t> confirmed_dialog_ids;
};

}  // namespace call_demo
