// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_garbage_collector.h"

#include "base/bind.h"
#include "base/file_util.h"
#include "base/files/file_enumerator.h"
#include "base/logging.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/sequenced_task_runner.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/extension_garbage_collector_factory.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_util.h"
#include "chrome/browser/extensions/install_tracker.h"
#include "chrome/browser/extensions/pending_extension_manager.h"
#include "chrome/common/extensions/manifest_handlers/app_isolation_info.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/storage_partition.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension.h"
#include "extensions/common/file_util.h"

namespace extensions {

namespace {

// Wait this many seconds before trying to garbage collect extensions again.
const int kGarbageCollectRetryDelayInSeconds = 30;

// Wait this many seconds after startup to see if there are any extensions
// which can be garbage collected.
const int kGarbageCollectStartupDelay = 30;

typedef std::multimap<std::string, base::FilePath> ExtensionPathsMultimap;

void CheckExtensionDirectory(const base::FilePath& path,
                             const ExtensionPathsMultimap& extension_paths) {
  base::FilePath basename = path.BaseName();
  // Clean up temporary files left if Chrome crashed or quit in the middle
  // of an extension install.
  if (basename.value() == file_util::kTempDirectoryName) {
    base::DeleteFile(path, true);  // Recursive.
    return;
  }

  // Parse directory name as a potential extension ID.
  std::string extension_id;
  if (base::IsStringASCII(basename.value())) {
    extension_id = base::UTF16ToASCII(basename.LossyDisplayName());
    if (!Extension::IdIsValid(extension_id))
      extension_id.clear();
  }

  // Delete directories that aren't valid IDs.
  if (extension_id.empty()) {
    base::DeleteFile(path, true);  // Recursive.
    return;
  }

  typedef ExtensionPathsMultimap::const_iterator Iter;
  std::pair<Iter, Iter> iter_pair = extension_paths.equal_range(extension_id);

  // If there is no entry in the prefs file, just delete the directory and
  // move on. This can legitimately happen when an uninstall does not
  // complete, for example, when a plugin is in use at uninstall time.
  if (iter_pair.first == iter_pair.second) {
    base::DeleteFile(path, true);  // Recursive.
    return;
  }

  // Clean up old version directories.
  base::FileEnumerator versions_enumerator(
      path, false /* Not recursive */, base::FileEnumerator::DIRECTORIES);
  for (base::FilePath version_dir = versions_enumerator.Next();
       !version_dir.empty();
       version_dir = versions_enumerator.Next()) {
    bool known_version = false;
    for (Iter iter = iter_pair.first; iter != iter_pair.second; ++iter) {
      if (version_dir.BaseName() == iter->second.BaseName()) {
        known_version = true;
        break;
      }
    }
    if (!known_version)
      base::DeleteFile(version_dir, true);  // Recursive.
  }
}

void GarbageCollectExtensionsOnFileThread(
    const base::FilePath& install_directory,
    const ExtensionPathsMultimap& extension_paths) {
  DCHECK(!content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  // Nothing to clean up if it doesn't exist.
  if (!base::DirectoryExists(install_directory))
    return;

  base::FileEnumerator enumerator(install_directory,
                                  false,  // Not recursive.
                                  base::FileEnumerator::DIRECTORIES);

  for (base::FilePath extension_path = enumerator.Next();
       !extension_path.empty();
       extension_path = enumerator.Next()) {
    CheckExtensionDirectory(extension_path, extension_paths);
  }
}

}  // namespace

ExtensionGarbageCollector::ExtensionGarbageCollector(
    content::BrowserContext* context)
    : context_(context), crx_installs_in_progress_(0), weak_factory_(this) {
#if defined(OS_CHROMEOS)
  disable_garbage_collection_ = false;
#endif

  ExtensionSystem* extension_system = ExtensionSystem::Get(context_);
  DCHECK(extension_system);

  extension_system->ready().PostDelayed(
      FROM_HERE,
      base::Bind(&ExtensionGarbageCollector::GarbageCollectExtensions,
                 weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromSeconds(kGarbageCollectStartupDelay));

  extension_system->ready().Post(
      FROM_HERE,
      base::Bind(
          &ExtensionGarbageCollector::GarbageCollectIsolatedStorageIfNeeded,
          weak_factory_.GetWeakPtr()));

  InstallTracker::Get(context_)->AddObserver(this);
}

ExtensionGarbageCollector::~ExtensionGarbageCollector() {}

// static
ExtensionGarbageCollector* ExtensionGarbageCollector::Get(
    content::BrowserContext* context) {
  return ExtensionGarbageCollectorFactory::GetForBrowserContext(context);
}

void ExtensionGarbageCollector::Shutdown() {
  InstallTracker::Get(context_)->RemoveObserver(this);
}

void ExtensionGarbageCollector::GarbageCollectExtensionsForTest() {
  GarbageCollectExtensions();
}

void ExtensionGarbageCollector::GarbageCollectExtensions() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

#if defined(OS_CHROMEOS)
  if (disable_garbage_collection_)
    return;
#endif

  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(context_);
  DCHECK(extension_prefs);

  if (extension_prefs->pref_service()->ReadOnly())
    return;

  if (crx_installs_in_progress_ > 0) {
    // Don't garbage collect while there are installations in progress,
    // which may be using the temporary installation directory. Try to garbage
    // collect again later.
    base::MessageLoop::current()->PostDelayedTask(
        FROM_HERE,
        base::Bind(&ExtensionGarbageCollector::GarbageCollectExtensions,
                   weak_factory_.GetWeakPtr()),
        base::TimeDelta::FromSeconds(kGarbageCollectRetryDelayInSeconds));
    return;
  }

  scoped_ptr<ExtensionPrefs::ExtensionsInfo> info(
      extension_prefs->GetInstalledExtensionsInfo());
  std::multimap<std::string, base::FilePath> extension_paths;
  for (size_t i = 0; i < info->size(); ++i) {
    extension_paths.insert(
        std::make_pair(info->at(i)->extension_id, info->at(i)->extension_path));
  }

  info = extension_prefs->GetAllDelayedInstallInfo();
  for (size_t i = 0; i < info->size(); ++i) {
    extension_paths.insert(
        std::make_pair(info->at(i)->extension_id, info->at(i)->extension_path));
  }

  ExtensionService* service =
      ExtensionSystem::Get(context_)->extension_service();
  if (!service->GetFileTaskRunner()->PostTask(
          FROM_HERE,
          base::Bind(&GarbageCollectExtensionsOnFileThread,
                     service->install_directory(),
                     extension_paths))) {
    NOTREACHED();
  }
}

void ExtensionGarbageCollector::GarbageCollectIsolatedStorageIfNeeded() {
  DCHECK(content::BrowserThread::CurrentlyOn(content::BrowserThread::UI));

  ExtensionPrefs* extension_prefs = ExtensionPrefs::Get(context_);
  DCHECK(extension_prefs);
  if (!extension_prefs->NeedsStorageGarbageCollection())
    return;
  extension_prefs->SetNeedsStorageGarbageCollection(false);

  scoped_ptr<base::hash_set<base::FilePath> > active_paths(
      new base::hash_set<base::FilePath>());
  scoped_ptr<ExtensionSet> extensions =
      ExtensionRegistry::Get(context_)->GenerateInstalledExtensionsSet();
  for (ExtensionSet::const_iterator iter = extensions->begin();
       iter != extensions->end();
       ++iter) {
    if (AppIsolationInfo::HasIsolatedStorage(iter->get())) {
      active_paths->insert(
          content::BrowserContext::GetStoragePartitionForSite(
              context_, util::GetSiteForExtensionId((*iter)->id(), context_))
              ->GetPath());
    }
  }

  // The data of ephemeral apps can outlive their cache lifetime. Ensure
  // they are not garbage collected.
  scoped_ptr<ExtensionPrefs::ExtensionsInfo> evicted_apps_info(
      extension_prefs->GetEvictedEphemeralAppsInfo());
  for (size_t i = 0; i < evicted_apps_info->size(); ++i) {
    ExtensionInfo* info = evicted_apps_info->at(i).get();
    if (util::HasIsolatedStorage(*info)) {
      active_paths->insert(content::BrowserContext::GetStoragePartitionForSite(
                               context_,
                               util::GetSiteForExtensionId(
                                   info->extension_id, context_))->GetPath());
    }
  }

  ExtensionService* service =
      ExtensionSystem::Get(context_)->extension_service();
  service->OnGarbageCollectIsolatedStorageStart();
  content::BrowserContext::GarbageCollectStoragePartitions(
      context_,
      active_paths.Pass(),
      base::Bind(&ExtensionService::OnGarbageCollectIsolatedStorageFinished,
                 service->AsWeakPtr()));
}

void ExtensionGarbageCollector::OnBeginCrxInstall(
    const std::string& extension_id) {
  crx_installs_in_progress_++;
}

void ExtensionGarbageCollector::OnFinishCrxInstall(
    const std::string& extension_id,
    bool success) {
  crx_installs_in_progress_--;
  if (crx_installs_in_progress_ < 0) {
    // This can only happen if there is a mismatch in our begin/finish
    // accounting.
    NOTREACHED();

    // Don't let the count go negative to avoid garbage collecting when
    // an install is actually in progress.
    crx_installs_in_progress_ = 0;
  }
}

}  // namespace extensions
