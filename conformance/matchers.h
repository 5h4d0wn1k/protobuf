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
// runner covers.

#ifndef GOOGLE_PROTOBUF_CONFORMANCE_MATCHERS_H__
#define GOOGLE_PROTOBUF_CONFORMANCE_MATCHERS_H__

#include <gtest/gtest.h>
#include "absl/strings/string_view.h"
#include "conformance/binary_wireformat.h"
#include "conformance/testee.h"
#include "google/protobuf/message.h"

namespace google {
namespace protobuf {
namespace conformance {

// Matches a message equivalent to the message described by `text`, in text
// format, parsed as the actual message's type.  Equivalence is determined by
// MessageDifferencer with NaNs comparing equal.
testing::Matcher<const Message&> EqualsTextProto(absl::string_view text);

// Like EqualsTextProto(), but the expected message is the binary serialization
// `bytes`, parsed as the actual message's type.  Taking a Wire rather than a
// string keeps string literals, which stop at the first NUL byte, out.
testing::Matcher<const Message&> EqualsBinaryProto(Wire bytes);

// TODO: b/410122158 - Add JSON matchers once JSON support is migrated.

// Matches a response containing a parse error.  Any other response fails with
// the legacy runner's message: "Should have failed to parse, but raised an
// error instead." for a runtime error, "Should have failed to parse, but
// didn't." otherwise.
testing::Matcher<const internal::TestResult&> IsParseError();

// Matches a response containing a serialize error, with the corresponding
// "Should have failed to serialize, ..." messages.
testing::Matcher<const internal::TestResult&> IsSerializeError();

}  // namespace conformance
}  // namespace protobuf
}  // namespace google

#endif  // GOOGLE_PROTOBUF_CONFORMANCE_MATCHERS_H__
