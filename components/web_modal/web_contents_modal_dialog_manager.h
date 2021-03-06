// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_WEB_MODAL_WEB_CONTENTS_MODAL_DIALOG_MANAGER_H_
#define COMPONENTS_WEB_MODAL_WEB_CONTENTS_MODAL_DIALOG_MANAGER_H_

#include <deque>

#include "base/memory/scoped_ptr.h"
#include "components/web_modal/single_web_contents_dialog_manager.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/gfx/native_widget_types.h"

namespace web_modal {

class WebContentsModalDialogManagerDelegate;

// Per-WebContents class to manage WebContents-modal dialogs.
class WebContentsModalDialogManager
    : public SingleWebContentsDialogManagerDelegate,
      public content::WebContentsObserver,
      public content::WebContentsUserData<WebContentsModalDialogManager> {
 public:
  virtual ~WebContentsModalDialogManager();

  WebContentsModalDialogManagerDelegate* delegate() const { return delegate_; }
  void SetDelegate(WebContentsModalDialogManagerDelegate* d);

  static SingleWebContentsDialogManager* CreateNativeWebModalManager(
      NativeWebContentsModalDialog dialog,
      SingleWebContentsDialogManagerDelegate* native_delegate);

  // Shows the dialog as a web contents modal dialog. The dialog will notify via
  // WillClose() when it is being destroyed.
  void ShowModalDialog(NativeWebContentsModalDialog dialog);

  // Allow clients to supply their own native dialog manager. Suitable for
  // bubble clients.
  void ShowDialogWithManager(
      NativeWebContentsModalDialog dialog,
      scoped_ptr<SingleWebContentsDialogManager> manager);

  // Returns true if any dialogs are active and not closed.
  bool IsDialogActive() const;

  // Focus the topmost modal dialog.  IsDialogActive() must be true when calling
  // this function.
  void FocusTopmostDialog();

  // Set to true to close the window when a page load starts on the WebContents.
  void SetCloseOnInterstitialPage(NativeWebContentsModalDialog dialog,
                                  bool close);

  // Overriden from SingleWebContentsDialogManagerDelegate:
  virtual content::WebContents* GetWebContents() const OVERRIDE;
  // Called when a WebContentsModalDialogs we own is about to be closed.
  virtual void WillClose(NativeWebContentsModalDialog dialog) OVERRIDE;

  // For testing.
  class TestApi {
   public:
    explicit TestApi(WebContentsModalDialogManager* manager)
        : manager_(manager) {}

    void CloseAllDialogs() { manager_->CloseAllDialogs(); }
    void DidAttachInterstitialPage() { manager_->DidAttachInterstitialPage(); }
    void WebContentsWasShown() { manager_->WasShown(); }
    void WebContentsWasHidden() { manager_->WasHidden(); }

   private:
    WebContentsModalDialogManager* manager_;

    DISALLOW_COPY_AND_ASSIGN(TestApi);
  };

 private:
  explicit WebContentsModalDialogManager(content::WebContents* web_contents);
  friend class content::WebContentsUserData<WebContentsModalDialogManager>;

  struct DialogState {
    DialogState(NativeWebContentsModalDialog dialog,
                scoped_ptr<SingleWebContentsDialogManager> manager);
    ~DialogState();

    NativeWebContentsModalDialog dialog;
    scoped_ptr<SingleWebContentsDialogManager> manager;
    bool close_on_interstitial_webui;
  };

  typedef std::deque<DialogState*> WebContentsModalDialogList;

  // Utility function to get the dialog state for a dialog.
  WebContentsModalDialogList::iterator FindDialogState(
      NativeWebContentsModalDialog dialog);

  // Blocks/unblocks interaction with renderer process.
  void BlockWebContentsInteraction(bool blocked);

  bool IsWebContentsVisible() const;

  // Closes all WebContentsModalDialogs.
  void CloseAllDialogs();

  // Overridden from content::WebContentsObserver:
  virtual void DidNavigateMainFrame(
      const content::LoadCommittedDetails& details,
      const content::FrameNavigateParams& params) OVERRIDE;
  virtual void DidGetIgnoredUIEvent() OVERRIDE;
  virtual void WasShown() OVERRIDE;
  virtual void WasHidden() OVERRIDE;
  virtual void WebContentsDestroyed(content::WebContents* tab) OVERRIDE;
  virtual void DidAttachInterstitialPage() OVERRIDE;

  // Delegate for notifying our owner about stuff. Not owned by us.
  WebContentsModalDialogManagerDelegate* delegate_;

  // All active dialogs.
  WebContentsModalDialogList child_dialogs_;

  // True while closing the dialogs on WebContents close.
  bool closing_all_dialogs_;

  DISALLOW_COPY_AND_ASSIGN(WebContentsModalDialogManager);
};

}  // namespace web_modal

#endif  // COMPONENTS_WEB_MODAL_WEB_CONTENTS_MODAL_DIALOG_MANAGER_H_
