#include "gsr/replay_loader.h"

#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <tuple>
#include <utility>

#include "gsr/demo_domain.h"

namespace gsr {
namespace {

enum class Kind { kNull, kCallSession, kPeer, kMediaLeg, kTimerHandle };

struct Allocation {
  Kind kind = Kind::kNull;
  std::size_t count = 0;
  std::shared_ptr<void> storage;
};

struct ScalarRecord {
  std::string object_id;
  std::size_t index = 0;
  std::string field;
  std::string encoding;
  std::string value;
};

struct StringRecord {
  std::string object_id;
  std::size_t index = 0;
  std::string field;
  std::string value;
};

struct EdgeRecord {
  std::string owner_id;
  std::size_t owner_index = 0;
  std::string field;
  std::string target_id;
  std::size_t target_index = 0;
};

struct GlobalRecord {
  std::string name;
  std::string encoding;
  std::string value;
};

struct ParsedSnapshot {
  std::unordered_map<std::string, std::string> metadata;
  std::vector<std::tuple<std::string, std::string, std::size_t>> objects;
  std::vector<std::tuple<std::string, std::string, std::int32_t>> externals;
  std::vector<GlobalRecord> globals;
  std::vector<ScalarRecord> scalars;
  std::vector<StringRecord> strings;
  std::vector<EdgeRecord> edges;
  std::vector<ExpectedCall> expected_calls;
  std::vector<ScalarRecord> expectations;
};

struct RuntimeState {
  std::unordered_map<std::string, Allocation> allocations;
};

struct ResolvedPointer {
  void* address = nullptr;
  Kind kind = Kind::kNull;
};

[[noreturn]] void Fail(std::size_t line, const std::string& message) {
  throw ProtocolError("line " + std::to_string(line) + ": " + message);
}

bool HasTrailingToken(std::istringstream& input) {
  std::string token;
  return static_cast<bool>(input >> token);
}

std::int32_t ParseI32(const std::string& value, const std::string& context) {
  std::size_t consumed = 0;
  long long parsed = 0;
  try {
    parsed = std::stoll(value, &consumed, 10);
  } catch (const std::exception&) {
    throw ProtocolError(context + ": invalid i32 value '" + value + "'");
  }
  if (consumed != value.size() ||
      parsed < std::numeric_limits<std::int32_t>::min() ||
      parsed > std::numeric_limits<std::int32_t>::max()) {
    throw ProtocolError(context + ": invalid i32 value '" + value + "'");
  }
  return static_cast<std::int32_t>(parsed);
}

bool ParseBool(const std::string& value, const std::string& context) {
  if (value == "true") {
    return true;
  }
  if (value == "false") {
    return false;
  }
  throw ProtocolError(context + ": expected true or false, got '" + value + "'");
}

ParsedSnapshot Parse(std::istream& input) {
  ParsedSnapshot result;
  bool saw_header = false;
  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string::npos || line[first] == '#') {
      continue;
    }
    std::istringstream tokens(line.substr(first));
    std::string command;
    tokens >> command;

    if (!saw_header) {
      if (command != "GSR/1" || HasTrailingToken(tokens)) {
        Fail(line_number, "first record must be exactly GSR/1");
      }
      saw_header = true;
      continue;
    }

