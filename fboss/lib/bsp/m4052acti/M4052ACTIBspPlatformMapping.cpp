// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/lib/bsp/m4052acti/M4052ACTIBspPlatformMapping.h"

#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook::fboss {

M4052ACTIBspPlatformMapping::M4052ACTIBspPlatformMapping()
    : BspPlatformMapping("m4052acti") {}

M4052ACTIBspPlatformMapping::M4052ACTIBspPlatformMapping(
    const std::string& platformMappingStr)
    : BspPlatformMapping(
          apache::thrift::SimpleJSONSerializer::deserialize<
              BspPlatformMappingThrift>(platformMappingStr)) {}

} // namespace facebook::fboss
