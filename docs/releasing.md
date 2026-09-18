# Releasing

For whoever cuts a release — a person or an assistant. Everything here was learned by publishing
something broken, so the order matters more than it looks.

**Only a tag publishes anything.** Pushes to `main` run no workflow at all; `v*` tags run the build,
the assertions, the GitHub release and the Pages deployment. So batch work on `main` and tag when
there is something worth a run.

## Cutting one

```bash
git push github main                       # the tag must point at what is already pushed
git tag -a v0.1.4 -F /tmp/tag-message.txt  # what changed, and what not to retry
git push github v0.1.4
```

Then watch the run, and when it is green **check the published artefacts rather than the log** — the
log tells you what CI believes, which is the thing that has been wrong before:

```bash
# the Pages copies must match the release's own checksums
curl -s https://drakthedude.github.io/stackchan-local-llm/flash/xiaozhi.bin | sha256sum
curl -sL https://github.com/DrakTheDude/stackchan-local-llm/releases/download/v0.1.4/SHA256SUMS

# chip id lives at byte 12 of the image header; 9 is esp32s3, 0 is esp32
curl -s https://drakthedude.github.io/stackchan-local-llm/flash/xiaozhi.bin | xxd -s 12 -l 1

# and a browser can only fetch what carries this header
curl -sI https://drakthedude.github.io/stackchan-local-llm/flash/xiaozhi.bin |
    grep -i access-control-allow-origin
```

## What CI asserts, and why each one exists

| check | the release it would have stopped |
|---|---|
| `CONFIG_IDF_TARGET` is `esp32s3` **and** the board is `M5STACK_STACKCHAN` | v0.1.0–v0.1.2, all three built for the wrong chip |
| `CONFIG_OTA_URL` contains `.invalid` | any build that would dial a developer's own server |
| no `192.168.x.x` anywhere in `sdkconfig` | the same, by a different route |
| the assets partition contains a wake-word model and `happy.png` | v0.1.2, which compiled and could not hear its name |
| the assets partition is over 1,000,000 bytes | the same — and this blunt floor is the check that would have caught it on its own |

**The order is deliberate.** Chip and board are asserted *first*, because the OTA URL check passed on
all three wrong-chip releases: that value comes from the defaults file whatever board is selected, so
it could never have caught the thing that was actually wrong. An assertion near the problem is not an
assertion on the problem.

## Traps, each of which cost a release

- 🔴 **A clean checkout is a different build.** With no `sdkconfig` — every CI run, every fresh
  clone — ESP-IDF defaults `IDF_TARGET` to `esp32` and the board falls through to a bread-board
  default. The build *succeeds*. `deploy/build.sh` pins `-DIDF_TARGET=esp32s3` on **every** `idf.py`
  call for this reason; passing it to `build` but not to `reconfigure` writes an esp32 CMakeCache and
  then refuses to build against it.
- 🔴 **Release assets are not fetchable by a browser.** They answer `200` to `curl` and carry no
  `Access-Control-Allow-Origin`. The flasher therefore serves its binaries from the *Pages artefact*,
  with relative paths in the manifest. Do not "simplify" it to point at the release.
- 🔴 **The merged image erases NVS.** It spans `0x0` to the end of the assets partition and pads
  the gaps with `0xFF` — and NVS at `0x9000` is in one of those gaps, holding the per-unit factory
  servo calibration. It is published as a deliberate clean-slate option and is deliberately **not** in
  `site/flash/`. The manifest writes each part at its own offset instead.
- ⚠️ **`producer | grep -q` under `pipefail` reports a match as a miss.** `grep -q` exits at the
  first match, the producer takes SIGPIPE and dies with 141, and `pipefail` makes 141 the pipeline's
  status. It only happens once the output outgrows the pipe buffer, so it **passes on a small wrong
  artefact and fails on a correct one** — which is exactly how it blocked a good release. Dump to a
  file, then grep the file.
- ⚠️ **A re-run replays the commit it started from.** A fix pushed afterwards cannot rescue it, and
  the log looks identical to the fix not working. Start a new run.
- ⚠️ **Pages deploys need the tag allowed** in the `github-pages` environment's protection rules.

## Moving a tag

Only if it published nothing — a run that failed before the release step leaves no artefacts and no
Pages deployment, so the version number is still free. Once a release exists, cut the next number
instead: somebody may already hold the link.

## After a release

Flash a real robot from the browser page and decline the erase, then check **Settings → About** still
says `factory calibration`. That is the one claim on the flasher page that no amount of CI can check,
because the thing it protects only exists on somebody's actual desk.
