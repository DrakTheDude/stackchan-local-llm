---
name: Report from another robot
about: You ran this on a StackChan that is not the reference unit
title: "[robot] "
labels: hardware-report
---

Everything in this project is verified on **exactly one** StackChan. A report from a second one is the
most useful thing we can receive — including, especially, a boring one that says it all worked.

## Did it work?

<!-- Voice, face, head, LED ring, camera, wake word — what worked and what did not. -->

## The boot log

Serial at 115200 baud, from power-on to about twenty seconds in. This is the single most useful thing
you can attach — it carries the hardware bring-up, the self-check and the servo calibration.

<details><summary>boot log</summary>

```
paste here
```

</details>

## The lines we care about most

If you would rather not attach the whole log, these four answer most questions:

```
grep -E "factory centres|boot self-check|PY32|Detected Camera sensor" boot.log
```

- **`factory centres from NVS`** — your robot's own servo calibration. The reference unit reads
  `pan=460 tilt=620`; yours will differ, and knowing by how much tells us whether the fallback range is
  sensible. If it says `FALLBACK calibration`, that is itself worth reporting.
- **`boot self-check`** — PASSED, or which part did not come up.
- **`Detected Camera sensor PID`** — the reference unit is `0x9b` (GC0308). A different sensor would
  explain a lot about photographs.
- Any **`I2C_If: Fail to`** lines and roughly when — there is a known, undiagnosed bus stall about ten
  seconds into every boot, and we would like to know whether yours does it too.

## Your setup

- Where the server runs (OS, GPU or CPU-only):
- Model and how it is served:
- Anything unusual about your network:

## Did the head sit straight?

The centre comes from your robot's own NVS, but the small bench trim
(`CONFIG_STACKCHAN_PAN_TRIM`) is per-unit and defaults to 0. If his head sits off centre, roughly how
many degrees and which way?
