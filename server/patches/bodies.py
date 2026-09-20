#!/usr/bin/env python3
"""Let the robot's BODY choose its voice and its persona.

A body is the whole robot - how he looks, how he moves, how he sounds and what
he is like. The firmware owns the visible half; this is the other one. The two
share exactly one thing: the body's ID.

🔑 IT ARRIVES IN THE HANDSHAKE HEADERS, and that is the entire design decision.

   The obvious carrier is the hello message, and it does not work. Components are
   submitted to an executor at connection time and read self.config as they go -
   the TTS is constructed from it, the persona is taken from it - while hello is
   still in flight. A patch that swapped the persona on hello would land after
   the prompt was already in use and race the enhanced one, which half-works in
   testing and gives you a robot with one body's face and another's voice for the
   first reply.

   Headers are read at line ~209, before a single component exists. So the body
   is known before anything that depends on it is built, and no ordering has to
   be reasoned about at all.

⚠️ SAFE TO MUTATE, checked rather than assumed: self.config is a deepcopy per
   connection, so overriding it affects this robot's session and nothing else.

The config shape, which is deliberately boring:

    bodies:
      classic:
        prompt: |
          ...the persona for this body...
        tts:
          api_url: http://espeak:8890/v1/audio/speech
          voice: en-gb+m4

The default body needs NO entry. The top-level config is the default - a body
file that had to restate everything would drift from it the first time somebody
edited one and not the other.
"""

import pathlib
import sys

TARGET = pathlib.Path("core/connection.py")

APPLY_FN = '''
    def _apply_body(self):
        """Override this session's persona and voice for the robot's body.

        🔑 CALLED FROM THE HEADER READ, deliberately, and not from the hello
           handler. Components are submitted to an executor which reads
           self.config as it builds them - the TTS is constructed from it, the
           persona taken from it - so anything that arrives later is racing them.
           A header is present before any of that exists.

        ⚠️ self.config is a deepcopy per connection, so this changes one robot's
           session and nothing else.
        """
        body = (self.headers.get("body") or "").strip()
        if not body:
            return                      # a board with no notion of bodies

        bodies = self.config.get("bodies") or {}
        override = bodies.get(body)
        if not override:
            # Not an error. The default body has no entry ON PURPOSE - the
            # top-level config IS the default - and an unknown id from a newer
            # firmware should fall back rather than guess.
            self.logger.bind(tag=TAG).info(
                f"body '{body}': no override, using the default persona and voice"
            )
            return

        if override.get("prompt"):
            self.config["prompt"] = override["prompt"]

        tts_over = override.get("tts") or {}
        if tts_over:
            # Into the SELECTED provider's own block, not a new one: the server
            # reads TTS.<selected> and a second entry would be ignored silently.
            selected = self.config.get("selected_module", {}).get("TTS")
            if selected and selected in self.config.get("TTS", {}):
                self.config["TTS"][selected].update(tts_over)
            else:
                self.logger.bind(tag=TAG).warning(
                    f"body '{body}': no TTS module '{selected}' to override"
                )

        self.logger.bind(tag=TAG).info(
            f"body '{body}': persona {'set' if override.get('prompt') else 'default'}, "
            f"voice {tts_over.get('voice', 'default')}"
        )
'''

CALL_OLD = '''            self.device_id = self.headers.get("device-id", None)
'''

CALL_NEW = '''            self.device_id = self.headers.get("device-id", None)

            # 🎭 Before any component is built - see _apply_body for why this
            #    cannot live in the hello handler.
            self._apply_body()
'''

ANCHOR = "    def _initialize_components(self):"


def main():
    if not TARGET.exists():
        sys.exit(f"bodies.py: {TARGET} not found - is this the server image?")

    src = TARGET.read_text(encoding="utf-8")

    if "_apply_body" in src:
        print("bodies.py: already applied")
        return

    # Both replacements assert. An upstream bump that moves either of these
    # should fail the build rather than quietly produce a server that ignores
    # the body - which would look exactly like a robot with no personality.
    if src.count(ANCHOR) != 1:
        sys.exit(f"bodies.py: expected 1 '{ANCHOR}', found {src.count(ANCHOR)}")
    if src.count(CALL_OLD) != 1:
        sys.exit(f"bodies.py: expected 1 device-id read, found {src.count(CALL_OLD)}")

    src = src.replace(ANCHOR, APPLY_FN.strip("\n") + "\n\n" + ANCHOR, 1)
    src = src.replace(CALL_OLD, CALL_NEW, 1)

    TARGET.write_text(src, encoding="utf-8")
    print("bodies.py: the body chooses the persona and the voice")


if __name__ == "__main__":
    main()
