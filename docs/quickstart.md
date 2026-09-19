# Quickstart: factory robot → local robot

About 30 minutes of your attention, plus downloads you can walk away from.

**What that excludes, honestly:** the model download (5–10 GB depending on size), the Docker
images (~4 GB), and the ESP-IDF toolchain if you build the firmware yourself (~2 GB). On a slow
connection the downloads dominate and there is nothing anyone can do about that.

**What you need:**

- An M5Stack StackChan (CoreS3 / ESP32-S3) and a USB-C cable that carries data
- A machine with Docker, on the same network as the robot, left running — this is the robot's brain
- A GPU with 8 GB or more. **NVIDIA is the smoothest path** because the model runs in a container
  with no extra steps; Apple Silicon and AMD both work, running Ollama natively instead — see
  [standing up your own model](your-llm.md). CPU-only works and is slow enough to change how you
  feel about the robot; see [the model floor](model-floor.md)
- 2.4 GHz Wi-Fi. The radio in this robot cannot see a 5 GHz network, so if your router hides both
  behind one name, the robot will look broken while everything else works

---

## 0. Back up the factory firmware. First. Before anything.

Not a formality, and not something to come back to later. Flashing overwrites the firmware the
robot shipped with, and M5Stack do not publish it — if you skip this, "put it back the way it was"
stops being an option you have.

