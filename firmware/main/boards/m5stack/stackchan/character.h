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

// 🎨 THE LOOK HALF. A character's geometry is written in the SAME 128px design
//    space as the expression table, never in screen pixels - authoring in screen
//    pixels is resolving at authoring time, and it would make this token
//    impossible to express.
// 🎨 THE FACE'S OWN INK. Kept here rather than in the LVGL theme because these
//    are the cartoon's values, not the UI's - a sclera and a pupil are legible
//    because of the VALUE STEP between them, and that relationship has to hold
//    whatever the surrounding theme does.
struct Palette {
    // 🔴 SCLERA AND STROKE ARE SEPARATE, and they were one token until a light
    //    body proved they could not be. The sclera has to contrast with the
    //    PUPIL; the brows and mouth have to contrast with the GROUND. On a dark
    //    body both answers are "light" and the conflict is invisible - which is
    //    why the first light body came out with black eyes and white pupils.
    uint32_t sclera;  // the eye's field
    uint32_t stroke;  // brows and mouth, drawn against the ground
    uint32_t pupil;   // the value step against the sclera is what makes an eye read
    uint32_t glint;
    uint32_t glow;    // the accent, kept as a bloom rather than as a feature
    uint32_t ground;  // what the face is drawn on, and the screen behind it

    // The UI half. A body is the whole robot, so the chat text and panels come
    // with it - a face repainted onto somebody else's furniture is half a body.
    uint32_t text;
    uint32_t panel;   // assistant bubbles, cards

    // 🔑 The LED ring's resting colour, and NOT the screen's glow. An LED is a
    //    light source: a pale colour reads as white-with-a-hint at any real
    //    brightness, so the ring wants a saturated version of the same idea.
    //
    // ⚠️ Severity - ok, warn, alert, stale - is never a body's to set. A body
    //    that wants a different accent almost never wants a different meaning
    //    for "something is wrong".
    //
    // 🔴 AND CLASSIC'S RED IS A LATENT COLLISION, worth knowing before it bites.
    //    Alert is red too. Today that costs nothing: the severity colours belong
    //    to StatusSource, nothing is attached to it, and the ring always rests
    //    on the accent. The day an ambient status source lands, resting and
    //    alerting will differ by PATTERN alone - breathing against blinking -
    //    where today they differ by colour as well.
    //
    //    This red is deliberately deep and saturated, well away from the soft
    //    salmon alert uses, to keep as much of that distinction as there is. If
    //    it ever proves too thin, the answer is a second signal - the face
    //    already changes and the head can move - not a brighter red.
    uint32_t led;
};

struct Look {
    // Multiplies the whole face's geometry on top of the panel scale. 0.85 is a
    // smaller, more compact face; 1.1 is a broader one.
    float face_unit = 1.0f;
    // 🔑 SQUARE EVERYTHING WITH ONE NUMBER. FaceShape carries a per-eye radius,
    //    so 0 gives hard corners and 1 leaves them as drawn. This is the whole
    //    of a period identity - more of it than the palette is.
    float radius_unit = 1.0f;
};

// 📏 BOTH BOUNDS ARE MEASURED, not guessed - three builds, three captures off
//    the robot's own screen, looked at rather than reasoned about.
//
//    0.75 was tried and judged too small: the eyes lose presence and the brows
//    detach from them, because in this pass the LAYOUT does not scale with the
//    shape. So the floor sits above it.
//
//    At 1.12 the eyes nearly touch the brows, which is the right place to stop -
//    the table's own rule is that eye heights stay at or under 66 so the sclera
//    clears the brows above and the mouth below, and a character that can exceed
//    that draws a face on top of itself.
constexpr float kMinFaceUnit = 0.85f;
constexpr float kMaxFaceUnit = 1.12f;

inline Look& CurrentLook() {
    static Look l;
    return l;
}

// A design-space length, through the character. Rounded, because these become
// pixel counts and a half pixel is a blurred edge on a 320x240 panel.
inline int FaceLen(int design_px) {
    const float u = std::clamp(CurrentLook().face_unit, kMinFaceUnit, kMaxFaceUnit);
    return static_cast<int>(std::lround(design_px * u));
}

