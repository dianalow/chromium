# Copyright 2014 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import math
import unittest

# Special import necessary because filename contains dash characters.
bisect_perf_module = __import__('bisect-perf-regression')


class BisectPerfRegressionTest(unittest.TestCase):
  """Test case for top-level functions in the bisect-perf-regrssion module."""

  def setUp(self):
    """Sets up the test environment before each test method."""
    pass

  def tearDown(self):
    """Cleans up the test environment after each test method."""
    pass

  def testParseDEPSStringManually(self):
    """Tests DEPS parsing."""
    bisect_options = bisect_perf_module.BisectOptions()
    bisect_instance = bisect_perf_module.BisectPerformanceMetrics(
        None, bisect_options)

    deps_file_contents = """
vars = {
    'ffmpeg_hash':
         '@ac4a9f31fe2610bd146857bbd55d7a260003a888',
    'webkit_url':
         'https://chromium.googlesource.com/chromium/blink.git',
    'git_url':
         'https://chromium.googlesource.com',
    'webkit_rev':
         '@e01ac0a267d1017288bc67fa3c366b10469d8a24',
    'angle_revision':
         '74697cf2064c0a2c0d7e1b1b28db439286766a05'
}"""

    # Should only expect svn/git revisions to come through, and urls to be
    # filtered out.
    expected_vars_dict = {
        'ffmpeg_hash': '@ac4a9f31fe2610bd146857bbd55d7a260003a888',
        'webkit_rev': '@e01ac0a267d1017288bc67fa3c366b10469d8a24',
        'angle_revision': '74697cf2064c0a2c0d7e1b1b28db439286766a05'
    }
    vars_dict = bisect_instance._ParseRevisionsFromDEPSFileManually(
        deps_file_contents)
    self.assertEqual(vars_dict, expected_vars_dict)

  def testCalculateTruncatedMeanRaisesError(self):
    """CalculateTrunctedMean raises an error when passed an empty list."""
    with self.assertRaises(TypeError):
      bisect_perf_module.CalculateTruncatedMean([], 0)

  def testCalculateMeanSingleNum(self):
    """Tests the CalculateMean function with a single number."""
    self.assertEqual(3.0, bisect_perf_module.CalculateMean([3]))

  def testCalculateMeanShortList(self):
    """Tests the CalculateMean function with a short list."""
    self.assertEqual(0.5, bisect_perf_module.CalculateMean([-3, 0, 1, 4]))

  def testCalculateMeanCompareAlternateImplementation(self):
    """Tests CalculateMean by comparing against an alternate implementation."""
    def AlternateMeanFunction(values):
      """Simple arithmetic mean function."""
      return sum(values) / float(len(values))
    test_values_lists = [[1], [5, 6.5, 1.2, 3], [-3, 0, 1, 4],
                         [-3, -1, 0.12, 0.752, 3.33, 8, 16, 32, 439]]
    for values in test_values_lists:
      self.assertEqual(
          AlternateMeanFunction(values),
          bisect_perf_module.CalculateMean(values))

  def testCalculateConfidence(self):
    """Tests the confidence calculation."""
    bad_values = [[0, 1], [1, 2]]
    good_values = [[6, 7], [7, 8]]
    # Closest means are mean(1, 2) and mean(6, 7).
    distance = 6.5 - 1.5
    # Standard deviation of [n-1, n, n, n+1] is 0.8165.
    stddev_sum = 0.8165 + 0.8165
    # Expected confidence is an int in the range [0, 100].
    expected_confidence = min(100, int(100 * distance / float(stddev_sum)))
    self.assertEqual(
        expected_confidence,
        bisect_perf_module.CalculateConfidence(bad_values, good_values))

  def testCalculateConfidence0(self):
    """Tests the confidence calculation when it's expected to be 0."""
    bad_values = [[0, 1], [1, 2], [4, 5], [0, 2]]
    good_values = [[4, 5], [6, 7], [7, 8]]
    # Both groups have value lists with means of 4.5, which means distance
    # between groups is zero, and thus confidence is zero.
    self.assertEqual(
        0, bisect_perf_module.CalculateConfidence(bad_values, good_values))

  def testCalculateConfidence100(self):
    """Tests the confidence calculation when it's expected to be 100."""
    bad_values = [[1, 1], [1, 1]]
    good_values = [[1.2, 1.2], [1.2, 1.2]]
    # Standard deviation in both groups is zero, so confidence is 100.
    self.assertEqual(
        100, bisect_perf_module.CalculateConfidence(bad_values, good_values))

  def testCalculateRelativeChange(self):
    """Tests the common cases for calculating relative change."""
    # The change is relative to the first value, regardless of which is bigger.
    self.assertEqual(0.5, bisect_perf_module.CalculateRelativeChange(1.0, 1.5))
    self.assertEqual(0.5, bisect_perf_module.CalculateRelativeChange(2.0, 1.0))

  def testCalculateRelativeChangeFromZero(self):
    """Tests what happens when relative change from zero is calculated."""
    # If the first number is zero, then the result is not a number.
    self.assertEqual(0, bisect_perf_module.CalculateRelativeChange(0, 0))
    self.assertTrue(
        math.isnan(bisect_perf_module.CalculateRelativeChange(0, 1)))
    self.assertTrue(
        math.isnan(bisect_perf_module.CalculateRelativeChange(0, -1)))

  def testCalculateRelativeChangeWithNegatives(self):
    """Tests that relative change given is always positive."""
    self.assertEqual(3.0, bisect_perf_module.CalculateRelativeChange(-1, 2))
    self.assertEqual(3.0, bisect_perf_module.CalculateRelativeChange(1, -2))
    self.assertEqual(1.0, bisect_perf_module.CalculateRelativeChange(-1, -2))


if __name__ == '__main__':
  unittest.main()