Use **M5Burner** (M5Stack's own tool, Windows/macOS/Linux): connect the robot, and use its backup
function to save the full flash image before writing anything.

> 🔴 **The backup contains your Wi-Fi credentials**, in plain text, in the NVS partition. Keep it
> off shared drives, out of cloud sync, and out of any repository. Treat it like a password file,
> because it contains one.

M5Burner is also the recovery path if a flash goes wrong, so it is worth having installed even if
you never use the backup.

---

## 1. Bring up the server

```bash
git clone <this repo>
cd stackchan-local-llm/server

cp .env.example .env
cp config.example.yaml data/.config.yaml
```

Now edit **two files**, for the same reason: the robot has to dial this machine, so it needs this
machine's address on the network, and that cannot be auto-detected from inside a container.

- `.env` → `HOST_LAN_IP`
- `data/.config.yaml` → `server.websocket` and `server.vision_explain`

Find the address with `ip addr` (Linux/WSL) or `ipconfig` (Windows) and take the one on your Wi-Fi
network. **`localhost` and `127.0.0.1` are both wrong** — from the robot, those mean the robot.

> ⚠️ **Give this machine a DHCP reservation** in your router while you are here. When the address
> changes, the robot stops working and nothing anywhere tells you why. It is the single most common
> way a working setup breaks weeks later.

Then:

```bash
docker compose up -d          # first run pulls ~4 GB
./use-model.sh mistral-nemo:12b   # pulls it, points the config at it, restarts
```

That model is the recommendation for a card of 12 GB or more. On 8 GB use
`./no-think.sh qwen3:8b` and then `./use-model.sh qwen3:8b-nothink` instead — the matrix and the
measurements behind it are in [the model floor](model-floor.md).

Check it came up:

```bash
docker compose ps             # all services running
curl -s http://localhost:8003/xiaozhi/ota/ | head    # the robot's endpoint answers
```

### If you are on Windows with WSL2, two settings decide whether any of this works

Both of these produce the same symptom — the server is up, the port is listening, `curl` from
Windows works, and the robot cannot connect — because the packet dies a layer below where you are
looking. **Check these before re-reading your config.**

1. **Mirrored networking.** Put `networkingMode=mirrored` in `%USERPROFILE%\.wslconfig` (Windows 11
   22H2+), or use Start → "WSL Settings" → Networking → Mirrored, then `wsl --shutdown`. The default
   NAT mode means a device on your Wi-Fi cannot reach a server bound inside WSL at all, without
   hand-written `netsh portproxy` rules.
2. **The Hyper-V firewall**, which is separate from Windows Firewall and on by default for WSL
   traffic. Reachable from Windows is not the same as reachable from the LAN. You need inbound rules
   for **8000** and **8003**.

---

## 2. Put the firmware on the robot

**You do not have to compile your server's address in**, and you do not have to build anything. The
robot is told where its server is *after* it is flashed, from its own setup page, so one binary works
for everybody.

### The short way: flash it from your browser

**[→ The flasher](https://drakthedude.github.io/stackchan-local-llm/flash/)** — Chrome or Edge on a desktop, a USB-C cable that carries *data*, about
two minutes. It writes the same binaries this repository builds, each at its own flash offset. Safari
and Firefox do not implement Web Serial, and it does not work from a phone.

Four things about that page, in the order they catch people:

1. **Back it up first (§0).** The flasher cannot do that for you, and M5Stack do not publish the
   factory image. This is the one step with no undo.
2. **Choose `USB JTAG/serial debug unit`**, not `USB VCOM`. VCOM is first in the list and looks like the
   obvious answer; picking it returns you to the page with **no error message at all**, which reads like
   a broken robot rather than a wrong menu entry.
3. **Decline the erase** when it offers. Erasing wipes NVS — your Wi-Fi, your server address, and your
   robot's **factory servo calibration**, which is unique to your unit and cannot be recovered. Declining
   keeps all three and still replaces the firmware.
4. **Close the tab afterwards.** The browser holds the serial port for as long as that page is open, so
   anything else you point at the robot — a serial monitor, `esptool`, the flasher in a second tab —
   fails with *access denied* or *port busy*. That reads like a driver fault. It is the tab.

A robot that kept its Wi-Fi reconnects on its own and never offers the setup network, so skip to the
five-second hold in §3 to give it your server address.

### Or build it from source

Worth it if you are changing the firmware, and required if you want a binary with your own address
already in it. Install
[ESP-IDF v6.0.2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/), then:

```bash
cd firmware
../deploy/build.sh
```

> 🔴 **Always build through `deploy/build.sh`.** A plain `idf.py build` silently builds a *different
> board* with upstream's cloud OTA URL baked in. It compiles, it flashes, it boots, and the only
> outward sign is a binary a few hundred KB smaller. The script pins the `SDKCONFIG_DEFAULTS` list
> and prints the resulting OTA URL so a wrong build is obvious.

The address it prints will be `stackchan-server.invalid` — that is correct and deliberate. `.invalid`
is a reserved name that can never resolve, so a robot nobody has configured fails safely instead of
dialling a stranger's machine. You will set the real one in §3.

*(If you would rather bake it in — for a robot you flash often, say — put
`CONFIG_OTA_URL="http://YOUR_LAN_IP:8003/xiaozhi/ota/"` in `firmware/sdkconfig.defaults.local`. That
file is gitignored: it is your address and it does not belong in a repository. A value set on the
robot still wins over it.)*

Then flash. On Windows, do this **natively, not through WSL** — entering the ESP32 bootloader drives
the RTS/DTR lines, and those do not survive USB forwarding:

```bash
idf.py -p <PORT> flash
```

Only the app partition changes, so your Wi-Fi settings — and your robot's factory servo calibration,
which is unique to it and cannot be recovered — survive a reflash.

---

## 3. First run

1. Power the robot on.

   - **A robot with no Wi-Fi saved** serves its own setup network, **`StackChan-XXXX`**. Join it from a
     phone and the setup page opens by itself.
   - **A robot that kept its Wi-Fi** — anything you declined the erase on — reconnects to the network
     it already knows and never offers that setup network. Hold a finger on his screen for **five
     seconds** to reach the same page. Waiting for a network that is not coming is the most common way
     to conclude a good flash failed.
2. On **Wi-Fi Config**, give it your 2.4 GHz network.
3. On **Advanced**, put your server in **Custom OTA URL**:

   ```
   http://YOUR_LAN_IP:8003/xiaozhi/ota/
   ```

   The same address as `HOST_LAN_IP` in §1 — and again, not `localhost`. Save.

   > 🔑 **This is how you change servers later, too.** Hold a finger on the robot's screen for **five
   > seconds** and he returns to this page. Without that, changing the address would mean breaking his
   > Wi-Fi on purpose. It is five seconds because it is deliberate: it ends the conversation and takes
   > him off the network. A quick tap still just starts a conversation.
   >
   > If you never set an address, he says so on screen rather than looping a connection error — a robot
   > that has not been told where to go is not a broken one.
4. Wait for the boot chime. **It is a test result, not a decoration:** the success chime means the
   speaker, microphone and servo rail were all confirmed working. A different, sharper sound means
   something did not come up, and the serial log names it.
5. Say **"Hi, Stack Chan."**

The wake word runs on the robot itself, not on the server — it works even while the network is down.
Tapping the screen also starts a conversation.

> ⚠️ **The wake word is also the model's first message.** The firmware sends the phrase to the model
> as if you had spoken it, so it shapes the reply. Worth knowing before you change it to something
> that would read oddly as an opening line.

---

## Troubleshooting

| What you see | What it usually is |
|---|---|
| Robot never connects; server looks fine | The firewall layer, not your config. On Windows/WSL see §1. Check `HOST_LAN_IP` matches `server.websocket` in the config |
| Worked for weeks, now silent | This machine's IP changed. Set a DHCP reservation, then hold his screen 5 s and update **Custom OTA URL** — no reflash needed |
| "No server set yet" on screen, or **`NO LLM`** in the corner | Exactly what they say: hold the screen 5 s, then Advanced → Custom OTA URL. The badge stays up until an address is set, because the startup message scrolls away and he only retries when woken |
| The flasher's port list has two entries and neither works | Pick **USB JTAG/serial debug unit**. `USB VCOM` fails by closing the dialog with no error |
| *Access denied* / *port busy* on the serial port | The flasher tab is still open and holding it. Close the tab |
| Flashed fine, but no `StackChan-XXXX` network appears | You declined the erase, so he still has your Wi-Fi and went straight to it. Hold the screen 5 s instead |
| He hears you, answers on screen, no sound | Should be fixed — the amplifier can open while unreachable. If it persists, the serial log says `amp unreachable after 4 attempts` |
| Screen says "listening", nothing is picked up | Same class of bug on the microphone, also fixed. Look for `ES7210 unreachable` in the log |
| He talks over you | `min_silence_duration_ms` in the config. 900 ms lets you pause mid-sentence; upstream's 200 ms does not |
| He answers in Chinese | `TTS.LocalTTS.language` is missing. It does not just pick a voice — it tells the *model* what language to use, and defaults to Chinese |
| He repeats the same opening every time | Temperature too low, and memory off. Both are covered in the config comments |
| Transcripts come back in a language nobody spoke | A multilingual Whisper auto-detecting on a short utterance. Use an `.en` model |
| He invents answers instead of using tools | The model is too small, or its tool template is wrong. [model-floor.md](model-floor.md) |
| Robot vanishes from USB | Hold the power button ~6 s. Replugging alone does not clear it |

For anything else, the serial log at 115200 baud is the honest witness — and this firmware is
deliberately talkative about which chip did not answer.
