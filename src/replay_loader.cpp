#include "gsr/replay_loader.h"

#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <utility>

#include "gsr/demo_exporter.h"

namespace gsr {
namespace {

using FlatObject = std::unordered_map<std::string, std::string>;

class FlatJsonParser {
 public:
  explicit FlatJsonParser(std::string text) : text_(std::move(text)) {}

  FlatObject Parse() {
    FlatObject result;
    SkipSpace();
    Expect('{');
    SkipSpace();
    if (Take('}')) {
      Finish();
      return result;
    }
    while (true) {
      const std::string key = ParseString();
      SkipSpace();
      Expect(':');
      SkipSpace();
      std::string value;
      if (Peek() == '"') {
        value = ParseString();
      } else {
        const std::size_t begin = position_;
        while (position_ < text_.size() && text_[position_] != ',' &&
               text_[position_] != '}' &&
               !std::isspace(static_cast<unsigned char>(text_[position_]))) {
          ++position_;
        }
        if (position_ == begin) {
          Fail("expected a flat JSON value");
        }
        value = text_.substr(begin, position_ - begin);
      }
      if (!result.emplace(key, std::move(value)).second) {
        Fail("duplicate key '" + key + "'");
      }
      SkipSpace();
      if (Take('}')) {
        Finish();
        return result;
      }
      Expect(',');
      SkipSpace();
    }
  }

 private:
  char Peek() const {
    return position_ < text_.size() ? text_[position_] : '\0';
  }

  void SkipSpace() {
    while (position_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[position_]))) {
      ++position_;
    }
  }

  bool Take(char wanted) {
    if (Peek() != wanted) {
      return false;
    }
    ++position_;
    return true;
  }

  void Expect(char wanted) {
    if (!Take(wanted)) {
      Fail(std::string("expected '") + wanted + "'");
    }
  }

  std::string ParseString() {
    Expect('"');
    std::string result;
    while (position_ < text_.size()) {
      char value = text_[position_++];
      if (value == '"') {
        return result;
      }
      if (value != '\\') {
        if (static_cast<unsigned char>(value) < 0x20) {
          Fail("control byte in JSON string");
        }
        result.push_back(value);
        continue;
      }
      if (position_ == text_.size()) {
        Fail("unfinished JSON escape");
      }
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          result.push_back(escaped);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        default:
          Fail("unsupported JSON escape");
      }
    }
    Fail("unterminated JSON string");
  }

  void Finish() {
    SkipSpace();
    if (position_ != text_.size()) {
      Fail("trailing content");
    }
  }

  [[noreturn]] void Fail(const std::string& message) const {
    throw ProtocolError("flat JSON at byte " + std::to_string(position_) +
                        ": " + message);
  }

  std::string text_;
  std::size_t position_ = 0;
};

const std::string& Require(const FlatObject& object, const char* key) {
  const auto found = object.find(key);
  if (found == object.end()) {
    throw ProtocolError(std::string("missing JSON key '") + key + "'");
  }
  return found->second;
}

std::string Optional(const FlatObject& object, const char* key) {
  const auto found = object.find(key);
  return found == object.end() ? std::string() : found->second;
}

std::int32_t ParseI32(const std::string& value, const std::string& context) {
  std::size_t consumed = 0;
  long long parsed = 0;
  try {
    parsed = std::stoll(value, &consumed, 10);
  } catch (const std::exception&) {
    throw ProtocolError(context + ": invalid i32 '" + value + "'");
  }
  if (consumed != value.size() ||
      parsed < std::numeric_limits<std::int32_t>::min() ||
      parsed > std::numeric_limits<std::int32_t>::max()) {
    throw ProtocolError(context + ": invalid i32 '" + value + "'");
  }
  return static_cast<std::int32_t>(parsed);
}

std::uint32_t ParseU32(const std::string& value, const std::string& context) {
  std::size_t consumed = 0;
  unsigned long long parsed = 0;
  try {
    parsed = std::stoull(value, &consumed, 10);
  } catch (const std::exception&) {
    throw ProtocolError(context + ": invalid u32 '" + value + "'");
  }
  if (consumed != value.size() ||
      parsed > std::numeric_limits<std::uint32_t>::max()) {
    throw ProtocolError(context + ": invalid u32 '" + value + "'");
  }
  return static_cast<std::uint32_t>(parsed);
}

bool ParseBool(const std::string& value, const std::string& context) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  throw ProtocolError(context + ": invalid bool '" + value + "'");
}

