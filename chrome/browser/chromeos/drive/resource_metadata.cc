// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/resource_metadata.h"

#include "base/guid.h"
#include "base/rand_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/sys_info.h"
#include "chrome/browser/chromeos/drive/drive.pb.h"
#include "chrome/browser/chromeos/drive/file_system_util.h"
#include "chrome/browser/chromeos/drive/resource_metadata_storage.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {
namespace internal {
namespace {

// Returns true if enough disk space is available for DB operation.
// TODO(hashimoto): Merge this with FileCache's FreeDiskSpaceGetterInterface.
bool EnoughDiskSpaceIsAvailableForDBOperation(const base::FilePath& path) {
  const int64 kRequiredDiskSpaceInMB = 128;  // 128 MB seems to be large enough.
  return base::SysInfo::AmountOfFreeDiskSpace(path) >=
      kRequiredDiskSpaceInMB * (1 << 20);
}

// Returns a file name with a uniquifier appended. (e.g. "File (1).txt")
std::string GetUniquifiedName(const std::string& name, int uniquifier) {
  base::FilePath name_path = base::FilePath::FromUTF8Unsafe(name);
  name_path = name_path.InsertBeforeExtension(
      base::StringPrintf(" (%d)", uniquifier));
  return name_path.AsUTF8Unsafe();
}

// Returns true when there is no entry with the specified name under the parent
// other than the specified entry.
bool EntryCanUseName(ResourceMetadataStorage* storage,
                     const std::string& parent_local_id,
                     const std::string& local_id,
                     const std::string& base_name) {
  const std::string existing_entry_id = storage->GetChild(parent_local_id,
                                                          base_name);
  return existing_entry_id.empty() || existing_entry_id == local_id;
}

}  // namespace

ResourceMetadata::ResourceMetadata(
    ResourceMetadataStorage* storage,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner)
    : blocking_task_runner_(blocking_task_runner),
      storage_(storage) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

FileError ResourceMetadata::Initialize() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  if (!SetUpDefaultEntries())
    return FILE_ERROR_FAILED;

  return FILE_ERROR_OK;
}

void ResourceMetadata::Destroy() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  blocking_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&ResourceMetadata::DestroyOnBlockingPool,
                 base::Unretained(this)));
}

FileError ResourceMetadata::Reset() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  if (!storage_->SetLargestChangestamp(0))
    return FILE_ERROR_FAILED;

  // Remove all root entries.
  scoped_ptr<Iterator> it = GetIterator();
  for (; !it->IsAtEnd(); it->Advance()) {
    if (it->GetValue().parent_local_id().empty()) {
      if (!RemoveEntryRecursively(it->GetID()))
        return FILE_ERROR_FAILED;
    }
  }
  if (it->HasError())
    return FILE_ERROR_FAILED;

  if (!SetUpDefaultEntries())
    return FILE_ERROR_FAILED;

  return FILE_ERROR_OK;
}

ResourceMetadata::~ResourceMetadata() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
}

