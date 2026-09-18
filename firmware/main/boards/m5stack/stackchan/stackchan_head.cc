#include "stackchan_head.h"

#include "application.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <esp_random.h>
#include <esp_timer.h>

#include <cmath>
#include <algorithm>

#define TAG "StackChanHead"

namespace {

// 🔴 GLANCE AND HOLD, never sway.
//
//    The first version moved on EVERY step - a 300ms alternating left/right
//    with a tilt on a longer cycle. It was continuous, and continuous head
//    motion while talking does not read as alive, it reads as someone working
//    out a stiff neck. People mostly hold still while speaking and shift their
//    gaze now and then; the pauses are the part that looks natural, not the
//    movement.
//
//    So: pick a direction, take a slow half-second to get there, then HOLD for
//    a second or three before considering another. Roughly one glance every
//    two to four seconds, which at conversational length means two or three
//    per reply.
//
// The MCP tool allows +/-70 pan and +/-35 tilt; this stays well inside that.
// The servos are audible, so the motion has to sit under the speech.
constexpr float kGlancePan[] = {-11.0f, -6.0f, 0.0f, 6.0f, 11.0f};
constexpr int kGlancePanCount = sizeof(kGlancePan) / sizeof(kGlancePan[0]);
constexpr float kGlanceTilt[] = {-4.0f, 0.0f, 3.0f};
constexpr int kGlanceTiltCount = sizeof(kGlanceTilt) / sizeof(kGlanceTilt[0]);

// Slow enough to be a look rather than a flick.
constexpr int kGlanceMoveMinMs = 500;
constexpr int kGlanceMoveJitterMs = 400;
// And then stay put. This is what was missing.
constexpr int kGlanceHoldMinMs = 1200;
constexpr int kGlanceHoldJitterMs = 2600;

// Listening: chin up a touch. Attention reads as a small lift, held still.
constexpr float kListenTilt = 3.0f;

// How often the task wakes to look at the device state. Fast, because this is
// how quickly he reacts to starting and stopping talking; the glance timing is
// gated separately.
constexpr int kPollMs = 150;

// How long an explicit set_head_angles suppresses the sway.
constexpr int64_t kManualHoldUs = 6 * 1000 * 1000;

// Rail supervision, counted in poll steps rather than measured in time - the
// step loop is already the clock here. 200 x 150ms = 30 seconds.
//
// Not faster: this is one I2C byte read on the bus that also carries the LED
// ring, the touch controller and the codec, and hammering that bus is what made
// the PY32 writes NAK once already. Not slower: thirty seconds is about how long
// it takes to notice he is not moving and reach for the cable, and the point is
// to beat the person to it.
constexpr int kRailCheckSteps = 200;

}  // namespace

bool StackChanHead::Initialize() {
    ready_ = servo_.Initialize();
    if (!ready_) {
        ESP_LOGE(TAG, "servo bus init failed; head tools will report unavailable");
        return false;
    }
    // Byte order CONFIRMED on hardware 2026-08-04: with the servos resting at
    // their factory positions, get_head_angles read pan -1.2 / tilt -0.9, i.e.
    // ~zero. Big-endian (SCSCL) is correct. Commanded movement then worked in
    // the expected directions. The boot-time hold that used to be here existed
    // only until that was proven, so it is gone - centring on boot is now safe.
    servo_.CenterAll();
    ESP_LOGI(TAG, "head ready; centred");
    return true;
}

int StackChanHead::DegToCount(uint8_t id, float deg) {
    int zero = (id == SCS_ID_TILT) ? SCS_CENTER_TILT : SCS_CENTER_PAN;
    return zero + static_cast<int>(std::lround(deg / kDegreesPerCount));
}

float StackChanHead::CountToDeg(uint8_t id, int count) {
    int zero = (id == SCS_ID_TILT) ? SCS_CENTER_TILT : SCS_CENTER_PAN;
    return (count - zero) * kDegreesPerCount;
}

