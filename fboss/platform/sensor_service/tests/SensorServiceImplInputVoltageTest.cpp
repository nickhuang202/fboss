// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <fb303/ServiceData.h>
#include <folly/FileUtil.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fboss/platform/sensor_service/PmUnitInfoFetcher.h"
#include "fboss/platform/sensor_service/SensorServiceImpl.h"

using namespace facebook::fboss::platform::sensor_service;
using namespace facebook::fboss::platform::sensor_config;
namespace pm = facebook::fboss::platform::platform_manager;
using ::testing::_;
using ::testing::Return;

namespace facebook::fboss {

class MockPmUnitInfoFetcher : public PmUnitInfoFetcher {
 public:
  MockPmUnitInfoFetcher() : PmUnitInfoFetcher(nullptr) {}
  MOCK_METHOD(
      std::optional<pm::PmUnitInfo>,
      fetch,
      (const std::string& slotPath),
      (const, override));
};

inline pm::PmUnitInfo makePmUnitInfo(bool isPresent) {
  pm::PresenceInfo presenceInfo;
  presenceInfo.isPresent() = isPresent;
  pm::PmUnitInfo pmUnitInfo;
  pmUnitInfo.name() = "FAKE";
  pmUnitInfo.presenceInfo() = presenceInfo;
  return pmUnitInfo;
}

class SensorServiceImplInputVoltageTest : public ::testing::Test {
 protected:
  void SetUp() override {
    PmUnitSensors pmUnitSensors;
    pmUnitSensors.slotPath() = "/";
    pmUnitSensors.pmUnitName() = "MOCK_PSU";
    config_.pmUnitSensorsList() = {pmUnitSensors};

    PowerConfig powerConfig;
    powerConfig.perSlotPowerConfigs() = {};
    powerConfig.otherPowerSensorNames() = {};
    powerConfig.powerDelta() = 0.0;
    powerConfig.inputVoltageSensors() = {};
    config_.powerConfig() = powerConfig;
  }

  void addInputVoltageSensors(const std::vector<std::string>& sensorNames) {
    config_.powerConfig()->inputVoltageSensors() = sensorNames;
    for (const auto& name : sensorNames) {
      PmSensor sensor;
      sensor.name() = name;
      sensor.sysfsPath() = "/tmp/dummy/" + name;
      config_.pmUnitSensorsList()[0].sensors()->push_back(sensor);
    }
  }

  std::map<std::string, SensorData> createPolledData(
      const std::map<std::string, float>& sensorData,
      const std::map<std::string, std::string>& slotPaths = {}) {
    std::map<std::string, SensorData> polledData;
    for (const auto& [sensorName, value] : sensorData) {
      SensorData sData;
      sData.value() = value;
      auto it = slotPaths.find(sensorName);
      if (it != slotPaths.end()) {
        sData.slotPath() = it->second;
      }
      polledData.emplace(sensorName, sData);
    }
    return polledData;
  }

  void runInputVoltageTest(std::map<std::string, SensorData>& polledData) {
    impl_->processInputVoltage(polledData, *config_.powerConfig());
  }

  void createImpl() {
    auto mock = std::make_unique<MockPmUnitInfoFetcher>();
    mockPmUnitInfoFetcher_ = mock.get();
    // Default behavior: any slot path not given an explicit EXPECT_CALL
    // returns nullopt (mimics RPC failure / unknown slot).
    ON_CALL(*mockPmUnitInfoFetcher_, fetch(_))
        .WillByDefault(Return(std::nullopt));
    impl_ = std::make_unique<SensorServiceImpl>(config_);
    impl_->setPmUnitInfoFetcherForTest(std::move(mock));
  }

