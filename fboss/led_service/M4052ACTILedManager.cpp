// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/led_service/M4052ACTILedManager.h"
#include "fboss/lib/bsp/BspGenericSystemContainer.h"
#include "fboss/lib/bsp/m4052acti/M4052ACTIBspPlatformMapping.h"

namespace facebook::fboss {

/*
 * M4052ACTILedManager ctor()
 *
 * M4052ACTILedManager constructor will create the LedManager object for
 * M4052ACTI platform
 */
M4052ACTILedManager::M4052ACTILedManager() : BspLedManager() {
  init<M4052ACTIBspPlatformMapping>(PlatformType::PLATFORM_M4052ACTI);
  XLOG(INFO) << "Created M4052ACTI BSP LED Manager";
}

} // namespace facebook::fboss
