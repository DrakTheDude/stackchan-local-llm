# Characters — a skin for the whole robot

> **Status: designed, nothing built.** The ownership question is settled — built-in characters
> with a shared name, and the server able to override the robot's half later. The prerequisite
> is tokenising what already exists; the inventory of exactly what that means is below.
> The name is still not settled — see the last section.

A Winamp skin never only changed the colours. It changed the shape of the buttons, the typeface, the
way the thing carried itself. You did not think of it as a palette, you thought of it as a different
player.

This robot has more to play with than a media player did. It has a face that can be drawn any way we
like, a voice, a head that moves, and the words it chooses. Changing only the palette would be the
least interesting version of the idea.

So: **a character is a bundle that changes how he looks, sounds, moves and talks — together.**

---

## The five layers

| layer | what it sets | where it lives today |
|---|---|---|
| **Look** | palette, face geometry — eye shape, brow weight, mouth style | firmware: theme tokens + `stacky_face.cc` constants |
| **Motion** | how often he glances, how far, how fast, how he idles | firmware: `stackchan_head.cc` constants |
| **Light** | the LED ring's resting behaviour and colours | firmware: `stackchan_leds.cc` |
| **Voice** | the TTS voice and speaking rate | server config: `TTS.LocalTTS.voice` |
| **Words** | the persona prompt | server config: `prompt` |

Today every one of those is a constant or a config key, set independently, with nothing tying them
together. That is why the reference robot is currently wearing a night-shift-operator persona on
firmware that knows nothing about it: the two halves have never been introduced.

## ✅ Decided: built in, with the server able to override later

Of the three ownership options weighed below, **both halves with a shared name** is the one to build —
and the reasoning there stands, so this is the decision and the staging rather than a repeat of it.

**Stage 1 — the shared name.** Built-in characters ship in one binary, chosen in the settings menu,
the choice kept in NVS. The server holds a file of the same name with the voice and persona. Each side
is authoritative for what it owns and no new protocol exists yet.

**Stage 2 — the server may override the robot's half.** The server sends a character's look, motion
and light on connect; it applies while connected and is not persisted. That is what makes *custom*
characters possible without a firmware build, and it is why the hybrid is worth having:

```
server override  >  the one chosen in the settings menu  >  the compiled default
```

Disconnect and he returns to the selected built-in, so a character pushed for a demo cannot strand the
robot wearing it. Pull the network entirely and he is still himself — he just stops talking.

### 🔑 The face is not sprites, and it changes what stage 2 costs

Worth stating plainly, because the note below assumes otherwise. The face is drawn with LVGL
primitives — `lv_line` point arrays and stroke widths — and the whole expression is eight small
fields:

```cpp
struct FaceShape {
    EyeShape left, right;
    BrowShape brow_l, brow_r;
    int8_t gaze_x, gaze_y;
    uint8_t mouth_w;
    int8_t mouth_curve;   // + smile, - frown
    uint8_t mouth_open;
};
```

So a character is **a few hundred bytes of numbers**. No image assets, no assets-partition rebuild, no
`-WithAssets` flash to try one on. Custom characters do not need a data path on the device at all —
the connect message *is* the data path. That removes most of what made stage 2 look expensive.

### Partial characters merge

A character sets only the fields it cares about; the rest come from the default. "Just a palette" is a
valid character, and so is "the default, but he glances three times as often" — which is what keeps
the examples short. *Heavily caffeinated* is six numbers and a colour.

### What still has to be decided

- ⚠️ **How much of the arrays a character may set.** `kBlink[]` is a six-frame curve; the glance
  targets are lists of five and three. Fixed-size, at today's sizes, is the obvious start — a character
  that cannot change its own blink is missing something expressive, but one that can push
  arbitrary-length arrays at the device is a different kind of problem.
- ⚠️ **Whether a character's geometry is pre- or post-`Scaled()`.** Face constants run through
  `Scaled()` at 1.25×. Raw numbers landing in scaled slots change proportion as well as position, and
  the failure is silent and looks like a design choice. Pick one, state it in the schema.
- ⚠️ **Whether a character can be switched by voice.** Genuinely fun, and a mutation — which this
  project is careful about. The blast radius is his own face, which is the argument for; "be someone
  else" working by accident is the argument for deciding it on purpose.