template <std::size_t N>
void CopyString(char (&destination)[N], const std::string& value,
                const std::string& context) {
  if (value.size() >= N) {
    throw ProtocolError(context + ": string exceeds destination capacity");
  }
  std::memcpy(destination, value.c_str(), value.size() + 1);
}

enum class Kind {
  kNull,
  kSipRuntime,
  kRegistrationService,
  kUaContext,
  kDialog,
  kListEntry,
  kUaAppEvent,
  kAppEvent,
};

struct Allocation {
  Kind kind = Kind::kNull;
  std::shared_ptr<void> storage;
};

struct Resolved {
  void* address = nullptr;
  Kind kind = Kind::kNull;
};

struct FieldRecord {
  std::string object;
  std::string field;
  std::string encoding;
  std::string value;
};

struct EdgeRecord {
  std::string from;
  std::string field;
  std::string to;
  std::string subobject;
};

struct RootRecord {
  std::string name;
  std::string to;
  std::string subobject;
};

struct OracleString {
  std::string root;
  std::string field;
  std::string value;
};

struct ParsedState {
  std::vector<std::pair<std::string, std::string>> objects;
  std::vector<FieldRecord> fields;
  std::vector<EdgeRecord> edges;
  std::vector<RootRecord> roots;
  std::vector<ExpectedCall> expected_calls;
  std::vector<OracleString> oracle_strings;
  std::int32_t event = 0;
  bool saw_event = false;
  bool feature_enabled = false;
  bool saw_feature_enabled = false;
};

Kind ParseKind(const std::string& type) {
  if (type == "SipRuntime") return Kind::kSipRuntime;
  if (type == "UaContext") return Kind::kUaContext;
  if (type == "Dialog") return Kind::kDialog;
  if (type == "UaAppEvent") return Kind::kUaAppEvent;
  if (type == "AppEvent") return Kind::kAppEvent;
  throw ProtocolError("unsupported generated-codec type '" + type + "'");
}

template <typename T>
Allocation Make(Kind kind) {
  T* value = new T();
  return Allocation{kind, std::shared_ptr<void>(value, [](void* pointer) {
                      delete static_cast<T*>(pointer);
                    })};
}

Allocation Allocate(Kind kind) {
  switch (kind) {
    case Kind::kSipRuntime:
      return Make<call_demo::SipRuntime>(kind);
    case Kind::kUaContext:
      return Make<call_demo::UaContext>(kind);
    case Kind::kDialog:
      return Make<call_demo::Dialog>(kind);
    case Kind::kUaAppEvent:
      return Make<call_demo::UaAppEvent>(kind);
    case Kind::kAppEvent:
      return Make<call_demo::AppEvent>(kind);
    case Kind::kRegistrationService:
    case Kind::kListEntry:
    case Kind::kNull:
      break;
  }
  throw ProtocolError("cannot allocate a standalone subobject kind");
}

struct Runtime {
  std::unordered_map<std::string, Allocation> allocations;
};

Resolved Resolve(const Runtime& runtime, const std::string& id,
                 const std::string& subobject = std::string()) {
  if (id.empty() || id == "null") {
    if (!subobject.empty()) {
      throw ProtocolError("null target cannot have a subobject");
    }
    return {};
  }
  const auto found = runtime.allocations.find(id);
  if (found == runtime.allocations.end()) {
    throw ProtocolError("reference to unknown object '" + id + "'");
  }
  void* address = found->second.storage.get();
  Kind kind = found->second.kind;
  if (subobject.empty()) {
    return {address, kind};
  }
  if (kind == Kind::kSipRuntime && subobject == "service") {
    return {&static_cast<call_demo::SipRuntime*>(address)->service,
            Kind::kRegistrationService};
  }
  if (kind == Kind::kDialog && subobject == "link") {
    return {&static_cast<call_demo::Dialog*>(address)->link,
            Kind::kListEntry};
  }
  throw ProtocolError(id + ": unsupported subobject '" + subobject + "'");
}

void RequireKind(const Resolved& value, Kind expected,
                 const std::string& context) {
  if (value.kind != Kind::kNull && value.kind != expected) {
    throw ProtocolError(context + ": target has wrong type");
  }
}

