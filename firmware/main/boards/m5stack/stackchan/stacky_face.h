/*
 * Stacky's face - drawn, not an emoji font.
 *
 * The stock LcdDisplay::SetEmotion() picks a glyph out of a colour emoji font
 * (lcd_display.cc:1119) and drops it in the middle of the screen. That works,
 * but it is a picture of a face rather than a face: it cannot blink, cannot
 * look anywhere, and does not react while he speaks.
 *
 * This draws a CARTOON face with LVGL primitives and animates it on a timer.
 * Nothing comes from the assets partition and nothing is stored in flash.
 *
 * THE LOOK: classic black-and-white cartoon, inverted for a black screen.
 * White sclera, near-black pupil, heavy white brows, a single-stroke mouth.
 * The accent colour survives as a violet BLOOM around the eyes rather than as
 * the eyes themselves.
 *
 * 🔴 Why the eyes are white and not cyan. The previous version made the whole
 *    eye a saturated cyan slab with a magenta pupil, and it read as two glowing
 *    lozenges rather than as eyes. A cartoon eye is legible because of the
 *    VALUE step between a bright sclera and a dark pupil - that contrast is the
 *    whole mechanism, and hue cannot substitute for it. Tinting the sclera
 *    flattens the step and the face stops reading as a face. So the features
 *    are monochrome and the colour lives in the glow, where it costs nothing.
 *
 * ANATOMY, per eye:
 *
 *   sclera  rounded rect, near-white, violet bloom. Its SHAPE is the squint.
 *   pupil   near-black disc inside it. Carries the GAZE.
 *   glint   a small white dot on the pupil. Cheap, and it is most of what
 *           separates "alive" from "printed".
 *   lids    two background-coloured rects eating in from top or bottom,
 *           clipped to the sclera's rounded corners.
 *
 * Plus, shared:
 *
 *   brows   an lv_line each, two points. THE BROWS CARRY THE EXPRESSION -
 *           in the reference art they do more work than the eyes do. Angling
 *           them needs arbitrary geometry, which is exactly what lv_line gives
 *           for free; doing it with rotated rectangles would force LVGL 9 to
 *           push each brow onto a transformed layer.
 *   mouth   TWO objects, one visible at a time. A closed mouth is an lv_line
 *           bowed into a smile or a frown; an open mouth is a ring - a
 *           background-filled ellipse with a white border. A filled shape
 *           would be wrong: on black, an open mouth is a HOLE, so it has to be
 *           drawn as an outline or it turns into a bright blob.
 *
 * 🔴 GAZE MOVES THE PUPIL, NOT THE EYE. Sliding whole eyeballs around the
 *    screen looks like a rendering bug; pupils drifting inside stationary eyes
 *    looks like someone glancing at something. It is also far cheaper - the
 *    pupil is small and has no shadow, while the sclera carries a 22px blurred
 *    bloom that is the most expensive thing on this screen to repaint.
 *
 * TWO INDEPENDENT INPUTS, kept separate on purpose:
 *
 *   SetEmotion()  <- the model, once per reply. Sets the EXPRESSION.
 *   SetStatus()   <- the framework, on every state change. Sets the MODE
 *                    (idle / listening / thinking / speaking).
 *
 *   Tangling them was tempting and would have been wrong: "happy" and
 *   "speaking" are both true at once, and the face has to show both.
 */
#ifndef STACKY_FACE_H
#define STACKY_FACE_H

#include "display/lcd_display.h"

#include <functional>

class StatusSource;

class StackyFace : public SpiLcdDisplay {
public:
    StackyFace(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
               int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy);
    ~StackyFace();

    void SetupUI() override;
    // Replaces the emoji-font path entirely. Unknown names fall back to neutral
    // rather than warning - the model invents emotion names, and "robot_2" from
    // application.cc:414 is in no font either.
    void SetEmotion(const char* emotion) override;
    // Chains to the base (the status label is still wanted) and sets mode_.
    void SetStatus(const char* status) override;

    // 🔴 WHERE "THINKING" ACTUALLY COMES FROM, and it is not a device state.
    //
    //    There is no kDeviceStateThinking. The device is in LISTENING from the
    //    moment you stop talking until the first audio arrives, which with a large
    //    local model is often five to ten seconds - he looks like he is waiting
    //    for you to speak while he is in fact busy. The face's kThinking mode
    //    existed but was wired to CONNECTING, so it almost never appeared.
    //
    //    The transition IS observable, just not as a state: the server sends
    //    `stt` with the transcript the instant your turn ends, and the
    //    framework turns that into SetChatMessage("user", ...). Everything
    //    after that until the first "assistant" message is think time.
    //
    //    Taken here rather than in application.cc on purpose - that file is
    //    upstream, and this needs no change to it.
    void SetChatMessage(const char* role, const char* content) override;

