/*
 * Where the robot's ambient status comes from.
 *
 * Three things read from one of these: the LED ring's idle colour, the idle
 * status screen, and spoken alerts when the level changes. What sits behind it
 * is up to the owner - the intended implementation is a small MCP client that
 * polls a status tool on any MCP server (docs/roadmap.md, "Integration
 * interface").
 *
 * This build attaches NO source, and every consumer handles that:
 *
 *   LED ring      breathes lavender, exactly as it does before a first reading
 *   idle screen   not shown - the display simply dims when idle
 *   alerts        never fire
 *
 * 🔴 STALENESS IS PART OF THE CONTRACT. A confidently green ring in front of a
 *    source that stopped answering twenty minutes ago is worse than no ring at
 *    all. Consumers show the age of a reading and fall back to muted once it
 *    is old, so age_seconds() must be honest.
 *
 * 🔴 READ-ONLY. A status source reports; it never changes anything. Voice and
 *    ambient displays have no confirmation step.
 */
#ifndef STATUS_SOURCE_H
#define STATUS_SOURCE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class StatusSource {
public:
    // Ordered by severity, so `now > before` means "got worse".
    enum class Level { kUnknown, kOk, kWarn, kAlert };

    // One idle-screen card: a big value under a small label.
    struct Card {
        std::string label;   // "MEMORY"
        std::string value;   // "28.1 / 61.1 GB" - a "used/total" value also draws a bar
        std::string sub;     // one line of detail
    };

    virtual ~StatusSource() = default;

    // The worst level across everything the source watches.
    virtual Level level() const = 0;
    // One speakable sentence, e.g. "All healthy - 3 nodes."
    virtual std::string summary() const = 0;
    // Cards for the idle screen, shown in rotation. May be empty.
    virtual std::vector<Card> cards() const = 0;
    // Seconds since the last successful reading, or -1 if there has never been one.
    virtual int64_t age_seconds() const = 0;
    // Called after every successful reading, from the source's own task. The
    // ring only repaints on a device-state change, so without this a change
    // that happens while he sits idle would not show until the next
    // conversation - exactly the window an ambient display exists to cover.
    virtual void SetOnUpdate(std::function<void()> cb) = 0;
};

#endif  // STATUS_SOURCE_H