ParsedState ParseState(std::istream& input) {
  ParsedState parsed;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (line.empty()) continue;
    FlatObject record;
    try {
      record = FlatJsonParser(line).Parse();
    } catch (const ProtocolError& error) {
      throw ProtocolError("state.jsonl line " + std::to_string(line_number) +
                          ": " + error.what());
    }
    const std::string& kind = Require(record, "kind");
    if (kind == "object") {
      parsed.objects.emplace_back(Require(record, "id"),
                                  Require(record, "type"));
    } else if (kind == "scalar" || kind == "string") {
      parsed.fields.push_back({Require(record, "object"),
                               Require(record, "field"),
                               kind == "string" ? "string"
                                                : Require(record, "encoding"),
                               Require(record, "value")});
    } else if (kind == "edge") {
      parsed.edges.push_back({Require(record, "from"),
                              Require(record, "field"),
                              Require(record, "to"),
                              Optional(record, "subobject")});
    } else if (kind == "root") {
      parsed.roots.push_back({Require(record, "name"),
                              Require(record, "to"),
                              Optional(record, "subobject")});
    } else if (kind == "value") {
      if (Require(record, "name") != "arg.event" ||
          Require(record, "encoding") != "i32") {
        throw ProtocolError("unsupported value record");
      }
      parsed.event = ParseI32(Require(record, "value"), "arg.event");
      parsed.saw_event = true;
    } else if (kind == "global") {
      if (Require(record, "name") != "g_feature_enabled" ||
          Require(record, "encoding") != "bool") {
        throw ProtocolError("unsupported global record");
      }
      parsed.feature_enabled =
          ParseBool(Require(record, "value"), "g_feature_enabled");
      parsed.saw_feature_enabled = true;
    } else if (kind == "oracle_string") {
      parsed.oracle_strings.push_back({Require(record, "root"),
                                       Require(record, "field"),
                                       Require(record, "value")});
    } else if (kind == "expected_call") {
      parsed.expected_calls.push_back(
          {Require(record, "name"),
           ParseI32(Require(record, "argument"), "expected_call.argument")});
    } else {
      throw ProtocolError("state.jsonl line " +
                          std::to_string(line_number) +
                          ": unknown record kind '" + kind + "'");
    }
  }
  return parsed;
}

void SetField(const Runtime& runtime, const FieldRecord& record) {
  const Resolved target = Resolve(runtime, record.object);
  const std::string context = record.object + "." + record.field;
  if (target.kind == Kind::kSipRuntime) {
    auto* value = static_cast<call_demo::SipRuntime*>(target.address);
    if (record.field == "config_epoch" && record.encoding == "i32") {
      value->config_epoch = ParseI32(record.value, context);
      return;
    }
    if (record.field == "service.registered" && record.encoding == "bool") {
      value->service.registered = ParseBool(record.value, context);
      return;
    }
    if (record.field == "service.public_user_id" &&
        record.encoding == "string") {
      CopyString(value->service.public_user_id, record.value, context);
      return;
    }
  } else if (target.kind == Kind::kUaContext) {
    auto* value = static_cast<call_demo::UaContext*>(target.address);
    if (record.field == "task_id" && record.encoding == "u32") {
      value->task_id = ParseU32(record.value, context);
      return;
    }
    if (record.field == "scratch" && record.encoding == "string") {
      CopyString(value->scratch, record.value, context);
      return;
    }
  } else if (target.kind == Kind::kDialog) {
    auto* value = static_cast<call_demo::Dialog*>(target.address);
    if (record.field == "dialog_id" && record.encoding == "i32") {
      value->dialog_id = ParseI32(record.value, context);
      return;
    }
    if (record.field == "confirmed" && record.encoding == "bool") {
      value->confirmed = ParseBool(record.value, context);
      return;
    }
    if (record.field == "remote_uri" && record.encoding == "string") {
      CopyString(value->remote_uri, record.value, context);
      return;
    }
  } else if (target.kind == Kind::kUaAppEvent) {
    auto* value = static_cast<call_demo::UaAppEvent*>(target.address);
    if (record.field == "header.type" && record.encoding == "enum") {
      const auto type = ParseI32(record.value, context);
      if (type < static_cast<std::int32_t>(call_demo::UaEventType::kRequest) ||
          type > static_cast<std::int32_t>(call_demo::UaEventType::kResponse)) {
        throw ProtocolError(context + ": invalid UaEventType");
      }
      value->header.type = static_cast<call_demo::UaEventType>(type);
      return;
    }
    if (record.field == "remote_uri" && record.encoding == "string") {
      CopyString(value->remote_uri, record.value, context);
      return;
    }
    if (record.field == "call_id" && record.encoding == "string") {
      CopyString(value->call_id, record.value, context);
      return;
    }
  } else if (target.kind == Kind::kAppEvent) {
    auto* value = static_cast<call_demo::AppEvent*>(target.address);
    if (record.field == "correlation_id" && record.encoding == "i32") {
      value->correlation_id = ParseI32(record.value, context);
      return;
    }
    if (record.field == "call.to" && record.encoding == "string") {
      CopyString(value->call.to, record.value, context);
      return;
    }
  }
  throw ProtocolError(context + ": unsupported field or encoding");
}