  void expectMaxVoltageStats(int expectedValue, int expectedFailures = 0) {
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format(
                SensorServiceImpl::kDerivedValue,
                SensorServiceImpl::kMaxInputVoltage)),
        expectedValue);
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format(
                SensorServiceImpl::kDerivedFailure,
                SensorServiceImpl::kMaxInputVoltage)),
        expectedFailures);
  }

  void expectInputPowerType(int expectedType) {
    EXPECT_EQ(
        fb303::fbData->getCounter(
            fmt::format(
                SensorServiceImpl::kDerivedValue,
                SensorServiceImpl::kInputPowerType)),
        expectedType);
  }

  void expectTotalNumPresentPsu(int expectedValue) {
    EXPECT_EQ(
        fb303::fbData->getCounter(SensorServiceImpl::kTotalNumPresentPsu),
        expectedValue);
  }

  void expectNoTotalNumPresentPsuCounter() {
    EXPECT_THROW(
        fb303::fbData->getCounter(SensorServiceImpl::kTotalNumPresentPsu),
        std::invalid_argument);
  }

  void expectUnexpectedNumPresentPsu(int expectedValue) {
    EXPECT_EQ(
        fb303::fbData->getCounter(SensorServiceImpl::kUnexpectedNumPresentPsu),
        expectedValue);
  }

  void expectNoUnexpectedNumPresentPsuCounter() {
    EXPECT_THROW(
        fb303::fbData->getCounter(SensorServiceImpl::kUnexpectedNumPresentPsu),
        std::invalid_argument);
  }

  SensorConfig config_;
  std::unique_ptr<SensorServiceImpl> impl_;
  // Non-owning observer; impl_ owns the mock.
  MockPmUnitInfoFetcher* mockPmUnitInfoFetcher_{nullptr};
};

TEST_F(SensorServiceImplInputVoltageTest, MaxInputVoltageWithMultipleSensors) {
  addInputVoltageSensors({"PSU1_VIN", "PSU2_VIN"});
  createImpl();
  auto polledData =
      createPolledData({{"PSU1_VIN", 220.5}, {"PSU2_VIN", 215.3}});
  runInputVoltageTest(polledData);

  expectMaxVoltageStats(220);
}

TEST_F(SensorServiceImplInputVoltageTest, MaxInputVoltageWithMissingSensor) {
  addInputVoltageSensors({"PSU1_VIN", "PSU2_VIN"});
  createImpl();
  auto polledData = createPolledData({{"PSU1_VIN", 225.0}});
  runInputVoltageTest(polledData);

  expectMaxVoltageStats(225);
}

TEST_F(SensorServiceImplInputVoltageTest, EmptyInputVoltageSensors) {
  createImpl();
  auto polledData = createPolledData({});
  runInputVoltageTest(polledData);
  // Verify no crash when no sensors are configured
  SUCCEED();
}

TEST_F(SensorServiceImplInputVoltageTest, NoSensorData) {
  addInputVoltageSensors({"PSU1_VIN", "PSU2_VIN"});
  createImpl();
  auto polledData = createPolledData({});
  runInputVoltageTest(polledData);

  expectMaxVoltageStats(0, 1);
  expectInputPowerType(SensorServiceImpl::kInputPowerTypeUnknown);
}

TEST_F(SensorServiceImplInputVoltageTest, MaxInputVoltageWithZeroValue) {
  addInputVoltageSensors({"PSU1_VIN", "PSU2_VIN"});
  createImpl();
  auto polledData = createPolledData({{"PSU1_VIN", 0.0}, {"PSU2_VIN", 220.0}});
  runInputVoltageTest(polledData);

  expectMaxVoltageStats(220);
}

TEST_F(
    SensorServiceImplInputVoltageTest,
    InputPowerTypePersistsAcrossReadings) {
  addInputVoltageSensors({"PSU1_VIN", "PSU2_VIN"});
  createImpl();

  auto polledData1 =
      createPolledData({{"PSU1_VIN", 220.0}, {"PSU2_VIN", 215.0}});
  runInputVoltageTest(polledData1);
  expectInputPowerType(SensorServiceImpl::kInputPowerTypeAC);

  auto polledData2 = createPolledData({{"PSU1_VIN", 48.0}, {"PSU2_VIN", 50.0}});
  runInputVoltageTest(polledData2);
  expectInputPowerType(SensorServiceImpl::kInputPowerTypeAC);
}

TEST_F(SensorServiceImplInputVoltageTest, DCPowerTypeEstablished) {
  addInputVoltageSensors({"PSU1_VIN"});
  createImpl();
  auto polledData = createPolledData({{"PSU1_VIN", 50.0f}});
  runInputVoltageTest(polledData);
  expectInputPowerType(SensorServiceImpl::kInputPowerTypeDC);
}

TEST_F(SensorServiceImplInputVoltageTest, UnknownPowerTypeLowVoltage) {
  addInputVoltageSensors({"PSU1_VIN"});
  createImpl();
  auto polledData = createPolledData({{"PSU1_VIN", 5.0f}});
  runInputVoltageTest(polledData);
  expectInputPowerType(SensorServiceImpl::kInputPowerTypeUnknown);
}

