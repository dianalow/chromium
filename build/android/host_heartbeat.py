#!/usr/bin/env python
#
# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Sends a heart beat pulse to the currently online Android devices.
This heart beat lets the devices know that they are connected to a host.
"""
# pylint: disable=W0702

import sys
import time

from pylib import android_commands
from pylib.device import device_utils

PULSE_PERIOD = 20

def main():
  while True:
    try:
      devices = android_commands.GetAttachedDevices()
      for device_serial in devices:
        device_utils.DeviceUtils(device_serial).old_interface.RunShellCommand(
            'touch /sdcard/host_heartbeat')
    except:
      # Keep the heatbeat running bypassing all errors.
      pass
    time.sleep(PULSE_PERIOD)


if __name__ == '__main__':
  sys.exit(main())
