#!/usr/bin/env python3
"""claimcheck - does this project still have the features it claims to have?

Not a health check. A health check asks "is it up", and everything this was
built for was up at the time.

    The flasher wrote a six-week-old binary and printed "flashed".
    A build produced the wrong board, for the wrong chip, and reported success.
    A whole feature - the robot polling the fleet on its own - vanished from
      every build for weeks, and nothing said so, because the half of it that
      people exercise by hand still worked.

Every one of those is a SILENT SUCCESS: something stopped existing, and the
parts around it kept answering. Nothing was down. Nothing logged an error. They
were found by accident, days or weeks later, and one of them was found only
because somebody went looking for an unrelated stale path.

So the unit here is not a service, it is a CLAIM WITH A PROOF: a line saying
this feature exists, and a one-line command that would fail if it stopped. Run
it in CI, after a deploy, before a release, or on a loop while you work. The day
a proof stops holding, it says so - loudly, by name, with the reason the feature
mattered.

    ./claimcheck.py                     # every claim
    ./claimcheck.py --tag firmware      # just one area
    ./claimcheck.py --json              # for CI
    ./claimcheck.py --list              # what is claimed, without running it

Exit code is the number of FAILED REQUIRED claims, so a shell or a CI step can
branch on it directly.

────────────────────────────────────────────────────────────────────────────────
PORTING THIS TO ANOTHER PROJECT

Copy this file. Write a claims.ini. That is the whole of it - there is nothing
project-specific in here, no dependencies beyond the standard library, and no
config beyond the manifest path.

A claim looks like:

    [firmware: our OTA URL, not the vendor cloud]
    why   = Regenerating sdkconfig without the defaults list silently builds a
            different board pointed at api.tenclass.net, and reports success.
    prove = grep -q 'CONFIG_OTA_URL=' firmware/sdkconfig
    tag   = firmware

Optional keys:

    expect   = substring that must appear in the command's output
    absent   = substring that must NOT appear (for "this leak is not here")
    optional = true      -> a failure is a warning, not a failure
    timeout  = 30        -> seconds, default 30

🔴 THE `why` IS NOT DECORATION. A proof that fails six months from now will be
   read by somebody who does not know what it was protecting, and their first
   instinct will be to delete the claim. `why` is what stops that.

⚠️ A GOOD PROOF IS CHEAP AND SPECIFIC. If it needs a running robot, a network,
   or thirty seconds, it will get skipped and then removed. Prefer grepping a
   built artifact over calling a live service; prefer one specific string over
   "does it look right".
"""

from __future__ import annotations

import argparse
import configparser
import json
import os
import shutil
import subprocess
import sys
import time

DEFAULT_MANIFEST = "claims.ini"

# Colour only when a human is watching. CI logs are worse with escape codes in
# them, and a checker whose output is unreadable in CI is a checker nobody runs.
if sys.stdout.isatty() and os.environ.get("NO_COLOR") is None:
    RED, GRN, YEL, DIM, BLD, OFF = (
        "\033[31m", "\033[32m", "\033[33m", "\033[2m", "\033[1m", "\033[0m")
else:
    RED = GRN = YEL = DIM = BLD = OFF = ""

PASS, FAIL, WARN, SKIP = "pass", "fail", "warn", "skip"


