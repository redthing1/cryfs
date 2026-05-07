#include <gtest/gtest.h>
#include <cryfs/impl/formatv2/RootSelection.h>

#include <boost/optional.hpp>

#include <string>
#include <vector>

using cryfs::formatv2::AcceptedRoot;
using cryfs::formatv2::AuthenticatedRoot;
using cryfs::formatv2::RootId;
using cryfs::formatv2::RootSelectionStatus;
using cryfs::formatv2::selectRoot;

namespace {

RootId rootId(const std::string &value) {
  return RootId::FromString(value);
}

AuthenticatedRoot root(uint64_t epoch, const std::string &id) {
  return AuthenticatedRoot{epoch, rootId(id)};
}

void expectSelected(
  const std::vector<AuthenticatedRoot> &roots,
  const boost::optional<AcceptedRoot> &localAcceptedRoot,
  const AuthenticatedRoot &expectedRoot) {
  const auto result = selectRoot(roots, localAcceptedRoot);

  ASSERT_EQ(RootSelectionStatus::Selected, result.status);
  ASSERT_TRUE(result.selectedRoot.is_initialized());
  EXPECT_EQ(expectedRoot, *result.selectedRoot);
}

void expectFailure(
  const std::vector<AuthenticatedRoot> &roots,
  const boost::optional<AcceptedRoot> &localAcceptedRoot,
  RootSelectionStatus expectedStatus) {
  const auto result = selectRoot(roots, localAcceptedRoot);

  EXPECT_EQ(expectedStatus, result.status);
  EXPECT_FALSE(result.selectedRoot.is_initialized());
}

}

TEST(RootSelectionTest, FailsWithoutAuthenticatedRoots) {
  expectFailure({}, boost::none, RootSelectionStatus::NoAuthenticatedRoots);
}

TEST(RootSelectionTest, SelectsOnlyAuthenticatedRootWithoutLocalState) {
  expectSelected(
    {root(1, "0000000000000000000000000000000000000000000000000000000000000001")},
    boost::none,
    root(1, "0000000000000000000000000000000000000000000000000000000000000001"));
}

TEST(RootSelectionTest, SelectsHighestEpochWithoutLocalState) {
  expectSelected(
    {
      root(1, "0000000000000000000000000000000000000000000000000000000000000001"),
      root(3, "0000000000000000000000000000000000000000000000000000000000000003"),
      root(2, "0000000000000000000000000000000000000000000000000000000000000002")
    },
    boost::none,
    root(3, "0000000000000000000000000000000000000000000000000000000000000003"));
}

TEST(RootSelectionTest, AcceptsRootNewerThanLocalState) {
  expectSelected(
    {root(5, "0000000000000000000000000000000000000000000000000000000000000005")},
    root(4, "0000000000000000000000000000000000000000000000000000000000000004"),
    root(5, "0000000000000000000000000000000000000000000000000000000000000005"));
}

TEST(RootSelectionTest, AcceptsRootMatchingLocalState) {
  expectSelected(
    {root(7, "0000000000000000000000000000000000000000000000000000000000000007")},
    root(7, "0000000000000000000000000000000000000000000000000000000000000007"),
    root(7, "0000000000000000000000000000000000000000000000000000000000000007"));
}

TEST(RootSelectionTest, FailsWhenDiskRolledBackBeforeLocalEpoch) {
  expectFailure(
    {root(8, "0000000000000000000000000000000000000000000000000000000000000008")},
    root(9, "0000000000000000000000000000000000000000000000000000000000000009"),
    RootSelectionStatus::RollbackDetected);
}

TEST(RootSelectionTest, FailsWhenLocalRootAtSameEpochIsMissing) {
  expectFailure(
    {root(10, "000000000000000000000000000000000000000000000000000000000000000A")},
    root(10, "000000000000000000000000000000000000000000000000000000000000000B"),
    RootSelectionStatus::AcceptedRootMissing);
}

TEST(RootSelectionTest, FailsWhenHighestEpochHasDifferentRootIdentities) {
  expectFailure(
    {
      root(11, "000000000000000000000000000000000000000000000000000000000000000B"),
      root(11, "000000000000000000000000000000000000000000000000000000000000000C")
    },
    boost::none,
    RootSelectionStatus::AmbiguousRootEpoch);
}

TEST(RootSelectionTest, AcceptsDuplicateHighestRootIdentities) {
  expectSelected(
    {
      root(12, "000000000000000000000000000000000000000000000000000000000000000C"),
      root(12, "000000000000000000000000000000000000000000000000000000000000000C")
    },
    boost::none,
    root(12, "000000000000000000000000000000000000000000000000000000000000000C"));
}

TEST(RootSelectionTest, IgnoresAmbiguityBelowHighestEpoch) {
  expectSelected(
    {
      root(13, "000000000000000000000000000000000000000000000000000000000000000D"),
      root(13, "000000000000000000000000000000000000000000000000000000000000000E"),
      root(14, "000000000000000000000000000000000000000000000000000000000000000F")
    },
    boost::none,
    root(14, "000000000000000000000000000000000000000000000000000000000000000F"));
}
