// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <vector>

#include "fboss/agent/test/agent_hw_tests/AgentMPLSDataplaneTest.h"
#include "fboss/agent/types.h"

#include <gtest/gtest.h>

namespace {

using MplsHeadEndPortTypes =
    ::testing::Types<facebook::fboss::PortID, facebook::fboss::AggregatePortID>;

} // namespace

namespace facebook::fboss {

template <typename PortType>
class AgentMPLSHeadEndTest : public AgentMPLSDataplaneTest<PortType> {
 protected:
  using BaseT = AgentMPLSDataplaneTest<PortType>;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (BaseT::kIsTrunk) {
      return {ProductionFeature::MPLS_HEADEND, ProductionFeature::LAG};
    }
    return {ProductionFeature::MPLS_HEADEND};
  }
};

TYPED_TEST_SUITE(AgentMPLSHeadEndTest, MplsHeadEndPortTypes);

} // namespace facebook::fboss
