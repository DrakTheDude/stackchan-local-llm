## What changes, and what was measured

<!-- What is now true that was not before. If it is a fix, what was wrong.
     "It seems faster" is not a result - model-floor.md documents the noise
     floor so that claims can be checked against it: tok/s moves less than 1%
     between runs, and the tool score swings two cases in twenty-one. A change
     smaller than that has not been shown yet. -->

## Checks

- [ ] `python3 tools/claimcheck/claimcheck.py` — held
- [ ] Built with `./deploy/build.sh` if firmware changed. A plain `idf.py build`
      silently builds a different board pointed at the vendor's cloud, and
      reports success
- [ ] Tried on real hardware, or it says plainly that it was not

## If you are attaching files

- [ ] **No full-flash or NVS dump.** They contain your Wi-Fi password in plain
      text. `BOARD_REPORT` over the serial console is the safe equivalent and
      carries no MAC address and no network name
- [ ] **Bench results:** opened the JSON and checked `hardware` names the *card*
      and not your machine. Run `sweep.sh` with a label — an unlabelled sweep
      files under `unlabelled` on purpose
- [ ] No LAN addresses, Wi-Fi names, MAC addresses or photographs of a room
      anywhere in the diff. Git history is forever, and a claim enforces the
      address rule because it has been broken before

## 🔴 Never relax an assertion to get a build through

The patch kit and the claim checker exist to fail loudly when reality moves. An
assertion loosened to pass is a check that cannot fail, and this project has
been bitten by exactly that more than once. If a check is wrong, say so in the
PR and change it deliberately — that is a different conversation from making it
quiet.
