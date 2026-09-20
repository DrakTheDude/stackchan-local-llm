# Checking the privacy claim yourself

This project exists because an always-on microphone and a camera pointed at a room were talking to an
endpoint nobody documents. Replacing that with a promise would not be an improvement. So here is the
claim, stated narrowly enough to be wrong, and the checks that test it.

> **The claim.** With the stack as shipped, the robot talks to exactly one address: the server you
> configured. Your speech is transcribed, answered and spoken on machines you own. No audio, no
> transcript, no photograph and no summary of your conversation leaves your network.

Run the checks. Do not take the paragraph's word for it — that is the entire point.

---

## 1. Read every URL in the firmware

The strongest check, and the easiest. The firmware is a file; every address it could dial is a string
inside it.

```bash
strings -n 8 firmware/build/xiaozhi.bin | grep -oE 'https?://[^ "]+' | sort -u
```

On a build of this repo, that is the **entire** output:

```
http://YOUR_SERVER:8003/xiaozhi/ota/          <- the address you set. The only one dialled
http://192.168.4.1                            <- the robot's OWN address while serving Wi-Fi setup
http://www.w3.org/2000/svg                    <- an XML namespace, never fetched
https://<...>.feishu.cn/wiki/<...>            <- printed in a LOG LINE pointing a developer at
                                                 wake-word docs. Never fetched
```

Two of those are not network destinations at all, and one is the robot's own access point. If you see
anything else — particularly a vendor's API — that is a finding, and worth an issue.

> ⚠️ **Do this on a binary you built**, or on a release binary whose build you can reproduce. Checking
> a file somebody handed you proves something about that file, which is not the same thing.

Worth knowing what is *absent*: there is **no NTP server** in the image. The clock comes from your own
server's reply, so the robot does not quietly reach a time service either.

## 2. Confirm the camera talks to you, or to nobody

The upstream firmware can send camera frames to a vision endpoint for captioning. Its default is a
cloud API, and that is the single worst leak available on this hardware — pictures of your room.

> ⚠️ **This check changed, and it is weaker than it was.** It used to say: verify `GetCamera()`
> returns `nullptr`, which refused the vision path outright. That one line was refusing two things at
> once — the stock cloud-shaped tool, *and* the ability to accept any vision URL at all — so local
> vision could not exist alongside it. The camera now hands back a real object, and **a frame can
> leave the device**. What you can still verify is exactly where it goes, and that nothing goes
> anywhere unless you asked for it.

**First: the stock tool is still not registered.**

```bash
grep -A2 "UseStockCameraTool" firmware/main/boards/m5stack/stackchan/m5stack_stackchan.cc
```

**A pass looks like** `return false;`. That tool's body is capture-then-post; ours shows the photo on
the robot's own screen and only describes it if you configured something to describe it with.

**Second: nothing is sent unless a vision model is configured.** The robot cannot invent a
destination — it uses the vision URL the server offers, and the server offers one only when it has a
model. So the default state of a fresh install is still "the photo stays on the device":

```bash
grep -A3 "^selected_module:" server/data/.config.yaml | grep VLLM
```

**No output is a pass** in the strongest sense: no `VLLM` entry means no vision URL, which means the
camera has nowhere to send anything and `show_photo` behaves as it always did.

**Third: if you did configure one, check where it points.**

```bash
grep -A4 "^VLLM:" server/data/.config.yaml
```

**A pass looks like** a `base_url` on your own machine — `http://ollama:11434/v1`,
`http://host.docker.internal:11434/v1`, or a box on your own network. A hostname you do not recognise
is the leak this page exists to find. See [vision.md](vision.md) for what that costs and what it
buys.

## 3. Confirm the server's own plugins cannot call out

The server ships built-in tools the model may call, and three of them leave your network: a weather
API, a news service, a web search. In `server/config.example.yaml`:

```bash
grep -A2 "^Intent:" server/data/.config.yaml
```

