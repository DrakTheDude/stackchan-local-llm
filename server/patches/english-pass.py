#!/usr/bin/env python3
"""Translate the Chinese that the MODEL sees and the USER hears.

xiaozhi-esp32-server is a Chinese project and its prompts, tool schemas and a
few spoken fallbacks are Chinese. Almost all of that is comments and log lines
and does not matter. A small amount does, and it had been leaking out one
symptom at a time:

    - the idle farewell spoke Chinese          -> fixed in config (end_prompt)
    - internal errors spoke Chinese            -> fixed in config
    - "take care" spoke Chinese                -> handle_exit_intent, here

Patching each one as it surfaced was losing to a source that has more of them.
This does the whole active path at once.

WHAT COUNTS AS ACTIVE. This deployment runs Intent=function_call with
Intent.function_call.functions=[], so the only plugins loaded are upstream's
two mandatory ones. Every other tool the model sees arrives over MCP - from
whatever servers you mount, and from the robot itself - and those are already
whatever language you wrote them in. So the surface is tiny, and this
script is deliberately a short list of exact strings rather than a sweep.

🔴 EVERY REPLACEMENT IS ASSERTED. If an upstream bump changes one of these
   strings, the build FAILS with the file and the missing text. The failure
   mode that actually matters here is not a crash - it is a patch that
   silently matches nothing and leaves you with a robot that speaks Chinese
   again and a build log that says success.
"""
import sys
from pathlib import Path

ROOT = Path("/opt/xiaozhi-esp32-server")