    if (command == "meta") {
      std::string key;
      std::string value;
      if (!(tokens >> key >> std::quoted(value)) || HasTrailingToken(tokens)) {
        Fail(line_number, "expected: meta <key> \"<value>\"");
      }
      result.metadata[key] = value;
    } else if (command == "object") {
      std::string id;
      std::string type;
      std::size_t count = 0;
      if (!(tokens >> id >> type >> count) || count == 0 ||
          HasTrailingToken(tokens)) {
        Fail(line_number, "expected: object <id> <type> <positive-count>");
      }
      result.objects.emplace_back(id, type, count);
    } else if (command == "external") {
      std::string id;
      std::string type;
      std::string token;
      if (!(tokens >> id >> type >> token) || HasTrailingToken(tokens)) {
        Fail(line_number, "expected: external <id> <adapter-type> <token>");
      }
      result.externals.emplace_back(
          id, type, ParseI32(token, "line " + std::to_string(line_number)));
    } else if (command == "global") {
      GlobalRecord record;
      if (!(tokens >> record.name >> record.encoding >> record.value) ||
          HasTrailingToken(tokens)) {
        Fail(line_number, "expected: global <name> <encoding> <value>");
      }
      result.globals.push_back(std::move(record));
    } else if (command == "scalar" || command == "expect") {
      ScalarRecord record;
      if (!(tokens >> record.object_id >> record.index >> record.field >>
            record.encoding >> record.value) ||
          HasTrailingToken(tokens)) {
        Fail(line_number,
             "expected: " + command +
                 " <object-id> <index> <field> <encoding> <value>");
      }
      if (command == "scalar") {
        result.scalars.push_back(std::move(record));
      } else {
        result.expectations.push_back(std::move(record));
      }
    } else if (command == "string") {
      StringRecord record;
      if (!(tokens >> record.object_id >> record.index >> record.field >>
            std::quoted(record.value)) ||
          HasTrailingToken(tokens)) {
        Fail(line_number,
             "expected: string <object-id> <index> <field> \"<value>\"");
      }
      result.strings.push_back(std::move(record));
    } else if (command == "edge") {
      EdgeRecord record;
      if (!(tokens >> record.owner_id >> record.owner_index >> record.field >>
            record.target_id >> record.target_index) ||
          HasTrailingToken(tokens)) {
        Fail(line_number,
             "expected: edge <owner-id> <index> <field> <target-id> <index>");
      }
      result.edges.push_back(std::move(record));
    } else if (command == "expect_call") {
      ExpectedCall call;
      std::string argument;
      if (!(tokens >> call.name >> argument) || HasTrailingToken(tokens)) {
        Fail(line_number, "expected: expect_call <name> <i32-argument>");
      }
      call.argument =
          ParseI32(argument, "line " + std::to_string(line_number));
      result.expected_calls.push_back(std::move(call));
    } else {
      Fail(line_number, "unknown record type '" + command + "'");
    }
  }

  if (!saw_header) {
    throw ProtocolError("empty input: missing GSR/1 header");
  }
  return result;
}

Kind ParseKind(const std::string& type) {
  if (type == "CallSession") {
    return Kind::kCallSession;
  }
  if (type == "Peer") {
    return Kind::kPeer;
  }
  if (type == "MediaLeg") {
    return Kind::kMediaLeg;
  }
  if (type == "TimerHandle") {
    return Kind::kTimerHandle;
  }
  throw ProtocolError("unsupported demo type '" + type + "'");
}

template <typename T>
Allocation MakeArray(Kind kind, std::size_t count) {
  T* data = new T[count]();
  return Allocation{kind, count,
                    std::shared_ptr<void>(data, [](void* pointer) {
                      delete[] static_cast<T*>(pointer);
                    })};
}

void AddAllocation(RuntimeState& runtime, const std::string& id,
                   Allocation allocation) {
  if (!runtime.allocations.emplace(id, std::move(allocation)).second) {
    throw ProtocolError("duplicate object/external id '" + id + "'");
  }
}

ResolvedPointer Resolve(const RuntimeState& runtime, const std::string& id,
                        std::size_t index) {
  if (id == "null") {
    if (index != 0) {
      throw ProtocolError("null pointer must use index 0");
    }
    return {};
  }
  const auto found = runtime.allocations.find(id);
  if (found == runtime.allocations.end()) {
    throw ProtocolError("reference to unknown object id '" + id + "'");
  }
  const Allocation& allocation = found->second;
  if (index >= allocation.count) {
    throw ProtocolError("index " + std::to_string(index) +
                        " is outside allocation '" + id + "'");
  }
  void* address = nullptr;
  switch (allocation.kind) {
    case Kind::kCallSession:
      address =
          static_cast<call_demo::CallSession*>(allocation.storage.get()) + index;
      break;
    case Kind::kPeer:
      address = static_cast<call_demo::Peer*>(allocation.storage.get()) + index;
      break;
    case Kind::kMediaLeg:
      address =
          static_cast<call_demo::MediaLeg*>(allocation.storage.get()) + index;
      break;
    case Kind::kTimerHandle:
      address =
          static_cast<call_demo::TimerHandle*>(allocation.storage.get()) + index;
      break;
    case Kind::kNull:
      break;
  }
  return {address, allocation.kind};
}

void RequireKind(const ResolvedPointer& value, Kind expected,
                 const std::string& context) {
  if (value.kind != Kind::kNull && value.kind != expected) {
    throw ProtocolError(context + ": pointer target has the wrong type");
  }
}