- ⚠️ **What happens mid-sentence.** Switching while he speaks, or mid-glance, should either finish the
  gesture or apply at the next idle. The second is simpler and looks deliberate.

## Motion: one number, not ten

Borrowed wholesale from the design-token work in the sibling project, where the argument is made
about pixels and holds exactly for time:

> `--r-unit: 0` **squares every corner in the application with one number.**
> A flat resolved dict cannot express a scale.

`stackchan_head.cc` currently has the anti-pattern that argument is about — ten move durations,
tuned individually and written into the call sites:

```cpp
case 1: SetAngles(-30.0f, 10.0f, 200); return 240;
case 2: SetAngles( 24.0f, 10.0f, 240); return 280;
...
SetAngles(think_pan_, 6.0f, 1100);
SetAngles(0.0f, kListenTilt, 450);
```

A "calm" character would have to retune all ten, and whoever adds the eleventh gesture will not know
to. So:

| token | scales | today |
|---|---|---|
| `motion_unit` | every move duration **and** every interval — servo `time_ms`, glance hold, blink countdown, gaze drift | `1.0` |
| `gesture_unit` | every amplitude — glance pan and tilt targets, the thinking look-away, listening tilt | `1.0` |

**Smaller is faster**, because the unit multiplies *time*, exactly as `--r-unit` multiplies radius.
That is the one naming trap here and it is worth stating in the schema rather than discovering:

```
caffeinated   motion_unit 0.5   gesture_unit 1.4    quick, and looks further
default       motion_unit 1.0   gesture_unit 1.0
five in the morning
              motion_unit 2.2   gesture_unit 0.4    slow, and barely turns his head
```

That last one is the test case for whether the tokens are real. "Slow side to side, trying to
survive" is not a new animation — it is the existing glance behaviour with a long duration and a
small amplitude. If it needs new code, the tokens are not carrying enough.

### 🔴 The servo floor — this is a hardware difference, not a style one

`motion_unit: 0` on a web page means "no animation". On this robot it means `time_ms = 0`, which the
servo API documents as *"as fast as it likes"* — a head slamming pan and tilt at full speed into a
mechanical stop, repeatedly, on a desk.

**Clamp it in the code, not in a comment.** A minimum move duration that no character can go below,
and a maximum amplitude that no `gesture_unit` can exceed. A character is data from a file or a
network message; treating it as trusted input is how a theme becomes a way to break a robot.

The tilt limits are already derived from each unit's own factory calibration, which is exactly the
kind of thing a character must not be able to widen.

### Semantic motion has to survive `motion_unit: 0`

The sibling project's rule, and it transfers directly: *severity must not be carried by colour alone*.
Here — **greeting must still read differently from startled when motion is off.** If the only
difference between them is how fast the head moves, then a character that disables motion has
silently deleted the distinction rather than restyled it.

Today that mostly holds, because gestures differ by target and amplitude rather than only speed. It
is worth an explicit check the moment a second character exists, because it is the kind of thing that
is true by accident until it is not.

## Serialising one

The sibling project's token system has no serialisation at all — the CSS cascade *is* the merge, the
stylesheet *is* the storage, and the only saved value is which theme is selected. There is therefore
nothing to copy here, and this is the part that is actually new work.

What the cascade gives for free, and a format has to provide deliberately:

| property | what it means here |
|---|---|
| **complete base** | every token has a value before any character applies. A character can never produce an undefined one. |
| **per-key last-wins** | resolution is flat, key by key — *not* a deep merge of nested structures. |
| **absent ≠ null** | a key a character does not mention inherits. There is no way to say "unset this". |

⚠️ **That last one is where a format goes wrong.** In JSON, `{"motion_unit": null}` and a missing
`motion_unit` are different things and both are easy to produce by accident. **Prefer a format with no
null at all** — absent means inherit, and there is no second way to say it.

🔴 **Store units, not resolved values.** The whole point of `motion_unit` is that one number moves ten
durations. Serialise `{"glance_hold_ms": 1200, "glance_move_ms": 500, …}` and the property is gone:
a character now overrides ten numbers instead of one, and whoever adds the eleventh gesture will not
know to. Resolve **at use** (cheapest on an MCU — one multiply in the move call) or **at load** (a
small resolver expands the scale once). Never at authoring time.