void BindEdge(const Runtime& runtime, const EdgeRecord& record) {
  const Resolved owner = Resolve(runtime, record.from);
  const Resolved target = Resolve(runtime, record.to, record.subobject);
  const std::string context = record.from + "." + record.field;
  if (owner.kind == Kind::kSipRuntime && record.field == "primary_ua") {
    RequireKind(target, Kind::kUaContext, context);
    static_cast<call_demo::SipRuntime*>(owner.address)->primary_ua =
        static_cast<call_demo::UaContext*>(target.address);
    return;
  }
  if (owner.kind == Kind::kUaContext) {
    auto* value = static_cast<call_demo::UaContext*>(owner.address);
    if (record.field == "dialogs.head") {
      RequireKind(target, Kind::kListEntry, context);
      value->dialogs.head = static_cast<call_demo::ListEntry*>(target.address);
      return;
    }
    if (record.field == "dialogs.tail") {
      RequireKind(target, Kind::kListEntry, context);
      value->dialogs.tail = static_cast<call_demo::ListEntry*>(target.address);
      return;
    }
  }
  if (owner.kind == Kind::kDialog) {
    auto* value = static_cast<call_demo::Dialog*>(owner.address);
    if (record.field == "link.previous") {
      RequireKind(target, Kind::kListEntry, context);
      value->link.previous = static_cast<call_demo::ListEntry*>(target.address);
      return;
    }
    if (record.field == "link.next") {
      RequireKind(target, Kind::kListEntry, context);
      value->link.next = static_cast<call_demo::ListEntry*>(target.address);
      return;
    }
  }
  if (owner.kind == Kind::kUaAppEvent) {
    auto* value = static_cast<call_demo::UaAppEvent*>(owner.address);
    if (record.field == "header.ua") {
      RequireKind(target, Kind::kUaContext, context);
      value->header.ua = target.address;
      return;
    }
    if (record.field == "header.owner") {
      RequireKind(target, Kind::kDialog, context);
      value->header.owner = target.address;
      return;
    }
  }
  throw ProtocolError(context + ": unsupported pointer field");
}

FlatObject LoadManifest(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw ProtocolError("cannot open manifest.json");
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  FlatObject manifest = FlatJsonParser(buffer.str()).Parse();
  if (Require(manifest, "format") != "golden-state-replay" ||
      Require(manifest, "version") != "2" ||
      Require(manifest, "target") != "ProcessSipCallEvent" ||
      Require(manifest, "capture_point") != "function_entry") {
    throw ProtocolError("manifest does not describe this generated codec");
  }
  if (Require(manifest, "type_fingerprint") != kDemoTypeFingerprint) {
    throw ProtocolError("type fingerprint mismatch; regenerate the fixture");
  }
  if (ParseU32(Require(manifest, "pointer_size"), "pointer_size") !=
      sizeof(void*)) {
    throw ProtocolError("pointer-size mismatch; regenerate the fixture");
  }
  return manifest;
}

}  // namespace

struct ReplayContext::Impl {
  Runtime runtime;
  ReplayedCallArguments arguments;
  FlatObject manifest;
  std::vector<ExpectedCall> expected_calls;
  std::vector<OracleString> oracle_strings;
};