bool StackChanHead::SetAngles(float pan_deg, float tilt_deg, uint16_t time_ms) {
    if (!ready_) return false;
    servo_.WritePosition(SCS_ID_PAN,  DegToCount(SCS_ID_PAN,  pan_deg),  time_ms);
    servo_.WritePosition(SCS_ID_TILT, DegToCount(SCS_ID_TILT, tilt_deg), time_ms);
    return true;
}

bool StackChanHead::GetAngles(float& pan_deg, float& tilt_deg) {
    if (!ready_) return false;
    int p = servo_.ReadPosition(SCS_ID_PAN);
    int t = servo_.ReadPosition(SCS_ID_TILT);
    if (p < 0 || t < 0) return false;          // report failure, do not invent
    pan_deg  = CountToDeg(SCS_ID_PAN,  p);
    tilt_deg = CountToDeg(SCS_ID_TILT, t);
    return true;
}

void StackChanHead::NoteManualMove() {
    manual_until_us_ = esp_timer_get_time() + kManualHoldUs;
}

void StackChanHead::StartMotion() {
    // 🔴 STARTS EVEN WHEN THE HEAD IS DEAD, and it used to refuse to.
    //
    //    `if (!ready_) return;` reads as an obvious guard - no servos, nothing
    //    to animate - and it made a dead head PERMANENTLY dead: the motion task
    //    is where the rail supervisor runs, so the one situation that needs
    //    recovery was the one situation where the recovery never started. A
    //    head that failed to ping at boot because the rail was down would stay
    //    unavailable for the whole session even after the rail came back.
    //
    //    MotionStep() does nothing but supervise while !ready_, so this costs
    //    one I2C read every thirty seconds for the ability to heal.
    if (motion_task_ != nullptr) {
        return;
    }
    xTaskCreate(MotionTask, "head_motion", 3072, this, 2, &motion_task_);
    ESP_LOGI(TAG, "head motion running%s", ready_ ? "" : " (servos down - watching for the rail)");
}

void StackChanHead::MotionTask(void* arg) {
    // Let the boot settle before touching Application - this task is created
    // from the board constructor, which itself runs inside
    // Application::Initialize().
    vTaskDelay(pdMS_TO_TICKS(3000));
    auto* self = static_cast<StackChanHead*>(arg);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(self->MotionStep()));
    }
}

void StackChanHead::Startle() {
    if (!ready_) return;
    startle_stage_ = 1;
}

void StackChanHead::SuperviseRail() {
    if (!rail_check_) return;
    if (--rail_countdown_ > 0) return;
    rail_countdown_ = kRailCheckSteps;

    bool recovered = false;
    const bool on = rail_check_(&recovered);

    if (recovered) {
        ESP_LOGW(TAG, "servo rail had gone down; brought it back");
    }
    // Re-init on recovery, and also whenever the rail is on but the head is
    // not - that second case is a boot that came up before the rail did, and
    // retrying costs two pings.
    if ((recovered || on) && !ready_) {
        ready_ = servo_.Initialize();
        if (ready_) {
            servo_.CenterAll();
            last_state_ = -1;      // re-command the resting pose, do not assume
            ESP_LOGI(TAG, "head recovered; centred");
        }
    } else if (recovered && ready_) {
        // Powered back up at an unknown position, so put him straight.
        servo_.CenterAll();
        last_state_ = -1;
    }
}

