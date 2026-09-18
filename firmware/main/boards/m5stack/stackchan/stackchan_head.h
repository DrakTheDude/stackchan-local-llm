/*
 * Head control for the StackChan, exposed over device-side MCP.
 *
 * Tool NAMES deliberately match the factory firmware's, observed in its boot log:
 *     self.robot.get_head_angles
 *     self.robot.set_head_angles
 * Keeping the names identical means xiaozhi-esp32-server's device_mcp side needs
 * no change.
 *
 * Angles are presented in DEGREES to the model, because an LLM reasons about
 * "look left 30 degrees" far better than about raw 0..1023 servo counts. The
 * mapping to counts happens here.
 */
#ifndef STACKCHAN_HEAD_H
#define STACKCHAN_HEAD_H

#include "scs_servo.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <functional>

class StackChanHead {
public:
    bool Initialize();
    void RegisterMcpTools();

    // 🔴 THE SERVO RAIL CAN GO DOWN WHILE HE IS RUNNING, and when it does he
    //    keeps talking and never moves again until someone power-cycles him.
    //    Seen on the reference unit; the boot capture read
    //    `servo motor rail ON (was OFF)`, which is the whole story.
    //
    //    The rail lives on the PY32 over I2C, which the BOARD owns - the head
    //    only has a UART to the servos. So the board hands in a closure that
    //    checks and, if needed, re-asserts it. Called from the motion task, on
    //    a slow counter.
    //
    //    The callback returns true when the rail is on, and sets its argument
    //    when it had to bring it back - which is the head's cue to re-run its
    //    own init, since servos that lost power need re-centring and a head
    //    that failed to ping AT BOOT is sitting there disabled.
    void SetRailSupervisor(std::function<bool(bool*)> check) {
        rail_check_ = std::move(check);
    }

    // Degrees, 0 = centre (the factory zero_pos calibration).
    bool SetAngles(float pan_deg, float tilt_deg, uint16_t time_ms);
    bool GetAngles(float& pan_deg, float& tilt_deg);

    void Center() { servo_.CenterAll(); }

    // Idle presence: a small sway while he speaks, a slight lean while he
    // listens, centred and SILENT otherwise.
    //
    // 🔴 On its own FreeRTOS task, deliberately not on the shared esp_timer
    //    task. That task already carries the LED ring animation and the 20ms
    //    touch poll, its stack is CONFIG_ESP_TIMER_TASK_STACK_SIZE (3584), and
    //    overloading it is what crashed the device once already and starved the
    //    PY32 I2C writes into NAKing. The servos are on UART rather than the
    //    shared I2C bus, so this costs that bus nothing.
    void StartMotion();

    // Autonomous motion yields for a few seconds after an explicit command, so
    // the model asking the head to look somewhere is not immediately undone by
    // the next bop.
    void NoteManualMove();

    // A double-take: snap one way, back across, then settle facing forward.
    //
    // This is what a status alert looks like from across the room. It is a FLAG
    // rather than a sequence of blocking calls because the caller is the main
    // application task - the one that also drives audio and the protocol - and
    // three servo moves with holds is the better part of a second. The motion
    // task already exists, already owns the servos, and already runs a state
    // machine that returns its own delay; this just gives it something to do.
    //
    // Outranks the manual-command window, unlike the ordinary idle motion: if
    // something has gone red, that is more important than finishing whatever
    // the model last asked the head to do.
    void Startle();

    // He is working on a reply, and there is no device state that says so - the
    // device sits in LISTENING from the moment you stop talking until the first
    // audio arrives, which on a 32B is regularly five to ten seconds. The face
    // spots it (see StackyFace::SetChatMessage) and the board forwards it here.
    //
    // Written from the main task, read by the motion task - same treatment as
    // startle_stage_: one aligned word whose reader tolerates either value.
    void SetThinking(bool on) { thinking_ = on; }

    // 📷 Face front and hold absolutely still, for a photo.
    //
    // Not only so he looks at the person: a still on this sensor runs with
    // exposure pinned at the whole frame length, which is long enough that a
    // head drifting through a glance during capture smears the picture. Holding
    // still is part of the exposure, the same way it is on any camera with a
    // slow shutter.
    //
    // Same volatile-flag treatment as thinking_ and startle_stage_ - written by
    // whichever task is taking the photo, read by the motion task.
    void HoldStill(bool on) { hold_still_ = on; }

private:
    static void MotionTask(void* arg);
    // Returns how long to wait before the next step, in ms.
    int MotionStep();
    // The slow half of MotionStep: poll the rail, recover the head if it came
    // back. Separate because it is the only part that runs when !ready_.
    void SuperviseRail();

    ScsServo servo_;
    bool ready_ = false;

    std::function<bool(bool*)> rail_check_;
    int rail_countdown_ = 0;

    TaskHandle_t motion_task_ = nullptr;
    // Written from the main task, read and cleared by the motion task. A single
    // aligned int, set to a value the reader only ever counts up from - no lock
    // buys anything here, and taking one on the main task would.
    volatile int startle_stage_ = 0;
    volatile bool thinking_ = false;
    volatile bool hold_still_ = false;
    int think_stage_ = -1;      // -1 = not in the thinking script
    float think_pan_ = 0.0f;
    int64_t manual_until_us_ = 0;
    int64_t next_glance_us_ = 0;
    int step_ = 0;              // index of the direction he is currently facing
    int last_state_ = -1;

    // SCS servos are ~0.29 deg per count (300 deg over 1024 counts). Kept as a
    // named constant so it is obvious what to correct if bench measurement
    // disagrees - this figure is from the family datasheet, not measured on
    // this unit.
    static constexpr float kDegreesPerCount = 300.0f / 1024.0f;

    static int  DegToCount(uint8_t id, float deg);
    static float CountToDeg(uint8_t id, int count);
};

#endif  // STACKCHAN_HEAD_H
