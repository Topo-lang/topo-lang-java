#!/usr/bin/env python3
"""Require actual Java audit coverage and report platform-limited cases."""
import argparse
import json
from pathlib import Path
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--registered", type=Path, required=True)
    parser.add_argument("--junit", type=Path, required=True)
    parser.add_argument("--summary", type=Path, required=True)
    parser.add_argument("--platform", choices=("Linux", "macOS", "Windows"), required=True)
    args = parser.parse_args()
    registered = json.loads(args.registered.read_text())["tests"]
    names = {test["name"] for test in registered}
    required = {
        "JavaEmitterRealJavacFixture." + name for name in (
            "EmptyClassCompilesUnderJavac", "VoidFunctionInNamespaceCompilesUnderJavac",
            "IntFreeFunctionWithEmptyBodyCompilesUnderJavac", "GenericClassWithBoundCompilesUnderJavac",
            "ArrayFieldRoundtripsThroughJavac")
    }
    required.update({"JavaSourceFactsTest." + name for name in (
        "DirectFactoryLoadsRealJdtInstanceEdges", "AllFactoriesUseSameBatchForSymbolsCallsAndWrites",
        "RealJdtFfmFactsKeepStaticBindingAndEscapeCapability")})
    disabled = [test["name"] for test in registered for prop in test.get("properties", [])
                if prop["name"] == "DISABLED" and prop["value"]]
    cases = ET.parse(args.junit).getroot().findall(".//testcase")
    executed = {case.attrib["name"] for case in cases}
    failed = [case.attrib["name"] for case in cases
              if case.find("failure") is not None or case.find("error") is not None
              or case.attrib.get("status") == "fail"]
    skipped = [case.attrib["name"] for case in cases
               if case.attrib["name"] not in failed and
               (case.find("skipped") is not None or case.attrib.get("status") in ("notrun", "disabled", "skipped"))]
    unverified = [case.attrib["name"] for case in cases
                  if case.attrib["name"] not in failed + skipped and case.attrib.get("status", "run") != "run"]
    # Native FFM evidence inspection currently has a macOS ARM64 contract.
    # Keep this visible as unverified on the other runners, never as a pass.
    permitted_unverified = set() if args.platform == "macOS" else {
        "JavaArtifactEvidenceTest.DirectCAbiJoinUsesActualDylibAndNeverExecutesNativeInitializer"}
    summary = {"platform": args.platform, "registered": len(registered), "reported": len(cases),
               "passed": len(cases) - len(set(failed + skipped + unverified)), "failed": failed,
               "skippedUnverified": skipped, "unknownStatusUnverified": unverified, "disabled": disabled,
               "missingRequired": sorted(required - names), "missingResults": sorted(names - executed)}
    args.summary.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary, indent=2))
    if (not cases or failed or unverified or disabled or required - names or names != executed
            or set(skipped) - permitted_unverified):
        raise SystemExit("Java audit CTest coverage is absent, failed or unexpectedly unverified")


if __name__ == "__main__":
    main()