TEST_F(SensorServiceImplInputVoltageTest, UnknownPowerTypeGapVoltage) {
  addInputVoltageSensors({"PSU1_VIN"});
  createImpl();
  // 75V is between dcVoltageMax (64) and acVoltageMin (90)
  auto polledData = createPolledData({{"PSU1_VIN", 75.0f}});
  runInputVoltageTest(polledData);
  expectInputPowerType(SensorServiceImpl::kInputPowerTypeUnknown);
  // No thresholds should be assigned when power type is unknown
  EXPECT_FALSE(
      polledData["PSU1_VIN"].thresholds()->lowerCriticalVal().has_value());
  EXPECT_FALSE(
      polledData["PSU1_VIN"].thresholds()->upperCriticalVal().has_value());
}

TEST_F(SensorServiceImplInputVoltageTest, ThresholdsSetOnPowerTypeEstablished) {
  addInputVoltageSensors({"PSU1_VIN", "PSU2_VIN"});
  createImpl();

  auto polledData =
      createPolledData({{"PSU1_VIN", 220.0}, {"PSU2_VIN", 215.0}});

  EXPECT_FALSE(
      polledData["PSU1_VIN"].thresholds()->lowerCriticalVal().has_value());
  EXPECT_FALSE(
      polledData["PSU1_VIN"].thresholds()->upperCriticalVal().has_value());

  runInputVoltageTest(polledData);

  // Thresholds come from thrift PowerConfig defaults
  // (acVoltageMin=90, acVoltageMax=305)
  Thresholds expectedThresholds;
  expectedThresholds.lowerCriticalVal() = 90.0;
  expectedThresholds.upperCriticalVal() = 305.0;
  EXPECT_EQ(*polledData["PSU1_VIN"].thresholds(), expectedThresholds);
  EXPECT_EQ(*polledData["PSU2_VIN"].thresholds(), expectedThresholds);
}

TEST_F(SensorServiceImplInputVoltageTest, ThresholdsSetForDC) {
  addInputVoltageSensors({"PSU1_VIN"});
  createImpl();

  auto polledData = createPolledData({{"PSU1_VIN", 50.0}});
  runInputVoltageTest(polledData);

  // Thresholds come from thrift PowerConfig defaults
  // (dcVoltageMin=9, dcVoltageMax=64)
  Thresholds expectedThresholds;
  expectedThresholds.lowerCriticalVal() = 9.0;
  expectedThresholds.upperCriticalVal() = 64.0;
  EXPECT_EQ(*polledData["PSU1_VIN"].thresholds(), expectedThresholds);
}

TEST_F(
    SensorServiceImplInputVoltageTest,
    ThresholdsNotOverwrittenIfAlreadySet) {
  addInputVoltageSensors({"PSU1_VIN"});
  createImpl();

  auto polledData = createPolledData({{"PSU1_VIN", 220.0}});
  polledData["PSU1_VIN"].thresholds()->lowerCriticalVal() = 100.0f;
  polledData["PSU1_VIN"].thresholds()->upperCriticalVal() = 300.0f;

  runInputVoltageTest(polledData);

  Thresholds expectedThresholds;
  expectedThresholds.lowerCriticalVal() = 100.0f;
  expectedThresholds.upperCriticalVal() = 300.0f;
  EXPECT_EQ(*polledData["PSU1_VIN"].thresholds(), expectedThresholds);
}

TEST_F(SensorServiceImplInputVoltageTest, TotalNumPresentPsuAllPresent) {
  addInputVoltageSensors({"PSU1_VIN", "PSU2_VIN"});
  config_.powerConfig()->minAcPsuCount() = 2;
  int idx = 0;
  for (const auto& name : {"PSU1", "PSU2"}) {
    PerSlotPowerConfig psu;
    psu.name() = name;
    psu.powerSensorName() = std::string(name) + "_PWR";
    psu.slotPath() = fmt::format("/PSU_SLOT@{}", idx++);
    config_.powerConfig()->perSlotPowerConfigs()->push_back(psu);
  }
  createImpl();
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@0"))
      .WillRepeatedly(Return(makePmUnitInfo(true)));
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@1"))
      .WillRepeatedly(Return(makePmUnitInfo(true)));

  auto polledData = createPolledData(
      {{"PSU1_VIN", 220.0},
       {"PSU2_VIN", 220.0},
       {"PSU1_PWR", 500.0},
       {"PSU2_PWR", 500.0}},
      {{"PSU1_VIN", "/PSU_SLOT@0"},
       {"PSU2_VIN", "/PSU_SLOT@1"},
       {"PSU1_PWR", "/PSU_SLOT@0"},
       {"PSU2_PWR", "/PSU_SLOT@1"}});
  runInputVoltageTest(polledData);

  expectTotalNumPresentPsu(2);
  // present (2) >= minAcPsuCount (2) → no alert
  expectUnexpectedNumPresentPsu(0);
}