int StackChanHead::MotionStep() {
    SuperviseRail();
    // Nothing else is possible without servos, but the supervisor above still
    // runs - this is the loop that gets him back.
    if (!ready_) return kPollMs;

    // 📷 Checked before EVERYTHING, startle included. A photo runs with exposure
    //    pinned at the whole frame length, and any motion during it smears the
    //    picture - so for that moment nothing gets to move the head, not a
    //    glance and not a status alert. last_state_ is invalidated so the resting
    //    pose is re-commanded when the hold lifts, rather than assumed to have
    //    survived it.
    if (hold_still_) {
        last_state_ = -1;
        return kPollMs;
    }

    const int state = static_cast<int>(Application::GetInstance().GetDeviceState());
    const int64_t now = esp_timer_get_time();

    // Checked FIRST, ahead of the manual-command window: a status alert outranks
    // whatever the head was last told to do.
    //
    // Each case commands one move and then returns slightly MORE than that
    // move's own duration, so the next stage starts from a settled position.
    // A servo re-commanded mid-travel just changes target, and a double-take
    // written that way collapses into one vague sweep.
    if (startle_stage_ > 0) {
        // Whatever mode he is in, treat it as new once this finishes, so the
        // resting pose is re-commanded rather than assumed still to hold.
        last_state_ = -1;
        const int stage = startle_stage_;
        startle_stage_ = stage + 1;
        switch (stage) {
            case 1: SetAngles(-30.0f, 10.0f, 200); return 240;
            case 2: SetAngles( 24.0f, 10.0f, 240); return 280;
            case 3: SetAngles(  0.0f,  6.0f, 300); return 340;
            default: startle_stage_ = 0; return kPollMs;
        }
    }

    // An explicit command wins for a while. Reset last_state_ so that whatever
    // comes next is treated as a fresh transition rather than a continuation.
    if (now < manual_until_us_) {
        last_state_ = -1;
        return kPollMs;
    }

    // 🔴 THE THINKING TELL. Below a startle, above everything else.
    //
    //    The complaint it answers: ask for a story, get "Once upon a time" on
    //    the screen, then five to ten seconds of a robot sitting perfectly
    //    still. PERFECT STILLNESS IS WHAT BROKEN LOOKS LIKE - nothing
    //    distinguishes a model composing a paragraph from a hung device, and
    //    the instinct is to reach for the cable.
    //
    //    Deliberately not a "busy" animation. A spinner says the machine is
    //    working; a breath and a look away says a PERSON is. The script is:
    //    settle - chin drops, the way someone does before a long answer - then
    //    look up and off to one side, then small drifts around that pose. All
    //    of it slow. Quick movement reads as agitation, and he is meant to look
    //    like he is composing, not panicking.
    //
    // ⚠️ SILENT ON PURPOSE, and an audible sigh was the tempting version.
    //    There is no echo-cancellation reference on this board
    //    (AUDIO_INPUT_REFERENCE = false), so sound he makes while a session is
    //    open is sound he hears himself - the exact mechanism that had him
    //    interrupting himself mid-sentence when digital gain went in. A visual
    //    tell costs nothing and cannot break the audio pipeline.
    if (thinking_) {
        last_state_ = -1;              // re-command the real pose when this ends
        const int stage = think_stage_ < 0 ? 0 : think_stage_;
        think_stage_ = stage + 1;
        switch (stage) {
            case 0:
                // The breath. Chin down, slowly.
                SetAngles(0.0f, -7.0f, 700);
                return 900;
            case 1:
                // And away, up and to one side. Direction picked once, so the
                // drifts that follow stay on the same side of the room.
                think_pan_ = (esp_random() % 2) ? -14.0f : 12.0f;
                SetAngles(think_pan_, 6.0f, 1100);
                return 1400;
            default: {
                // Small drifts around the held pose. Wide enough to read from
                // across the desk, slow enough not to look nervous.
                const float dp = think_pan_ + (static_cast<int>(esp_random() % 9) - 4);
                const float dt = 6.0f + (static_cast<int>(esp_random() % 5) - 2);
                SetAngles(dp, dt, 900);
                return 1600 + static_cast<int>(esp_random() % 1400);
            }
        }
    }
    if (think_stage_ >= 0) {
        // Just stopped. Straighten up before the reply starts, so the first
        // word does not arrive with his head still parked off to one side.
        think_stage_ = -1;
        last_state_ = -1;
        SetAngles(0.0f, 0.0f, 350);
        return 380;
    }

    if (state == kDeviceStateSpeaking) {
        // 🔴 The POLL stays fast and the GLANCE is what is gated on a
        //    timestamp. Returning the whole hold as the task delay was the
        //    obvious way to write this and it is wrong: a state change would
        //    then go unnoticed for up to three seconds, leaving him staring off
        //    to one side well after he had finished talking. Long sleeps and
        //    responsive state machines do not mix.
        if (last_state_ != state) {
            last_state_ = state;
            // Start the reply facing forward, then glance shortly after.
            next_glance_us_ = now + 700 * 1000;
            SetAngles(0.0f, 0.0f, 400);
            return kPollMs;
        }
        if (now >= next_glance_us_) {
            // Never glance to where he is already looking - that burns a whole
            // hold period looking like nothing happened.
            int pick = static_cast<int>(esp_random() % kGlancePanCount);
            if (pick == step_) {
                pick = (pick + 1) % kGlancePanCount;
            }
            step_ = pick;

            const int move_ms =
                kGlanceMoveMinMs + static_cast<int>(esp_random() % kGlanceMoveJitterMs);
            const int hold_ms =
                kGlanceHoldMinMs + static_cast<int>(esp_random() % kGlanceHoldJitterMs);
            SetAngles(kGlancePan[pick], kGlanceTilt[esp_random() % kGlanceTiltCount],
                      static_cast<uint16_t>(move_ms));
            next_glance_us_ = now + static_cast<int64_t>(move_ms + hold_ms) * 1000;
        }
        return kPollMs;
    }

    // Everything else is a HOLD, commanded once on the transition. Re-sending
    // the same position every step would keep the servos driving - audible, and
    // pointless when nothing is moving.
    if (state != last_state_) {
        last_state_ = state;
        if (state == kDeviceStateListening) {
            SetAngles(0.0f, kListenTilt, 450);
        } else {
            SetAngles(0.0f, 0.0f, 600);
        }
    }
    return kPollMs;
}

