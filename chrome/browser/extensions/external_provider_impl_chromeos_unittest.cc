// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/external_provider_impl.h"

#include "base/command_line.h"
#include "base/memory/scoped_ptr.h"
#include "base/prefs/testing_pref_service.h"
#include "base/test/scoped_path_override.h"
#include "chrome/browser/chrome_notification_types.h"
#include "chrome/browser/chromeos/customization_document.h"
#include "chrome/browser/extensions/extension_service_unittest.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/testing_browser_process.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/system/mock_statistics_provider.h"
#include "chromeos/system/statistics_provider.h"
#include "content/public/browser/notification_service.h"
#include "content/public/test/test_utils.h"
#include "testing/gmock/include/gmock/gmock.h"

using ::testing::_;
using ::testing::NotNull;
using ::testing::Return;

namespace extensions {

namespace {

const char kExternalAppId[] = "kekdneafjmhmndejhmbcadfiiofngffo";

class ExternalProviderImplChromeOSTest : public ExtensionServiceTestBase {
 public:
  ExternalProviderImplChromeOSTest() {}
  virtual ~ExternalProviderImplChromeOSTest() {}

  void InitServiceWithExternalProviders() {
    InitializeEmptyExtensionService();
    service_->Init();

    ProviderCollection providers;
    extensions::ExternalProviderImpl::CreateExternalProviders(
        service_, profile_.get(), &providers);

    for (ProviderCollection::iterator i = providers.begin();
         i != providers.end();
         ++i) {
      service_->AddProviderForTesting(i->release());
    }
  }

  // ExtensionServiceTestBase overrides:
  virtual void SetUp() OVERRIDE {
    ExtensionServiceTestBase::SetUp();

    TestingBrowserProcess::GetGlobal()->SetLocalState(&local_state_);
    chromeos::ServicesCustomizationDocument::RegisterPrefs(
        local_state_.registry());

    external_externsions_overrides_.reset(
        new base::ScopedPathOverride(chrome::DIR_EXTERNAL_EXTENSIONS,
                                     data_dir_.Append("external")));

    chromeos::system::StatisticsProvider::SetTestProvider(
        &mock_statistics_provider_);
    EXPECT_CALL(mock_statistics_provider_, GetMachineStatistic(_, NotNull()))
        .WillRepeatedly(Return(false));
  }

  virtual void TearDown() OVERRIDE {
    chromeos::system::StatisticsProvider::SetTestProvider(NULL);
    TestingBrowserProcess::GetGlobal()->SetLocalState(NULL);
  }

 private:
  TestingPrefServiceSimple local_state_;
  scoped_ptr<base::ScopedPathOverride> external_externsions_overrides_;
  chromeos::system::MockStatisticsProvider mock_statistics_provider_;

  DISALLOW_COPY_AND_ASSIGN(ExternalProviderImplChromeOSTest);
};

}  // namespace

// Normal mode, external app should be installed.
TEST_F(ExternalProviderImplChromeOSTest, Normal) {
  InitServiceWithExternalProviders();

  service_->CheckForExternalUpdates();
  content::WindowedNotificationObserver(
      chrome::NOTIFICATION_CRX_INSTALLER_DONE,
      content::NotificationService::AllSources()).Wait();

  EXPECT_TRUE(service_->GetInstalledExtension(kExternalAppId));
}

// App mode, no external app should be installed.
TEST_F(ExternalProviderImplChromeOSTest, AppMode) {
  CommandLine* command = CommandLine::ForCurrentProcess();
  command->AppendSwitchASCII(switches::kForceAppMode, std::string());
  command->AppendSwitchASCII(switches::kAppId, std::string("app_id"));

  InitServiceWithExternalProviders();

  service_->CheckForExternalUpdates();
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(service_->GetInstalledExtension(kExternalAppId));
}

}  // namespace extensions
