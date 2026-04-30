#!/usr/bin/env python3

import sys
import os
import xml.etree.ElementTree as ET
from collections import defaultdict
from pathlib import Path


def process_report(xml_path):
    if not os.path.exists(xml_path):
        print(f"Error: {xml_path} not found.")
        return

    tree = ET.parse(xml_path)
    root = tree.getroot()

    # Update the top-level <testsuites> name based on the report filename
    report_filename = Path(xml_path).stem
    root.set("name", report_filename)

    # Find and extract all test cases from the original generic suite
    original_suite = root.find("testsuite")
    if original_suite is None:
        print("Error: original_suite not found.")
        return

    all_cases = list(original_suite.findall("testcase"))
    groups = defaultdict(list)

    # Initialize global totals for the <testsuites> header
    total_tests = 0
    total_failures = 0
    total_errors = 0
    total_skipped = 0
    orig_time = original_suite.get("time", None)

    for tc in all_cases:
        c_file = tc.get("file", "unknown")
        suite_name = Path(c_file).stem
        tc.set("classname", suite_name)
        groups[suite_name].append(tc)
        original_suite.remove(tc)

    for suite_name, cases in groups.items():
        new_suite = ET.SubElement(root, "testsuite", name=suite_name)
        s_failures = 0
        s_errors = 0
        s_skipped = 0

        for tc in cases:
            # Catch the ignored test message
            sysout = tc.find("system-out")
            is_ignored = False

            # Check if system-out contains our "Skipped:" message
            if sysout is not None and "Skipped:" in (sysout.get("message") or ""):
                is_ignored = True

            # Inject <skipped> tag if found
            if is_ignored and tc.find("skipped") is None:
                skipped_tag = ET.SubElement(tc, "skipped")
                skipped_tag.set("message", sysout.get("message"))

            # Update suite counters
            if tc.find("failure") is not None:
                s_failures += 1
            if tc.find("error") is not None:
                s_errors += 1
            if tc.find("skipped") is not None:
                s_skipped += 1

            new_suite.append(tc)

        # Set suite attributes
        new_suite.set("tests", str(len(cases)))
        new_suite.set("failures", str(s_failures))
        new_suite.set("errors", str(s_errors))
        new_suite.set("skipped", str(s_skipped))
        new_suite.set("time", str(0))

        # Accumulate global totals
        total_tests += len(cases)
        total_failures += s_failures
        total_errors += s_errors
        total_skipped += s_skipped

    # Update the <testsuites> global attributes
    root.set("tests", str(total_tests))
    root.set("failures", str(total_failures))
    root.set("errors", str(total_errors))
    root.set("skipped", str(total_skipped))
    root.set("time", orig_time)

    # Remove the now-empty original suite
    root.remove(original_suite)

    # Save the finalized report
    try:
        tree.write(xml_path, encoding="utf-8", xml_declaration=True)
        print(f"Report updated: {total_tests} tests in {len(groups)} suites.")
    except Exception as e:
        print(f"Failed to save the Junit xml file: {e}")
        return


if __name__ == "__main__":
    if len(sys.argv) > 1:
        process_report(sys.argv[1])
