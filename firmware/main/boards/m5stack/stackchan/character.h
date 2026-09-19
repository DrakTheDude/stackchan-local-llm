#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// 🎭 CHARACTER TOKENS - the numbers that make him a different robot to sit with.
//
// The idea is borrowed from a sibling project's design tokens, where it is made
// about pixels and holds exactly for time and movement:
//
//     `--r-unit: 0` squares every corner in the application with one number.
//     A flat resolved dict cannot express a scale.
//
// So a character does NOT carry a list of durations. It carries a unit, and the
// durations are computed from it at the point of use. One number moves all of
// them, and the gesture somebody adds next year moves with it without anybody
// remembering to update a character file.
//
// ⚠️ SMALLER IS FASTER. The unit multiplies TIME, exactly as the radius unit
//    multiplies radius. It reads backwards the first time - "caffeinated" is
//    0.5, not 2.0 - and it is the one naming trap in here.
//
// Status: motion only. The look tokens (27 constants in stacky_face.cc) come
// next, and the same rules apply to them.
namespace character {

// 🔴 THE CLAMPS ARE NOT STYLE, THEY ARE THE MECHANISM.
//
//    On a web page a motion unit of 0 means "no animation". Here it would mean
//    time_ms = 0, which the servo API documents as "as fast as it likes" - a
//    head slamming pan and tilt into a mechanical stop, repeatedly, on a desk.
//
//    A character is DATA. It arrives from a table today and from a file or a
//    network message tomorrow, so it is untrusted input and gets treated as
//    such. Same principle as the head's own limits: "the driver clamps again
//    independently - the model is not the last line of defence for the
//    mechanism."
constexpr int kMinMoveMs = 120;     // no character may move the head faster
constexpr int kMaxMoveMs = 6000;    // nor leave it crawling for a minute
constexpr float kMaxPanDeg = 45.0f;   // inside the head's own +/-70 safe window
constexpr float kMaxTiltDeg = 20.0f;  // inside its +/-35

struct Motion {
    // Multiplies every duration and interval: servo move time, glance hold,
    // and the idle timings that sit between them.
    float motion_unit = 1.0f;
    // Multiplies every amplitude: glance targets, the thinking look-away, the
    // listening lift.
    float gesture_unit = 1.0f;
};

// The character in force. One global because there is one robot and one face;
// when the settings menu and the server override arrive, they set this.
inline Motion& CurrentMotion() {
    static Motion m;
    return m;
}

// A base duration in milliseconds, through the character, clamped.
inline uint16_t MoveMs(int base_ms) {
    const float scaled = base_ms * CurrentMotion().motion_unit;
    const int ms = static_cast<int>(std::lround(scaled));
    return static_cast<uint16_t>(std::clamp(ms, kMinMoveMs, kMaxMoveMs));
}

// 🔴 CLAMP WITHOUT SCALING, for an EXPLICIT command.
//
//    Somebody asking for "pan 30 in 400ms" means exactly that; scaling it by a
//    character would answer a different question. The duration still gets a
//    floor, because the MCP tool that reaches here accepted duration_ms down to
//    ZERO - and zero is the servo API's "as fast as it likes".
inline uint16_t ClampMoveMs(int ms) {
    return static_cast<uint16_t>(std::clamp(ms, kMinMoveMs, kMaxMoveMs));
}

// An interval - a hold, a countdown - through the character. Unlike a move this
// may legitimately be short, so it has no floor beyond being positive: nothing
// mechanical depends on it.
inline int IntervalMs(int base_ms) {
    const float scaled = base_ms * CurrentMotion().motion_unit;
    const int ms = static_cast<int>(std::lround(scaled));
    return ms < 1 ? 1 : ms;
}

// An amplitude in degrees, through the character, clamped to the safe window.
inline float PanDeg(float base_deg) {
    return std::clamp(base_deg * CurrentMotion().gesture_unit, -kMaxPanDeg, kMaxPanDeg);
}

inline float TiltDeg(float base_deg) {
    return std::clamp(base_deg * CurrentMotion().gesture_unit, -kMaxTiltDeg, kMaxTiltDeg);
}

}  // namespace character
