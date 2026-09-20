/*
 * An ambient status source that polls ONE MCP tool.
 *
 * This is the producer half of status_source.h. The consumers - the LED ring,
 * the idle screen, the spoken alert - were written first and work with any
 * implementation of that interface; this is the one most people will use, and
 * the one the reference robot uses.
 *
 * It is OFF unless a URL has been stored. No URL, no task, no polling, no
 * traffic. That is the default.
 *
 * -- THE CONTRACT ------------------------------------------------------------
 *
 * The robot sends ONE HTTP POST, containing one JSON-RPC 2.0 `tools/call` for
 * the configured tool (default `status.get`) with no arguments, and expects one
 * JSON reply. That is the whole protocol.
 *
 * 🔴 ONE STATELESS POST - no `initialize`, no session id, no SSE stream. A
 *    server that requires a handshake before it will answer is not usable here.
 *    This is a deliberate floor, not an omission: an ambient poller that has to
 *    hold a session is a thing that can silently lose one, and that failure
 *    would look like a green ring rather than an error. tools/status-server/ is
 *    a complete example of a server that meets this in about a hundred lines.
 *
 * The tool's text content should itself be JSON:
 *
 *   {
 *     "level":   "ok" | "warn" | "alert",
 *     "summary": "All healthy - 3 nodes, 2 VRMs.",
 *     "cards":   [ {"label": "MEMORY", "value": "28.1 / 61.1 GB",
 *                   "sub": "used of total"}, ... ]
 *   }
 *
 *   level     Required. Anything else - missing, misspelt, null - is UNKNOWN,
 *             which mutes the ring. It is never guessed from the summary.
 *   summary   Required. One sentence, because it can be SPOKEN: on a change of
 *             level he says it out loud, unprompted. Write it to be heard.
 *   cards     Optional, first 6 used. A value shaped "a / b" also draws a bar.
 *
 * If the text is not JSON at all, the whole of it becomes the summary and the
 * level is UNKNOWN. That makes any existing tool that returns a sentence usable
 * at reduced function instead of failing outright - you get the words on the
 * idle screen, you do not get a colour, and the colour is the part that would
 * have been a guess.
 *
 * 🔴 READ-ONLY. This calls one named tool and never anything else. Do not add a
 *    call here that changes something: an ambient display has no confirmation
 *    step, and neither does the voice path that shares the ring with it.
 *
 * 🔴 STALENESS IS PART OF THE CONTRACT. age_seconds() is honest, including when
 *    nothing has ever been read (-1). A confidently green ring in front of a
 *    server that stopped answering twenty minutes ago is worse than no ring.
 *
 * 🔴 THE TOKEN LIVES IN NVS AND NOWHERE ELSE. Not in this repository, not in a
 *    header, not in a build file. It is sent over USB serial, which already
 *    requires holding the robot. Be honest about what that buys: NVS is not
 *    encrypted here, so whoever holds the robot can read it back out of flash.
 *    Use a read-only, revocable, LAN-scoped credential and nothing more.
 *
 * Provisioning, one line at a time over the USB serial console:
 *
 *   STATUS_URL http://10.0.0.5:8899/mcp    where to poll. Enables the feature
 *   STATUS_TOOL fleet.status               default status.get
 *   STATUS_TOKEN <secret>                  optional; sent as a Bearer header
 *   STATUS_SECS 180                        poll interval, 30..3600
 *   STATUS_CA -----BEGIN CERTIFICATE-----\n...   a private CA, \n escaped
 *   STATUS_SHOW                            print the config. Never the token
 *   STATUS_OFF                             forget all of it
 *
 * https:// verifies against the built-in public CA bundle, or against STATUS_CA
 * if one is stored. http:// is fine on a LAN you trust - but a token sent over
 * http:// is a token sent in the clear, so do not pair the two.
 */
#ifndef MCP_STATUS_SOURCE_H
#define MCP_STATUS_SOURCE_H

#include "status_source.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <mutex>

class McpStatusSource : public StatusSource {
public:
    // True when a URL has been stored. Nothing starts without one.
    static bool Configured();
    // Reads provisioning lines from the USB serial console, for the life of the
    // device. Safe to call when the feature is off - that is how it gets turned
    // on.
    static void ProvisionFromSerial();

    void Start();

    Level level() const override;
    std::string summary() const override;
    std::vector<Card> cards() const override;
    int64_t age_seconds() const override;
    void SetOnUpdate(std::function<void()> cb) override { on_update_ = std::move(cb); }

private:
    static void PollTask(void* arg);
    static void SerialTask(void*);
    static void HandleProvisionLine(const std::string& line);

    bool PollOnce();
    // The tool's text content, or empty on any failure.
    std::string CallTool();
    // Fills the fields from that text. False when there was nothing usable.
    bool Parse(const std::string& text);

    mutable std::mutex mutex_;
    Level level_ = Level::kUnknown;
    std::string summary_;
    std::vector<Card> cards_;
    int64_t last_ok_us_ = 0;
    TaskHandle_t task_ = nullptr;
    std::function<void()> on_update_;
};

#endif  // MCP_STATUS_SOURCE_H
