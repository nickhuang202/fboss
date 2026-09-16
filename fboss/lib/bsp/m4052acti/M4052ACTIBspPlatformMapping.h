// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "fboss/lib/bsp/BspPlatformMapping.h"

namespace facebook {
namespace fboss {

class m4052ACTIBspPlatformMapping : public BspPlatformMapping {
 public:
  m4052ACTIBspPlatformMapping();
  explicit m4052ACTIBspPlatformMapping(
      const std::string& platformMappingStr);
};

} // namespace fboss
} // namespace facebook
