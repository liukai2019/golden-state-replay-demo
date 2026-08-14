#pragma once

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace gsr {

class ProtocolError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct ExpectedCall {
  std::string name;
  std::int32_t argument = 0;
};

class ReplayContext {
 public:
  static std::unique_ptr<ReplayContext> Load(std::istream& input);
  static std::unique_ptr<ReplayContext> LoadFile(const std::string& path);

  ~ReplayContext();
  ReplayContext(const ReplayContext&) = delete;
  ReplayContext& operator=(const ReplayContext&) = delete;

  const std::unordered_map<std::string, std::string>& metadata() const;
  const std::vector<ExpectedCall>& expected_calls() const;

  // Evaluates the post-action values declared by `expect` records.
  bool OracleMatches(std::string* mismatch) const;

 private:
  struct Impl;
  explicit ReplayContext(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace gsr
