// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook {
namespace fboss {

class M4052ACTIBspPlatformMapping : public BspPlatformMapping {
 public:
  M4052ACTIBspPlatformMapping();
  explicit M4052ACTIBspPlatformMapping(
      const std::string& platformMappingStr);
};

} // namespace fboss
} // namespace facebook
