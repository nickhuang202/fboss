// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <boost/container/flat_set.hpp>
#include <folly/Conv.h>
#include <folly/IPAddressV4.h>
#include <folly/IPAddressV6.h>

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "fboss/agent/AddressUtil.h"
#include "fboss/agent/TxPacket.h"
#include "fboss/agent/if/gen-cpp2/common_types.h"
#include "fboss/agent/packet/EthFrame.h"
#include "fboss/agent/test/EcmpSetupHelper.h"
#include "fboss/agent/test/agent_hw_tests/AgentMPLSDataplaneTest.h"
#include "fboss/agent/test/agent_hw_tests/AgentMPLSDataplaneTestUtils.h"
#include "fboss/agent/test/utils/TrapPacketUtils.h"
#include "fboss/agent/types.h"

#include <gtest/gtest.h>

namespace {

namespace mpls_test = facebook::fboss::utility::mpls_dataplane_test;
using mpls_test::MplsIpVersion;
using mpls_test::MplsPacketInjectionType;
using mpls_test::MplsTrapPacketMechanism;

const facebook::fboss::LabelForwardingAction::Label kSwapLabel{201};
constexpr uint32_t kSinglePushedLabelBase = 101;
constexpr uint32_t kMaxPushedLabelBase = 1001;
constexpr auto kUdpPayloadSize = 256;

using MplsHeadEndPortTypes =
    ::testing::Types<facebook::fboss::PortID, facebook::fboss::AggregatePortID>;

} // namespace

namespace facebook::fboss {

template <typename AddrT>
RoutePrefix<AddrT> headEndIp2MplsRoutePrefix();

template <>
RoutePrefix<folly::IPAddressV4> headEndIp2MplsRoutePrefix() {
  return {folly::IPAddressV4{"200.1.1.0"}, 24};
}

template <>
RoutePrefix<folly::IPAddressV6> headEndIp2MplsRoutePrefix() {
  return {folly::IPAddressV6{"2001::"}, 64};
}

template <typename AddrT>
AddrT headEndIngressPacketSrcIp();

template <>
folly::IPAddressV4 headEndIngressPacketSrcIp() {
  return folly::IPAddressV4{"100.1.1.1"};
}

template <>
folly::IPAddressV6 headEndIngressPacketSrcIp() {
  return folly::IPAddressV6{"1001::1"};
}

template <typename AddrT>
AddrT headEndIngressPacketDstIp();

template <>
folly::IPAddressV4 headEndIngressPacketDstIp() {
  return folly::IPAddressV4{"200.1.1.1"};
}

template <>
folly::IPAddressV6 headEndIngressPacketDstIp() {
  return folly::IPAddressV6{"2001::1"};
}

template <typename AddrT>
MplsIpVersion headEndIpVersion();

template <>
MplsIpVersion headEndIpVersion<folly::IPAddressV4>() {
  return MplsIpVersion::V4;
}

template <>
MplsIpVersion headEndIpVersion<folly::IPAddressV6>() {
  return MplsIpVersion::V6;
}

template <typename PortType>
class AgentMPLSHeadEndTest : public AgentMPLSDataplaneTest<PortType> {
 protected:
  using BaseT = AgentMPLSDataplaneTest<PortType>;
  using MplsEcmpSetupHelper =
      utility::MplsEcmpSetupTargetedPorts<folly::IPAddressV6>;

  using BaseT::applyConfigAndEnableTrunks;
  using BaseT::egressPort;
  using BaseT::egressPortDescriptor;
  using BaseT::getAgentEnsemble;
  using BaseT::getProgrammedState;
  using BaseT::getSw;
  using BaseT::getVlanIDForTx;
  using BaseT::ingressPort;
  using BaseT::initialConfig;
  using BaseT::maxPushedLabelStack;
  using BaseT::pushedLabelStack;
  using BaseT::pushedTopLabel;
  using BaseT::routerMac;
  using BaseT::secondPassEgressPort;

  std::vector<ProductionFeature> getProductionFeaturesVerified()
      const override {
    if constexpr (BaseT::kIsTrunk) {
      return {ProductionFeature::MPLS_HEADEND, ProductionFeature::LAG};
    }
    return {ProductionFeature::MPLS_HEADEND};
  }

  MplsTrapPacketMechanism trapPacketMechanism() const {
    auto asic = checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
    return asic->isSupported(HwAsic::Feature::SAI_ACL_ENTRY_SRC_PORT_QUALIFIER)
        ? MplsTrapPacketMechanism::SrcPortAcl
        : MplsTrapPacketMechanism::TtlExpiry;
  }

  LabelForwardingAction::LabelStack singlePushedLabelStack() const {
    return pushedLabelStack(kSinglePushedLabelBase, 1);
  }

  LabelForwardingAction::LabelStack maxPushedLabelStack() const {
    return maxPushedLabelStack(kMaxPushedLabelBase);
  }

