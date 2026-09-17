#!/usr/bin/env python3
"""Fix upstream bugs that are not about language.

Separate from english-pass.py on purpose. That file has one job - translate the
Chinese the model sees or the user hears - and it is easier to reason about, and
to hand to someone else, if it stays that job. These are plain defects that
would be worth reporting upstream and that happen to bite us.

🔴 SAME DISCIPLINE, AND FOR THE SAME REASON: every replacement is asserted, so
   an upstream bump FAILS THE BUILD rather than silently reverting a fix. A
   patch that matches nothing is indistinguishable from a patch that worked,
   right up until someone notices the robot has started slurring its words
   again and has no idea when it started.
"""
import sys
from pathlib import Path

ROOT = Path("/opt/xiaozhi-esp32-server")

REPLACEMENTS = [
    # 🔴 EVERY STREAMED CHUNK GETS ITS TRAILING SPACE EATEN, so words run
    #    together in what he SAYS, not just what the screen shows.
    #
    #    Observed in the server's own log, spoken aloud exactly like this:
    #        "therewas astorm unlike anyother. Thepowerflickered,nodes dropped"
    #
    #    _clean_response_garbage() exists for a real reason: this deployment
    #    runs Intent=function_call, so the model's whole reply arrives as the
    #    `response` argument of the direct_answer tool - a JSON string - and
    #    models do sometimes leak the closing "}} into the text. Stripping that
    #    off the END OF A COMPLETE REPLY is correct.
    #
    #    The bug is that connection.py calls it on EVERY DELTA of the stream
    #    (around line 1183), and it ends with .rstrip(). A delta that happens to
    #    end on a space - which is most of them, since the stream is sliced on a
    #    character budget and not on word boundaries - loses that space, and the
    #    next delta is concatenated straight onto the previous word. A replay of
    #    thirteen realistic deltas through the container's own copy of this
    #    function lost ten spaces out of thirteen.
    #
    #    It also silently drops a delta that is ONLY whitespace: the result is
    #    "" which is falsy, and the caller's `if new_part:` skips it entirely.
    #
    # The fix keeps the garbage-stripping and puts the original trailing
    # whitespace back afterwards. Deliberately NOT "stop calling it per delta" -
    # that is the more correct fix and a much larger change to a 77KB upstream
    # file, and the leaked-JSON cleanup genuinely does need to see the stream.
    (
        "core/connection.py",
        """        result = re.sub(r'["\\'}\\]]+$', '', result.rstrip()).rstrip()
        return result""",
        """        # Trailing whitespace is PRESERVED. This runs on every streamed
        # delta, not only on a finished reply, and a delta ending in a space is
        # normal - eating it concatenates two words ("therewas"). The garbage
        # strip still happens; the space is put back after it.
        tail = result[len(result.rstrip()):]
        result = re.sub(r'["\\'}\\]]+$', '', result.rstrip()).rstrip() + tail
        return result""",
    ),
    # The memory summariser asks for up to 2000 tokens of output for a document
    # its own prompt caps at 900 CHARACTERS - call it 250 tokens. The ceiling is
    # therefore eight times the target, and it is not free: this generation runs
    # on the same single GPU as the conversation, and it starts the moment a
    # chat ends, which is moments before the next wake word. Every token it is
    # allowed to spend is time the next reply may have to queue behind.
    #
    # 700 is generous headroom over a measured ~250 and still bounds the wait.
    # It also acts as a brake on a summary that starts rambling - which would be
    # a bad summary anyway, since the useful ones are short.
    (
        "core/providers/memory/mem_local_short/mem_local_short.py",
        "                    max_tokens=2000,",
        "                    max_tokens=700,",
    ),
]


def main() -> int:
    failures = []
    edits = 0
    for rel, old, new in REPLACEMENTS:
        path = ROOT / rel
        if not path.is_file():
            failures.append(f"{rel}: file not found")
            continue
        text = path.read_text(encoding="utf-8")
        if new in text and old not in text:
            print(f"  already applied: {rel}")
            continue
        if old not in text:
            failures.append(f"{rel}: source text not found -> {old.strip()[:70]}")
            continue
        path.write_text(text.replace(old, new), encoding="utf-8")
        edits += 1
        print(f"  patched: {rel}")

    if failures:
        print("\nUPSTREAM FIXES FAILED - upstream has moved:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        print(
            "\nRe-check these against the image and update deploy/upstream-fixes.py. "
            "If a fix has landed upstream, DELETE the entry rather than relaxing "
            "the assertion - a patch that matches nothing is how a fix quietly "
            "disappears.",
            file=sys.stderr,
        )
        return 1

    print(f"upstream fixes ok ({edits} edit(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
