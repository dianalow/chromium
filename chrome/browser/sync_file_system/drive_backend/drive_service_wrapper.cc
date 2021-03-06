// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/sync_file_system/drive_backend/drive_service_wrapper.h"

#include <string>

#include "base/memory/weak_ptr.h"
#include "chrome/browser/drive/drive_service_interface.h"

namespace sync_file_system {
namespace drive_backend {

DriveServiceWrapper::DriveServiceWrapper(
    drive::DriveServiceInterface* drive_service)
    : drive_service_(drive_service) {
  DCHECK(drive_service_);
}

void DriveServiceWrapper::AddNewDirectory(
    const std::string& parent_resource_id,
    const std::string& directory_title,
    const drive::DriveServiceInterface::AddNewDirectoryOptions& options,
    const google_apis::GetResourceEntryCallback& callback) {
  drive_service_->AddNewDirectory(parent_resource_id,
                                  directory_title,
                                  options,
                                  callback);
}

void DriveServiceWrapper::DeleteResource(
    const std::string& resource_id,
    const std::string& etag,
    const google_apis::EntryActionCallback& callback) {
  drive_service_->DeleteResource(resource_id,
                                 etag,
                                 callback);
}

void DriveServiceWrapper::DownloadFile(
    const base::FilePath& local_cache_path,
    const std::string& resource_id,
    const google_apis::DownloadActionCallback& download_action_callback,
    const google_apis::GetContentCallback& get_content_callback,
    const google_apis::ProgressCallback& progress_callback) {
  drive_service_->DownloadFile(local_cache_path,
                               resource_id,
                               download_action_callback,
                               get_content_callback,
                               progress_callback);
}

void DriveServiceWrapper::GetAboutResource(
    const google_apis::AboutResourceCallback& callback) {
  drive_service_->GetAboutResource(callback);
}

void DriveServiceWrapper::GetChangeList(
    int64 start_changestamp,
    const google_apis::GetResourceListCallback& callback) {
  drive_service_->GetChangeList(start_changestamp, callback);
}

void DriveServiceWrapper::GetRemainingChangeList(
    const GURL& next_link,
    const google_apis::GetResourceListCallback& callback) {
  drive_service_->GetRemainingChangeList(next_link, callback);
}

void DriveServiceWrapper::GetRemainingFileList(
    const GURL& next_link,
    const google_apis::GetResourceListCallback& callback) {
  drive_service_->GetRemainingFileList(next_link, callback);
}

void DriveServiceWrapper::GetResourceEntry(
    const std::string& resource_id,
    const google_apis::GetResourceEntryCallback& callback) {
  drive_service_->GetResourceEntry(resource_id, callback);
}

void DriveServiceWrapper::GetResourceListInDirectory(
    const std::string& directory_resource_id,
    const google_apis::GetResourceListCallback& callback) {
  drive_service_->GetResourceListInDirectory(directory_resource_id,
                                             callback);
}

void DriveServiceWrapper::RemoveResourceFromDirectory(
    const std::string& parent_resource_id,
    const std::string& resource_id,
    const google_apis::EntryActionCallback& callback) {
  drive_service_->RemoveResourceFromDirectory(
      parent_resource_id, resource_id, callback);
}

void DriveServiceWrapper::SearchByTitle(
    const std::string& title,
    const std::string& directory_resource_id,
    const google_apis::GetResourceListCallback& callback) {
  drive_service_->SearchByTitle(
      title, directory_resource_id, callback);
}

}  // namespace drive_backend
}  // namespace sync_file_system
