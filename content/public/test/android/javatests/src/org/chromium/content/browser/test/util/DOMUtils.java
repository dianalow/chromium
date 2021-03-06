// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content.browser.test.util;

import android.graphics.Rect;
import android.test.ActivityInstrumentationTestCase2;
import android.util.JsonReader;

import junit.framework.Assert;

import org.chromium.content.browser.ContentViewCore;

import java.io.IOException;
import java.io.StringReader;
import java.util.concurrent.TimeoutException;

/**
 * Collection of DOM-based utilities.
 */
public class DOMUtils {

    /**
     * Returns the rect boundaries for a node by its id.
     */
    public static Rect getNodeBounds(final ContentViewCore viewCore, String nodeId)
            throws InterruptedException, TimeoutException {
        StringBuilder sb = new StringBuilder();
        sb.append("(function() {");
        sb.append("  var node = document.getElementById('" + nodeId + "');");
        sb.append("  if (!node) return null;");
        sb.append("  var width = Math.round(node.offsetWidth);");
        sb.append("  var height = Math.round(node.offsetHeight);");
        sb.append("  var x = -window.scrollX;");
        sb.append("  var y = -window.scrollY;");
        sb.append("  do {");
        sb.append("    x += node.offsetLeft;");
        sb.append("    y += node.offsetTop;");
        sb.append("  } while (node = node.offsetParent);");
        sb.append("  return [ Math.round(x), Math.round(y), width, height ];");
        sb.append("})();");

        String jsonText = JavaScriptUtils.executeJavaScriptAndWaitForResult(
                viewCore, sb.toString());

        Assert.assertFalse("Failed to retrieve bounds for " + nodeId,
                jsonText.trim().equalsIgnoreCase("null"));

        JsonReader jsonReader = new JsonReader(new StringReader(jsonText));
        int[] bounds = new int[4];
        try {
            jsonReader.beginArray();
            int i = 0;
            while (jsonReader.hasNext()) {
                bounds[i++] = jsonReader.nextInt();
            }
            jsonReader.endArray();
            Assert.assertEquals("Invalid bounds returned.", 4, i);

            jsonReader.close();
        } catch (IOException exception) {
            Assert.fail("Failed to evaluate JavaScript: " + jsonText + "\n" + exception);
        }

        return new Rect(bounds[0], bounds[1], bounds[0] + bounds[2], bounds[1] + bounds[3]);
    }

    /**
     * Focus a DOM node by its id.
     */
    public static void focusNode(final ContentViewCore viewCore, String nodeId)
            throws InterruptedException, TimeoutException {
        StringBuilder sb = new StringBuilder();
        sb.append("(function() {");
        sb.append("  var node = document.getElementById('" + nodeId + "');");
        sb.append("  if (node) node.focus();");
        sb.append("})();");

        JavaScriptUtils.executeJavaScriptAndWaitForResult(viewCore, sb.toString());
    }

    /**
     * Click a DOM node by its id.
     */
    public static void clickNode(ActivityInstrumentationTestCase2 activityTestCase,
            final ContentViewCore viewCore, String nodeId)
            throws InterruptedException, TimeoutException {
        int[] clickTarget = getClickTargetForNode(viewCore, nodeId);
        TouchCommon touchCommon = new TouchCommon(activityTestCase);
        touchCommon.singleClickView(viewCore.getContainerView(), clickTarget[0], clickTarget[1]);
    }

    /**
     * Long-press a DOM node by its id.
     */
    public static void longPressNode(ActivityInstrumentationTestCase2 activityTestCase,
            final ContentViewCore viewCore, String nodeId)
            throws InterruptedException, TimeoutException {
        int[] clickTarget = getClickTargetForNode(viewCore, nodeId);
        TouchCommon touchCommon = new TouchCommon(activityTestCase);
        touchCommon.longPressView(viewCore.getContainerView(), clickTarget[0], clickTarget[1]);
    }

    /**
     * Scrolls the view to ensure that the required DOM node is visible.
     */
    public static void scrollNodeIntoView(ContentViewCore viewCore, String nodeId)
            throws InterruptedException, TimeoutException {
        JavaScriptUtils.executeJavaScriptAndWaitForResult(viewCore,
                "document.getElementById('" + nodeId + "').scrollIntoView()");
    }

    /**
     * Returns the contents of the node by its id.
     */
    public static String getNodeContents(ContentViewCore viewCore, String nodeId)
            throws InterruptedException, TimeoutException {
        return getNodeField("textContent", viewCore, nodeId);
    }

    /**
     * Returns the value of the node by its id.
     */
    public static String getNodeValue(final ContentViewCore viewCore, String nodeId)
            throws InterruptedException, TimeoutException {
        return getNodeField("value", viewCore, nodeId);
    }

    private static String getNodeField(String fieldName, final ContentViewCore viewCore,
            String nodeId)
            throws InterruptedException, TimeoutException {
        StringBuilder sb = new StringBuilder();
        sb.append("(function() {");
        sb.append("  var node = document.getElementById('" + nodeId + "');");
        sb.append("  if (!node) return null;");
        sb.append("  if (!node." + fieldName + ") return null;");
        sb.append("  return [ node." + fieldName + " ];");
        sb.append("})();");

        String jsonText = JavaScriptUtils.executeJavaScriptAndWaitForResult(
                viewCore, sb.toString());
        Assert.assertFalse("Failed to retrieve contents for " + nodeId,
                jsonText.trim().equalsIgnoreCase("null"));

        JsonReader jsonReader = new JsonReader(new StringReader(jsonText));
        String value = null;
        try {
            jsonReader.beginArray();
            if (jsonReader.hasNext()) value = jsonReader.nextString();
            jsonReader.endArray();
            Assert.assertNotNull("Invalid contents returned.", value);

            jsonReader.close();
        } catch (IOException exception) {
            Assert.fail("Failed to evaluate JavaScript: " + jsonText + "\n" + exception);
        }
        return value;
    }

    /**
     * Wait until a given node has non-zero bounds.
     * @return Whether the node started having non-zero bounds.
     */
    public static boolean waitForNonZeroNodeBounds(final ContentViewCore viewCore,
            final String nodeName)
            throws InterruptedException {
        return CriteriaHelper.pollForCriteria(new Criteria() {
            @Override
            public boolean isSatisfied() {
                try {
                    return !DOMUtils.getNodeBounds(viewCore, nodeName).isEmpty();
                } catch (InterruptedException e) {
                    // Intentionally do nothing
                    return false;
                } catch (TimeoutException e) {
                    // Intentionally do nothing
                    return false;
                }
            }
        });
    }

    /**
     * Returns click targets for a given DOM node.
     */
    private static int[] getClickTargetForNode(ContentViewCore viewCore, String nodeName)
            throws InterruptedException, TimeoutException {
        Rect bounds = getNodeBounds(viewCore, nodeName);
        Assert.assertNotNull("Failed to get DOM element bounds of '" + nodeName + "'.", bounds);

        int clickX = (int) viewCore.getRenderCoordinates().fromLocalCssToPix(bounds.exactCenterX())
                + viewCore.getViewportSizeOffsetWidthPix();
        int clickY = (int) viewCore.getRenderCoordinates().fromLocalCssToPix(bounds.exactCenterY())
                + viewCore.getViewportSizeOffsetHeightPix();
        return new int[] { clickX, clickY };
    }
}