class Claim:
    """One thing this project says is true, and how to find out."""

    def __init__(self, name: str, sect: configparser.SectionProxy):
        self.name = name
        self.why = " ".join(sect.get("why", "").split())
        # Joined onto ONE line. configparser hands back a wrapped value with
        # newlines in it, and while "cmd &&<newline>cmd" happens to be valid
        # shell, "cmd<newline>| grep" is a syntax error - so a manifest that
        # wrapped a pipe for readability would fail in a way that looks like
        # the claim being false rather than the claim being unreadable.
        self.prove = " ".join(sect.get("prove", "").split())
        self.expect = sect.get("expect", "").strip()
        self.absent = sect.get("absent", "").strip()
        self.tag = sect.get("tag", "").strip()
        self.optional = sect.getboolean("optional", fallback=False)
        self.timeout = sect.getint("timeout", fallback=30)
        self.needs = sect.get("needs", "").strip()

    def run(self) -> tuple[str, str]:
        """Returns (status, detail). Never raises: a broken proof is a result."""
        if not self.prove:
            return FAIL, "no `prove` command in the manifest"

        # `needs` lets a claim opt out where its tooling is absent, rather than
        # failing. A laptop without docker should not fail the server claims; it
        # should say it could not check them, which is a different statement.
        if self.needs and shutil.which(self.needs) is None:
            return SKIP, f"needs `{self.needs}`, which is not on PATH"

        try:
            p = subprocess.run(
                self.prove, shell=True, capture_output=True, text=True,
                timeout=self.timeout,
            )
        except subprocess.TimeoutExpired:
            return FAIL, f"proof timed out after {self.timeout}s"
        except Exception as e:                      # noqa: BLE001 - report, never crash
            return FAIL, f"proof could not run: {e}"

        out = (p.stdout or "") + (p.stderr or "")

        if p.returncode != 0:
            first = next((l for l in out.splitlines() if l.strip()), "")
            detail = f"exit {p.returncode}"
            return FAIL, f"{detail}: {first}" if first else detail

        if self.expect and self.expect not in out:
            return FAIL, f"expected {self.expect!r} in the output, not found"

        if self.absent and self.absent in out:
            return FAIL, f"{self.absent!r} is present and should not be"

        return PASS, ""


def load(path: str) -> list[Claim]:
    cp = configparser.ConfigParser(interpolation=None)
    # Keep key case as written; the manifest is read by people.
    cp.optionxform = str
    if not cp.read(path, encoding="utf-8"):
        sys.exit(f"claimcheck: no manifest at {path}")
    return [Claim(name, cp[name]) for name in cp.sections()]


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Check that this project still has the features it claims.")
    ap.add_argument("-f", "--manifest", default=DEFAULT_MANIFEST,
                    help=f"claims file (default: {DEFAULT_MANIFEST})")
    ap.add_argument("-t", "--tag", action="append", default=[],
                    help="only claims with this tag (repeatable)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--list", action="store_true",
                    help="print the claims without proving them")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="only show anything that is not a pass")
    args = ap.parse_args()

    claims = load(args.manifest)
    if args.tag:
        claims = [c for c in claims if c.tag in args.tag]
    if not claims:
        sys.exit("claimcheck: no claims matched")

    if args.list:
        for c in claims:
            tag = f"{DIM}[{c.tag}]{OFF} " if c.tag else ""
            print(f"{tag}{c.name}")
            if c.why:
                print(f"    {DIM}{c.why}{OFF}")
        return 0

    results, counts = [], {PASS: 0, FAIL: 0, WARN: 0, SKIP: 0}
    started = time.time()

    for c in claims:
        status, detail = c.run()
        if status == FAIL and c.optional:
            status = WARN
        counts[status] += 1
        results.append({"claim": c.name, "tag": c.tag, "status": status,
                        "detail": detail, "why": c.why})

        if args.json:
            continue
        if status == PASS:
            if not args.quiet:
                print(f"  {GRN}✓{OFF} {c.name}")
        elif status == SKIP:
            print(f"  {DIM}-{OFF} {c.name}\n    {DIM}{detail}{OFF}")
        else:
            mark = f"{RED}✗{OFF}" if status == FAIL else f"{YEL}!{OFF}"
            print(f"  {mark} {BLD}{c.name}{OFF}")
            print(f"    {detail}")
            # The reason the feature mattered, printed exactly when somebody is
            # deciding whether to care. This is the whole point of `why`.
            if c.why:
                print(f"    {DIM}why it matters: {c.why}{OFF}")

    if args.json:
        print(json.dumps({"claims": results, "summary": counts}, indent=2))
        return counts[FAIL]

    took = time.time() - started
    print()
    print(f"{BLD}{counts[PASS]} held, {counts[FAIL]} FAILED, "
          f"{counts[WARN]} warnings, {counts[SKIP]} unchecked{OFF}  ({took:.1f}s)")
    if counts[FAIL]:
        print(f"{RED}A feature this project claims to have is missing or broken.{OFF}")
        print("Each failure above says what it was protecting.")
    elif counts[SKIP]:
        print(f"{DIM}Everything checkable held. Some proofs needed tooling that "
              f"is not here.{OFF}")
    else:
        print(f"{GRN}Every claim held.{OFF}")
    return counts[FAIL]


if __name__ == "__main__":
    sys.exit(main())
