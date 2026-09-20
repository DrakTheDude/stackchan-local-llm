# What he does, and why

Everything the robot does on his own, with the reason it works that way. Most of
these look like decoration and are not — several of them are the only feedback
you get when something is wrong.

---

## The face

**Drawn, not animated.** Every expression is a handful of numbers — eye height,
lid, brow angle, mouth width and curve — turned into LVGL shapes at 20 frames a
second. There is no sprite sheet, so a new body changes the *drawing*, not a set
of pictures, and a face is about 200 bytes rather than a megabyte of PNGs.

**Twenty-one expressions**, chosen by the model as it replies: `neutral`,
`happy`, `laughing`, `funny`, `loving`, `kissy`, `delicious`, `sad`, `crying`,
`angry`, `confident`, `cool`, `relaxed`, `sleepy`, `surprised`, `shocked`,
`thinking`, `confused`, `embarrassed`, `winking`, `silly`.

⚠️ **An unknown name is a neutral face and no log line.** Models invent emotion
names constantly, and the framework itself asks for `robot_2`. Warning about it
would mean a warning on a large fraction of replies.

On top of the expression sit two things the expression does not control:

- **Blinking**, every 2–6 seconds, jittered. Regular blinking reads as
  mechanical; the jitter is the entire point.
- **Gaze.** The pupils drift within the eye and the eye itself never moves. A
  dead-centre stare reads as switched off.

And on top of *that*, the conversation state:

| state | the face |
|---|---|
| idle | expression only, blinking and drifting |
| listening | eyes wider, brows up, gaze straight at you |
| thinking | eyes narrowed, one brow up, looking away, mouth shut |
| speaking | the mouth re-rolls its opening about three times a second |

**The mouth is not lip-sync.** It re-rolls roughly at syllable rate and eases
between values, which tracks the rhythm of speech without needing the audio
envelope — and costs nothing. An open mouth is drawn as a *ring*, not a disc:
on a dark face an open mouth is a hole, and filling it puts a bright blob in the
middle of his face.

---

## The head

Two servos, pan and tilt. Everything below is scaled by the current **mood** —
see [characters.md](characters.md) — so the same behaviour at `motion_unit 0.55`
is the same robot in a hurry.

**Idle glances.** Every 1.2–3.8 seconds he looks somewhere else, ±11° of pan and
a little tilt, taking half a second or so to get there. Never to where he is
already looking, which would burn a whole hold doing nothing visible.

**The thinking tell — the most deliberate thing here.** Ask for a story and the
model takes five to ten seconds. A robot sitting *perfectly still* for those ten
seconds is indistinguishable from a hung one, and the instinct is to reach for
the cable. So: the chin drops, the way someone settles before a long answer;
then he looks up and off to one side; then small slow drifts around that pose.

🔴 **Not a "busy" animation, on purpose.** A spinner says the machine is working.
A breath and a look away says a *person* is.

⚠️ **And silent, also on purpose.** An audible sigh was the tempting version.
There is no echo-cancellation reference on this board, so any sound he makes
while a session is open is sound he hears himself — the exact mechanism that had
him interrupting himself mid-sentence. A visual tell cannot break the audio
pipeline.

When the thinking ends he **straightens up before the first word**, so the reply
does not start with his head parked off to one side.

**Speaking** starts facing forward, then glances as he talks.

**The double-take** — a hard look left, a look right, then back to centre, all
inside a second — fires when an [ambient status](ambient-status.md) level gets
*worse*. It is the one movement designed to be seen out of the corner of your
eye.

---

## The light ring

Twelve LEDs. The colour is the **body's accent** — lavender for Drax, red for
Classic — so the ring is part of who he is, not a fixed scheme.

| state | ring |
|---|---|
| starting | a quick comet chasing round |
| Wi-Fi setup | blue, blinking |
| connecting | blue, breathing |
| **idle** | a slow resting breath — or the **status colour**, if one is configured |
| listening | a comet, at a walking pace |
| speaking | solid |
| updating | blue, fast blink |
| fatal error | solid red |

🔴 **Idle is never off.** Fully dark reads as "the robot is broken". A
barely-there breath reads as "waiting", and it is the only thing on the desk
saying he is listening for his name without saying anything.

With a status source attached, that idle breath carries health instead: green,
amber, or a red blink, and muted when the reading has gone stale. See
[ambient-status.md](ambient-status.md) — including why a *confidently green* ring
is the failure that design is built to prevent.

---

## The camera

He photographs only when asked. The sensor is started for the shot and stopped
afterwards, so it is idle the rest of the time — that is a privacy property and
also a correctness one: running it continuously loads the same bus the wake-word
engine uses, and detection went spotty the moment it was tried.

The photo goes on his own screen, full width. With a vision model configured it
is also described — [vision.md](vision.md) — and with none, it just stays on the
screen, which is what it did before.

The camera switch in the settings menu refuses at the point of capture, and the
tool then says so in a sentence rather than failing.

---

## The wake word

**"Hi, Stack Chan"**, recognised **on the robot** — not on the server. Nothing
leaves the device until the phrase has been heard, which is the whole reason the
detector is on this side. Tapping the screen works too.

⚠️ **The wake phrase is also the model's first user message.** It is handed
straight to the LLM as if it had been spoken. Pick a phrase for what it *says*,
not only for how reliably it triggers: a robot greeted by another robot's name
will remark on it, and a persona paragraph explaining the discrepancy makes him
do it more.

---

## Standby

After 90 seconds idle the screen dims; after 300 he goes to standby. Touching
the screen or saying his name brings him back.

With a status source configured, standby is not a blank screen — it becomes a
rotating card of whatever he is watching, one card every 5 seconds, **with the
age of the reading on it**. A screen showing "3 healthy" from a reading half an
hour old is worse than a blank one.

---

## Things you can do to him directly

| | |
|---|---|
| **tap the screen** | wake him, same as the wake word |
| **hold the screen for five seconds** | the settings menu — Wi-Fi and server, volume, brightness, body and mood, the self-check, the privacy switches |
| **`BOARD_REPORT`** over USB serial | everything the firmware assumes about the hardware, read back from it. See the [board README](../firmware/main/boards/m5stack/stackchan/README.md) |

And one thing he does at boot that is worth listening for: **the chime is a test
result.** A success chime means the speaker, the microphone and the servo rail
all answered. An exclamation means one of them did not, and the log says which.
*No* chime at all means the check never finished — which is itself information.
