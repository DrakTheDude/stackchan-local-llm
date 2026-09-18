# Characters — a skin for the whole robot

> **Status: design note, nothing built.** Written down while the idea was clear.
> The name is not settled — see the last section.

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
