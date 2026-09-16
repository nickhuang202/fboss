/*
 *  Copyright (c) 2018-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#pragma once

#include "fboss/led_service/BspLedManager.h"
#include "fboss/lib/bsp/BspSystemContainer.h"

namespace facebook::fboss {

/*
 * M4052ACTILedManager class definiton:
 *
 * The BspLedManager class managing all LED in the system. The object is spawned
 * by LED Service. This will subscribe to Fsdb to get Switch state update and
 * then update the LED in hardware
 */
class M4052ACTILedManager : public BspLedManager {
 public:
  M4052ACTILedManager();
  virtual ~M4052ACTILedManager() override {}

  // Forbidden copy constructor and assignment operator
  M4052ACTILedManager(M4052ACTILedManager const&) = delete;
  M4052ACTILedManager& operator=(M4052ACTILedManager const&) = delete;
};

} // namespace facebook::fboss
