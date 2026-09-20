#!/usr/bin/env python3
"""Fix upstream bugs that are not about language.

Separate from english-pass.py on purpose. That file has one job - translate the
Chinese the model sees or the user hears - and it is easier to reason about, and
to hand to someone else, if it stays that job. These are plain defects that
would be worth reporting upstream and that happen to bite us.

🔴 SAME DISCIPLINE, AND FOR THE SAME REASON: every replacement is asserted, so
   an upstream bump FAILS THE BUILD rather than silently reverting a fix. A
   patch that matches nothing is indistinguishable from a patch that worked,
   right up until someone notices the robot has started slurring its words
   again and has no idea when it started.
"""
import sys
from pathlib import Path

ROOT = Path("/opt/xiaozhi-esp32-server")

REPLACEMENTS = [
    # 🔴 DO NOT SEND `tts stop` WHEN THE CONNECTION IS ABOUT TO CLOSE.
    #
    #    `tts stop` means "I have finished speaking, your turn", and the firmware
    #    reads it exactly that way: in the default auto-listening mode it leaves
    #    Speaking for Listening, which OPENS A NEW AUDIO CHANNEL. Sending it and
    #    then closing tells the robot to start a fresh session and hangs up in
    #    the same breath - so it reconnects, greets the room and listens, and the
    #    idle timeout never actually puts it to sleep.
    #
    #    Seen in the log as a reconnect one second after the goodbye:
    #        客户端断开连接  /  <robot ip> conn - Headers…   (one second later)
    #
    # ⚠️ The stop does not end the audio - the queued packets do. It only drives
    #    the device state machine, so skipping it on this one path costs nothing
    #    and OnAudioChannelClosed sets Idle when the close lands.
    (
        "core/handle/sendAudioHandle.py",
        """        await send_tts_message(conn, "stop", None)
        if conn.close_after_chat:
            await conn.close()""",
        """        if conn.close_after_chat:
            await conn.close()
        else:
            await send_tts_message(conn, "stop", None)""",
    ),
    # 🔴 MARKDOWN COMES OFF BEFORE PUNCTUATION DOES.
    #
    #    Heard: "Image](https://via.placeholder.com/300x200)", spoken.
    #
    #    get_string_no_punctuation_or_emoji strips the leading "[" from a
    #    markdown link, and MarkdownCleaner's link regex anchors on that exact
    #    character - so by the time the cleaner runs there is nothing left for
    #    it to match, and the URL goes to the speaker.
    #
    # ⚠️ The cleaned text goes into segment_text ONLY. The next line advances
    #    processed_chars by len(segment_text_raw), which indexes into the
    #    stream; cleaning that string in place would shorten it and put every
    #    later segment out of step.
    (
        "core/providers/tts/base.py",
        """            segment_text = textUtils.get_string_no_punctuation_or_emoji(
                segment_text_raw
            )""",
        """            segment_text = textUtils.get_string_no_punctuation_or_emoji(
                MarkdownCleaner.clean_markdown(segment_text_raw)
            )""",
    ),
    # 🔴 MISTRAL'S TOOL-CALL TOKEN, SPOKEN ALOUD.
    #
    #    Heard: "TOOL_CALLS]Good evening. It's nice to meet you."
    #
    #    mistral-nemo emits [TOOL_CALLS] as ordinary content when it calls a
    #    tool, and it reaches the voice because nothing removes it. Reported as
    #    the robot "making up a name", which is what it sounds like.
    #
    # ⚠️ Fixed HERE, in the cleaner, and not in the streaming tool-call path in
    #    core/providers/llm/openai/openai.py. That path carries every MCP call
    #    the robot makes; buffering its first chunks to strip a prefix could
    #    swallow a real tool call, which is a bad trade for a cosmetic leak.
    (
        "core/utils/tts.py",
        r"""        (re.compile(r'```.*?```', re.DOTALL), ''),  # 代码块""",
        r"""        (re.compile(r'\[?TOOL_CALLS\]'), ''),  # Mistral's token, leaked as text
        (re.compile(r'```.*?```', re.DOTALL), ''),  # 代码块""",
    ),
    # 🔴 THE FEW-SHOT EXAMPLE WAS ANSWERING REAL GREETINGS.
    #
    #    Injected into every conversation to demonstrate direct_answer, it
    #    showed a short user message answered with "Sure - what sort? Something
    #    with a bit of adventure, or something daft?". Said "Hi, Stack Chan",
    #    the model reproduced it, and every conversation opened by asking what
    #    sort of story the user wanted.
    #
    #    The example needs to demonstrate the CALL, not supply a personality.
    #    This keeps the shape and replaces the content with a flat factual
    #    exchange nobody would mistake for an opening line.
    #
    # ⚠️ Worth reporting upstream: any wording here becomes a thing the robot
    #    says unprompted, which is not obvious from the code.
    (
        "core/connection.py",
        '''self.dialogue.put(Message(role="user", content="Tell me a story", is_temporary=True))''',
        '''self.dialogue.put(Message(role="user", content="How many nodes are there?", is_temporary=True))''',
    ),
    (
        "core/connection.py",
        '''"function": {"arguments": \'{"response": "Sure - what sort? Something with a bit of adventure, or something daft?"}\', "name": "direct_answer"},''',
        '''"function": {"arguments": \'{"response": "Four, and all of them are up."}\', "name": "direct_answer"},''',
    ),
    # 🕐 THE CLOCK THE MODEL IS SHOWN, because telling it to convert does not
    #    work. The prompt substitutes {{current_time}} as "21:07" and the persona
    #    then asks for spoken, twelve-hour time - and the model says "twenty one
    #    oh seven" anyway. Same lesson as the Chinese farewell and the memory
    #    example, for the third time in this project: A DEMONSTRATION BEATS AN
    #    INSTRUCTION. A literal sitting in the context outranks a rule about it.
    #
    #    So the substitution itself changes shape, and the model is never shown a
    #    24-hour clock in the first place.
    #
    # ⚠️ Driven by an ENVIRONMENT VARIABLE, not hard-coded. Twelve-hour time is a
    #    local convention rather than an improvement, and this image is shared -
    #    set CLOCK_12H=1 in compose where that is the convention, leave it unset
    #    everywhere else and nothing changes.
    (
        "core/utils/current_time.py",
        '    return datetime.now().strftime("%H:%M")',
        '    import os\n'
        '\n'
        '    if os.environ.get("CLOCK_12H") == "1":\n'
        '        # %I zero-pads ("09:05 PM"), which reads as a timestamp rather\n'
        '        # than as something a person says. Strip it, lowercase the rest.\n'
        '        return datetime.now().strftime("%I:%M %p").lstrip("0").lower()\n'
        '    return datetime.now().strftime("%H:%M")',
    ),
    (
        "core/utils/dialogue.py",
        '                "{{current_time}}", datetime.now().strftime("%H:%M")',
        '                "{{current_time}}",\n'
        '                (\n'
        '                    datetime.now().strftime("%I:%M %p").lstrip("0").lower()\n'
        '                    if __import__("os").environ.get("CLOCK_12H") == "1"\n'
        '                    else datetime.now().strftime("%H:%M")\n'
        '                ),',
    ),
    # 🔴 EVERY STREAMED CHUNK GETS ITS TRAILING SPACE EATEN, so words run
    #    together in what he SAYS, not just what the screen shows.
    #
    #    Observed in the server's own log, spoken aloud exactly like this:
    #        "therewas astorm unlike anyother. Thepowerflickered,nodes dropped"
    #
    #    _clean_response_garbage() exists for a real reason: this deployment
    #    runs Intent=function_call, so the model's whole reply arrives as the
    #    `response` argument of the direct_answer tool - a JSON string - and
    #    models do sometimes leak the closing "}} into the text. Stripping that
    #    off the END OF A COMPLETE REPLY is correct.
    #
    #    The bug is that connection.py calls it on EVERY DELTA of the stream
    #    (around line 1183), and it ends with .rstrip(). A delta that happens to
    #    end on a space - which is most of them, since the stream is sliced on a
    #    character budget and not on word boundaries - loses that space, and the
    #    next delta is concatenated straight onto the previous word. A replay of
    #    thirteen realistic deltas through the container's own copy of this
    #    function lost ten spaces out of thirteen.
    #
    #    It also silently drops a delta that is ONLY whitespace: the result is
    #    "" which is falsy, and the caller's `if new_part:` skips it entirely.
    #
    # The fix keeps the garbage-stripping and puts the original trailing
    # whitespace back afterwards. Deliberately NOT "stop calling it per delta" -
    # that is the more correct fix and a much larger change to a 77KB upstream
    # file, and the leaked-JSON cleanup genuinely does need to see the stream.
    (
        "core/connection.py",
        """        result = re.sub(r'["\\'}\\]]+$', '', result.rstrip()).rstrip()
        return result""",
        """        # Trailing whitespace is PRESERVED. This runs on every streamed
        # delta, not only on a finished reply, and a delta ending in a space is
        # normal - eating it concatenates two words ("therewas"). The garbage
        # strip still happens; the space is put back after it.
        tail = result[len(result.rstrip()):]
        result = re.sub(r'["\\'}\\]]+$', '', result.rstrip()).rstrip() + tail
        return result""",
    ),
    # The memory summariser asks for up to 2000 tokens of output for a document
    # its own prompt caps at 900 CHARACTERS - call it 250 tokens. The ceiling is
    # therefore eight times the target, and it is not free: this generation runs
    # on the same single GPU as the conversation, and it starts the moment a
    # chat ends, which is moments before the next wake word. Every token it is
    # allowed to spend is time the next reply may have to queue behind.
    #
    # 700 is generous headroom over a measured ~250 and still bounds the wait.
    # It also acts as a brake on a summary that starts rambling - which would be
    # a bad summary anyway, since the useful ones are short.
    (
        "core/providers/memory/mem_local_short/mem_local_short.py",
        "                    max_tokens=2000,",
        "                    max_tokens=700,",
    ),
    # 🔴 `/no_think` IS PUT IN THE USER'S MOUTH, AND IT DOES NOTHING.
    #
    #    The Ollama provider prepends "/no_think " to the last user message for
    #    any model whose name starts with qwen3, then calls the
    #    OpenAI-compatible endpoint. Two things are wrong with that, and the
    #    second is the reason this is a defect rather than a preference:
    #
    #    1. It cannot work. The no-think branch of the qwen3 chat template is
    #       gated on `$.Think`, which only Ollama's NATIVE api sets. Nothing
    #       sent through the OpenAI-compatible path can reach it, whatever the
    #       wording. Measured: identical output, identical timing, with and
    #       without.
    #
    #    2. It is visible to the model AS PART OF WHAT THE USER SAID. The
    #       robot is therefore told, every single turn, that the person in the
    #       room opened with a slash command. It is a prompt injection with
    #       good intentions.
    #
    #    Disabled at the flag rather than by deleting the two injection blocks:
    #    one anchor instead of two ~15-line ones, so an upstream refactor that
    #    moves the blocks does not silently stop matching. The blocks become
    #    unreachable, which is honest about what happened and leaves them in
    #    place to delete upstream.
    #
    # ⚠️ Turning reasoning OFF is still worth doing - it is worth seconds of
    #    silence before the first word. It just cannot be done from here:
    #    server/no-think.sh builds a model variant with that template branch
    #    pinned, which is the supported way and is measured in model-floor.md.
    (
        "core/providers/llm/ollama/ollama.py",
        '        self.is_qwen3 = self.model_name and self.model_name.lower().startswith("qwen3")',
        "        # PATCHED: see server/patches/upstream-fixes.py. The /no_think\n"
        "        # injection below cannot reach the template through the\n"
        "        # OpenAI-compatible endpoint, and lands in the user's message\n"
        "        # where the model reads it as something the person said.\n"
        "        self.is_qwen3 = False",
    ),
]


def main() -> int:
    failures = []
    edits = 0
    for rel, old, new in REPLACEMENTS:
        path = ROOT / rel
        if not path.is_file():
            failures.append(f"{rel}: file not found")
            continue
        text = path.read_text(encoding="utf-8")
        if new in text and old not in text:
            print(f"  already applied: {rel}")
            continue
        if old not in text:
            failures.append(f"{rel}: source text not found -> {old.strip()[:70]}")
            continue
        path.write_text(text.replace(old, new), encoding="utf-8")
        edits += 1
        print(f"  patched: {rel}")

    if failures:
        print("\nUPSTREAM FIXES FAILED - upstream has moved:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        print(
            "\nRe-check these against the image and update deploy/upstream-fixes.py. "
            "If a fix has landed upstream, DELETE the entry rather than relaxing "
            "the assertion - a patch that matches nothing is how a fix quietly "
            "disappears.",
            file=sys.stderr,
        )
        return 1

    print(f"upstream fixes ok ({edits} edit(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
