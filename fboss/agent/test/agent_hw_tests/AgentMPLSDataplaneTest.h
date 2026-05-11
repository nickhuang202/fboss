// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <memory>
#include <type_traits>

#include <folly/MacAddress.h>

#include "fboss/agent/AgentFeatures.h"
#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/state/LabelForwardingAction.h"
#include "fboss/agent/state/PortDescriptor.h"
#include "fboss/agent/state/SwitchState.h"
#include "fboss/agent/test/AgentHwTest.h"
#include "fboss/agent/test/TestUtils.h"
#include "fboss/agent/test/TrunkUtils.h"
#include "fboss/agent/test/utils/CoppTestUtils.h"
#include "fboss/agent/types.h"

namespace facebook::fboss {

template <typename PortType>
class AgentMPLSDataplaneTest : public AgentHwTest {
 protected:
  static constexpr bool kIsTrunk = std::is_same_v<PortType, AggregatePortID>;

  void setCmdLineFlagOverrides() const override {
    AgentHwTest::setCmdLineFlagOverrides();
    FLAGS_observe_rx_packets_without_interface = true;
  }

  cfg::SwitchConfig initialConfig(
      const AgentEnsemble& ensemble) const override {
    auto config = utility::onePortPerInterfaceConfig(
        ensemble.getSw(),
        ensemble.masterLogicalPortIds(),
        true /* interfaceHasSubnet */);

    if constexpr (kIsTrunk) {
      utility::addAggPort(1, {ensemble.masterLogicalPortIds()[0]}, &config);
    }

    utility::setDefaultCpuTrafficPolicyConfig(
        config, ensemble.getL3Asics(), ensemble.isSai());
    utility::addCpuQueueConfig(config, ensemble.getL3Asics(), ensemble.isSai());
    return config;
  }

  PortID egressPort() const {
    return this->masterLogicalInterfacePortIds()[0];
  }

  PortDescriptor egressPortDescriptor() const {
    if constexpr (kIsTrunk) {
      return PortDescriptor(AggregatePortID(1));
    }
    return PortDescriptor(egressPort());
  }

  PortID ingressPort() const {
    return this->masterLogicalInterfacePortIds()[1];
  }

  PortID secondPassEgressPort() const {
    return this->masterLogicalInterfacePortIds()[2];
  }

  folly::MacAddress routerMac() const {
    return getMacForFirstInterfaceWithPortsForTesting(
        this->getProgrammedState());
  }

  void applyConfigAndEnableTrunks(const cfg::SwitchConfig& config) {
    this->applyNewConfig(config);
    if constexpr (kIsTrunk) {
      this->applyNewState(
          [](const std::shared_ptr<SwitchState>& state) {
            return utility::enableTrunkPorts(state);
          },
          "enable trunk ports");
    }
  }

  Label pushedTopLabel(
      const LabelForwardingAction::LabelStack& pushStack) const {
    CHECK(!pushStack.empty());
    return pushStack.back();
  }
};

} // namespace facebook::fboss