bool ResourceMetadata::SetUpDefaultEntries() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  // Initialize "/drive", "/drive/other", "drive/trash" and "drive/root".
  ResourceEntry entry;
  if (!storage_->GetEntry(util::kDriveGrandRootLocalId, &entry)) {
    ResourceEntry root;
    root.mutable_file_info()->set_is_directory(true);
    root.set_local_id(util::kDriveGrandRootLocalId);
    root.set_title(util::kDriveGrandRootDirName);
    root.set_base_name(util::kDriveGrandRootDirName);
    if (!storage_->PutEntry(root))
      return false;
  } else if (!entry.resource_id().empty()) {
    // Old implementations used kDriveGrandRootLocalId as a resource ID.
    entry.clear_resource_id();
    if (!storage_->PutEntry(entry))
      return false;
  }
  if (!storage_->GetEntry(util::kDriveOtherDirLocalId, &entry)) {
    ResourceEntry other_dir;
    other_dir.mutable_file_info()->set_is_directory(true);
    other_dir.set_local_id(util::kDriveOtherDirLocalId);
    other_dir.set_parent_local_id(util::kDriveGrandRootLocalId);
    other_dir.set_title(util::kDriveOtherDirName);
    if (!PutEntryUnderDirectory(other_dir))
      return false;
  } else if (!entry.resource_id().empty()) {
    // Old implementations used kDriveOtherDirLocalId as a resource ID.
    entry.clear_resource_id();
    if (!storage_->PutEntry(entry))
      return false;
  }
  if (!storage_->GetEntry(util::kDriveTrashDirLocalId, &entry)) {
    ResourceEntry trash_dir;
    trash_dir.mutable_file_info()->set_is_directory(true);
    trash_dir.set_local_id(util::kDriveTrashDirLocalId);
    trash_dir.set_parent_local_id(util::kDriveGrandRootLocalId);
    trash_dir.set_title(util::kDriveTrashDirName);
    if (!PutEntryUnderDirectory(trash_dir))
      return false;
  }
  if (storage_->GetChild(util::kDriveGrandRootLocalId,
                         util::kDriveMyDriveRootDirName).empty()) {
    ResourceEntry mydrive;
    mydrive.mutable_file_info()->set_is_directory(true);
    mydrive.set_parent_local_id(util::kDriveGrandRootLocalId);
    mydrive.set_title(util::kDriveMyDriveRootDirName);

    std::string local_id;
    if (AddEntry(mydrive, &local_id) != FILE_ERROR_OK)
      return false;
  }
  return true;
}

void ResourceMetadata::DestroyOnBlockingPool() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  delete this;
}

int64 ResourceMetadata::GetLargestChangestamp() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  return storage_->GetLargestChangestamp();
}

FileError ResourceMetadata::SetLargestChangestamp(int64 value) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  return storage_->SetLargestChangestamp(value) ?
      FILE_ERROR_OK : FILE_ERROR_FAILED;
}

FileError ResourceMetadata::AddEntry(const ResourceEntry& entry,
                                     std::string* out_id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(entry.local_id().empty());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  ResourceEntry parent;
  if (!storage_->GetEntry(entry.parent_local_id(), &parent) ||
      !parent.file_info().is_directory())
    return FILE_ERROR_NOT_FOUND;

  // Multiple entries with the same resource ID should not be present.
  std::string local_id;
  ResourceEntry existing_entry;
  if (!entry.resource_id().empty() &&
      storage_->GetIdByResourceId(entry.resource_id(), &local_id) &&
      storage_->GetEntry(local_id, &existing_entry))
    return FILE_ERROR_EXISTS;

  // Generate unique local ID when needed.
  while (local_id.empty() || storage_->GetEntry(local_id, &existing_entry))
    local_id = base::GenerateGUID();

  ResourceEntry new_entry(entry);
  new_entry.set_local_id(local_id);

  if (!PutEntryUnderDirectory(new_entry))
    return FILE_ERROR_FAILED;

  *out_id = local_id;
  return FILE_ERROR_OK;
}

FileError ResourceMetadata::RemoveEntry(const std::string& id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  // Disallow deletion of default entries.
  if (id == util::kDriveGrandRootLocalId ||
      id == util::kDriveOtherDirLocalId ||
      id == util::kDriveTrashDirLocalId)
    return FILE_ERROR_ACCESS_DENIED;

  ResourceEntry entry;
  if (!storage_->GetEntry(id, &entry))
    return FILE_ERROR_NOT_FOUND;

  if (!RemoveEntryRecursively(id))
    return FILE_ERROR_FAILED;
  return FILE_ERROR_OK;
}

FileError ResourceMetadata::GetResourceEntryById(const std::string& id,
                                                 ResourceEntry* out_entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(!id.empty());
  DCHECK(out_entry);

  return storage_->GetEntry(id, out_entry) ?
      FILE_ERROR_OK : FILE_ERROR_NOT_FOUND;
}

FileError ResourceMetadata::GetResourceEntryByPath(const base::FilePath& path,
                                                   ResourceEntry* out_entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(out_entry);

  std::string id;
  FileError error = GetIdByPath(path, &id);
  if (error != FILE_ERROR_OK)
    return error;

  return GetResourceEntryById(id, out_entry);
}

