/*
 * Copyright 2013 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <vector>

#include "ash/shell.h"
#include "base/command_line.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/render_widget_host_iterator.h"
#include "content/public/browser/site_instance.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test_utils.h"
#include "ui/aura/client/aura_constants.h"
#include "ui/base/ime/input_method.h"
#include "ui/keyboard/keyboard_controller.h"
#include "ui/keyboard/keyboard_switches.h"

namespace {

const base::FilePath kWebuiTestDir =
    base::FilePath(FILE_PATH_LITERAL("webui"));

const base::FilePath kVirtualKeyboardTestDir =
    base::FilePath(FILE_PATH_LITERAL("chromeos/virtual_keyboard"));

const base::FilePath kMockController =
    base::FilePath(FILE_PATH_LITERAL("mock_controller.js"));

const base::FilePath kMockTimer =
    base::FilePath(FILE_PATH_LITERAL("mock_timer.js"));

const base::FilePath kBaseKeyboardTestFramework =
    base::FilePath(FILE_PATH_LITERAL("virtual_keyboard_test_base.js"));

}  // namespace

class VirtualKeyboardBrowserTest : public InProcessBrowserTest {
 public:

  // Ensure that the virtual keyboard is enabled.
  virtual void SetUpCommandLine(CommandLine* command_line) OVERRIDE {
    command_line->AppendSwitch(
        keyboard::switches::kEnableVirtualKeyboard);
  }

  // Injects javascript in |file| into the keyboard page and runs test methods.
  void RunTest(const base::FilePath& file) {
    ui_test_utils::NavigateToURL(browser(), GURL("chrome://keyboard"));

    content::WebContents* web_contents =
        browser()->tab_strip_model()->GetActiveWebContents();
    ASSERT_TRUE(web_contents);

    // Inject testing scripts.
    InjectJavascript(kWebuiTestDir, kMockController);
    InjectJavascript(kWebuiTestDir, kMockTimer);
    InjectJavascript(kVirtualKeyboardTestDir, kBaseKeyboardTestFramework);
    InjectJavascript(kVirtualKeyboardTestDir, file);

    ASSERT_TRUE(content::ExecuteScript(web_contents, utf8_content_));

    // Inject DOM-automation test harness and run tests.
    std::vector<int> resource_ids;
    EXPECT_TRUE(ExecuteWebUIResourceTest(web_contents, resource_ids));
  }

  void showVirtualKeyboard() {
    aura::Window *window = ash::Shell::GetPrimaryRootWindow();
    ui::InputMethod* input_method = window->GetProperty(
        aura::client::kRootWindowInputMethodKey);
    ASSERT_TRUE(input_method);
    input_method->ShowImeIfNeeded();
  }

  content::RenderViewHost* GetKeyboardRenderViewHost() {
    showVirtualKeyboard();
    std::string kVirtualKeyboardURL =
        "chrome-extension://mppnpdlheglhdfmldimlhpnegondlapf/";
    scoped_ptr<content::RenderWidgetHostIterator> widgets(
        content::RenderWidgetHost::GetRenderWidgetHosts());
    while (content::RenderWidgetHost* widget = widgets->GetNextHost()) {
      if (widget->IsRenderView()) {
        content::RenderViewHost* view = content::RenderViewHost::From(widget);
        std::string url = view->GetSiteInstance()->GetSiteURL().spec();
        if (url == kVirtualKeyboardURL) {
          content::WebContents* wc =
              content::WebContents::FromRenderViewHost(view);
          // Waits for Polymer to load.
          content::WaitForLoadStop(wc);
          return view;
        }
      }
    }
    return NULL;
  }

 private:

  // Injects javascript into the keyboard page.  The test |file| is in
  // directory |dir| relative to the root testing directory.
  void InjectJavascript(const base::FilePath& dir,
                        const base::FilePath& file) {
    base::FilePath path = ui_test_utils::GetTestFilePath(dir, file);
    std::string library_content;
    ASSERT_TRUE(base::ReadFileToString(path, &library_content))
        << path.value();
    utf8_content_.append(library_content);
    utf8_content_.append(";\n");
  }

  std::string utf8_content_;
};

// crbug.com/367817. Either this feature or just the test are depending
// on the presense of Object.observe which is presently disabled by default.
IN_PROC_BROWSER_TEST_F(VirtualKeyboardBrowserTest, DISABLED_AttributesTest) {
  RunTest(base::FilePath(FILE_PATH_LITERAL("attributes_test.js")));
}

IN_PROC_BROWSER_TEST_F(VirtualKeyboardBrowserTest, TypingTest) {
  RunTest(base::FilePath(FILE_PATH_LITERAL("typing_test.js")));
}

IN_PROC_BROWSER_TEST_F(VirtualKeyboardBrowserTest, ControlKeysTest) {
  RunTest(base::FilePath(FILE_PATH_LITERAL("control_keys_test.js")));
}

IN_PROC_BROWSER_TEST_F(VirtualKeyboardBrowserTest, HideKeyboardKeyTest) {
  RunTest(base::FilePath(FILE_PATH_LITERAL("hide_keyboard_key_test.js")));
}

IN_PROC_BROWSER_TEST_F(VirtualKeyboardBrowserTest, KeysetTransitionTest) {
  RunTest(base::FilePath(FILE_PATH_LITERAL("keyset_transition_test.js")));
}

// Fails when enabling Object.observe. See http://crbug.com/370004
#if defined(OS_CHROMEOS)
#define MAYBE_IsKeyboardLoaded DISABLED_IsKeyboardLoaded
#else
#define MAYBE_IsKeyboardLoaded IsKeyboardLoaded
#endif
IN_PROC_BROWSER_TEST_F(VirtualKeyboardBrowserTest, MAYBE_IsKeyboardLoaded) {
  content::RenderViewHost* keyboard_rvh = GetKeyboardRenderViewHost();
  ASSERT_TRUE(keyboard_rvh);
  bool loaded = false;
  std::string script = "!!chrome.virtualKeyboardPrivate";
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
      keyboard_rvh,
      "window.domAutomationController.send(" + script + ");",
      &loaded));
  // Catches the regression in crbug.com/308653.
  ASSERT_TRUE(loaded);
}

IN_PROC_BROWSER_TEST_F(VirtualKeyboardBrowserTest, DISABLED_EndToEndTest) {
  // Get the virtual keyboard's render view host.
  content::RenderViewHost* keyboard_rvh = GetKeyboardRenderViewHost();
  ASSERT_TRUE(keyboard_rvh);

  // Get the test page's render view host.
  content::RenderViewHost* browser_rvh = browser()->tab_strip_model()->
      GetActiveWebContents()->GetRenderViewHost();
  ASSERT_TRUE(browser_rvh);

  // Set up the test page.
  GURL url = ui_test_utils::GetTestUrl(
      base::FilePath(),
      base::FilePath(FILE_PATH_LITERAL(
          "chromeos/virtual_keyboard/end_to_end_test.html")));
  ui_test_utils::NavigateToURL(browser(), url);

  // Press 'a' on keyboard.
  base::FilePath path = ui_test_utils::GetTestFilePath(
      kVirtualKeyboardTestDir,
      base::FilePath(FILE_PATH_LITERAL("end_to_end_test.js")));
  std::string script;
  ASSERT_TRUE(base::ReadFileToString(path, &script));
  EXPECT_TRUE(content::ExecuteScript(keyboard_rvh, script));
  // Verify 'a' appeared on test page.
  bool success = false;
  EXPECT_TRUE(content::ExecuteScriptAndExtractBool(
      browser_rvh,
      "success ? verifyInput('a') : waitForInput('a');",
      &success));
  ASSERT_TRUE(success);
}

// TODO(kevers|rsadam|bshe):  Add UI tests for remaining virtual keyboard
// functionality.
