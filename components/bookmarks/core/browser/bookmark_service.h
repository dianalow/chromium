// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_BOOKMARKS_CORE_BROWSER_BOOKMARK_SERVICE_H_
#define COMPONENTS_BOOKMARKS_CORE_BROWSER_BOOKMARK_SERVICE_H_

#include <vector>

#include "base/strings/string16.h"
#include "url/gurl.h"

// BookmarkService provides a thread safe view of bookmarks. It can be used
// to determine the set of bookmarked URLs or to check if an URL is bookmarked.
class BookmarkService {
 public:
  struct URLAndTitle {
    GURL url;
    base::string16 title;
  };

  // Returns true if the specified URL is bookmarked.
  //
  // If not on the main thread you *must* invoke BlockTillLoaded first.
  virtual bool IsBookmarked(const GURL& url) = 0;

  // Returns, by reference in |bookmarks|, the set of bookmarked urls and their
  // titles. This returns the unique set of URLs. For example, if two bookmarks
  // reference the same URL only one entry is added not matter the titles are
  // same or not.
  //
  // If not on the main thread you *must* invoke BlockTillLoaded first.
  virtual void GetBookmarks(std::vector<URLAndTitle>* bookmarks) = 0;

  // Blocks until loaded. This is intended for usage on a thread other than
  // the main thread.
  virtual void BlockTillLoaded() = 0;

 protected:
  virtual ~BookmarkService() {}
};

#endif  // COMPONENTS_BOOKMARKS_CORE_BROWSER_BOOKMARK_SERVICE_H_
