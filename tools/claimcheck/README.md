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

## Two things learned building it

**Scope privacy checks to files you author.** A repo-wide grep for private addresses flagged the
robot's own `192.168.4.1` setup page, then upstream's `192.168.2.100` placeholder in upstream's own
Kconfig. Neither is a leak. Excluding them one value at a time is whack-a-mole; scoping the claim to
the directories this project writes is both accurate and a smaller thing to keep true.

**`prove` is joined onto one line** before running. `configparser` returns a wrapped value with
newlines in it, and while `cmd &&⏎cmd` happens to be valid shell, `cmd⏎| grep` is a syntax error — so
a manifest that wrapped a pipe for readability would fail in a way that looks like the claim being
false rather than the claim being unreadable.
