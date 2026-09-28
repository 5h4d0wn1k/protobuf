#ifndef GOOGLE_PROTOBUF_CONFORMANCE_TEST_MANAGER_H__
#define GOOGLE_PROTOBUF_CONFORMANCE_TEST_MANAGER_H__

#include <string>
#include <utility>
#include <vector>

#include "absl/container/btree_map.h"
#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "conformance/conformance.pb.h"
#include "conformance/failure_list_trie_node.h"
#include "conformance/testee.h"

namespace google {
namespace protobuf {
namespace conformance {
namespace internal {

// A test whose result contradicted the failure list.  See
// TestManager::UnexpectedFailures() and TestManager::UnexpectedSuccesses().
// TODO: b/563707827 - Remove with conformance_test_runner.
struct UnexpectedResult {
  // The full name of the test, as reported to the TestManager.
  std::string test_name;
  // For an unexpected failure, the test's failure message as
  // TestManager::SaveFailureList() would write it.  For an unexpected success,
  // the message of the failure list entry that matched the test.
  std::string failure_message;
  // For an unexpected success, the failure list entry (possibly a wildcard)
  // that matched `test_name`.  Unset for an unexpected failure, which by
  // definition matched no entry (or matched one with a different message).
  absl::optional<std::string> matched_entry;
};

// This class is used to manage the expected failures and actual results of
// a test suite.  The conformance matchers (next CL) report the outcome of
// every test into here so that results get properly tracked.  The results are
// exposed to the test environment, which reports them as test properties, and
// populate the new failure list when the --fix flag is provided.
class TestManager {
 public:
  TestManager() : expected_failure_list_("root") {}
  ~TestManager();

  // The highest priority level (0 for kP0, 1 for kP1; see TestPriority in
  // testee.h) whose failures fail the suite: ReportFailure() tolerates a
  // failing test above this level unless it is in the failure list.  Defaults
  // to kEnforceAllPriorities.
  void set_enforcement_level(int level) { enforcement_level_ = level; }

  // Loads a list of expected failures from disk, adding to the ones loaded so
  // far.  Returns an error if the file can't be opened, an entry is already
  // present (also from an earlier file) or contains an invalid wildcard.
  absl::Status LoadFailureList(absl::string_view filename);

  // Saves an updated list of failures to disk based on the reported results.
  // Returns an error if `filename` can't be opened for writing or the write
  // fails.
  absl::Status SaveFailureList(absl::string_view filename) const;

  // Reports a successful test run.  This will return an error if the test was
  // expected to fail.
  absl::Status ReportSuccess(absl::string_view test_name);

  // Reports a failed test run along with the failure message.  Returns OK if
  // the failure doesn't fail the suite: either the test is in the failure list
  // with a matching message (an expected failure, logged at INFO), or it isn't
  // listed and `priority` is above the enforcement level (a tolerated failure,
  // logged as a WARNING and only counted by tolerated_failures(): neither a
  // failure nor a skip, and not written to the failure list).  Otherwise the
  // failure is unexpected and the error says why.  A listed test is checked
  // whatever its priority, so that the failure list can't go stale unnoticed.
  // Like the legacy runner, the (normalized) actual message only needs to
  // start with the expected message, so an empty expected message matches any
  // failure.
  absl::Status ReportFailure(absl::string_view test_name, TestPriority priority,
                             absl::string_view failure_message);

  // Reports a test that was skipped by the testee for `skip_reason`.  A skip
  // is not a verdict on the test's failure list entry, if any: the entry
  // counts as seen and matched (so that Finalize() doesn't report it and
  // SaveFailureList() keeps it), but the skip is not an expected failure.  A
  // listed test that is skipped is recorded (see ListedSkips()) and, like an
  // unexpected success, returns an error that names the matched entry.  Whether
  // that error fails the test is the caller's policy (the matchers, next CL,
  // fail such a test).
  absl::Status ReportSkip(absl::string_view test_name,
                          absl::string_view skip_reason);

  // Reports a test the runner didn't run because it wasn't selected (its
  // response was skipped with the runner's not-selected skip reason,
  // kTestNotSelectedSkipReason, added with the matchers in the next CL).
  // Like the legacy runner, which matches a test name against the failure list
  // before checking whether the test was selected, this only marks the entry
  // the name matches (if any) as matched for UnmatchedExpectedFailures(); the
  // test is not counted by any statistic and its entry stays unseen.
  // TODO: b/563707827 - Remove with conformance_test_runner.
  void ReportNotSelected(absl::string_view test_name);

  // Runs sanity checks over the failure list to make sure everything we
  // expected to run was reported: returns an error naming the (sorted) expected
  // failure entries that have not been reported as a failure, an unexpected
  // success or a skip.  Must be called before destruction.
  absl::Status Finalize();

  // Returns the (sorted) expected failure entries that no reported test name
  // (via ReportSuccess(), ReportFailure(), ReportSkip() or ReportNotSelected())
  // has matched so far, regardless of the outcome.  A wildcard entry counts as
  // matched once it has matched any test name.  Unlike the unseen entries
  // Finalize() reports, an entry whose test was not selected is not returned:
  // it is a real test, only not run here.
  // TODO: b/563707827 - Remove with conformance_test_runner.
  std::vector<std::string> UnmatchedExpectedFailures() const;

