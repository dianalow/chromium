// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/message_loop.h"
#include "base/ref_counted.h"
#include "base/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/chrome_thread.h"
#include "chrome/browser/host_zoom_map.h"
#include "chrome/browser/pref_service.h"
#include "chrome/common/notification_details.h"
#include "chrome/common/notification_observer_mock.h"
#include "chrome/common/notification_registrar.h"
#include "chrome/common/notification_service.h"
#include "chrome/common/notification_type.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/testing_profile.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::_;
using testing::Pointee;
using testing::Property;

class HostZoomMapTest : public testing::Test {
 public:
  static const int kZoomLevel;
  HostZoomMapTest()
      : ui_thread_(ChromeThread::UI, &message_loop_),
        prefs_(profile_.GetPrefs()),
        per_host_zoom_levels_pref_(prefs::kPerHostZoomLevels),
        host_name_("http://example/com/") {}

 protected:
  void SetPrefObserverExpectation() {
    EXPECT_CALL(
        pref_observer_,
        Observe(NotificationType(NotificationType::PREF_CHANGED),
                _,
                Property(&Details<std::wstring>::ptr,
                         Pointee(per_host_zoom_levels_pref_))));
  }

  MessageLoopForUI message_loop_;
  ChromeThread ui_thread_;
  TestingProfile profile_;
  PrefService* prefs_;
  std::wstring per_host_zoom_levels_pref_;  // For the observe matcher.
  std::string host_name_;
  NotificationObserverMock pref_observer_;
};
const int HostZoomMapTest::kZoomLevel = 42;

TEST_F(HostZoomMapTest, LoadNoPrefs) {
  scoped_refptr<HostZoomMap> map(new HostZoomMap(&profile_));
  EXPECT_EQ(0, map->GetZoomLevel(host_name_));
}

TEST_F(HostZoomMapTest, Load) {
  DictionaryValue* dict =
      prefs_->GetMutableDictionary(prefs::kPerHostZoomLevels);
  dict->SetWithoutPathExpansion(UTF8ToWide(host_name_),
                                Value::CreateIntegerValue(kZoomLevel));
  scoped_refptr<HostZoomMap> map(new HostZoomMap(&profile_));
  EXPECT_EQ(kZoomLevel, map->GetZoomLevel(host_name_));
}

TEST_F(HostZoomMapTest, SetZoomLevel) {
  scoped_refptr<HostZoomMap> map(new HostZoomMap(&profile_));
  prefs_->AddPrefObserver(prefs::kPerHostZoomLevels, &pref_observer_);
  SetPrefObserverExpectation();
  map->SetZoomLevel(host_name_, kZoomLevel);
  EXPECT_EQ(kZoomLevel, map->GetZoomLevel(host_name_));
  const DictionaryValue* dict =
      prefs_->GetDictionary(prefs::kPerHostZoomLevels);
  int zoom_level = 0;
  EXPECT_TRUE(dict->GetIntegerWithoutPathExpansion(UTF8ToWide(host_name_),
                                                   &zoom_level));
  EXPECT_EQ(kZoomLevel, zoom_level);

  SetPrefObserverExpectation();
  map->SetZoomLevel(host_name_, 0);
  EXPECT_EQ(0, map->GetZoomLevel(host_name_));
  EXPECT_FALSE(dict->HasKey(UTF8ToWide(host_name_)));
  prefs_->RemovePrefObserver(prefs::kPerHostZoomLevels, &pref_observer_);
}

TEST_F(HostZoomMapTest, ResetToDefaults) {
  scoped_refptr<HostZoomMap> map(new HostZoomMap(&profile_));
  map->SetZoomLevel(host_name_, kZoomLevel);

  prefs_->AddPrefObserver(prefs::kPerHostZoomLevels, &pref_observer_);
  SetPrefObserverExpectation();
  map->ResetToDefaults();
  EXPECT_EQ(0, map->GetZoomLevel(host_name_));
  EXPECT_EQ(NULL, prefs_->GetDictionary(prefs::kPerHostZoomLevels));
  prefs_->RemovePrefObserver(prefs::kPerHostZoomLevels, &pref_observer_);
}

TEST_F(HostZoomMapTest, ReloadOnPrefChange) {
  scoped_refptr<HostZoomMap> map(new HostZoomMap(&profile_));
  map->SetZoomLevel(host_name_, kZoomLevel);

  DictionaryValue dict;
  dict.SetWithoutPathExpansion(UTF8ToWide(host_name_),
                               Value::CreateIntegerValue(0));
  prefs_->Set(prefs::kPerHostZoomLevels, dict);
  EXPECT_EQ(0, map->GetZoomLevel(host_name_));
}