TEST_F(
    SensorServiceImplInputVoltageTest,
    UnexpectedNumPresentPsuFiresWhenAbsent) {
  // platform_manager reports PSU2 as absent. Sensor read success is not
  // consulted; presence comes from PmUnitInfo.presenceInfo.isPresent.
  // present (1) < minAcPsuCount (2) → boolean fires.
  addInputVoltageSensors({"PSU1_VIN"});
  config_.powerConfig()->minAcPsuCount() = 2;
  int idx = 0;
  for (const auto& name : {"PSU1", "PSU2"}) {
    PerSlotPowerConfig psu;
    psu.name() = name;
    psu.powerSensorName() = std::string(name) + "_PWR";
    psu.voltageSensorName() = std::string(name) + "_VIN";
    psu.slotPath() = fmt::format("/PSU_SLOT@{}", idx++);
    config_.powerConfig()->perSlotPowerConfigs()->push_back(psu);
  }
  createImpl();
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@0"))
      .WillRepeatedly(Return(makePmUnitInfo(true)));
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@1"))
      .WillRepeatedly(Return(makePmUnitInfo(false)));

  auto polledData = createPolledData(
      {{"PSU1_VIN", 220.0},
       {"PSU1_PWR", 500.0},
       {"PSU2_VIN", 220.0},
       {"PSU2_PWR", 500.0}},
      {{"PSU1_VIN", "/PSU_SLOT@0"},
       {"PSU1_PWR", "/PSU_SLOT@0"},
       {"PSU2_VIN", "/PSU_SLOT@1"},
       {"PSU2_PWR", "/PSU_SLOT@1"}});
  runInputVoltageTest(polledData);

  expectTotalNumPresentPsu(1);
  expectUnexpectedNumPresentPsu(1);
}

TEST_F(SensorServiceImplInputVoltageTest, TotalNumPresentPsuExcludesHsc) {
  // HSC slots are not field-replaceable and must not count toward
  // psu.total_num_present, even when platform_manager reports them present.
  addInputVoltageSensors({"PSU1_VIN"});
  config_.powerConfig()->minAcPsuCount() = 1;
  PerSlotPowerConfig psu;
  psu.name() = "PSU1";
  psu.powerSensorName() = "PSU1_PWR";
  psu.slotPath() = "/PSU_SLOT@0";
  config_.powerConfig()->perSlotPowerConfigs()->push_back(psu);
  PerSlotPowerConfig hsc;
  hsc.name() = "HSC1";
  hsc.powerSensorName() = "HSC1_PWR";
  config_.powerConfig()->perSlotPowerConfigs()->push_back(hsc);
  createImpl();
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@0"))
      .WillRepeatedly(Return(makePmUnitInfo(true)));

  auto polledData = createPolledData(
      {{"PSU1_VIN", 220.0}, {"PSU1_PWR", 500.0}, {"HSC1_PWR", 100.0}},
      {{"PSU1_VIN", "/PSU_SLOT@0"},
       {"PSU1_PWR", "/PSU_SLOT@0"},
       {"HSC1_PWR", "/HSC_SLOT@0"}});
  runInputVoltageTest(polledData);

  expectTotalNumPresentPsu(1);
  expectUnexpectedNumPresentPsu(0);
}

TEST_F(SensorServiceImplInputVoltageTest, TotalNumPresentPsuWhenFetchFails) {
  // platform_manager fetch returns nullopt (e.g., RPC failed). The slot is
  // counted as absent — fail closed so dashboards surface the issue.
  addInputVoltageSensors({"PSU1_VIN"});
  config_.powerConfig()->minAcPsuCount() = 1;
  PerSlotPowerConfig psu;
  psu.name() = "PSU1";
  psu.powerSensorName() = "PSU1_PWR";
  psu.slotPath() = "/PSU_SLOT@0";
  config_.powerConfig()->perSlotPowerConfigs()->push_back(psu);
  createImpl();
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@0"))
      .WillRepeatedly(Return(std::nullopt));

  auto polledData = createPolledData(
      {{"PSU1_VIN", 220.0}, {"PSU1_PWR", 500.0}},
      {{"PSU1_VIN", "/PSU_SLOT@0"}, {"PSU1_PWR", "/PSU_SLOT@0"}});
  runInputVoltageTest(polledData);

  expectTotalNumPresentPsu(0);
  // present (0) < minAcPsuCount (1) → alert
  expectUnexpectedNumPresentPsu(1);
}