ReplayContext::ReplayContext(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

ReplayContext::~ReplayContext() { call_demo::ResetGlobals(); }

std::unique_ptr<ReplayContext> ReplayContext::LoadBundle(
    const std::string& directory_name) {
  call_demo::ResetGlobals();
  const auto directory = std::filesystem::path(directory_name);
  auto impl = std::make_unique<Impl>();
  impl->manifest = LoadManifest(directory / "manifest.json");

  std::ifstream input(directory / "state.jsonl");
  if (!input) {
    throw ProtocolError("cannot open state.jsonl");
  }
  ParsedState parsed = ParseState(input);

  try {
    // Phase 1: allocate every complete object. Embedded subobjects are never
    // allocated independently.
    for (const auto& object : parsed.objects) {
      const Kind kind = ParseKind(object.second);
      if (!impl->runtime.allocations
               .emplace(object.first, Allocate(kind))
               .second) {
        throw ProtocolError("duplicate object id '" + object.first + "'");
      }
    }

    // Phase 2: restore typed scalar, enum, bool and bounded-string fields.
    for (const FieldRecord& field : parsed.fields) {
      SetField(impl->runtime, field);
    }

    // Phase 3: all allocation addresses now exist, so repair pointer edges,
    // aliases, void-handle targets and intrusive-list subobject pointers.
    for (const EdgeRecord& edge : parsed.edges) {
      BindEdge(impl->runtime, edge);
    }

    // Phase 4: bind argument/global roots. A real generated codec emits these
    // assignments using the declarations visible to the test build.
    for (const RootRecord& root : parsed.roots) {
      const Resolved value = Resolve(impl->runtime, root.to, root.subobject);
      if (root.name == "arg.sip_ptr") {
        RequireKind(value, Kind::kSipRuntime, root.name);
        impl->arguments.sip_ptr =
            static_cast<call_demo::SipRuntime*>(value.address);
      } else if (root.name == "arg.service_ptr") {
        RequireKind(value, Kind::kRegistrationService, root.name);
        impl->arguments.service_ptr =
            static_cast<call_demo::RegistrationService*>(value.address);
      } else if (root.name == "arg.h_ua") {
        RequireKind(value, Kind::kUaContext, root.name);
        impl->arguments.h_ua = value.address;
      } else if (root.name == "arg.h_dialog") {
        RequireKind(value, Kind::kDialog, root.name);
        impl->arguments.h_dialog = value.address;
      } else if (root.name == "arg.ua_event_ptr") {
        RequireKind(value, Kind::kUaAppEvent, root.name);
        impl->arguments.ua_event_ptr =
            static_cast<call_demo::UaAppEvent*>(value.address);
      } else if (root.name == "arg.event_ptr") {
        RequireKind(value, Kind::kAppEvent, root.name);
        impl->arguments.event_ptr =
            static_cast<call_demo::AppEvent*>(value.address);
      } else if (root.name == "global.g_runtime") {
        RequireKind(value, Kind::kSipRuntime, root.name);
        call_demo::g_runtime =
            static_cast<call_demo::SipRuntime*>(value.address);
      } else {
        throw ProtocolError("unknown root '" + root.name + "'");
      }
    }
    if (!parsed.saw_event || !parsed.saw_feature_enabled) {
      throw ProtocolError("missing event or selected scalar global");
    }
    impl->arguments.event = parsed.event;
    call_demo::g_feature_enabled = parsed.feature_enabled;

    if (impl->arguments.sip_ptr == nullptr ||
        impl->arguments.service_ptr != &impl->arguments.sip_ptr->service ||
        impl->arguments.h_ua == nullptr ||
        impl->arguments.h_dialog == nullptr ||
        impl->arguments.ua_event_ptr == nullptr ||
        impl->arguments.event_ptr == nullptr ||
        call_demo::g_runtime != impl->arguments.sip_ptr) {
      throw ProtocolError("required roots or embedded relationship are missing");
    }

    impl->expected_calls = std::move(parsed.expected_calls);
    impl->oracle_strings = std::move(parsed.oracle_strings);
  } catch (...) {
    call_demo::ResetGlobals();
    throw;
  }
  return std::unique_ptr<ReplayContext>(new ReplayContext(std::move(impl)));
}

const ReplayedCallArguments& ReplayContext::arguments() const {
  return impl_->arguments;
}

const std::unordered_map<std::string, std::string>& ReplayContext::manifest()
    const {
  return impl_->manifest;
}

const std::vector<ExpectedCall>& ReplayContext::expected_calls() const {
  return impl_->expected_calls;
}

bool ReplayContext::OracleMatches(std::string* mismatch) const {
  for (const OracleString& oracle : impl_->oracle_strings) {
    if (oracle.root != "arg.event_ptr" || oracle.field != "call.to") {
      throw ProtocolError("unsupported string oracle");
    }
    const std::string actual = impl_->arguments.event_ptr->call.to;
    if (actual != oracle.value) {
      if (mismatch != nullptr) {
        *mismatch = oracle.root + "." + oracle.field + ": expected '" +
                    oracle.value + "', actual '" + actual + "'";
      }
      return false;
    }
  }
  if (mismatch != nullptr) mismatch->clear();
  return true;
}

}  // namespace gsr
