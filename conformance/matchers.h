// Protocol Buffers - Google's data interchange format
// Copyright 2026 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

// This file defines gMock matchers for conformance tests.  They are pure
// matchers that compose with gMock like any other, and the failure text they
// write to the listener is the one that ends up in failure lists: it is kept
// byte-identical to the legacy conformance runner's for every case the legacy
// runner covers.  It also defines how a TestResult is printed when a matcher
// on it fails.

#ifndef GOOGLE_PROTOBUF_CONFORMANCE_MATCHERS_H__
#define GOOGLE_PROTOBUF_CONFORMANCE_MATCHERS_H__

#include <ostream>
#include <string>
#include <type_traits>
#include <utility>

#include <gmock/gmock.h>
#include "absl/strings/string_view.h"
#include "conformance/binary_wireformat.h"
#include "conformance/conformance.pb.h"
#include "conformance/testee.h"
#include "google/protobuf/message.h"

namespace google {
namespace protobuf {
namespace conformance {
namespace internal {

// Pretty-prints a TestResult in gtest failure output: the test's priority and
// name and a short form of the response, with long payloads truncated and
// binary payloads also decoded as the test's message type.  It is declared in
// TestResult's namespace so that gtest finds it through ADL, and lives with the
// matchers because their failures are where results get printed.
void PrintTo(const TestResult& result, std::ostream* os);

// Whether `T` is one of the types accepted as literal bytes by
// EqualsBinaryProto().  Bare `const char*` (and string literals) are
// deliberately excluded: binary data routinely contains NUL bytes, which would
// silently truncate them.
template <typename T>
inline constexpr bool kIsBytesLike =
    std::is_same_v<T, Wire> || std::is_same_v<T, std::string> ||
    std::is_same_v<T, absl::string_view>;

// The bytes held by a bytes-like value.
inline std::string BytesOf(Wire wire) { return std::move(wire).str(); }
inline std::string BytesOf(std::string bytes) { return bytes; }
inline std::string BytesOf(absl::string_view bytes) {
  return std::string(bytes);
}

// Implements EqualsTextProto() and EqualsBinaryProto(): matches a message
// equivalent to the one obtained by decoding `expected` (in `format`) as the
// actual message's type.
class EquivalentMessageMatcher {
 public:
  using is_gtest_matcher = void;

  EquivalentMessageMatcher(::conformance::WireFormat format,
                           std::string expected);

  bool MatchAndExplain(const Message& actual,
                       testing::MatchResultListener* listener) const;
  void DescribeTo(std::ostream* os) const;
  void DescribeNegationTo(std::ostream* os) const;

 private:
  ::conformance::WireFormat format_;
  std::string expected_;
};

// Matches a result whose response holds a specific kind of error.
class FailureMatcher {
 public:
  using is_gtest_matcher = void;

  // `name` is used for descriptions (e.g. "parse error").  `failure_message`
  // is the explanation when the response isn't the expected error, and
  // `runtime_error_failure_message` the one when it is a runtime error.
  FailureMatcher(absl::string_view name,
                 ::conformance::ConformanceResponse::ResultCase expected_result,
                 absl::string_view failure_message,
                 absl::string_view runtime_error_failure_message)
      : name_(name),
        expected_result_(expected_result),
        failure_message_(failure_message),
        runtime_error_failure_message_(runtime_error_failure_message) {}

  bool MatchAndExplain(const TestResult& result,
                       testing::MatchResultListener* listener) const;
  void DescribeTo(std::ostream* os) const { *os << "is a " << name_; }
  void DescribeNegationTo(std::ostream* os) const {
    *os << "is not a " << name_;
  }

 private:
  std::string name_;
  ::conformance::ConformanceResponse::ResultCase expected_result_;
  std::string failure_message_;
  std::string runtime_error_failure_message_;
};

}  // namespace internal

// Matches a message equivalent to the message described by `text`, in text
// format, parsed as the actual message's type.  Equivalence is determined by
// MessageDifferencer with NaNs comparing equal, exactly like the legacy
// conformance runner; the failure message is the legacy "Output was not
// equivalent to reference message: " followed by the differences.
internal::EquivalentMessageMatcher EqualsTextProto(absl::string_view text);

// Like EqualsTextProto(), but the expected message is the binary serialization
// `bytes` (a Wire, std::string or absl::string_view; bare string literals and
// `const char*` are rejected because binary data may contain NUL bytes), parsed
// as the actual message's type.
template <typename T>
internal::EquivalentMessageMatcher EqualsBinaryProto(T bytes) {
  static_assert(internal::kIsBytesLike<T>,
                "EqualsBinaryProto() only accepts Wire, std::string or "
                "absl::string_view: string literals and const char* would be "
                "truncated at the first NUL byte.");
  return internal::EquivalentMessageMatcher(
      ::conformance::PROTOBUF, internal::BytesOf(std::move(bytes)));
}

// TODO: b/410122158 - Add JSON matchers once JSON support is migrated.

// Matches a response containing a parse error.  Any other response fails with
// the legacy runner's message: "Should have failed to parse, but raised an
// error instead." for a runtime error, "Should have failed to parse, but
// didn't." otherwise.
internal::FailureMatcher IsParseError();

// Matches a response containing a serialize error, with the corresponding
// "Should have failed to serialize, ..." messages.
internal::FailureMatcher IsSerializeError();

}  // namespace conformance
}  // namespace protobuf
}  // namespace google

#endif  // GOOGLE_PROTOBUF_CONFORMANCE_MATCHERS_H__