TEST_F(
    SensorServiceImplInputVoltageTest,
    UnexpectedNumPresentPsuSkippedWhenMinZero) {
  // Platform has PSU slots but minAcPsuCount/minDcPsuCount left at 0
  // (not yet configured). The count is still published; the boolean is
  // skipped because we have nothing to compare against.
  addInputVoltageSensors({"PSU1_VIN"});
  PerSlotPowerConfig psu;
  psu.name() = "PSU1";
  psu.powerSensorName() = "PSU1_PWR";
  psu.slotPath() = "/PSU_SLOT@0";
  config_.powerConfig()->perSlotPowerConfigs()->push_back(psu);
  createImpl();
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@0"))
      .WillRepeatedly(Return(makePmUnitInfo(true)));

  auto polledData = createPolledData(
      {{"PSU1_VIN", 220.0}, {"PSU1_PWR", 500.0}},
      {{"PSU1_VIN", "/PSU_SLOT@0"}, {"PSU1_PWR", "/PSU_SLOT@0"}});
  runInputVoltageTest(polledData);

  expectTotalNumPresentPsu(1);
  expectNoUnexpectedNumPresentPsuCounter();
}

TEST_F(
    SensorServiceImplInputVoltageTest,
    UnexpectedNumPresentPsuSkippedWhenPowerTypeUnknown) {
  // Voltage falls in the gap between DC max (64) and AC min (90), so input
  // power type cannot be determined. Without a determined type, we can't
  // pick AC or DC threshold — boolean is skipped. Count is still published.
  addInputVoltageSensors({"PSU1_VIN"});
  config_.powerConfig()->minAcPsuCount() = 1;
  config_.powerConfig()->minDcPsuCount() = 1;
  PerSlotPowerConfig psu;
  psu.name() = "PSU1";
  psu.powerSensorName() = "PSU1_PWR";
  psu.slotPath() = "/PSU_SLOT@0";
  config_.powerConfig()->perSlotPowerConfigs()->push_back(psu);
  createImpl();
  EXPECT_CALL(*mockPmUnitInfoFetcher_, fetch("/PSU_SLOT@0"))
      .WillRepeatedly(Return(makePmUnitInfo(true)));

  auto polledData = createPolledData(
      {{"PSU1_VIN", 75.0}, {"PSU1_PWR", 500.0}},
      {{"PSU1_VIN", "/PSU_SLOT@0"}, {"PSU1_PWR", "/PSU_SLOT@0"}});
  runInputVoltageTest(polledData);

  expectInputPowerType(SensorServiceImpl::kInputPowerTypeUnknown);
  expectTotalNumPresentPsu(1);
  expectNoUnexpectedNumPresentPsuCounter();
}

TEST_F(SensorServiceImplInputVoltageTest, PresentPsuSkippedForHscOnlyPlatform) {
  addInputVoltageSensors({"HSC_VIN"});
  PerSlotPowerConfig hsc;
  hsc.name() = "HSC1";
  hsc.powerSensorName() = "HSC_PWR";
  config_.powerConfig()->perSlotPowerConfigs()->push_back(hsc);
  createImpl();

  auto polledData = createPolledData({{"HSC_VIN", 220.0}, {"HSC_PWR", 50.0}});
  runInputVoltageTest(polledData);

  expectNoTotalNumPresentPsuCounter();
  expectNoUnexpectedNumPresentPsuCounter();
}

TEST_F(SensorServiceImplInputVoltageTest, PsuWithoutSlotPathSkipped) {
  // Defensive: a PSU/PEM PerSlotPowerConfig without slotPath should log a
  // WARN and be skipped (not counted), so the boolean fires from the low
  // present count.
  addInputVoltageSensors({"PSU1_VIN"});
  config_.powerConfig()->minAcPsuCount() = 1;
  PerSlotPowerConfig psu;
  psu.name() = "PSU1";
  psu.powerSensorName() = "PSU1_PWR";
  // Intentionally NO psu.slotPath().
  config_.powerConfig()->perSlotPowerConfigs()->push_back(psu);
  createImpl();

  auto polledData =
      createPolledData({{"PSU1_VIN", 220.0}, {"PSU1_PWR", 500.0}});
  runInputVoltageTest(polledData);

  expectTotalNumPresentPsu(0);
  expectUnexpectedNumPresentPsu(1);
}

} // namespace facebook::fboss
