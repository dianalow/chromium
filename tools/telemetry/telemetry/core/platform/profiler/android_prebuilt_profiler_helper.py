# Copyright 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Android-specific, installs pre-built profilers."""

import logging
import os

from telemetry import decorators
from telemetry.util import support_binaries


_DEVICE_PROFILER_DIR = '/data/local/tmp/profilers/'


def GetDevicePath(profiler_binary):
  return os.path.join(_DEVICE_PROFILER_DIR, os.path.basename(profiler_binary))


@decorators.Cache
def InstallOnDevice(device, profiler_binary):
  host_path = support_binaries.FindPath(profiler_binary, 'android')
  if not host_path:
    logging.error('Profiler binary "%s" not found. Could not be installed',
                  host_path)
    return False

  device_binary_path = GetDevicePath(profiler_binary)
  device.old_interface.PushIfNeeded(host_path, device_binary_path)
  device.old_interface.RunShellCommand('chmod 777 ' + device_binary_path)
  return True

