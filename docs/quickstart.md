# Quickstart: factory robot → local robot

About 30 minutes of your attention, plus downloads you can walk away from.

**What that excludes, honestly:** the model download (5–10 GB depending on size), the Docker
images (~4 GB), and the ESP-IDF toolchain if you build the firmware yourself (~2 GB). On a slow
connection the downloads dominate and there is nothing anyone can do about that.

**What you need:**

- An M5Stack StackChan (CoreS3 / ESP32-S3) and a USB-C cable that carries data
- A machine with Docker, on the same network as the robot, left running — this is the robot's brain
- An NVIDIA GPU with 8 GB or more. CPU-only works and is slow enough to change how you feel about
  the robot; see [the model floor](model-floor.md)
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
./use-model.sh qwen3:14b      # pulls the model, points the config at it, restarts
```

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

> ⚠️ **Today this means building it yourself**, because the server address is compiled in. Making it
> settable after flashing — so a prebuilt binary can work for anyone — is the top item on
> [the roadmap](roadmap.md), and it is what a browser-based flasher is waiting on.

Install [ESP-IDF v6.0.2](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/),
then:

```bash
cd firmware
echo 'CONFIG_OTA_URL="http://YOUR_LAN_IP:8003/xiaozhi/ota/"' > sdkconfig.defaults.local
../deploy/build.sh
```

`sdkconfig.defaults.local` is gitignored — it is your address, and it does not belong in a
repository.

> 🔴 **Always build through `deploy/build.sh`.** A plain `idf.py build` silently builds a *different
> board* with upstream's cloud OTA URL baked in. It compiles, it flashes, it boots, and the only
> outward sign is a binary a few hundred KB smaller. The script pins the `SDKCONFIG_DEFAULTS` list
> and prints the resulting OTA URL so a wrong build is obvious.

Check that output before flashing. It should be your address, not `api.tenclass.net`.

Then flash. On Windows, do this **natively, not through WSL** — entering the ESP32 bootloader drives
the RTS/DTR lines, and those do not survive USB forwarding:

```bash
idf.py -p <PORT> flash
```

Only the app partition changes, so your Wi-Fi settings survive a reflash.

---

## 3. First run

1. Power the robot on. On first boot it serves its own Wi-Fi setup page — join its access point and
   give it your network.
2. Wait for the boot chime. **It is a test result, not a decoration:** the success chime means the
   speaker, microphone and servo rail were all confirmed working. A different, sharper sound means
   something did not come up, and the serial log names it.
3. Say **"Hi, Stack Chan."**

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
| Worked for weeks, now silent | This machine's IP changed. DHCP reservation, then update both files and reflash |
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