# (file, old, new). Exact, anchored strings - no regex, nothing clever.
REPLACEMENTS = [
    # 👁 THE VISION PROVIDER, which appends this to EVERY question it asks about
    #    a photo - unconditionally, in code, below any config anyone can reach.
    #
    #    Measured live against a local vision model: the description came back
    #    as "我看到一个黄色的圆和一个红色的矩形" - correct, and unusable, because
    #    it goes straight to a text-to-speech voice speaking English.
    #
    # ⚠️ IT HID HERE FOR AS LONG AS VISION WAS OFF. The path had never run, so
    #    no amount of using the robot could surface it. Worth remembering for
    #    the next feature that gets switched on: the Chinese in a code path is
    #    not found by the pass, it is found by running the path.
    #
    #    Replaced rather than deleted: this answer is SPOKEN, and the vision
    #    model has no persona telling it to be brief. Without that it writes a
    #    paragraph and the robot reads all of it.
    (
        "core/providers/vllm/openai.py",
        'question = question + "(请使用中文回复)"',
        # ⚠️ A DESCRIPTION, NOT A LINE OF SPEECH. Asking this model to answer
        #    "as if saying it out loud" produced exactly that - "I see a person
        #    taking a selfie in front of some shelves" - and the chat model read
        #    it back word for word instead of reacting to it. It recites
        #    anything that already sounds like something to say, so it gets raw
        #    material now and has to do the talking itself.
        'question = question + (\n'
        '            " Describe the scene in one short sentence of plain English:"\n'
        '            " the people, what they are doing, and the notable things around"\n'
        '            " them. Do not begin with \\"I see\\" or \\"The image shows\\";"\n'
        '            " just describe it. No markdown, no lists."\n'
        '        )',
    ),
    # The catch-all tool offered on EVERY turn. A Chinese description on the one
    # tool the model reaches for most is the strongest single nudge toward
    # Chinese output in the whole server.
    (
        "core/connection.py",
        "当用户的请求不匹配其他任何工具时，可用此选项直接回复。将回复内容写在response参数里。",
        "Use this when the user's request does not match any other tool. "
        "Put your full reply in the response argument.",
    ),
    (
        "core/connection.py",
        "你回复用户的完整内容",
        "Your complete reply to the user",
    ),
    # The exit intent. Its say_goodbye argument is spoken VERBATIM - it never
    # passes back through the model - so a Chinese schema here produces a
    # Chinese goodbye that no prompt can reach.
    (
        "plugins_func/functions/handle_exit_intent.py",
        "当用户想结束对话或需要退出系统时调用",
        "Call this when the user wants to end the conversation or sign off, "
        "for example 'goodbye', 'goodnight' or 'take care'.",
    ),
    (
        "plugins_func/functions/handle_exit_intent.py",
        "和用户友好结束对话的告别语",
        "A short, warm goodbye to end the conversation. Write it in English - "
        "it is spoken to the user exactly as written.",
    ),
    (
        "plugins_func/functions/handle_exit_intent.py",
        '"再见，祝您生活愉快！"',
        '"Goodbye. Say hi Stack Chan when you need me."',
    ),
    # 🔴🔴 THE FEW-SHOT EXAMPLES. THIS is what actually produced the Chinese
    #      goodbye, and it beat two rounds of fixing instructions.
    #
    #      core/connection.py injects a worked example into the dialogue of
    #      EVERY conversation: user says 拜拜, assistant calls handle_exit_intent
    #      with say_goodbye "再见，下次再聊~". The robot's screen then showed
    #      下次再聊~ - copied out of the example verbatim.
    #
    #      A DEMONSTRATION BEATS AN INSTRUCTION. The system prompt can say
    #      "answer in English" as loudly as it likes; a few-shot turn showing a
    #      Chinese answer is a stronger signal than any rule, because it is what
    #      the model is being shown rather than what it is being told. Rounds
    #      one and two both fixed rules and both left this in place.
    #
    #      The direct_answer example is Chinese too and rides along on every
    #      single turn, not just farewells.
    (
        "core/connection.py",
        'content="给我讲个故事吧"',
        'content="Tell me a story"',
    ),
    (
        "core/connection.py",
        '\'{"response": "好呀，你想听什么类型的呀？童话、冒险还是搞笑的？选一个我给你开讲~"}\'',
        '\'{"response": "Sure - what sort? Something with a bit of adventure, or something daft?"}\'',
    ),
    (
        "core/connection.py",
        'content="已直接回复"',
        'content="replied directly"',
    ),
    (
        "core/connection.py",
        'content="拜拜"',
        'content="bye"',
    ),
    (
        "core/connection.py",
        '\'{"say_goodbye": "再见，下次再聊~"}\'',
        '\'{"say_goodbye": "Goodbye - say hi Stack Chan when you need me."}\'',
    ),
    (
        "core/connection.py",
        'content="退出意图已处理"',
        'content="exit intent handled"',
    ),
    (
        "core/connection.py",
        'content="再见，下次再聊~"',
        'content="Goodbye - say hi Stack Chan when you need me."',
    ),
    # 🔴 A SECOND COPY OF THE EXIT SCHEMA, in the block that teaches the model
    #    how to emit a tool call at all. Its worked example is - again - a
    #    Chinese goodbye. Patching plugins_func/functions/handle_exit_intent.py
    #    fixed the schema the model is GIVEN; this is the schema the model is
    #    SHOWN in the formatting instructions, and they are different files.
    (
        "core/providers/llm/system_prompt.py",
        '"description": "当用户想结束对话或需要退出系统时调用",',
        '"description": "Call this when the user wants to end the conversation.",',
    ),
    (
        "core/providers/llm/system_prompt.py",
        '"description": "和用户友好结束对话的告别语",',
        '"description": "A short, warm goodbye, in the user\'s language.",',
    ),
    (
        "core/providers/llm/system_prompt.py",
        '"say_goodbye": "再见，祝您生活愉快！"',
        '"say_goodbye": "Goodbye - talk to you soon."',
    ),
    # The tool RESULT text, which goes back into the dialogue as the tool
    # message and is therefore model-visible on the next turn.
    (
        "plugins_func/functions/handle_exit_intent.py",
        'result="退出意图已处理"',
        'result="exit intent handled"',
    ),
    (
        "plugins_func/functions/handle_exit_intent.py",
        'result="退出意图处理失败"',
        'result="exit intent failed"',
    ),
    # 🔴 agent-base-prompt.txt - the one that looked like the cause in round two.
    #
    #    The first version of this file patched the tool schemas and the spoken
    #    fallback and stopped there, on the reasoning that those were the only
    #    places a Chinese string could reach anyone. "take care" still produced
    #    Chinese, because the system prompt itself ORDERS it:
    #
    #      "you must clearly respond with 再见 or the corresponding farewell"
    #
    #    A direct instruction in the system prompt beats every persona prompt
    #    and every tool description. It was hiding in plain sight in a file that
    #    is otherwise English, which is exactly why it was not looked at.
    (
        "agent-base-prompt.txt",
        "4. [Exit mechanism] When the user says farewell words like “再见”, “拜拜”, “晚安”, "
        "“退下”, “待机”, etc., you must clearly respond with “再见” or the corresponding "
        "farewell, and call the exit tool (handle_exit_intent).",
        "4. [Exit mechanism] When the user says farewell words like \"goodbye\", \"goodnight\", "
        "\"take care\", \"see you\", etc., reply with a short farewell IN THE SAME LANGUAGE "
        "THE USER USED, and call the exit tool (handle_exit_intent).",
    ),
    # These two hand the model Chinese vocabulary to imitate. They are style
    # guidance, so they never look like a language setting - but "use words like
    # 我在呢" is a Chinese instruction whatever section it sits in.
    (
        "agent-base-prompt.txt",
        '- [Banned clichés] NEVER use written/AI-flavored phrases such as "烦心事", "有趣的事", '
        '"好玩的事", "新鲜事", "根据资料", "综上所述".',
        '- [Banned clichés] NEVER use written or AI-flavoured filler such as "as an AI", '
        '"based on the information available", "in conclusion", "I hope this helps".',
    ),
    (
        "agent-base-prompt.txt",
        '- [Recommended colloquialisms] Use grounded, natural spoken words like "我在呢", '
        '"咋回事", "说来听听".',
        '- [Recommended colloquialisms] Use grounded, natural spoken words like "right here", '
        '"what happened", "go on then".',
    ),
    (
        "agent-base-prompt.txt",
        "then ask ”要继续听吗？”)",
        "then ask \"want me to go on?\")",
    ),
    # Ends "Multiple items must be joined ... using Chinese semicolons ；or commas ，".
    # Chinese punctuation reaching an English TTS is either spelled out or dropped.
    (
        "agent-base-prompt.txt",
        "Multiple items must be joined into natural spoken paragraphs using Chinese "
        "semicolons ”；” or commas ”，”.",
        "Multiple items must be joined into natural spoken paragraphs using ordinary "
        "semicolons and commas.",
    ),
    # get_lunar is a Chinese almanac - lunar dates, zodiac, ba zi, solar terms.
    # Upstream forces it on via necessary_functions, so its several hundred
    # characters of Chinese description ride in every prompt for a tool this
    # deployment will never call. Dropped rather than translated: translating it
    # would keep the prompt noise and add a tool that answers in Chinese anyway.
    (
        "core/providers/tools/server_plugins/plugin_executor.py",
        'necessary_functions = ["handle_exit_intent", "get_lunar"]',
        'necessary_functions = ["handle_exit_intent"]',
    ),
    # 🔴 THE FULL STOP IS NOT IN THE SENTENCE SPLITTER, and that is why he froze
    #    in the middle of a story.
    #
    #    Measured 2026-08-08 in the server's own log:
    #        19:18:07  "Once upon a time"
    #        19:18:25  "deep in the heart of a data center ... in its wake."
    #    Eighteen seconds of silence, mid-sentence, with the first fragment
    #    still on the screen.
    #
    #    base.py streams TTS by splitting the model's output at punctuation, and
    #    it uses two different sets. The FIRST sentence may break at "，~、,"
    #    among others - ASCII comma included - which is why "Once upon a time"
    #    comes out fast. Everything after it may only break at 。？?！!；;：
    #    Note what is missing: the ASCII FULL STOP. Chinese ends sentences with
    #    。 so nobody noticed. An English story written in ordinary sentences
    #    therefore has NO legal split point anywhere, and the whole remainder is
    #    flushed in one block when generation finally stops.
    #
    # ⚠️ Raising max_tokens 500 -> 1200 did not cause this, it EXPOSED it. The
    #    block was always unsplittable; it just used to be short enough to hide.
    #    Do not "fix" this by putting the ceiling back - that trades the good
    #    long stories away to conceal a one-line bug.
    #
    # A period does also appear in decimals and abbreviations, so "ninety six
    # point five" would split oddly - acceptable, and the persona already asks
    # for numbers as words. Newline is in here too: the model writes paragraph
    # breaks in long stories and they are natural pauses.
    (
        "core/providers/tts/base.py",
        '        self.punctuations = (\n'
        '            "。",\n'
        '            "？",\n'
        '            "?",\n'
        '            "！",\n'
        '            "!",\n'
        '            "；",\n'
        '            ";",\n'
        '            "：",\n'
        '        )',
        '        self.punctuations = (\n'
        '            "。",\n'
        '            ".",\n'
        '            "\\n",\n'
        '            "？",\n'
        '            "?",\n'
        '            "！",\n'
        '            "!",\n'
        '            "；",\n'
        '            ";",\n'
        '            "：",\n'
        '        )',
    ),
    # 🔴 A FULL STOP ONLY ENDS A SENTENCE IF SOMETHING FOLLOWS IT.
    #
    #    Heard aloud, one fragment per breath:
    #        "Here's the photo you asked for" / "Photo](https://example"
    #        "com/photo" / "jpg)"
    #
    #    The model invented a markdown link to a photo that does not exist. The
    #    server's MarkdownCleaner would have removed it, but it runs AFTER
    #    segmentation - and segmentation had already cut the link apart at the
    #    dots in "example.com/photo.jpg", so there was no link left to match.
    #
    # ⚠️ THE SPLIT ON "." IS OURS, from the patch just above, and it was right:
    #    English sentences had no legal split point at all and the whole reply
    #    arrived in one block after eighteen seconds of silence. Do not revert
    #    it. It simply treated every dot as a sentence end, domains and decimals
    #    included.
    #
    #    So the dot splits only when whitespace or the end of the buffer follows
    #    it, tested against a MASKED COPY - same length, same offsets. The
    #    streaming path indexes into this string with processed_chars, so
    #    rewriting it in place would desynchronise the stream.
    (
        "core/providers/tts/base.py",
        """        for punct in punctuations_to_use:
            pos = current_text.rfind(punct)""",
        r"""        # A dot inside "example.com" or "96.5" is not a sentence end.
        # Masked rather than removed: same length, so every offset below - and
        # processed_chars, which indexes into current_text - still lines up.
        scan_text = re.sub(r"\.(?=\S)", "\x00", current_text)

        for punct in punctuations_to_use:
            pos = scan_text.rfind(punct)""",
    ),
    # The memory summariser's own working labels. They are prepended to the
    # transcript it is asked to summarise, so they are in the model's input on
    # every save - and the thing it writes goes straight back into the system
    # prompt. See the block replacement below.
    (
        "core/providers/memory/mem_local_short/mem_local_short.py",
        r'msgStr += "历史记忆：\n"',
        r'msgStr += "Previous memory:\n"',
    ),
    (
        "core/providers/memory/mem_local_short/mem_local_short.py",
        'msgStr += f"当前时间：{time_str}"',
        'msgStr += f"Current time: {time_str}"',
    ),
]

