#!/usr/bin/env python3
"""espeak-ng behind an OpenAI-compatible /v1/audio/speech endpoint.

Why this exists: a body can change how the robot LOOKS and MOVES, and a voice is
the other half of being somebody else. Kokoro has 68 voices and every one of them
is a person - flattening one gives you a bored human, not a machine.

🔑 AND FOR A 1991 BODY THIS IS NOT A DOWNGRADE, IT IS THE PERIOD-CORRECT CHOICE.
   espeak-ng is a formant synthesiser: the same class of technology as MacinTalk,
   which is what a System 7 machine actually sounded like. A neural voice
   imitating a robot is an impression. This is the real thing, and it is about
   two megabytes.

No framework on purpose - the standard library's http.server is plenty for one
endpoint on a LAN, and it means this container is espeak-ng plus Python and
nothing else. Fewer moving parts than the thing it is speaking for.

⚠️ NOT EXPOSED BEYOND THE COMPOSE NETWORK. It runs whatever text it is given
   through a subprocess; that is fine between containers and would not be fine on
   an interface anybody else can reach.
"""

import json
import shutil
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

# espeak-ng speaks in words per minute. 175 is its default and sounds hurried
# through a 1 W speaker; 150 is a steadier baseline that still reads as brisk.
BASE_WPM = 150

# A voice may carry a pitch: "en-gb+m4@65". See split_voice - the contract has
# no pitch field, and the voice string is the one slot that passes through.
# The "voice" field arrives from the same config slot Kokoro's voices use, so it
# is espeak's own voice name - en-us, en-gb, or a variant like en-us+m3. The
# +mN and +fN variants are the interesting ones: they change the formant model
# rather than the accent, which is where the different machines live.
DEFAULT_VOICE = "en-us+m3"




def split_voice(spec: str) -> tuple[str, int | None]:
    """`en-gb+m4@65` -> ("en-gb+m4", 65). No @ means espeak's own default.

    The pitch rides on the voice string because the OpenAI speech contract has
    no field for it and the server's config has the same shape. `@` rather than
    `+`, since `+` already means a formant variant to espeak.
    """
    if "@" not in spec:
        return spec, None
    voice, _, raw = spec.partition("@")
    try:
        # 0 is a drone and 99 is a whistle; this range is all listenable.
        return voice, max(20, min(80, int(raw)))
    except ValueError:
        return voice, None

def fix_wav_sizes(wav: bytes) -> bytes:
    """Rewrite the RIFF and data chunk sizes to match the bytes actually here.

    🔴 espeak-ng STREAMING CANNOT WRITE THESE. Piping to stdout means it never
       learns the total length in time to seek back and record it, so it emits a
       placeholder - measured here as a header claiming over thirteen hours for
       under three seconds of speech.

       Harmless to a decoder that reads until the stream ends, and not harmless
       to one that believes the header. Both exist, so the header is made true.
    """
    if len(wav) < 44 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        return wav                      # not a RIFF stream; hand it back untouched

    out = bytearray(wav)
    # RIFF size covers everything after the first eight bytes.
    out[4:8] = (len(out) - 8).to_bytes(4, "little")

    # Walk the chunks to find `data`, rather than assuming the canonical 44-byte
    # header - espeak's layout is not guaranteed and a wrong guess here is
    # silence.
    pos = 12
    while pos + 8 <= len(out):
        cid = bytes(out[pos:pos + 4])
        size = int.from_bytes(out[pos + 4:pos + 8], "little")
        if cid == b"data":
            out[pos + 4:pos + 8] = (len(out) - (pos + 8)).to_bytes(4, "little")
            break
        if size <= 0:
            break                       # a zero-length chunk would loop for ever
        pos += 8 + size + (size & 1)    # chunks are word-aligned
    return bytes(out)

class Handler(BaseHTTPRequestHandler):
    # Quieter logs: one line per request, not the default's noise.
    def log_message(self, fmt, *args):
        print("espeak: " + (fmt % args), flush=True)

    def _send(self, code, body=b"", content_type="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if body:
            self.wfile.write(body)

    def do_GET(self):
        # A liveness path, so a compose healthcheck has something to ask.
        if self.path in ("/health", "/v1/models"):
            self._send(200, json.dumps({"status": "ok"}).encode())
        else:
            self._send(404)

    def do_POST(self):
        if self.path != "/v1/audio/speech":
            self._send(404)
            return

        try:
            length = int(self.headers.get("Content-Length", 0))
            payload = json.loads(self.rfile.read(length) or b"{}")
        except Exception as e:                      # noqa: BLE001 - report, never crash
            self._send(400, json.dumps({"error": str(e)}).encode())
            return

        text = (payload.get("input") or "").strip()
        if not text:
            # An empty body is not an error worth a 500 - it is silence.
            self._send(400, json.dumps({"error": "no input"}).encode())
            return

        voice = payload.get("voice") or DEFAULT_VOICE
        # OpenAI's `speed` is a multiplier; espeak wants words per minute.
        try:
            speed = float(payload.get("speed") or 1.0)
        except (TypeError, ValueError):
            speed = 1.0
        # Clamped: espeak accepts 80-450 and both ends are unintelligible.
        wpm = max(90, min(320, int(BASE_WPM * speed)))

        voice, pitch = split_voice(voice)
        cmd = ["espeak-ng", "-v", voice, "-s", str(wpm)]
        if pitch is not None:
            cmd += ["-p", str(pitch)]
        cmd += ["--stdout", text]
        try:
            done = subprocess.run(cmd, capture_output=True, timeout=30)
        except Exception as e:                      # noqa: BLE001
            self._send(500, json.dumps({"error": str(e)}).encode())
            return

        if done.returncode != 0 or not done.stdout:
            err = done.stderr.decode(errors="replace")[:200]
            self._send(500, json.dumps({"error": err or "espeak produced nothing"}).encode())
            return

        # espeak-ng --stdout emits a RIFF/WAV stream, which is what the config
        # asks for with `format: wav` - but with lengths it could not know when
        # it wrote the header. See fix_wav_sizes.
        self._send(200, fix_wav_sizes(done.stdout), "audio/wav")


def main():
    if shutil.which("espeak-ng") is None:
        raise SystemExit("espeak-ng is not installed in this image")
    print(f"espeak: serving /v1/audio/speech on 8890, default voice {DEFAULT_VOICE}",
          flush=True)
    ThreadingHTTPServer(("0.0.0.0", 8890), Handler).serve_forever()


if __name__ == "__main__":
    main()
