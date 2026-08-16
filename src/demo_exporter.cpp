#include "gsr/demo_exporter.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <string>

namespace gsr {
namespace {

std::string JsonEscape(const char* text, std::size_t capacity) {
  const void* terminator = std::memchr(text, '\0', capacity);
  if (terminator == nullptr) {
    throw std::runtime_error("captured text is not null terminated");
  }
  const auto length = static_cast<std::size_t>(
      static_cast<const char*>(terminator) - text);
  std::string escaped;
  escaped.reserve(length + 8);
  for (std::size_t i = 0; i < length; ++i) {
    const unsigned char value = static_cast<unsigned char>(text[i]);
    switch (value) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        if (value < 0x20) {
          throw std::runtime_error("captured text contains a control byte");
        }
        escaped.push_back(static_cast<char>(value));
    }
  }
  return escaped;
}

void Require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void StringRecord(std::ostream& out, const char* object, const char* field,
                  const char* value, std::size_t capacity) {
  out << "{\"kind\":\"string\",\"object\":\"" << object
      << "\",\"field\":\"" << field << "\",\"value\":\""
      << JsonEscape(value, capacity) << "\"}\n";
}

void ScalarRecord(std::ostream& out, const char* object, const char* field,
                  const char* encoding, std::int64_t value) {
  out << "{\"kind\":\"scalar\",\"object\":\"" << object
      << "\",\"field\":\"" << field << "\",\"encoding\":\""
      << encoding << "\",\"value\":" << value << "}\n";
}

void BoolRecord(std::ostream& out, const char* object, const char* field,
                bool value) {
  out << "{\"kind\":\"scalar\",\"object\":\"" << object
      << "\",\"field\":\"" << field
      << "\",\"encoding\":\"bool\",\"value\":"
      << (value ? "true" : "false") << "}\n";
}

void EdgeRecord(std::ostream& out, const char* from, const char* field,
                const char* to, const char* subobject = "") {
  out << "{\"kind\":\"edge\",\"from\":\"" << from
      << "\",\"field\":\"" << field << "\",\"to\":";
  if (to == nullptr) {
    out << "null";
  } else {
    out << "\"" << to << "\"";
  }
  if (subobject[0] != '\0') {
    out << ",\"subobject\":\"" << subobject << "\"";
  }
  out << "}\n";
}

void RootRecord(std::ostream& out, const char* name, const char* to,
                const char* subobject = "") {
  out << "{\"kind\":\"root\",\"name\":\"" << name
      << "\",\"to\":\"" << to << "\"";
  if (subobject[0] != '\0') {
    out << ",\"subobject\":\"" << subobject << "\"";
  }
  out << "}\n";
}

}  // namespace

