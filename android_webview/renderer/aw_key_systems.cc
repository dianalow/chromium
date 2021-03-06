// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/renderer/aw_key_systems.h"

#include <string>

#include "base/command_line.h"
#include "base/logging.h"
#include "content/public/common/eme_codec.h"
#include "third_party/widevine/cdm/widevine_cdm_common.h"

using content::KeySystemInfo;

namespace {

// Return |name|'s parent key system.
std::string GetDirectParentName(const std::string& name) {
  int last_period = name.find_last_of('.');
  DCHECK_GT(last_period, 0);
  return name.substr(0, last_period);
}

void AddWidevineWithCodecs(const std::string& key_system_name,
                           bool add_parent_name,
                           std::vector<KeySystemInfo>* concrete_key_systems) {
  KeySystemInfo info(key_system_name);

  if (add_parent_name)
    info.parent_key_system = GetDirectParentName(key_system_name);

  info.supported_codecs = content::EME_CODEC_MP4_ALL;

  concrete_key_systems->push_back(info);
}

}  // namespace

namespace android_webview {

void AwAddKeySystems(
    std::vector<KeySystemInfo>* key_systems_info) {
  AddWidevineWithCodecs(kWidevineKeySystem, true, key_systems_info);
}

}  // namespace android_webview