FileError ResourceMetadata::ReadDirectoryByPath(
    const base::FilePath& path,
    ResourceEntryVector* out_entries) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(out_entries);

  std::string id;
  FileError error = GetIdByPath(path, &id);
  if (error != FILE_ERROR_OK)
    return error;
  return ReadDirectoryById(id, out_entries);
}

FileError ResourceMetadata::ReadDirectoryById(
    const std::string& id,
    ResourceEntryVector* out_entries) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(out_entries);

  ResourceEntry entry;
  FileError error = GetResourceEntryById(id, &entry);
  if (error != FILE_ERROR_OK)
    return error;

  if (!entry.file_info().is_directory())
    return FILE_ERROR_NOT_A_DIRECTORY;

  std::vector<std::string> children;
  storage_->GetChildren(id, &children);

  ResourceEntryVector entries(children.size());
  for (size_t i = 0; i < children.size(); ++i) {
    if (!storage_->GetEntry(children[i], &entries[i]))
      return FILE_ERROR_FAILED;
  }
  out_entries->swap(entries);
  return FILE_ERROR_OK;
}

FileError ResourceMetadata::RefreshEntry(const ResourceEntry& entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  if (!EnoughDiskSpaceIsAvailableForDBOperation(storage_->directory_path()))
    return FILE_ERROR_NO_LOCAL_SPACE;

  ResourceEntry old_entry;
  if (!storage_->GetEntry(entry.local_id(), &old_entry))
    return FILE_ERROR_NOT_FOUND;

  if (old_entry.parent_local_id().empty() ||  // Reject root.
      old_entry.file_info().is_directory() !=  // Reject incompatible input.
      entry.file_info().is_directory())
    return FILE_ERROR_INVALID_OPERATION;

  if (!entry.resource_id().empty()) {
    // Multiple entries cannot share the same resource ID.
    std::string local_id;
    FileError error = GetIdByResourceId(entry.resource_id(), &local_id);
    switch (error) {
      case FILE_ERROR_OK:
        if (local_id != entry.local_id())
          return FILE_ERROR_INVALID_OPERATION;
        break;

      case FILE_ERROR_NOT_FOUND:
        break;

      default:
        return error;
    }
  }

  // Make sure that the new parent exists and it is a directory.
  ResourceEntry new_parent;
  if (!storage_->GetEntry(entry.parent_local_id(), &new_parent))
    return FILE_ERROR_NOT_FOUND;

  if (!new_parent.file_info().is_directory())
    return FILE_ERROR_NOT_A_DIRECTORY;

  // Remove from the old parent and add it to the new parent with the new data.
  if (!PutEntryUnderDirectory(entry))
    return FILE_ERROR_FAILED;
  return FILE_ERROR_OK;
}

void ResourceMetadata::GetSubDirectoriesRecursively(
    const std::string& id,
    std::set<base::FilePath>* sub_directories) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  std::vector<std::string> children;
  storage_->GetChildren(id, &children);
  for (size_t i = 0; i < children.size(); ++i) {
    ResourceEntry entry;
    if (storage_->GetEntry(children[i], &entry) &&
        entry.file_info().is_directory()) {
      sub_directories->insert(GetFilePath(children[i]));
      GetSubDirectoriesRecursively(children[i], sub_directories);
    }
  }
}

std::string ResourceMetadata::GetChildId(const std::string& parent_local_id,
                                         const std::string& base_name) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  return storage_->GetChild(parent_local_id, base_name);
}

scoped_ptr<ResourceMetadata::Iterator> ResourceMetadata::GetIterator() {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  return storage_->GetIterator();
}

base::FilePath ResourceMetadata::GetFilePath(const std::string& id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  base::FilePath path;
  ResourceEntry entry;
  if (storage_->GetEntry(id, &entry)) {
    if (!entry.parent_local_id().empty()) {
      path = GetFilePath(entry.parent_local_id());
    } else if (entry.local_id() != util::kDriveGrandRootLocalId) {
      DVLOG(1) << "Entries not under the grand root don't have paths.";
      return base::FilePath();
    }
    path = path.Append(base::FilePath::FromUTF8Unsafe(entry.base_name()));
  }
  return path;
}

