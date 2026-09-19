# claimcheck

**Does this project still have the features it claims to have?**

Not a health check. A health check asks *is it up*, and everything this was built for **was up at
the time**.

- A flasher wrote a six-week-old binary and printed `flashed`.
- A build produced firmware for the wrong board, on the wrong chip, and reported success.
- A whole feature — the robot polling its fleet on its own — vanished from every build for weeks,
  and nothing said so, because the half people exercise by hand still worked.

Every one of those is a **silent success**: something stopped existing, and the parts around it kept
answering. Nothing was down. Nothing logged an error. Each was found by accident, days or weeks
later, and one was found only because somebody went looking for an unrelated stale path.

So the unit here is not a service. It is a **claim with a proof**: a line saying this feature exists,
and a one-line command that would fail if it stopped.

```bash
./tools/claimcheck/claimcheck.py            # every claim
./tools/claimcheck/claimcheck.py -t server  # one area
./tools/claimcheck/claimcheck.py --json     # for CI
./tools/claimcheck/claimcheck.py --list     # what is claimed, without proving it
```

Exit code is the number of failed required claims, so a shell or a CI step branches on it directly.

## Porting it

Copy `claimcheck.py`. Write a `claims.ini`. That is all of it — there is nothing project-specific in
the runner, no dependencies beyond the Python standard library, and no configuration beyond the
manifest path.

```ini
[firmware: our OTA URL, not the vendor cloud]
why   = Regenerating sdkconfig without the defaults list silently builds a
        different board pointed at the vendor cloud, and reports success.
prove = grep -q '^CONFIG_OTA_URL=' firmware/sdkconfig
absent = tenclass
tag   = firmware
```

| key | meaning |
|---|---|
| `why` | what breaks if this stops being true. **Not decoration** — see below |
| `prove` | a shell command; exit 0 means the claim holds. May wrap across lines |
| `expect` | substring that must appear in the output |
| `absent` | substring that must **not** appear — for "this leak is not here" |
| `tag` | group, for `-t` |
| `needs` | a command that must be on `PATH`, else the claim is *unchecked* rather than failed |
| `optional` | `true` → a failure is a warning |
| `timeout` | seconds, default 30 |

## Writing a good claim

🔑 **The `why` is the most important field.** A proof that fails six months from now will be read by
somebody who does not know what it was protecting, and their first instinct will be to delete the
claim to get the build green. The `why` is what stops that, and it is printed at exactly the moment
somebody is deciding whether to care.

**A good proof is cheap and specific.** If it needs a running robot, a network round trip, or thirty
seconds, it will get skipped, then commented out, then deleted. Prefer grepping a built artifact
over calling a live service. Prefer one exact string over "does it look right".

**`needs` instead of a red cross.** A laptop without Docker should not *fail* the container claims —
it cannot check them, which is a different and more honest statement. `needs = docker` says so.

**The bar for adding a claim** is not "this seems important". It is *this broke silently once, and we
found out later*. A manifest of everything you can think of becomes noise, and a noisy checker is one
nobody runs.

## Falsify every claim before you trust it

**A proof you have never seen fail is a claim about your grep, not about your project.**

Take a throwaway `git worktree`, break the thing each claim guards, confirm the claim fails, throw
the worktree away. It takes minutes and it is the difference between a manifest and a decoration.

Doing this to the manifest in this repo found three bugs immediately, all of them the *same* bug —
a claim that could not fail:

- **`absent` could never fire.** The most important claim here — the one guarding against the robot's
  microphone and camera talking to an undocumented vendor endpoint — used `grep -q`, which prints
  nothing. `absent` then searched an empty string and passed for any value at all. It had been
  decorative since the day it was written, and it looked identical on a correct repo and a broken one.
- **Four claims failed on a clean checkout**, because they inspect generated, gitignored artifacts.
  A fresh clone was told its OTA URL was wrong when the truth was that nothing had been built.
- **`optional = true` hid a real failure** behind a warning, on a claim that was only optional to
  work around the problem above.

## `needs` should ask a question, not look for a binary

`needs = docker` establishes that the client is installed. It does **not** establish that the
container is running — so a claim asking a live container whether it carries the current code will
run anyway, fail, and report *"the image lost the code"* when the truth is *"there is no image."*

A wrong answer, in this tool's own failure mode, from the feature built to prevent exactly that.
Found porting to a large repo, and present here too.

So a `needs` value containing a space is run as a command and skips on a non-zero exit:

```ini
needs = docker inspect my-container     # asks the question that matters
needs = docker                          # still just checks PATH
```

Same lever fixes the clean-checkout problem — `needs = test -f firmware/sdkconfig` turns four
confident accusations into an honest *unchecked*.

## ⚠️ Hand-verify a proof and you may be testing a different binary

In some agent shells `grep` is a shell function routing to `ugrep`, which silently returns *no match*
where GNU grep matches. `claimcheck` runs proofs through `/bin/sh`, which has no such function and
gets the real binary.

**If a proof passes by hand and fails in claimcheck — or the reverse — check which `grep` before
suspecting the runner.** That one cost an hour elsewhere.

## Two things learned building it

**Scope privacy checks to files you author.** (Note this section names no addresses: a
document that quotes the values a privacy claim looks for will trip that claim. Describing
them beats excluding this file, because every exclusion shrinks what the claim covers.) A repo-wide grep for private addresses flagged the
robot's own SoftAP setup address, then an upstream placeholder address in upstream's own
Kconfig. Neither is a leak. Excluding them one value at a time is whack-a-mole; scoping the claim to
the directories this project writes is both accurate and a smaller thing to keep true.

**`prove` is joined onto one line** before running. `configparser` returns a wrapped value with
newlines in it, and while `cmd &&⏎cmd` happens to be valid shell, `cmd⏎| grep` is a syntax error — so
a manifest that wrapped a pipe for readability would fail in a way that looks like the claim being
false rather than the claim being unreadable.
