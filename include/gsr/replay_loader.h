#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "gsr/demo_domain.h"

namespace gsr {

class ProtocolError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct ExpectedCall {
  std::string name;
  std::int32_t argument = 0;
};

struct ReplayedCallArguments {
  call_demo::SipRuntime* sip_ptr = nullptr;
  call_demo::RegistrationService* service_ptr = nullptr;
  call_demo::SipHandle h_ua = nullptr;
  call_demo::SipHandle h_dialog = nullptr;
  std::int32_t event = 0;
  call_demo::UaAppEvent* ua_event_ptr = nullptr;
  call_demo::AppEvent* event_ptr = nullptr;
};

class ReplayContext {
 public:
  static std::unique_ptr<ReplayContext> LoadBundle(
      const std::string& directory);

  ~ReplayContext();
  ReplayContext(const ReplayContext&) = delete;
  ReplayContext& operator=(const ReplayContext&) = delete;

  const ReplayedCallArguments& arguments() const;
  const std::unordered_map<std::string, std::string>& manifest() const;
  const std::vector<ExpectedCall>& expected_calls() const;

  // Compares only fields selected by oracle records after the real function
  // has run. Unselected state is deliberately ignored.
  bool OracleMatches(std::string* mismatch) const;

 private:
  struct Impl;
  explicit ReplayContext(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace gsr