void SetScalar(const RuntimeState& runtime, const ScalarRecord& record) {
  const ResolvedPointer target =
      Resolve(runtime, record.object_id, record.index);
  const std::string context = record.object_id + "." + record.field;

  if (target.kind == Kind::kCallSession) {
    auto* call = static_cast<call_demo::CallSession*>(target.address);
    if (record.field == "call_id" && record.encoding == "i32") {
      call->call_id = ParseI32(record.value, context);
      return;
    }
    if (record.field == "state" && record.encoding == "enum") {
      const auto value = ParseI32(record.value, context);
      if (value < static_cast<std::int32_t>(call_demo::CallState::kIdle) ||
          value >
              static_cast<std::int32_t>(call_demo::CallState::kRemoteHold)) {
        throw ProtocolError(context + ": unknown CallState value");
      }
      call->state = static_cast<call_demo::CallState>(value);
      return;
    }
  } else if (target.kind == Kind::kMediaLeg) {
    auto* leg = static_cast<call_demo::MediaLeg*>(target.address);
    if (record.field == "id" && record.encoding == "i32") {
      leg->id = ParseI32(record.value, context);
      return;
    }
    if (record.field == "active" && record.encoding == "bool") {
      leg->active = ParseBool(record.value, context);
      return;
    }
  }
  throw ProtocolError(context + ": unsupported scalar field or encoding");
}

void SetString(const RuntimeState& runtime, const StringRecord& record) {
  const ResolvedPointer target =
      Resolve(runtime, record.object_id, record.index);
  const std::string context = record.object_id + "." + record.field;
  if (target.kind != Kind::kPeer || record.field != "uri") {
    throw ProtocolError(context + ": unsupported string field");
  }
  auto* peer = static_cast<call_demo::Peer*>(target.address);
  if (record.value.size() >= sizeof(peer->uri)) {
    throw ProtocolError(context + ": string exceeds destination capacity");
  }
  std::memcpy(peer->uri, record.value.c_str(), record.value.size() + 1);
}

void BindEdge(const RuntimeState& runtime, const EdgeRecord& record) {
  const ResolvedPointer target =
      Resolve(runtime, record.target_id, record.target_index);
  const std::string context = record.owner_id + "." + record.field;

  if (record.owner_id == "root") {
    if (record.owner_index != 0) {
      throw ProtocolError(context + ": root index must be 0");
    }
    if (record.field == "g_call") {
      RequireKind(target, Kind::kCallSession, context);
      call_demo::g_call = static_cast<call_demo::CallSession*>(target.address);
      return;
    }
    if (record.field == "g_active_leg") {
      RequireKind(target, Kind::kMediaLeg, context);
      call_demo::g_active_leg =
          static_cast<call_demo::MediaLeg*>(target.address);
      return;
    }
    throw ProtocolError(context + ": unknown global root");
  }

  const ResolvedPointer owner =
      Resolve(runtime, record.owner_id, record.owner_index);
  if (owner.kind == Kind::kCallSession) {
    auto* call = static_cast<call_demo::CallSession*>(owner.address);
    if (record.field == "peer") {
      RequireKind(target, Kind::kPeer, context);
      call->peer = static_cast<call_demo::Peer*>(target.address);
      return;
    }
    if (record.field == "active_leg") {
      RequireKind(target, Kind::kMediaLeg, context);
      call->active_leg = static_cast<call_demo::MediaLeg*>(target.address);
      return;
    }
    if (record.field == "refresh_timer") {
      RequireKind(target, Kind::kTimerHandle, context);
      call->refresh_timer = static_cast<call_demo::TimerHandle*>(target.address);
      return;
    }
  } else if (owner.kind == Kind::kPeer && record.field == "owner") {
    RequireKind(target, Kind::kCallSession, context);
    static_cast<call_demo::Peer*>(owner.address)->owner =
        static_cast<call_demo::CallSession*>(target.address);
    return;
  } else if (owner.kind == Kind::kMediaLeg) {
    auto* leg = static_cast<call_demo::MediaLeg*>(owner.address);
    if (record.field == "owner") {
      RequireKind(target, Kind::kCallSession, context);
      leg->owner = static_cast<call_demo::CallSession*>(target.address);
      return;
    }
    if (record.field == "sibling") {
      RequireKind(target, Kind::kMediaLeg, context);
      leg->sibling = static_cast<call_demo::MediaLeg*>(target.address);
      return;
    }
  }
  throw ProtocolError(context + ": unsupported pointer field");
}

bool ScalarMatches(const RuntimeState& runtime, const ScalarRecord& expected,
                   std::string* actual) {
  const ResolvedPointer target =
      Resolve(runtime, expected.object_id, expected.index);
  if (target.kind == Kind::kCallSession && expected.field == "state" &&
      expected.encoding == "enum") {
    const auto value = static_cast<std::int32_t>(
        static_cast<call_demo::CallSession*>(target.address)->state);
    *actual = std::to_string(value);
    return value == ParseI32(expected.value, "oracle");
  }
  if (target.kind == Kind::kMediaLeg && expected.field == "active" &&
      expected.encoding == "bool") {
    const bool value =
        static_cast<call_demo::MediaLeg*>(target.address)->active;
    *actual = value ? "true" : "false";
    return value == ParseBool(expected.value, "oracle");
  }
  throw ProtocolError(expected.object_id + "." + expected.field +
                      ": unsupported oracle field");
}

}  // namespace