    // The head wants to know too, and the face is where the signal lands. The
    // board wires this to StackChanHead::SetThinking. Not the LED ring: that can
    // carry ambient STATUS, and an animation that masked an alert would trade
    // the point of the ring for a nicety.
    void SetOnThinking(std::function<void(bool)> cb) { on_thinking_ = std::move(cb); }

    // The screensaver rides the EXISTING sleep hook rather than adding a second
    // idea of "asleep". PowerSaveTimer already decides when he has been left
    // alone; this just gives that moment something to show.
    void SetPowerSaveMode(bool on) override;

    // Chains to the base and then resizes. The stock preview is deliberately
    // half-size - see the note in the .cc for why that is wrong here.
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;

    // Where the idle status screen's cards come from. May be null - with no
    // source there is no status screen, and the display simply dims when idle.
    void SetStatusSource(StatusSource* s) { status_ = s; }

    // 🔇 A MUTED ROBOT MUST LOOK MUTED. The mute survives a reboot, which is the
    //    honest behaviour and also the dangerous one: without a mark on the
    //    screen, the difference between "I switched his microphone off last
    //    week" and "he is broken" is a memory nobody has. Drawn over the face,
    //    so it is visible whenever he is.
    void SetMuted(bool muted);
    bool muted() const { return muted_; }

    // 🧠 AND A ROBOT WITH NOWHERE TO THINK MUST LOOK LIKE ONE.
    //
    //    A prebuilt binary ships pointing at an address that cannot resolve, so
    //    an owner who has not set theirs yet gets one startup alert and then a
    //    robot that looks perfectly well: he wakes, he listens, he blinks, and
    //    nothing happens - and the thing that would explain it scrolled away
    //    minutes ago. He only tries the server again when woken, so there is not
    //    even a repeating error to notice.
    //
    //    Same shape as the mute badge and for the same reason: the state is
    //    persistent, so the evidence for it has to be persistent too.
    void SetNoServer(bool no_server);

    // Public only so the expression table in the .cc can be a plain static
    // array at namespace scope. Nothing outside constructs these.
    struct EyeShape {
        uint8_t w, h;
        uint8_t radius;
        uint8_t lid_top;      // px of sclera eaten from the top
        uint8_t lid_bottom;
    };
    // Brow endpoint heights, in px below the brow baseline. Positive is LOWER.
    // inner is the end nearest the nose - inner-down is angry, inner-up is sad,
    // and that single sign flip is most of the emotional range.
    struct BrowShape {
        int8_t inner, outer;
    };
    struct FaceShape {
        EyeShape left, right;
        BrowShape brow_l, brow_r;
        int8_t gaze_x, gaze_y;
        uint8_t mouth_w;
        int8_t mouth_curve;   // vertical bow in px: + smile, - frown, 0 flat
        uint8_t mouth_open;   // 0 = use the line; else the ring's height
    };

    // 🎨 Re-apply the body's palette to widgets that already exist. Without it a
    //    new body only appears after a reboot, which reads as the setting not
    //    working. Called from the settings row, i.e. inside the LVGL task.
    void Repaint();

private:
    enum class Mode { kIdle, kListening, kThinking, kSpeaking };

    struct Eye {
        lv_obj_t* sclera = nullptr;
        lv_obj_t* pupil = nullptr;
        lv_obj_t* glint = nullptr;
        lv_obj_t* lid_top = nullptr;
        lv_obj_t* lid_bottom = nullptr;
    };

    // 🔴 What was last actually pushed to LVGL.
    //
    //    Every lv_obj_set_size()/lv_obj_align() invalidates the object's area
    //    and schedules a redraw - INCLUDING the blurred bloom, which is the
    //    most expensive thing here. Writing identical values again is not free
    //    and not a no-op.
    //
    //    Without this cache the face repainted 20x a second forever, even
    //    sitting perfectly still, and the CPU it stole was enough to make the
    //    I2C writes to the PY32 start NAKing - the LED ring visibly dropped
    //    pixels because of the screen. A static face must cost nothing.
    struct EyeState {
        int w = -1, h = -1, radius = -1, lid_top = -1, lid_bottom = -1;
        int pupil_w = -1, pupil_h = -1, pupil_dx = 0, pupil_dy = 0;
    };

    // BY VALUE, not by reference: the table is stored unscaled and the layout
    // factor is applied on the way out.
    static FaceShape ShapeFor(const char* emotion);

    // Assumes the display lock is held. Never fires the callback - the caller
    // does that after releasing, because it reaches the servos.
    void SetThinkingInternal(bool on);