  template <typename AddrT>
  std::unique_ptr<utility::EcmpSetupTargetedPorts<AddrT>> setupIpEcmpHelper()
      const {
    return std::make_unique<utility::EcmpSetupTargetedPorts<AddrT>>(
        getProgrammedState(), getSw()->needL2EntryForNeighbor());
  }

  std::unique_ptr<MplsEcmpSetupHelper> setupMplsECMPHelper(
      Label topLabel,
      LabelForwardingAction::LabelForwardingType actionType) const {
    return std::make_unique<MplsEcmpSetupHelper>(
        getProgrammedState(),
        getSw()->needL2EntryForNeighbor(),
        topLabel,
        actionType);
  }

  template <typename AddrT>
  void configureStaticIp2MplsPushRoute(
      cfg::SwitchConfig& config,
      const LabelForwardingAction::LabelStack& pushStack) const {
    config.staticIp2MplsRoutes()->emplace_back();
    auto& route = config.staticIp2MplsRoutes()->back();
    route.prefix() = headEndIp2MplsRoutePrefix<AddrT>().str();

    auto helper = setupIpEcmpHelper<AddrT>();
    auto nhop = helper->nhop(egressPortDescriptor());

    NextHopThrift nextHopThrift;
    nextHopThrift.address() = network::toBinaryAddress(nhop.ip);
    nextHopThrift.address()->ifName() =
        folly::to<std::string>("fboss", nhop.intf);
    nextHopThrift.mplsAction() =
        LabelForwardingAction(
            LabelForwardingAction::LabelForwardingType::PUSH, pushStack)
            .toThrift();
    route.nexthops()->push_back(nextHopThrift);
  }

  void configureStaticMplsRoute(
      cfg::SwitchConfig& config,
      Label ingressLabel,
      const LabelForwardingAction& action,
      PortDescriptor nextHop) const {
    config.staticMplsRoutesWithNhops()->emplace_back();
    auto& route = config.staticMplsRoutesWithNhops()->back();
    route.ingressLabel() = ingressLabel.value();

    auto helper = setupMplsECMPHelper(ingressLabel, action.type());
    auto nhop = helper->nhop(std::move(nextHop));

    NextHopThrift nextHopThrift;
    nextHopThrift.address() = network::toBinaryAddress(nhop.ip);
    nextHopThrift.address()->ifName() =
        folly::to<std::string>("fboss", nhop.intf);
    nextHopThrift.mplsAction() = action.toThrift();
    route.nexthops()->push_back(nextHopThrift);
  }

  void configureStaticMplsSwapRoute(
      cfg::SwitchConfig& config,
      Label ingressLabel,
      LabelForwardingAction::Label swapLabel,
      PortDescriptor nextHop) const {
    configureStaticMplsRoute(
        config,
        ingressLabel,
        LabelForwardingAction(
            LabelForwardingAction::LabelForwardingType::SWAP, swapLabel),
        std::move(nextHop));
  }

  void configureTrapPacketMechanism(
      cfg::SwitchConfig& config,
      MplsTrapPacketMechanism mechanism,
      const LabelForwardingAction::LabelStack& pushStack) const {
    switch (mechanism) {
      case MplsTrapPacketMechanism::SrcPortAcl: {
        auto asic =
            checkSameAndGetAsicForTesting(getAgentEnsemble()->getL3Asics());
        utility::addTrapPacketAcl(asic, &config, egressPort());
        break;
      }
      case MplsTrapPacketMechanism::TtlExpiry:
        configureStaticMplsSwapRoute(
            config,
            pushedTopLabel(pushStack),
            kSwapLabel,
            PortDescriptor(secondPassEgressPort()));
        break;
    }
  }

  template <typename AddrT>
  void resolveIpNextHopForPortWithMac(
      const PortDescriptor& nextHop,
      folly::MacAddress nextHopMac) {
    this->applyNewState(
        [this, nextHop, nextHopMac](const std::shared_ptr<SwitchState>& state) {
          utility::EcmpSetupTargetedPorts<AddrT> helper(
              state, getSw()->needL2EntryForNeighbor(), nextHopMac);
          return helper.resolveNextHops(
              state, boost::container::flat_set<PortDescriptor>{nextHop});
        },
        "resolve head-end IP nexthop with explicit MAC");
  }

  void resolveMplsNextHopForPort(
      const PortDescriptor& nextHop,
      Label topLabel,
      LabelForwardingAction::LabelForwardingType actionType) {
    this->applyNewState(
        [this, nextHop, topLabel, actionType](
            const std::shared_ptr<SwitchState>& state) {
          auto helper = MplsEcmpSetupHelper(
              state, getSw()->needL2EntryForNeighbor(), topLabel, actionType);
          return helper.resolveNextHops(
              state, boost::container::flat_set<PortDescriptor>{nextHop});
        },
        "resolve head-end MPLS nexthop");
  }