struct ReplayContext::Impl {
  RuntimeState runtime;
  std::unordered_map<std::string, std::string> metadata;
  std::vector<ExpectedCall> expected_calls;
  std::vector<ScalarRecord> expectations;
};

ReplayContext::ReplayContext(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

ReplayContext::~ReplayContext() { call_demo::ResetGlobals(); }

std::unique_ptr<ReplayContext> ReplayContext::Load(std::istream& input) {
  call_demo::ResetGlobals();
  ParsedSnapshot parsed = Parse(input);
  auto impl = std::make_unique<Impl>();

  try {
    // Phase 1: allocate every object so forward references and cycles are safe.
    for (const auto& object : parsed.objects) {
      const std::string& id = std::get<0>(object);
      const Kind kind = ParseKind(std::get<1>(object));
      const std::size_t count = std::get<2>(object);
      switch (kind) {
        case Kind::kCallSession:
          AddAllocation(impl->runtime, id,
                        MakeArray<call_demo::CallSession>(kind, count));
          break;
        case Kind::kPeer:
          AddAllocation(impl->runtime, id,
                        MakeArray<call_demo::Peer>(kind, count));
          break;
        case Kind::kMediaLeg:
          AddAllocation(impl->runtime, id,
                        MakeArray<call_demo::MediaLeg>(kind, count));
          break;
        case Kind::kTimerHandle:
        case Kind::kNull:
          throw ProtocolError("TimerHandle must be declared as external");
      }
    }

    // External resources are reconstructed by type-specific adapters.
    for (const auto& external : parsed.externals) {
      const std::string& id = std::get<0>(external);
      const Kind kind = ParseKind(std::get<1>(external));
      if (kind != Kind::kTimerHandle) {
        throw ProtocolError("unsupported external adapter for '" + id + "'");
      }
      Allocation timer = MakeArray<call_demo::TimerHandle>(kind, 1);
      static_cast<call_demo::TimerHandle*>(timer.storage.get())->token =
          std::get<2>(external);
      AddAllocation(impl->runtime, id, std::move(timer));
    }

    // Phase 2: copy value fields and process-wide scalar globals.
    for (const GlobalRecord& global : parsed.globals) {
      if (global.name == "g_service_ready" &&
          global.encoding == "bool") {
        call_demo::g_service_ready = ParseBool(global.value, global.name);
      } else {
        throw ProtocolError("unsupported global scalar '" + global.name + "'");
      }
    }
    for (const ScalarRecord& scalar : parsed.scalars) {
      SetScalar(impl->runtime, scalar);
    }
    for (const StringRecord& string : parsed.strings) {
      SetString(impl->runtime, string);
    }

    // Phase 3: fix all pointer fields. IDs plus element indexes preserve aliasing,
    // cycles, and interior pointers without persisting raw addresses.
    for (const EdgeRecord& edge : parsed.edges) {
      BindEdge(impl->runtime, edge);
    }

    impl->metadata = std::move(parsed.metadata);
    impl->expected_calls = std::move(parsed.expected_calls);
    impl->expectations = std::move(parsed.expectations);
  } catch (...) {
    call_demo::ResetGlobals();
    throw;
  }

  return std::unique_ptr<ReplayContext>(new ReplayContext(std::move(impl)));
}

std::unique_ptr<ReplayContext> ReplayContext::LoadFile(
    const std::string& path) {
  std::ifstream input(path);
  if (!input) {
    throw ProtocolError("cannot open snapshot '" + path + "'");
  }
  return Load(input);
}

const std::unordered_map<std::string, std::string>& ReplayContext::metadata()
    const {
  return impl_->metadata;
}

const std::vector<ExpectedCall>& ReplayContext::expected_calls() const {
  return impl_->expected_calls;
}

bool ReplayContext::OracleMatches(std::string* mismatch) const {
  for (const ScalarRecord& expected : impl_->expectations) {
    std::string actual;
    if (!ScalarMatches(impl_->runtime, expected, &actual)) {
      if (mismatch != nullptr) {
        *mismatch = expected.object_id + "[" + std::to_string(expected.index) +
                    "]." + expected.field + ": expected " + expected.value +
                    ", actual " + actual;
      }
      return false;
    }
  }
  if (mismatch != nullptr) {
    mismatch->clear();
  }
  return true;
}

}  // namespace gsr
