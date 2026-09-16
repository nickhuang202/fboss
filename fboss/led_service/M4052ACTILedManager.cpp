// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include "fboss/led_service/m4052ACTILedManager.h"
#include "fboss/lib/bsp/BspGenericSystemContainer.h"
#include "fboss/lib/bsp/m4052acti/m4052ACTIBspPlatformMapping.h"

namespace facebook::fboss {

/*
 * m4052ACTILedManager ctor()
 *
 * m4052ACTILedManager constructor will create the LedManager object for
 * m4052ACTI platform
 */
m4052ACTILedManager::m4052ACTILedManager() : BspLedManager() {
  init<m4052ACTIBspPlatformMapping>(PlatformType::PLATFORM_m4052ACTI);
  XLOG(INFO) << "Created m4052ACTI BSP LED Manager";
}

} // namespace facebook::fboss