  template <typename AddrT>
  utility::EthFrame makeIpIngressFrame(uint8_t ttlOrHopLimit) const {
    auto vlan = getVlanIDForTx();
    CHECK(vlan.has_value());

    constexpr auto isV4 = std::is_same_v<AddrT, folly::IPAddressV4>;
    constexpr auto etherType =
        isV4 ? ETHERTYPE::ETHERTYPE_IPV4 : ETHERTYPE::ETHERTYPE_IPV6;
    auto tags = EthHdr::VlanTags_t{VlanTag(*vlan, 0x8100)};
    EthHdr ethHdr{
        utility::kLocalCpuMac(),
        routerMac(),
        {tags},
        static_cast<uint16_t>(etherType)};

    std::conditional_t<isV4, IPv4Hdr, IPv6Hdr> ipHdr;
    ipHdr.srcAddr = headEndIngressPacketSrcIp<AddrT>();
    ipHdr.dstAddr = headEndIngressPacketDstIp<AddrT>();
    ipHdr.setProtocol(static_cast<uint8_t>(IP_PROTO::IP_PROTO_UDP));
    if constexpr (isV4) {
      ipHdr.ttl = ttlOrHopLimit;
    } else {
      ipHdr.hopLimit = ttlOrHopLimit;
    }

    UDPHeader udpHdr;
    udpHdr.srcPort = 10000;
    udpHdr.dstPort = 20000;

    return utility::EthFrame(
        ethHdr,
        utility::IPPacket<AddrT>(
            ipHdr,
            utility::UDPDatagram(
                udpHdr, std::vector<uint8_t>(kUdpPayloadSize, 0xff))));
  }

  template <typename AddrT>
  std::unique_ptr<TxPacket> makeIpIngressPacket(uint8_t ttlOrHopLimit) const {
    auto frame = makeIpIngressFrame<AddrT>(ttlOrHopLimit);
    return frame.getTxPacket(
        [sw = getSw()](uint32_t size) { return sw->allocatePacket(size); });
  }

  template <typename AddrT>
  void sendIpIngressPacket(
      uint8_t ttlOrHopLimit,
      MplsPacketInjectionType injectionType) {
    auto pkt = makeIpIngressPacket<AddrT>(ttlOrHopLimit);
    switch (injectionType) {
      case MplsPacketInjectionType::FrontPanel:
        EXPECT_TRUE(
            getAgentEnsemble()->ensureSendPacketOutOfPort(
                std::move(pkt), ingressPort()));
        break;
      case MplsPacketInjectionType::Cpu:
        EXPECT_TRUE(
            getAgentEnsemble()->ensureSendPacketSwitched(std::move(pkt)));
        break;
    }
  }

  template <typename AddrT>
  void setupStaticIp2MplsRoutePush(
      const LabelForwardingAction::LabelStack& pushStack) {
    auto mechanism = trapPacketMechanism();
    auto config = initialConfig(*getAgentEnsemble());
    configureStaticIp2MplsPushRoute<AddrT>(config, pushStack);
    configureTrapPacketMechanism(config, mechanism, pushStack);
    applyConfigAndEnableTrunks(config);

    resolveIpNextHopForPortWithMac<AddrT>(egressPortDescriptor(), routerMac());
    if (mechanism == MplsTrapPacketMechanism::TtlExpiry) {
      resolveMplsNextHopForPort(
          PortDescriptor(secondPassEgressPort()),
          pushedTopLabel(pushStack),
          LabelForwardingAction::LabelForwardingType::SWAP);
    }
  }

  template <typename AddrT>
  void verifyIp2MplsPushAndTrapPacket(
      MplsPacketInjectionType injectionType,
      const LabelForwardingAction::LabelStack& expectedPushStack) {
    auto mechanism = trapPacketMechanism();
    BaseT::verifyMplsPushAndTrapPacket(
        "mpls-head-end-push-verifier",
        headEndIpVersion<AddrT>(),
        injectionType,
        mechanism,
        expectedPushStack,
        [this, injectionType](uint8_t ttlOrHopLimit) {
          sendIpIngressPacket<AddrT>(ttlOrHopLimit, injectionType);
        });
  }
};

TYPED_TEST_SUITE(AgentMPLSHeadEndTest, MplsHeadEndPortTypes);

TYPED_TEST(AgentMPLSHeadEndTest, PushLabel) {
  auto setup = [this]() {
    this->template setupStaticIp2MplsRoutePush<folly::IPAddressV6>(
        this->singlePushedLabelStack());
  };

  auto verify = [this]() {
    auto pushStack = this->singlePushedLabelStack();
    this->template verifyIp2MplsPushAndTrapPacket<folly::IPAddressV6>(
        MplsPacketInjectionType::FrontPanel, pushStack);
  };

  this->verifyAcrossWarmBoots(setup, verify);
}

} // namespace facebook::fboss