## Proving a character is real

The sibling project's audit is the most transferable idea in its whole token system, and it is worth
stealing before the first character is written rather than after.

**The instrument is a deliberately hideous character — and it is not a character.** That distinction
is load-bearing. It is never offered as a look, never tidied, never judged as a design; it exists to
be applied during testing so you can see *what changed*, and anything that did not change is
hardcoded. It is a measuring tool that happens to be rendered.

Every token swung to a value nothing in the real design resembles — a face in colours that clash, a
`motion_unit` far off 1, amplitudes at the clamp. A subtle test character hides exactly the failures
it exists to find, which is why the sibling project's is called `test-hideous` and carries no
annotation, so it can be applied by hand and never reaches the picker.

⚠️ The failure mode to guard against is somebody making it presentable. The moment it is pleasant
enough to ship, it is no longer capable of proving anything.

Three guards there are worth copying exactly:

1. **Token coverage** — if the test character does not vary a token, that token is untested. Six
   tokens went unvaried there once and twenty-four correctly-tokenised properties were reported as
   literals.
2. **Movement** — if nothing changed at all, the character never applied and the result means nothing.
3. **Source scan** — a literal and a token look identical in a rendered frame, so grep the source as a
   backstop.

> 🔴 **And the lesson that cost them the most: the number is only a claim about the states it
> visited.** Their audit reported 100% while never opening a dialog with a text input — and every form
> field in the application was rendering a hardcoded white label on white.

**That is sharper here, not softer.** A face is a small state space and therefore easy to believe you
have covered: 21 named expressions × 4 modes (idle, listening, thinking, speaking), plus blink frames,
the screensaver, the photo preview and the settings menu. An expression not in the list has
**unproven** theming. Enumerate the states explicitly and assert the count — which is exactly the
shape `claims.ini` is for.

## What tokenising actually involves

Counted from the source, not estimated. Every one of these is already named and grouped in the file
its layer belongs to, which is why this is a gathering job rather than a redesign.

**Look** — `stacky_face.cc`, 27 constants. Four colours (`kInk`, `kPupil`, `kGlint`, `kGlow`), the
face box (`kFaceW/H`, `kFaceYOffset`), and the geometry that gives him his expression: `kEyeSpread`,
`kEyeY`, `kBrowY`, `kBrowHalfW`, `kBrowThick`, `kMouthY`, `kStrokeW`.

**Motion** — `stackchan_head.cc`, 12 constants. The glance targets (`kGlancePan`, `kGlanceTilt`), how
fast he moves (`kGlanceMoveMinMs`, `kGlanceMoveJitterMs`) and how long he holds
(`kGlanceHoldMinMs`, `kGlanceHoldJitterMs`). These *are* the "heavily caffeinated" example — that
character is those six numbers.

**Light** — `stackchan_leds.cc`. A named-colour table and the ring's resting behaviour.

**Voice** and **Words** — server config, not firmware: `TTS.*.voice` and `prompt`.

### Three things that will bite

- 🔴 **Not everything is a scalar.** `kBlink[]` is a six-frame animation curve and `kGlancePan[]` /
  `kGlanceTilt[]` are target lists. A character that only overrides numbers cannot change how he
  blinks, which is one of the more expressive things on the list. The bundle needs to carry small
  arrays, or the interesting half of "look" stays fixed.
- ⚠️ **The geometry is derived, not absolute.** Face constants run through `Scaled()`
  (`kScaleNum/kScaleDen`, currently 1.25×). Overriding a scaled value with a raw one silently changes
  proportion as well as position. Whatever a character sets should go through the same scaling, or the
  scaling should be resolved before the bundle is applied — not both.
- ⚠️ **The two halves have never been introduced.** Look, motion and light are compiled in; voice and
  words are server config. A character that sets all five has to cross that boundary, and today
  nothing does. The simplest version is a character *name* the server knows and the firmware also
  knows, with each side holding its own half — which keeps the firmware from having to be told about
  prompts, and keeps the server from having to know about brow thickness.

## What a character actually is, concretely

The useful test is whether an example writes itself. Take **"heavily caffeinated"**:

