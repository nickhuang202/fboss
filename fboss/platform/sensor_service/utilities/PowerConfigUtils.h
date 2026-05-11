// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <algorithm>

#include "fboss/platform/sensor_service/if/gen-cpp2/sensor_config_types.h"

namespace facebook::fboss::platform::sensor_service {

// Returns true if any perSlotPowerConfig has a PSU or PEM name prefix.
// HSC and PWRBRK are not field-replaceable and are excluded.
inline bool hasPsuOrPem(const sensor_config::PowerConfig& powerConfig) {
  return std::any_of(
      powerConfig.perSlotPowerConfigs()->begin(),
      powerConfig.perSlotPowerConfigs()->end(),
      [](const auto& cfg) {
        return cfg.name()->starts_with("PSU") || cfg.name()->starts_with("PEM");
      });
}

} // namespace facebook::fboss::platform::sensor_service
