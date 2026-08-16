#include "gsr/demo_domain.h"

#include <cstddef>
#include <cstring>

namespace call_demo {
namespace {

ExternalApi* g_external_api = nullptr;

Dialog* DialogFromLink(ListEntry* entry) {
  if (entry == nullptr) {
    return nullptr;
  }
  auto* bytes = reinterpret_cast<unsigned char*>(entry);
  return reinterpret_cast<Dialog*>(bytes - offsetof(Dialog, link));
}

bool IsDialogInList(const UaContext& ua, const Dialog* wanted) {
  ListEntry* current = ua.dialogs.head;
  for (std::size_t visited = 0; current != nullptr && visited < 1024;
       ++visited, current = current->next) {
    if (DialogFromLink(current) == wanted) {
      return true;
    }
  }
  return false;
}

bool CopyText(char* destination, std::size_t capacity, const char* source,
              std::size_t source_capacity) {
  const void* terminator = std::memchr(source, '\0', source_capacity);
  if (terminator == nullptr) {
    return false;
  }
  const auto length = static_cast<std::size_t>(
      static_cast<const char*>(terminator) - source);
  if (length >= capacity) {
    return false;
  }
  std::memcpy(destination, source, length + 1);
  return true;
}

}  // namespace

bool g_feature_enabled = false;
SipRuntime* g_runtime = nullptr;

void SetExternalApi(ExternalApi* api) { g_external_api = api; }

HandlerResult ProcessSipCallEvent(SipRuntime* sip_ptr,
                                  RegistrationService* service_ptr,
                                  SipHandle h_ua, SipHandle h_dialog,
                                  std::int32_t event,
                                  UaAppEvent* ua_event_ptr,
                                  AppEvent* event_ptr) {
  if (!g_feature_enabled || sip_ptr == nullptr || service_ptr == nullptr ||
      h_ua == nullptr || h_dialog == nullptr || ua_event_ptr == nullptr ||
      event_ptr == nullptr) {
    return HandlerResult::kMissingState;
  }
  if (event != 200 ||
      ua_event_ptr->header.type != UaEventType::kResponse) {
    return HandlerResult::kWrongEvent;
  }

  auto* ua = static_cast<UaContext*>(h_ua);
  auto* dialog = static_cast<Dialog*>(h_dialog);
  if (g_runtime != sip_ptr || service_ptr != &sip_ptr->service ||
      sip_ptr->primary_ua != ua || ua_event_ptr->header.ua != h_ua ||
      ua_event_ptr->header.owner != h_dialog || !service_ptr->registered ||
      !IsDialogInList(*ua, dialog)) {
    return HandlerResult::kInvalidRelationship;
  }

  if (!CopyText(event_ptr->call.to, sizeof(event_ptr->call.to),
                ua_event_ptr->remote_uri,
                sizeof(ua_event_ptr->remote_uri))) {
    return HandlerResult::kInvalidText;
  }

  dialog->confirmed = true;
  if (g_external_api == nullptr) {
    return HandlerResult::kExternalUnavailable;
  }
  g_external_api->NotifyDialogConfirmed(dialog->dialog_id);
  return HandlerResult::kOk;
}

void ResetGlobals() {
  g_feature_enabled = false;
  g_runtime = nullptr;
  g_external_api = nullptr;
}

void RecordingExternalApi::NotifyDialogConfirmed(std::int32_t dialog_id) {
  confirmed_dialog_ids.push_back(dialog_id);
}

}  // namespace call_demo