**A pass looks like** `functions: []` — an empty list, present. A pass is *not* the key being absent:
an empty list is a privacy control, and the config merges over upstream's defaults, so an absent key
means "use theirs".

## 4. Confirm your conversations are summarised locally

The memory feature summarises each conversation with an LLM. Upstream's default for that summariser is
a **cloud** provider, and because the config merges, omitting the key silently uses it — every
conversation posted to an API to be summarised.

```bash
grep -A3 "^Memory:" server/data/.config.yaml
grep -A8 "^selected_module:" server/data/.config.yaml | grep LLM
```

**A pass looks like** both naming the same entry — e.g. `llm: Ollama` under `mem_local_short`, and
`LLM: Ollama` under `selected_module`. If they differ, your conversations are being summarised by
something other than the model you chose. If the `llm:` line is missing entirely, they are being
summarised in the cloud.

## 5. Read the patch assertions

The server image is built from upstream's with patches applied. Every replacement **asserts** that the
text it is replacing is still there, so an upstream change fails the build rather than silently
restoring the original behaviour:

```bash
grep -c "assert\|MISSING\|sys.exit" server/patches/*.py
```

**A pass looks like** a non-zero count for each file, and — more to the point — a *build that fails*
when upstream moves. You can prove that one: change a search string in a patch to something that
cannot match, run `docker compose build xiaozhi`, and watch it refuse. A patch kit that cannot fail is
a patch kit that has already stopped working and not told you.

## 6. Watch the wire

The checks above read code. This one watches behaviour — point it at your robot's address for an
evening:

```bash
sudo tcpdump -n "host YOUR_ROBOT_IP and not host YOUR_SERVER_IP"
```

**A pass looks like** near-silence: ARP, and DHCP renewals every few hours. Talk to the robot while it
runs — a conversation should produce *nothing here at all*, because all of it goes to the server you
excluded. Anything else, particularly a DNS lookup for a name you do not recognise, is worth
investigating and worth an issue.

Your router's client list will do the same job more crudely if you would rather not run tcpdump.

---

## The switches

Two settings, on the robot, reached by holding the screen for five seconds:

- **Microphone off** *closes the input device.* Not a flag that blanks the audio afterwards — the codec
  stops streaming, and the wake word stops working too. A robot advertised as not listening should not
  be listening for its own name. A **MIC OFF** badge shows on screen, and the setting survives a reboot:
  it is restored before anything can open the microphone.
- **Camera off** refuses at the point of capture and says so.

Both persist deliberately. A mute that quietly lapses overnight is worse than no mute.

---

## What this does not protect you from

A checklist that only lists reassurances is marketing. These are real and unfixed:

- **Anyone holding the robot.** NVS is unencrypted, so your Wi-Fi credentials — and any token you add —
  can be read off the flash by someone with the device and a cable. Same as the factory firmware.
- **Your own LAN.** The robot talks to your server over plain HTTP and an unauthenticated WebSocket.
  Anyone already on your network can watch the traffic. Encrypting it is a real task and not done.
- **Your server's own model.** If you point the LLM at a hosted API instead of a local one, your
  conversations go there. The stack defaults to local; the setting is yours to get wrong.
- **MCP servers you add.** Tools you mount can do whatever they do. That is the point of them, and it
  is your judgement.
- **The backup you made.** The factory firmware backup contains your Wi-Fi password in plain text. Keep
  it off cloud sync and out of any repository. ⚠️ Running check 1 against it is worth doing — that is
  [factory-firmware.md](factory-firmware.md) — but run it on the **app partition**, never on the
  full-flash dump. The password is in the dump.
- **Supply chain.** You are trusting ESP-IDF, the upstream firmware, the Docker images and this
  project. The checks above test *behaviour*, not the good intentions of everyone upstream.

## What still uses the internet, occasionally

Setting up, not running:

- pulling Docker images and downloading a model, once
- your own `git clone`, and the ESP-IDF toolchain if you build the firmware

Once it is running, nothing on the robot or in the stack needs the internet. You can unplug the WAN
and talk to him, which is the most satisfying test on this page.