FileError ResourceMetadata::GetIdByPath(const base::FilePath& file_path,
                                        std::string* out_id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  // Start from the root.
  std::vector<base::FilePath::StringType> components;
  file_path.GetComponents(&components);
  if (components.empty() || components[0] != util::kDriveGrandRootDirName)
    return FILE_ERROR_NOT_FOUND;

  // Iterate over the remaining components.
  std::string id = util::kDriveGrandRootLocalId;
  for (size_t i = 1; i < components.size(); ++i) {
    const std::string component = base::FilePath(components[i]).AsUTF8Unsafe();
    id = storage_->GetChild(id, component);
    if (id.empty())
      return FILE_ERROR_NOT_FOUND;
  }
  *out_id = id;
  return FILE_ERROR_OK;
}

FileError ResourceMetadata::GetIdByResourceId(const std::string& resource_id,
                                              std::string* out_local_id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  return storage_->GetIdByResourceId(resource_id, out_local_id) ?
      FILE_ERROR_OK : FILE_ERROR_NOT_FOUND;
}

bool ResourceMetadata::PutEntryUnderDirectory(const ResourceEntry& entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(!entry.local_id().empty());
  DCHECK(!entry.parent_local_id().empty());

  ResourceEntry updated_entry(entry);
  updated_entry.set_base_name(GetDeduplicatedBaseName(updated_entry));
  return storage_->PutEntry(updated_entry);
}

std::string ResourceMetadata::GetDeduplicatedBaseName(
    const ResourceEntry& entry) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());
  DCHECK(!entry.parent_local_id().empty());
  DCHECK(!entry.title().empty());

  // The entry name may have been changed due to prior name de-duplication.
  // We need to first restore the file name based on the title before going
  // through name de-duplication again when it is added to another directory.
  std::string base_name = entry.title();
  if (entry.has_file_specific_info() &&
      entry.file_specific_info().is_hosted_document()) {
    base_name += entry.file_specific_info().document_extension();
  }
  base_name = util::NormalizeFileName(base_name);

  // If |base_name| is not used, just return it.
  if (EntryCanUseName(storage_, entry.parent_local_id(), entry.local_id(),
                      base_name))
    return base_name;

  // Find an unused number with binary search.
  int smallest_known_unused_modifier = 1;
  while (true) {
    if (EntryCanUseName(storage_, entry.parent_local_id(), entry.local_id(),
                        GetUniquifiedName(base_name,
                                          smallest_known_unused_modifier)))
      break;

    const int delta = base::RandInt(1, smallest_known_unused_modifier);
    if (smallest_known_unused_modifier <= INT_MAX - delta) {
      smallest_known_unused_modifier += delta;
    } else {  // No luck finding an unused number. Try again.
      smallest_known_unused_modifier = 1;
    }
  }

  int largest_known_used_modifier = 1;
  while (smallest_known_unused_modifier - largest_known_used_modifier > 1) {
    const int modifier = largest_known_used_modifier +
        (smallest_known_unused_modifier - largest_known_used_modifier) / 2;

    if (EntryCanUseName(storage_, entry.parent_local_id(), entry.local_id(),
                        GetUniquifiedName(base_name, modifier))) {
      smallest_known_unused_modifier = modifier;
    } else {
      largest_known_used_modifier = modifier;
    }
  }
  return GetUniquifiedName(base_name, smallest_known_unused_modifier);
}

bool ResourceMetadata::RemoveEntryRecursively(const std::string& id) {
  DCHECK(blocking_task_runner_->RunsTasksOnCurrentThread());

  ResourceEntry entry;
  if (!storage_->GetEntry(id, &entry))
    return false;

  if (entry.file_info().is_directory()) {
    std::vector<std::string> children;
    storage_->GetChildren(id, &children);
    for (size_t i = 0; i < children.size(); ++i) {
      if (!RemoveEntryRecursively(children[i]))
        return false;
    }
  }
  return storage_->RemoveEntry(id);
}

}  // namespace internal
}  // namespace drive
