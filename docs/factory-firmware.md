# What the factory firmware talks to

This project exists because the robot shipped with an always-on microphone, a
camera pointed at the room, and an undocumented endpoint. This page is what could
be **established** about that, rather than assumed — and it is deliberately
separate from [privacy.md](privacy.md), which is about what *this* firmware does.

⚠️ **This is static analysis, not a packet capture.** It shows what the binary
*can* reach. It does not show what it did reach, how often, or what was in it.
Where that distinction matters it is called out, and it matters more here than
usual — see [the last section](#what-this-does-not-establish).

---

## How to check your own

🔴 **Do this on the app partition, not on a full-flash dump.** A full dump
contains the NVS partition, and NVS holds **your Wi-Fi SSID and password in
plain text**. Never paste the output of `strings` on a full dump anywhere, and
never commit one.

If you backed up before flashing — and you should have — you have the file
already:

```bash
strings -n 6 factory-app.bin > /tmp/fa.txt
grep -oE '(https?|wss?|mqtts?)://[A-Za-z0-9._~:/?#@!$&%*+,;=-]+' /tmp/fa.txt | sort -u
grep -oE '\b([0-9]{1,3}\.){3}[0-9]{1,3}\b' /tmp/fa.txt | sort -u
```

The same two commands on this firmware are check 1 in [privacy.md](privacy.md),
and the results sit side by side below.

---

## What is in there

Run against the 3.7 MB factory app partition of a CoreS3 StackChan, backed up
2026-08-04.

### Endpoints

| address | what it is |
|---|---|
| `https://api.tenclass.net/xiaozhi/ota/` | **the one that matters.** The OTA/config endpoint, hardcoded. The device asks it for its configuration and is handed the address it should then talk to |
| `ws://47.113.125.164:12800` | a WebSocket service at a **bare IP address**, no hostname |
| `http://47.113.125.164:12800/stackChan/device/info` | same host: device info |
| `http://47.113.125.164:12800/stackChan/device/user` | same host: the device/user association |
| `http://47.113.125.164:12800/stackChan/device/unbind` | same host: unbinding |
| `http://47.113.125.164:12800/stackChan/apps` | same host: an app list |
| `https://ezdata2.m5stack.com/api/v2/device/registerMac` | **registers the device's MAC address** |
| `https://my.m5stack.com/ezdata2` | the EzData service this belongs to |
| `uiflow2.m5stack.com` | the UiFlow2 programming environment |
| `cn.pool.ntp.org`, `time.google.com` | the clock |
| `http://192.168.4.1` | the robot's **own** setup access point, not a destination |

And three that are **not** endpoints, listed so nobody re-derives them in alarm:
`apps.apple.com` and `play.google.com` are the companion app's store listings,
`pcn7cs20v8cr.feishu.cn` is a documentation wiki link, and `www.entrust.net`
comes from a bundled CA certificate.

### Three things worth saying plainly

**The bare IP is the thing that stands out.** `47.113.125.164:12800` carries a
WebSocket channel and four device-management endpoints, over **plain `http://`**
for the four, and it has no hostname at all. We did not establish who operates
it; this page says what was found and not who owns it.

**Something registers the MAC address.** `device/registerMac` is explicit about
it. A MAC is a stable, unique identifier for one physical device.

**The OTA endpoint hands over the rest.** This is how the XiaoZhi design works:
the device asks `api.tenclass.net` for its configuration and is given the
WebSocket address to use. So the destinations above are a **lower bound** —
anything that endpoint chooses to return is also a destination, and it does not
have to be the same one twice.

That last point is the reason this project reflashes rather than reconfigures.
There is no setting for it: the URL is compiled in, which is
[the first gotcha in the quickstart](quickstart.md).

### What the same scan finds here

One configured address — **yours** — plus the robot's own setup AP, an XML
namespace and a documentation link in a log message. No analytics, no telemetry,
no NTP server (the clock comes from your own server). The full output is
published as check 1 of [privacy.md](privacy.md), because a privacy claim you
cannot run yourself is a slogan.

---

## What this does *not* establish

- **Not what was actually sent.** No packet capture was made. A URL in a binary
  is a capability, not an event.
- **Not what is in the audio or camera path.** The strings say which services
  exist, not what data reaches them or how long anyone keeps it. M5Stack's own
  documentation names XiaoZhi as the provider and documents nothing about
  inference provider, retention, or the camera path — which is the gap this
  project was started over, and it is still a gap.
- **Not whether any of it is malicious.** Every endpoint above has an obvious
  legitimate purpose. "Undocumented" is not "hostile", and none of this is
  unusual for a consumer device.
- **Not current.** One unit, one firmware version, one point in time. A later
  factory build may differ, and the check above is two commands.

The argument for a local stack was never that the vendor is untrustworthy. It is
that **an always-on microphone should not require you to decide that question at
all.**