  // Returns the tests reported as unexpected failures so far, i.e. the ones
  // unexpected_failures() counts, sorted by test name.  Each result's
  // `failure_message` is the message as SaveFailureList() would write it and
  // its `matched_entry` is unset.  Note that this includes tests that were
  // expected to fail, but with a different message; SaveFailureList() replaces
  // their entry, whereas adding these lines to the failure list would
  // duplicate it.
  // TODO: b/563707827 - Remove with conformance_test_runner.
  std::vector<UnexpectedResult> UnexpectedFailures() const;

  // Returns the tests reported as unexpected successes so far, i.e. the ones
  // unexpected_successes() counts, sorted by test name.  Each result's
  // `matched_entry` is the failure list entry the test matched, and
  // `failure_message` is that entry's message.
  // TODO: b/563707827 - Remove with conformance_test_runner.
  std::vector<UnexpectedResult> UnexpectedSuccesses() const;

  // Returns the tests reported as skipped by the testee although they are in
  // the failure list, i.e. the ones listed_skips() counts, sorted by test name.
  // Each pair is the test name and the failure list entry it matched.  Such a
  // test fails the gtest run (the matchers, next CL, fail it with the error
  // ReportSkip() returns), but it is not an unexpected failure or success: its
  // entry is kept as is, also by SaveFailureList() under --fix, so removing
  // the entry is up to the user.
  // TODO: b/563707827 - Remove with conformance_test_runner.
  std::vector<std::pair<std::string, std::string>> ListedSkips() const;

  // The number of tests that were reported skipped.  This includes the listed
  // skips.
  int skipped() const { return skipped_; }

  // The number of skipped tests that are in the failure list.  See
  // ListedSkips().
  int listed_skips() const { return listed_skips_; }

  // The number of tests that failed but were tolerated because their priority
  // is above the enforcement level.
  int tolerated_failures() const { return tolerated_failures_; }

  // The number of tests that were in the failure list and failed with the
  // expected message.
  int expected_failures() const { return expected_failures_; }

  // The number of tests that failed but were not in the failure list, or
  // failed with a different message than the one listed.
  int unexpected_failures() const { return unexpected_failures_; }

  // The number of tests that were not in the failure list and succeeded.
  int expected_successes() const { return expected_successes_; }

  // The number of tests that were in the failure list but succeeded.
  int unexpected_successes() const { return unexpected_successes_; }

 private:
  // Adds one failure list entry, as LoadFailureList() does for each line.
  // Returns an error if the test name is already present or contains an
  // invalid wildcard.
  absl::Status AddExpectedFailure(absl::string_view test_name,
                                  absl::string_view failure_message);

  // The (sorted) expected failure entries Finalize() complains about: those
  // not reported as a failure, an unexpected success or a skip so far.
  std::vector<std::string> UnseenExpectedFailures() const;

  // Marks the failure list entry `test_name` matches, if any, as matched (see
  // UnmatchedExpectedFailures()) and returns it.
  absl::optional<std::string> MarkMatched(absl::string_view test_name);

  FailureListTrieNode expected_failure_list_;
  absl::flat_hash_map<std::string, std::string> expected_failure_messages_;

  absl::flat_hash_set<std::string> unseen_expected_failures_;
  // Entries never matched by name by any Report*() call.
  absl::flat_hash_set<std::string> unmatched_expected_failures_;
  absl::flat_hash_set<std::string> seen_unexpected_successes_;
  absl::flat_hash_map<std::string, int> number_of_matches_;

  // Every test name reported so far, so that a test reported more than once
  // (which the matchers prevent, but nothing else does) is only counted once.
  absl::flat_hash_set<std::string> seen_tests_;

  // The tests counted by unexpected_failures_, mapped to their formatted
  // failure message, and the tests counted by unexpected_successes_, mapped to
  // the failure list entry they matched.
  absl::btree_map<std::string, std::string> unexpected_failure_messages_;
  absl::btree_map<std::string, std::string> unexpected_success_matches_;
  // The tests counted by listed_skips_, mapped to the entry they matched.
  absl::btree_map<std::string, std::string> listed_skip_matches_;

  std::vector<std::string> failure_list_lines_;
  absl::btree_map<std::string, std::string> new_failures_;

  int skipped_ = 0;
  int listed_skips_ = 0;
  int tolerated_failures_ = 0;
  int expected_failures_ = 0;
  int unexpected_failures_ = 0;
  int expected_successes_ = 0;
  int unexpected_successes_ = 0;
  bool finalized_ = false;
  int enforcement_level_ = kEnforceAllPriorities;
};

}  // namespace internal
}  // namespace conformance
}  // namespace protobuf
}  // namespace google

#endif  // GOOGLE_PROTOBUF_CONFORMANCE_TEST_MANAGER_H__
