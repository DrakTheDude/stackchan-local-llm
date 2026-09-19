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
        #
        # Fold the LINES, not every run of whitespace. `.split()` would also
        # collapse the two spaces inside grep -q '^  resident:' down to one,
        # and that proof then fails against a file it should match - which is
        # the exact confusion the joining exists to prevent.
        self.prove = " ".join(
            ln.strip() for ln in sect.get("prove", "").splitlines() if ln.strip())
        self.expect = sect.get("expect", "").strip()
        self.absent = sect.get("absent", "").strip()
        self.tag = sect.get("tag", "").strip()
        self.optional = sect.getboolean("optional", fallback=False)
        self.timeout = sect.getint("timeout", fallback=30)
        self.needs = sect.get("needs", "").strip()

    def needs_met(self) -> tuple[bool, str]:
        """Can this proof even be attempted? Returns (ok, why-not)."""
        if " " not in self.needs:
            if shutil.which(self.needs) is None:
                return False, f"needs `{self.needs}`, which is not on PATH"
            return True, ""
        try:
            p = subprocess.run(self.needs, shell=True, capture_output=True,
                               text=True, timeout=self.timeout)
        except Exception as e:                  # noqa: BLE001 - report, never crash
            return False, f"needs `{self.needs}`, which could not run: {e}"
        if p.returncode != 0:
            return False, f"needs `{self.needs}`, which is not satisfied here"
        return True, ""

    def run(self, cwd: str | None = None) -> tuple[str, str]:
        """Returns (status, detail). Never raises: a broken proof is a result."""
        if not self.prove:
            return FAIL, "no `prove` command in the manifest"

        # `needs` lets a claim opt out where its tooling is absent, rather than
        # failing. A laptop without docker should not fail the server claims; it
        # should say it could not check them, which is a different statement.
        #
        # 🔴 A BINARY ON PATH IS NOT THE SAME QUESTION AS "CAN THIS PROOF RUN".
        #    A claim that asks a running container whether it carries the current
        #    code needs the CONTAINER, not the docker client. With the stack down
        #    the old check passed `needs`, ran the proof, and reported that the
        #    image had lost the code - when the truth was that there was no image.
        #    A wrong answer in this tool's own failure mode, from the feature
        #    built to prevent exactly that. Found porting to a large repo.
        #
        #    So a `needs` containing a space is run as a command and skips on a
        #    non-zero exit; one without keeps the old meaning, so every manifest
        #    written before this behaves identically.
        if self.needs:
            ok, why = self.needs_met()
            if not ok:
                return SKIP, why

        try:
            p = subprocess.run(
                self.prove, shell=True, capture_output=True, text=True,
                timeout=self.timeout, cwd=cwd,
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


def default_manifest() -> str:
    """The manifest to use when nobody passed -f.

    CWD-relative first, so a project that keeps claims.ini at its root and runs
    ./claimcheck.py from there is unchanged. Failing that, the copy filed beside
    this script - a big repo files its tooling under tooling/, and there the
    CWD-relative default can never resolve.
    """
    if os.path.exists(DEFAULT_MANIFEST):
        return DEFAULT_MANIFEST
    beside = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          DEFAULT_MANIFEST)
    return beside if os.path.exists(beside) else DEFAULT_MANIFEST


def project_root(manifest: str) -> str:
    """Where a proof's relative paths are measured from.

    Proofs say `grep -q x drax/tools/janitor.py`, so they need a fixed origin.
    Inheriting the caller's cwd is not one: run from a subdirectory and every
    path-based proof fails at once, each announcing that a feature is missing
    when the feature is there and only the checker is lost. That is precisely
    the confusion this tool exists to stop, so it must not be how it behaves.

    The origin is the repo top level when the manifest is in a checkout, and
    the manifest's own directory otherwise - so a manifest at the root and a
    manifest filed under tooling/ both measure from the same place.
    """
    here = os.path.dirname(os.path.abspath(manifest)) or "."
    try:
        top = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=here,
                             capture_output=True, text=True, timeout=5)
        if top.returncode == 0 and top.stdout.strip():
            return top.stdout.strip()
    except Exception:                           # noqa: BLE001 - git is optional
        pass
    return here


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
    ap.add_argument("-f", "--manifest", default=None,
                    help=f"claims file (default: {DEFAULT_MANIFEST}, in the "
                         f"working directory or beside this script)")
    ap.add_argument("-t", "--tag", action="append", default=[],
                    help="only claims with this tag (repeatable)")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    ap.add_argument("--list", action="store_true",
                    help="print the claims without proving them")
    ap.add_argument("-q", "--quiet", action="store_true",
                    help="only show anything that is not a pass")
    args = ap.parse_args()

    manifest = args.manifest or default_manifest()
    claims = load(manifest)
    root = project_root(manifest)
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
        status, detail = c.run(root)
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
