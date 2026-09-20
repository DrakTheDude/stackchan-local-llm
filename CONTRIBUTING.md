# Contributing

Everything in this repository is verified on **one robot and two graphics
cards**. That is the honest limit of it, and it is also the thing you can most
easily help with.

## The most useful things you can send

**1. A `BOARD_REPORT` from a different unit.** Hold nothing back and paste the
whole thing — it prints the I²C scan, the servo rail, the servos and their
calibration source, the LED chain, the codec and the camera's own registers, in
a fixed order so two robots diff. It deliberately carries **no MAC address and
no Wi-Fi name**, so it is safe in a public issue. There is an issue template for
this: *a second robot*.

**2. A model sweep from a third card**, especially a non-NVIDIA one:

```bash
OLLAMA=http://your-host:11434 ./tools/model-bench/sweep.sh "RX 7900 XT 20GB"
```

Two cards were enough to disagree with each other, which was the most useful
thing they said. A third would be worth more than any feature on the roadmap.

**3. A result that contradicts the documentation.** [privacy.md](docs/privacy.md)
is six checks you can run yourself. If one of them fails on your machine, that
is the single most valuable issue anybody could open here.

**4. Telling us a guide is wrong.** If you followed the quickstart and something
did not happen the way it said, that is a bug in the quickstart.

## Before you open a pull request

```bash
python3 tools/claimcheck/claimcheck.py     # 21 assertions about what this thing claims to be
./deploy/build.sh                          # the only invocation that passes the right sdkconfig defaults
```

🔴 **Never relax an assertion to get a build through.** The patch kit and the
claim checker exist to fail loudly when reality moves; an assertion that has
been loosened to pass is a check that cannot fail, and this project has been
bitten by exactly that more than once.

## House rules, and why

**Nothing enters history that could not be public.** No LAN addresses, no Wi-Fi
names, no MAC addresses, no tokens, no factory firmware or NVS dumps, and no
photographs of anybody's room. A claim enforces the address rule because it has
been broken before — seven benchmark files once carried the machine they were
measured on.

**Commit messages are written to be read.** Most of them record a measurement,
the theory it killed, and what not to retry. `git log` before re-investigating
anything; there is a good chance the answer, and the two wrong answers that came
first, are already in there.

**Comments say why, not what.** The code says what it does. The comment exists
for the person who is about to "simplify" something that looks redundant and is
load-bearing — there are several, and each one cost a day.

**Say what you measured.** "It seems faster" is not a result;
[model-floor.md](docs/model-floor.md) documents the noise floor precisely so
that claims can be checked against it. Tokens per second moves less than 1%
between runs; the tool score swings two cases in twenty-one. If your change is
smaller than that, it has not been shown yet.

## Firmware, specifically

Read the [board README](firmware/main/boards/m5stack/stackchan/README.md) first.
It has a list of invariants that each rebooted or broke the robot at least once
— never `ESP_ERROR_CHECK` an I²C call, no LVGL work on the `esp_timer` task, and
a handful of others that look like fussiness and are not.

⚠️ **Build with `./deploy/build.sh`.** A plain `idf.py build` silently builds a
different board, with the vendor's cloud as the OTA URL, and reports success.