void StackChanHead::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    // Ranges here are the SAFE travel window converted to degrees, not the
    // servo's electrical range. The driver clamps again independently - the
    // model is not the last line of defence for the mechanism.
    constexpr int kPanLimitDeg  = 70;
    constexpr int kTiltLimitDeg = 35;

    mcp.AddTool(
        "self.robot.set_head_angles",
        "Move the robot's head. pan: left/right in degrees, negative is left, "
        "0 is straight ahead. tilt: up/down in degrees, negative is down. "
        "duration_ms: how long the movement should take.",
        PropertyList({
            Property("pan",         kPropertyTypeInteger, 0, -kPanLimitDeg,  kPanLimitDeg),
            Property("tilt",        kPropertyTypeInteger, 0, -kTiltLimitDeg, kTiltLimitDeg),
            Property("duration_ms", kPropertyTypeInteger, 400, 0, 5000),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            int pan  = p["pan"].value<int>();
            int tilt = p["tilt"].value<int>();
            int dur  = p["duration_ms"].value<int>();
            if (!SetAngles(static_cast<float>(pan), static_cast<float>(tilt),
                           static_cast<uint16_t>(dur))) {
                return std::string("head unavailable: servo bus did not initialise");
            }
            // Hold off the speaking sway, or this move is undone within 300ms.
            NoteManualMove();
            ESP_LOGI(TAG, "set_head_angles pan=%d tilt=%d in %dms", pan, tilt, dur);
            return true;
        });

    mcp.AddTool(
        "self.robot.get_head_angles",
        "Read where the robot's head is actually pointing, in degrees.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            float pan = 0, tilt = 0;
            if (!GetAngles(pan, tilt)) {
                // Say so rather than returning a plausible number. A confident
                // wrong answer is worse than an error - the same lesson the
                // server side taught us when it invented a status it never checked.
                return std::string("head position unavailable: no reply from servo bus");
            }
            char buf[96];
            snprintf(buf, sizeof(buf), "{\"pan\":%.1f,\"tilt\":%.1f}", pan, tilt);
            return std::string(buf);
        });

    mcp.AddTool(
        "self.robot.center_head",
        "Return the robot's head to its centre position.",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            if (!ready_) return std::string("head unavailable");
            Center();
            NoteManualMove();
            return true;
        });

    ESP_LOGI(TAG, "registered head MCP tools");
}