// A corner radius, through the body. Clamped at 0 because a negative radius is
// not a shape, and at the drawn value because a rounder-than-round eye is a
// circle with a different bug.
inline int FaceRadius(int design_px) {
    const float u = std::clamp(CurrentLook().radius_unit, 0.0f, 1.0f);
    return static_cast<int>(std::lround(FaceLen(design_px) * u));
}

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


// 🎭 A BODY: who he is. 🌡️ A MOOD: how he is right now.
//
// 🔴 THESE ARE TWO AXES AND THE FIRST VERSION OF THIS FILE HAD THEM AS ONE.
//
//    "Caffeinated Stacky is still Stacky - he just moves and talks fast."
//
//    Caffeinated and droopy modulate a robot; they do not replace him. A System
//    7 body IS a different robot - squared eyes, a white ground, a robot voice,
//    a grumpy desktop persona. Putting both on one list made "caffeinated
//    Classic" a contradiction, when it is obviously a thing somebody would want
//    to be.
//
//    The server's half of a BODY - voice and persona - is not here and must not
//    be. The id is the only thing both sides share.
struct Body {
    const char* id;
    const char* label;
    Palette palette;
    Look look;
};

struct Mood {
    const char* id;
    const char* label;
    Motion motion;
};

// 🔒 THE DEFAULT BODY IS LOCKED. This is the robot as he is, and every number in
//    it is load-bearing: the ink is inverted for a black screen, the pupil is
//    near-black rather than pure so it still reads, and the glow is an accent
//    kept as a bloom. Changing any of it changes every robot nobody has touched.
inline constexpr Body kBodies[] = {
    {"drax", "Drax",
     Palette{0xF2FAFF, 0xF2FAFF, 0x07080E, 0xFFFFFF, 0xA855FF, 0x000000,
             0xC9A9FF, 0x1A1430, 0xA17EFF},
     Look{1.00f, 1.00f}},

    // 🖥️ A DIFFERENT ROBOT, not a repainted one. Dark ink on a white ground and
    //    every corner square - the period identity is the geometry more than the
    //    palette, which is why radius_unit leads here.
    //
    //    Its voice and persona live on the server under this same id.
    {"classic", "Classic",
     Palette{0xFFFFFF, 0x0A0A0A, 0x111111, 0xFFFFFF, 0x808080, 0xE8E8E8,
             0x111111, 0xCFCFCF, 0xFF2A2A},
     Look{1.00f, 0.00f}},
};

inline constexpr Mood kMoods[] = {
    {"steady", "Steady", Motion{1.00f, 1.00f}},
    {"caffeinated", "Caffeinated", Motion{0.55f, 1.35f}},
    // 📐 These two ARE the instrument. Caffeinated and Five in the morning sit
    //    far enough apart that switching between them shows what is wired and
    //    what is not - a third entry existing only to be extreme would be one
    //    more thing to scroll past.
    {"fivebell", "Five in the morning", Motion{2.20f, 0.40f}},
};

inline constexpr int kBodyCount = sizeof(kBodies) / sizeof(kBodies[0]);
inline constexpr int kMoodCount = sizeof(kMoods) / sizeof(kMoods[0]);

inline Palette& CurrentPalette() {
    static Palette p = kBodies[0].palette;
    return p;
}

inline const char*& CurrentBodyId() {
    static const char* id = kBodies[0].id;
    return id;
}

inline const char*& CurrentMoodId() {
    static const char* id = kMoods[0].id;
    return id;
}

// Applied separately, because they are separate axes - changing your mood must
// not repaint you.
inline void ApplyBody(const Body& b) {
    CurrentPalette() = b.palette;
    CurrentLook() = b.look;
    CurrentBodyId() = b.id;
}

inline void ApplyMood(const Mood& m) {
    CurrentMotion() = m.motion;
    CurrentMoodId() = m.id;
}

inline bool SameId(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) return false;
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == '\0' && *b == '\0';
}

// nullptr for an unknown id: an id from an older build, or one a server knows
// and this firmware does not, falls back rather than guessing at a near match.
inline const Body* FindBody(const char* id) {
    for (const auto& b : kBodies) {
        if (SameId(b.id, id)) return &b;
    }
    return nullptr;
}

inline const Mood* FindMood(const char* id) {
    for (const auto& m : kMoods) {
        if (SameId(m.id, id)) return &m;
    }
    return nullptr;
}

}  // namespace character