void ExportEntryState(const std::string& bundle_directory,
                      const CapturedCallArguments& arguments) {
  auto* ua = static_cast<call_demo::UaContext*>(arguments.h_ua);
  auto* dialog = static_cast<call_demo::Dialog*>(arguments.h_dialog);
  Require(arguments.sip_ptr != nullptr, "sip_ptr is null");
  Require(arguments.service_ptr == &arguments.sip_ptr->service,
          "service_ptr is not the embedded service subobject");
  Require(ua != nullptr && dialog != nullptr, "internal SIP handle is null");
  Require(arguments.ua_event_ptr != nullptr && arguments.event_ptr != nullptr,
          "event argument is null");
  Require(arguments.sip_ptr->primary_ua == ua,
          "h_ua is not sip_ptr->primary_ua");
  Require(ua->dialogs.head == &dialog->link,
          "demo capture expects target dialog at list head");

  std::filesystem::create_directories(bundle_directory);
  const auto directory = std::filesystem::path(bundle_directory);
  std::ofstream manifest(directory / "manifest.json", std::ios::trunc);
  if (!manifest) {
    throw std::runtime_error("cannot create manifest.json");
  }
  manifest
      << "{\n"
      << "  \"format\": \"golden-state-replay\",\n"
      << "  \"version\": 2,\n"
      << "  \"scenario\": \"invite_200_ok\",\n"
      << "  \"target\": \"ProcessSipCallEvent\",\n"
      << "  \"capture_point\": \"function_entry\",\n"
      << "  \"build_id\": \"public-demo-cxx17\",\n"
      << "  \"type_fingerprint\": \"" << kDemoTypeFingerprint << "\",\n"
      << "  \"pointer_size\": " << sizeof(void*) << ",\n"
      << "  \"endianness\": \"little\",\n"
      << "  \"data_classification\": \"synthetic\"\n"
      << "}\n";

  std::ofstream state(directory / "state.jsonl", std::ios::trunc);
  if (!state) {
    throw std::runtime_error("cannot create state.jsonl");
  }

  state << "{\"kind\":\"object\",\"id\":\"sip-1\",\"type\":\"SipRuntime\"}\n"
        << "{\"kind\":\"object\",\"id\":\"ua-1\",\"type\":\"UaContext\"}\n"
        << "{\"kind\":\"object\",\"id\":\"dialog-1\",\"type\":\"Dialog\"}\n"
        << "{\"kind\":\"object\",\"id\":\"ua-event-1\",\"type\":\"UaAppEvent\"}\n"
        << "{\"kind\":\"object\",\"id\":\"app-event-1\",\"type\":\"AppEvent\"}\n";

  ScalarRecord(state, "sip-1", "config_epoch", "i32",
               arguments.sip_ptr->config_epoch);
  BoolRecord(state, "sip-1", "service.registered",
             arguments.sip_ptr->service.registered);
  StringRecord(state, "sip-1", "service.public_user_id",
               arguments.sip_ptr->service.public_user_id,
               sizeof(arguments.sip_ptr->service.public_user_id));
  ScalarRecord(state, "ua-1", "task_id", "u32", ua->task_id);
  StringRecord(state, "ua-1", "scratch", ua->scratch,
               sizeof(ua->scratch));
  ScalarRecord(state, "dialog-1", "dialog_id", "i32",
               dialog->dialog_id);
  BoolRecord(state, "dialog-1", "confirmed", dialog->confirmed);
  StringRecord(state, "dialog-1", "remote_uri", dialog->remote_uri,
               sizeof(dialog->remote_uri));
  ScalarRecord(state, "ua-event-1", "header.type", "enum",
               static_cast<std::int32_t>(arguments.ua_event_ptr->header.type));
  StringRecord(state, "ua-event-1", "remote_uri",
               arguments.ua_event_ptr->remote_uri,
               sizeof(arguments.ua_event_ptr->remote_uri));
  StringRecord(state, "ua-event-1", "call_id",
               arguments.ua_event_ptr->call_id,
               sizeof(arguments.ua_event_ptr->call_id));
  ScalarRecord(state, "app-event-1", "correlation_id", "i32",
               arguments.event_ptr->correlation_id);
  StringRecord(state, "app-event-1", "call.to",
               arguments.event_ptr->call.to,
               sizeof(arguments.event_ptr->call.to));

  EdgeRecord(state, "sip-1", "primary_ua", "ua-1");
  EdgeRecord(state, "ua-1", "dialogs.head", "dialog-1", "link");
  EdgeRecord(state, "ua-1", "dialogs.tail", "dialog-1", "link");
  EdgeRecord(state, "dialog-1", "link.previous", nullptr);
  EdgeRecord(state, "dialog-1", "link.next", nullptr);
  EdgeRecord(state, "ua-event-1", "header.ua", "ua-1");
  EdgeRecord(state, "ua-event-1", "header.owner", "dialog-1");

  RootRecord(state, "arg.sip_ptr", "sip-1");
  RootRecord(state, "arg.service_ptr", "sip-1", "service");
  RootRecord(state, "arg.h_ua", "ua-1");
  RootRecord(state, "arg.h_dialog", "dialog-1");
  state << "{\"kind\":\"value\",\"name\":\"arg.event\","
           "\"encoding\":\"i32\",\"value\":"
        << arguments.event << "}\n";
  RootRecord(state, "arg.ua_event_ptr", "ua-event-1");
  RootRecord(state, "arg.event_ptr", "app-event-1");
  RootRecord(state, "global.g_runtime", "sip-1");
  state << "{\"kind\":\"global\",\"name\":\"g_feature_enabled\","
           "\"encoding\":\"bool\",\"value\":"
        << (call_demo::g_feature_enabled ? "true" : "false") << "}\n";
}

void AppendExpectedResult(
    const std::string& bundle_directory,
    const call_demo::AppEvent& event_after,
    const std::vector<std::int32_t>& confirmed_dialog_ids) {
  const auto path =
      std::filesystem::path(bundle_directory) / "state.jsonl";
  std::ofstream state(path, std::ios::app);
  if (!state) {
    throw std::runtime_error("cannot append state.jsonl");
  }
  state << "{\"kind\":\"oracle_string\",\"root\":\"arg.event_ptr\","
           "\"field\":\"call.to\",\"value\":\""
        << JsonEscape(event_after.call.to, sizeof(event_after.call.to))
        << "\"}\n";
  for (const std::int32_t id : confirmed_dialog_ids) {
    state << "{\"kind\":\"expected_call\","
             "\"name\":\"notify_dialog_confirmed\",\"argument\":"
          << id << "}\n";
  }
}

}  // namespace gsr