# 🔴 THE MEMORY PROMPT IS A WHOLE BLOCK, not a string, so it gets its own list.
#
#    mem_local_short summarises each conversation with an LLM and injects the
#    result into <memory> in the system prompt of every LATER conversation.
#    Upstream's prompt is Chinese from top to bottom AND specifies a schema with
#    Chinese KEYS - 时空档案, 记忆立方, 高光语录. Enabling memory as shipped would
#    therefore park a block of Chinese in the robot's context permanently.
#
#    That is the exact shape of the bug that took three rounds to find on the
#    farewell: A DEMONSTRATION BEATS AN INSTRUCTION. The output-language rule
#    can shout in English all it likes while the model is being shown Chinese
#    inline. Translating the schema is not optional polish; it is the condition
#    for turning memory on at all.
#
#    It is also the wrong prompt for a desk robot even in translation - it grades
#    memories on 情感强度 (emotional intensity) and keeps a 高光语录 list of the
#    user's most moving quotes. So this is rewritten rather than translated: what
#    a robot you talk to every day needs to carry between conversations is what
#    you are working on, what has happened in whatever it can see, and WHAT IT HAS
#    ALREADY SAID - the last one being the actual fix for a robot that repeats its
#    own lines, which temperature only ever papered over.
#
# 🔴 THE EXAMPLE HAD REALISTIC VALUES IN IT AND THE MODEL COPIED THEM. The very
#    first memory written on real hardware came back with three entries lifted
#    straight out of the sample JSON - a hobby the user does not have, a
#    preference they never stated, a story they never told - sitting directly
#    below a line reading "Never copy anything out of the example". It also
#    filled in the robot's own name under "name", because the example did not say
#    whose name it was.
#
#    Same lesson as the Chinese farewell, in a file that documents the Chinese
#    farewell: A DEMONSTRATION BEATS AN INSTRUCTION. Plausible sample content is
#    indistinguishable from a memory, and a summariser's whole job is to copy
#    things that look like memories. Worse than a one-off: memory is rewritten
#    FROM ITSELF each time, so an invented fact is re-copied for ever.
#
#    So the schema is now empty containers, and what belongs in each field is
#    described in prose above it. Prose cannot be mistaken for content.
NEW_MEMORY_PROMPT = '''
# Memory

You are the part of this robot that remembers between conversations. You are
given a transcript and the memory you wrote last time. Rewrite the memory so the
next conversation starts already knowing what matters.

## The fields
- about: the PERSON the robot talks to - never the robot itself. Their name if
  they say it, and short notes on who they are and what they are working on.
- events: what has happened in whatever the robot can see through its tools. A
  machine that went down, a door left unlocked, a light that keeps failing.
  Leave it empty if the robot has no tools.
- threads: unfinished business. Something they said they would do, a decision
  they were chewing on, a question you left open.
- already_said: WHAT YOU HAVE ALREADY SAID TO THEM. Openings, jokes, stories,
  turns of phrase. This is what stops you telling the same story twice, and it
  is the most useful thing in here.
- promised: things the robot said it would do.

## Drop
- Small talk, the wake word, anything answered and finished.
- Their replies and yours, unless one contains a promise or a story you told.
- Anything stale: if it has not come up in several conversations and nothing
  depends on it, let it go.

## Rules
- English only.
- Facts in short phrases, not narration.
- Under 900 characters total. When it grows past that, merge or delete the
  oldest, least-referenced entries - never truncate mid-entry.
- 🔴 EVERY ENTRY MUST COME FROM THE TRANSCRIPT OR FROM THE PREVIOUS MEMORY.
  The skeleton below is EMPTY on purpose - it shows the shape and contains no
  content. If you cannot point at the line that taught you something, it does
  not go in. An invented entry is copied forward for ever.
- An empty list is a correct answer. Leave a field empty rather than filling it.
- Output ONE json object inside a ```json fence and nothing else: no
  explanation, no commentary, no text before or after.

```json
{
  "about": {"name": "", "notes": []},
  "events": [],
  "threads": [],
  "already_said": [],
  "promised": []
}
```
'''

