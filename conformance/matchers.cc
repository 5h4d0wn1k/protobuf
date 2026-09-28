// Protocol Buffers - Google's data interchange format
// Copyright 2026 Google LLC.  All rights reserved.
//
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file or at
// https://developers.google.com/open-source/licenses/bsd

#include "conformance/matchers.h"

#include <cstddef>
#include <memory>
#include <ostream>
#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "absl/log/absl_check.h"
#include "absl/memory/memory.h"
#include "absl/strings/ascii.h"
#include "absl/strings/escaping.h"
#include "absl/strings/string_view.h"
#include "conformance/conformance.pb.h"
#include "conformance/testee.h"
#include "google/protobuf/descriptor.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "google/protobuf/util/field_comparator.h"
#include "google/protobuf/util/message_differencer.h"

namespace google {
namespace protobuf {
namespace conformance {
namespace internal {
namespace {

using ::conformance::ConformanceResponse;
using ::conformance::WireFormat;

// The legacy prefix of every payload mismatch.
constexpr absl::string_view kNotEquivalentPrefix =
    "Output was not equivalent to reference message: ";

// Truncates a payload for debug output, exactly like the legacy runner.
void TruncateDebugPayload(std::string* payload) {
  constexpr size_t kMaxDebugPayloadSize = 200;
  if (payload->size() > kMaxDebugPayloadSize) {
    payload->resize(kMaxDebugPayloadSize);
    payload->append("...(truncated)");
  }
}

// Returns a copy of `response` whose payload is truncated for debug output.
ConformanceResponse TruncateResponse(const ConformanceResponse& response) {
  ConformanceResponse debug_response(response);
  switch (debug_response.result_case()) {
    case ConformanceResponse::kProtobufPayload:
      TruncateDebugPayload(debug_response.mutable_protobuf_payload());
      break;
    case ConformanceResponse::kJsonPayload:
      TruncateDebugPayload(debug_response.mutable_json_payload());
      break;
    case ConformanceResponse::kTextPayload:
      TruncateDebugPayload(debug_response.mutable_text_payload());
      break;
    case ConformanceResponse::kJspbPayload:
      TruncateDebugPayload(debug_response.mutable_jspb_payload());
      break;
    default:
      break;
  }
  return debug_response;
}

// Creates an empty message of the given type, which must be a generated one.
std::unique_ptr<Message> NewMessage(const Descriptor* type) {
  const Message* prototype =
      MessageFactory::generated_factory()->GetPrototype(type);
  ABSL_CHECK(prototype != nullptr)
      << "Not a generated message type: " << type->full_name();
  return absl::WrapUnique(prototype->New());
}

// Prints a message on a single line (expanding Any, short repeated
// primitives) for failure output.  An explicit printer keeps the output
// stable, unlike DebugString()'s.
std::string ToShortString(const Message& message) {
  TextFormat::Printer printer;
  printer.SetSingleLineMode(true);
  printer.SetExpandAny(true);
  printer.SetUseShortRepeatedPrimitives(true);
  std::string text;
  ABSL_CHECK(printer.PrintToString(message, &text));
  absl::StripTrailingAsciiWhitespace(&text);
  return text;
}

}  // namespace

EquivalentMessageMatcher::EquivalentMessageMatcher(WireFormat format,
                                                   std::string expected)
    : format_(format), expected_(std::move(expected)) {
  ABSL_CHECK(format == ::conformance::PROTOBUF ||
             format == ::conformance::TEXT_FORMAT)
      << "Unsupported expected message format " << WireFormat_Name(format);
}

bool EquivalentMessageMatcher::MatchAndExplain(
    const Message& actual, testing::MatchResultListener* listener) const {
  std::unique_ptr<Message> expected = absl::WrapUnique(actual.New());
  if (format_ == ::conformance::PROTOBUF) {
    ABSL_CHECK(expected->ParseFromString(expected_))
        << "Failed to parse expected wire data for "
        << actual.GetDescriptor()->full_name() << ": "
        << absl::CEscape(expected_);
  } else {
    ABSL_CHECK(TextFormat::ParseFromString(expected_, expected.get()))
        << "Failed to parse expected text proto for "
        << actual.GetDescriptor()->full_name() << ": " << expected_;
  }

  util::MessageDifferencer differencer;
  util::DefaultFieldComparator field_comparator;
  field_comparator.set_treat_nan_as_equal(true);
  differencer.set_field_comparator(&field_comparator);
  std::string differences;
  differencer.ReportDifferencesToString(&differences);
  if (differencer.Compare(*expected, actual)) {
    return true;
  }
  *listener << kNotEquivalentPrefix << differences;
  return false;
}

void EquivalentMessageMatcher::DescribeTo(std::ostream* os) const {
  *os << "equals "
      << (format_ == ::conformance::PROTOBUF ? "binary proto" : "text proto")
      << " \"" << absl::CEscape(expected_) << "\"";
}

void EquivalentMessageMatcher::DescribeNegationTo(std::ostream* os) const {
  *os << "doesn't equal "
      << (format_ == ::conformance::PROTOBUF ? "binary proto" : "text proto")
      << " \"" << absl::CEscape(expected_) << "\"";
}

bool FailureMatcher::MatchAndExplain(
    const TestResult& result, testing::MatchResultListener* listener) const {
  ConformanceResponse::ResultCase actual = result.response().result_case();
  if (actual == expected_result_) {
    return true;
  }
  if (actual == ConformanceResponse::kSkipped) {
    *listener << "the testee skipped the test: " << result.response().skipped();
    return false;
  }
  if (actual == ConformanceResponse::kRuntimeError) {
    *listener << runtime_error_failure_message_;
    return false;
  }
  *listener << failure_message_;
  return false;
}

void PrintTo(const TestResult& result, std::ostream* os) {
  *os << PriorityLevelName(result.priority()) << " test \"" << result.name()
      << "\" with response {"
      << ToShortString(TruncateResponse(result.response())) << "}";

  // Binary payloads are opaque, so also show what they decode to.
  if (result.response().has_protobuf_payload()) {
    std::unique_ptr<Message> decoded = NewMessage(result.type());
    if (decoded->ParseFromString(result.response().protobuf_payload())) {
      *os << " (decoded: {" << ToShortString(*decoded) << "})";
    } else {
      *os << " (unparseable)";
    }
  }
}

}  // namespace internal

internal::EquivalentMessageMatcher EqualsTextProto(absl::string_view text) {
  return internal::EquivalentMessageMatcher(::conformance::TEXT_FORMAT,
                                            std::string(text));
}

internal::FailureMatcher IsParseError() {
  return internal::FailureMatcher(
      "parse error", ::conformance::ConformanceResponse::kParseError,
      "Should have failed to parse, but didn't.",
      "Should have failed to parse, but raised an error instead.");
}

internal::FailureMatcher IsSerializeError() {
  return internal::FailureMatcher(
      "serialize error", ::conformance::ConformanceResponse::kSerializeError,
      "Should have failed to serialize, but didn't.",
      "Should have failed to serialize, but raised an error instead.");
}

}  // namespace conformance
}  // namespace protobuf
}  // namespace google