    void BuildFace();
    void BuildScreensaver();
    // Applies muted_want_. Called from Tick(), inside the LVGL task.
    void ApplyMuteBadge();
    // Applies no_server_want_. Called from Tick(), inside the LVGL task.
    void ApplyNoServerBadge();
    // Repaints the visible card from the status source.
    void PaintCard();
    void MakeEye(lv_obj_t* parent, Eye& eye, int centre_x);
    lv_obj_t* MakeStroke(lv_obj_t* parent, int width_px);
    // Runs as an lv_timer, i.e. INSIDE the LVGL task, which already holds the
    // port lock. Do not take DisplayLockGuard here - see anim_timer_ below.
    void Tick();

    void ApplyEye(const Eye& eye, const EyeShape& s, int open_pct, int gx, int gy,
                  EyeState& cache);
    void ApplyBrows(const BrowShape& l, const BrowShape& r);
    void ApplyMouth(int w, int curve, int open_h);

    lv_obj_t* face_ = nullptr;
    lv_obj_t* mute_badge_ = nullptr;   // built lazily, the first time he is muted
    bool muted_ = false;
    // muted_want_ is written from whatever task flips the switch; muted_shown_
    // only from the LVGL task. Same split as saver_want_/saver_on_ below, and
    // for the same reason - see SetMuted.
    volatile bool muted_want_ = false;
    bool muted_shown_ = false;
    lv_obj_t* no_server_badge_ = nullptr;   // built lazily, like the mute badge
    volatile bool no_server_want_ = false;  // written from any task
    bool no_server_shown_ = false;          // read and written only by the LVGL task
    Eye left_, right_;
    lv_obj_t* brow_l_ = nullptr;
    lv_obj_t* brow_r_ = nullptr;
    lv_obj_t* mouth_line_ = nullptr;
    lv_obj_t* mouth_ring_ = nullptr;

    // lv_line does NOT copy the point array - it keeps the pointer. These must
    // outlive the widgets, so they are members rather than locals.
    lv_point_precise_t brow_l_pts_[2]{};
    lv_point_precise_t brow_r_pts_[2]{};
    lv_point_precise_t mouth_pts_[5]{};

    // 🔴 An lv_timer, NOT an esp_timer. This was an esp_timer first and it
    //    crashed the device.
    //
    //    esp_timer callbacks with ESP_TIMER_TASK dispatch all run on ONE shared
    //    task whose stack is CONFIG_ESP_TIMER_TASK_STACK_SIZE - 3584 bytes here.
    //    Driving LVGL from it means running the whole style/layout/invalidate
    //    path on 3.5KB, and that task is already shared with the LED ring
    //    animation and the 20ms FT6336 touch poll. Two failures came out of it:
    //    intermittent crashes, and - because a slow face tick delays everything
    //    else on that task - the PY32 I2C writes started NAKing.
    //
    //    lv_timer runs in the LVGL task instead: a real stack, no contention
    //    with the I2C work, and the port lock is ALREADY HELD when the callback
    //    fires - so Tick() must not try to take it again.
    lv_timer_t* anim_timer_ = nullptr;

    FaceShape shape_{};
    Mode mode_ = Mode::kIdle;

    // Set when the user's transcript arrives, cleared by the first assistant
    // message or any status change. Its only job is to stop SetStatus from
    // putting him back into the listening face on an unrelated status update
    // while he is still working.
    bool thinking_ = false;
    std::function<void(bool)> on_thinking_;

    int blink_countdown_ = 0;
    int blink_frame_ = -1;           // -1 = not blinking, else index into kBlink
    int gaze_x_ = 0, gaze_y_ = 0;
    int gaze_target_x_ = 0, gaze_target_y_ = 0;
    int gaze_countdown_ = 0;
    int mouth_h_ = 0;
    int tick_ = 0;

    EyeState last_l_, last_r_;
    int last_brow_[4] = {-99, -99, -99, -99};
    int last_mouth_w_ = -1, last_mouth_curve_ = -99, last_mouth_open_ = -1;

    // --- screensaver -------------------------------------------------------
    StatusSource* status_ = nullptr;
    lv_obj_t* saver_ = nullptr;        // full-screen overlay, hidden by default
    lv_obj_t* saver_label_ = nullptr;  // "MEMORY"
    lv_obj_t* saver_value_ = nullptr;  // "28.1/61.1 GB"
    lv_obj_t* saver_bar_ = nullptr;    // only for values shaped "used/total"
    lv_obj_t* saver_sub_ = nullptr;    // the original sentence
    lv_obj_t* saver_foot_ = nullptr;   // "updated 2 min ago"
    lv_obj_t* saver_dot_ = nullptr;    // health colour
    // saver_want_ is written from the esp_timer task, saver_on_ only from the
    // LVGL task. The gap between them is where the LVGL work safely happens.
    volatile bool saver_want_ = false;
    bool saver_on_ = false;
    int saver_index_ = 0;
    int saver_ticks_ = 0;
};

#endif  // STACKY_FACE_H