- **Look** — eyes a little wider, brows higher, blink more often
- **Motion** — glances two or three times as often, faster moves, shorter holds
- **Light** — quicker, brighter idle pulse
- **Voice** — a faster rate, a brighter voice from the top of the loudness table
- **Words** — shorter sentences, more interruptions of himself, enthusiasm as a default

Every one of those is a number or a string that already exists. Nothing new has to be invented — they
have to be *named, grouped, and switchable*. That is the whole feature.

And it is a genuinely different robot to be in a room with, which is the point. A "night shift"
character would be the opposite of every line above, and a "System 7" one would be square-eyed,
monochrome, chunky-bordered and clipped in speech.

---

## 🔴 The hard part: a character spans two machines

This is the thing to solve first, because it decides everything else.

**Look, motion and light are on the robot. Voice and words are on the server.** They are not only
different files, they are different computers, different languages, and different lifecycles — the
firmware is flashed, the server config is edited and reloaded.

So "switch character" cannot be one edit in one place without deciding who owns the bundle:

- **Robot owns it.** The character is chosen on the robot — a settings row, which is where it belongs
  for a thing you pick up and change. The robot then has to *tell* the server which voice and persona
  to use, over a channel that does not exist yet.
- **Server owns it.** The character is a config file, and the server pushes the look and motion
  parameters down to the robot on connect. Simpler to author — everything in one text file — but the
  robot has no character until something connects, and it has to be told again every time.
- **Both, with a shared name.** The robot stores the character's *name* and applies its own half; the
  server holds a file of the same name and applies its half. No new protocol, each side authoritative
  for what it owns, and the failure mode is legible: a name on one side with no file on the other
  falls back to the default and says so.

The third is the one to try first. It is the least machinery, and it fails in a way somebody can
diagnose from across the room.

⚠️ And it has an obvious trap: two halves of a character can drift out of step, so a robot could end
up looking caffeinated and sounding like a night watchman. Whatever is built needs a way to see which
character each half thinks it is wearing — the About page is the natural place.

## 🔑 Switching one must NOT need a flash

Worth deciding early, because the wrong answer shapes everything downstream.

The built-in characters ship **together, in one binary**, and you pick one in the settings menu. The
robot's half of a character is numbers and short strings — eye geometry, glance intervals, a few
colours — so several of them cost a few hundred bytes. The choice lives in NVS next to the microphone
mute.

The alternative — a build per character, chosen at flash time — is tempting because it is less work,
and it is wrong:

- You would reflash to change your robot's mood, which nobody will do twice.
- The **web flasher** would need one image per character, multiplied by every character ever added.
  It should offer one button, not a menu of near-identical binaries.
- Releases would multiply the same way.

So flashing is for *adding* characters, exactly like any other firmware change. Choosing between the
ones you have is a settings row.

⚠️ That settles the robot's half only. The server's half — voice and persona — is config on another
machine, so the first version will be "pick the look and motion on the robot, edit the voice and
persona in the config". One switch for both is the ownership question above, and it is the thing that
makes this feel finished rather than clever.

Custom characters, written by owners rather than shipped with the firmware, are stage 2 above. They
need a data path, and the one that costs nothing is the server handing the bundle over on connect —
**not** the assets partition. A character is numbers, not sprites (see the decision section), so it
fits in a message. None of it should be built until the built-in ones work.

## Before any of this, one prerequisite

**Tokenise what is already there.** The look and motion values are constants spread across four
files. Until they are named and gathered into one structure, there is nothing to swap. That is
unglamorous and it is the whole foundation — and a second character is the test that proves it,
because anything the tokens cannot express shows up immediately as a value still hard-coded.

---

## What to call it

"Theme" and "skin" both undersell it — they promise a repaint, and this changes behaviour. "Persona"
is closer but describes only the words, and the whole point is that the words are one layer of five.

Candidates:

- **Character** — descriptive and honest. A character in a game has a look, a voice, a way of moving.
  Fits the existing idiom around this project, where the homelab side already talks about NPCs.
- **Cartridge** — same console, entirely different thing when you swap it. Playful, physical, and
  unambiguous next to "theme". Carries the retro-hardware feeling the idea came from.
- **Presence** — accurate about what actually changes, which is what it is like to be in a room with
  him. Quieter, more abstract.

This document uses **character** throughout as a placeholder, chosen because it is the least clever.
It is not a decision.