# (file, start anchor, end anchor, fingerprint that must be inside, replacement).
# The fingerprint is what makes this an assertion rather than a hope: finding the
# anchors is not enough, the text between them has to still be the thing this
# was written against.
BLOCK_REPLACEMENTS = [
    (
        "core/providers/memory/mem_local_short/mem_local_short.py",
        'short_term_memory_prompt = """',
        '"""',
        "时空记忆编织者",
        NEW_MEMORY_PROMPT,
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
            failures.append(f"{rel}: source text not found -> {old[:60]}")
            continue
        path.write_text(text.replace(old, new), encoding="utf-8")
        edits += 1
        print(f"  patched: {rel}")

    for rel, start, end, fingerprint, new in BLOCK_REPLACEMENTS:
        path = ROOT / rel
        if not path.is_file():
            failures.append(f"{rel}: file not found")
            continue
        text = path.read_text(encoding="utf-8")
        i = text.find(start)
        if i < 0:
            failures.append(f"{rel}: block start not found -> {start}")
            continue
        j = text.find(end, i + len(start))
        if j < 0:
            failures.append(f"{rel}: block end not found -> {end}")
            continue
        body = text[i + len(start) : j]
        if fingerprint not in body:
            if new.strip() in body:
                print(f"  already applied: {rel} ({start[:40]})")
            else:
                failures.append(
                    f"{rel}: block no longer contains {fingerprint!r} - "
                    "upstream rewrote it, re-read it before trusting this patch"
                )
            continue
        path.write_text(text[: i + len(start)] + new + text[j:], encoding="utf-8")
        edits += 1
        print(f"  patched block: {rel} ({start[:40]})")

    if failures:
        print("\nENGLISH PASS FAILED - upstream has moved:", file=sys.stderr)
        for f in failures:
            print(f"  {f}", file=sys.stderr)
        print(
            "\nRe-check these strings against the image and update "
            "deploy/english-pass.py. Do NOT relax the assertions: a patch that "
            "matches nothing is how the Chinese comes back silently.",
            file=sys.stderr,
        )
        return 1

    print(f"english pass ok ({edits} edit(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main())
