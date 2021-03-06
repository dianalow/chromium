// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_METRICS_METRICS_STATE_MANAGER_H_
#define CHROME_BROWSER_METRICS_METRICS_STATE_MANAGER_H_

#include <string>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"
#include "base/memory/scoped_ptr.h"
#include "base/metrics/field_trial.h"

class PrefService;
class PrefRegistrySimple;

namespace metrics {

class ClonedInstallDetector;

// Responsible for managing MetricsService state prefs, specifically the UMA
// client id and low entropy source. Code outside the metrics directory should
// not be instantiating or using this class directly.
class MetricsStateManager {
 public:
  virtual ~MetricsStateManager();

  // Returns true if the user opted in to sending metric reports.
  // TODO(asvitkine): This function does not report the correct value on
  // Android, see http://crbug.com/362192.
  bool IsMetricsReportingEnabled();

  // Returns the client ID for this client, or the empty string if the user is
  // not opted in to metrics reporting.
  const std::string& client_id() const { return client_id_; }

  // Forces the client ID to be generated. This is useful in case it's needed
  // before recording.
  void ForceClientIdCreation();

  // Checks if this install was cloned or imaged from another machine. If a
  // clone is detected, resets the client id and low entropy source. This
  // should not be called more than once.
  void CheckForClonedInstall();

  // Returns the preferred entropy provider used to seed persistent activities
  // based on whether or not metrics reporting is permitted on this client.
  //
  // If metrics reporting is enabled, this method returns an entropy provider
  // that has a high source of entropy, partially based on the client ID.
  // Otherwise, it returns an entropy provider that is based on a low entropy
  // source.
  scoped_ptr<const base::FieldTrial::EntropyProvider> CreateEntropyProvider();

  // Creates the MetricsStateManager, enforcing that only a single instance
  // of the class exists at a time. Returns NULL if an instance exists already.
  static scoped_ptr<MetricsStateManager> Create(PrefService* local_state);

  // Registers local state prefs used by this class.
  static void RegisterPrefs(PrefRegistrySimple* registry);

 private:
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, EntropySourceUsed_Low);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, EntropySourceUsed_High);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, LowEntropySource0NotReset);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest,
                           PermutedEntropyCacheClearedWhenLowEntropyReset);
  FRIEND_TEST_ALL_PREFIXES(MetricsStateManagerTest, ResetMetricsIDs);

  // Designates which entropy source was returned from this class.
  // This is used for testing to validate that we return the correct source
  // depending on the state of the service.
  enum EntropySourceType {
    ENTROPY_SOURCE_NONE,
    ENTROPY_SOURCE_LOW,
    ENTROPY_SOURCE_HIGH,
  };

  // Creates the MetricsStateManager with the given |local_state|. Clients
  // should instead use Create(), which enforces a single instance of this class
  // is alive at any given time.
  explicit MetricsStateManager(PrefService* local_state);

  // Returns the low entropy source for this client. This is a random value
  // that is non-identifying amongst browser clients. This method will
  // generate the entropy source value if it has not been called before.
  int GetLowEntropySource();

  // Returns the first entropy source that was returned by this service since
  // start up, or NONE if neither was returned yet. This is exposed for testing
  // only.
  EntropySourceType entropy_source_returned() const {
    return entropy_source_returned_;
  }

  // Reset the client id and low entropy source if the kMetricsResetMetricIDs
  // pref is true.
  void ResetMetricsIDsIfNecessary();

  // Whether an instance of this class exists. Used to enforce that there aren't
  // multiple instances of this class at a given time.
  static bool instance_exists_;

  // Weak pointer to the local state prefs store.
  PrefService* local_state_;

  // The identifier that's sent to the server with the log reports.
  std::string client_id_;

  // The non-identifying low entropy source value.
  int low_entropy_source_;

  // The last entropy source returned by this service, used for testing.
  EntropySourceType entropy_source_returned_;

  scoped_ptr<ClonedInstallDetector> cloned_install_detector_;

  DISALLOW_COPY_AND_ASSIGN(MetricsStateManager);
};

}  // namespace metrics

#endif  // CHROME_BROWSER_METRICS_METRICS_STATE_MANAGER_H_
