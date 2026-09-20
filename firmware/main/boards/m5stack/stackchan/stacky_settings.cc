#include "stacky_settings.h"

#include "lvgl_display/lvgl_theme.h"

#include <esp_log.h>

#include <string>

#define TAG "StackySettings"

namespace {

// 320x240, and the finger is the pointer. 44px rows are the smallest that can be
// hit reliably without looking - below that you start aiming, and a settings
// menu you have to aim at is worse than no settings menu.
constexpr int kRowHeight = 44;
constexpr int kPad = 8;

// Every event callback gets the StackySettings through the button's user data,
// so none of this needs a singleton.
struct RowCtx {
    StackySettings* self;
};

// 📐 One trim button's worth of intent. LVGL hands a callback exactly one
//    user-data pointer, and the trim buttons need two facts each - who to call
//    and which way - so each button points at one of these instead.
struct Nudge {
    StackySettings* self = nullptr;
    int d_pan = 0;
    int d_tilt = 0;
    bool save = false;   // only meaningful for the two leave buttons
};

// Counts per tap. One count is about a third of a degree, which is finer than
// anybody can see and would make crossing the range forty taps; two is a
// visible step and twenty taps end to end.
constexpr int kTrimStep = 2;

}  // namespace

void StackySettings::Build(const Actions& actions) {
    actions_ = actions;

    // 🎨 From the theme, not from literals. See the header.
    auto* theme = LvglThemeManager::GetInstance().GetTheme("dark");
    if (theme != nullptr) {
        c_bg_ = theme->background_color();
        c_panel_ = theme->assistant_bubble_color();
        c_text_ = theme->text_color();
        c_dim_ = theme->system_text_color();
        c_accent_ = theme->border_color();
    } else {
        // Only reachable if the theme was never registered, which would mean the
        // display never came up either. Legible rather than correct.
        c_bg_ = lv_color_black();
        c_panel_ = lv_color_hex(0x202020);
        c_text_ = lv_color_white();
        c_dim_ = lv_color_hex(0x909090);
        c_accent_ = lv_color_hex(0x808080);
    }
    // Said out loud because the first build came out in LVGL's default blue, and
    // "the theme is wrong" and "the theme was never read" look identical on a
    // panel across the room.
    ESP_LOGI(TAG, "theme %s: bg=%06X panel=%06X text=%06X accent=%06X",
             theme != nullptr ? "found" : "MISSING",
             (unsigned)lv_color_to_u32(c_bg_) & 0xFFFFFF,
             (unsigned)lv_color_to_u32(c_panel_) & 0xFFFFFF,
             (unsigned)lv_color_to_u32(c_text_) & 0xFFFFFF,
             (unsigned)lv_color_to_u32(c_accent_) & 0xFFFFFF);

    root_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, c_bg_, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    BuildList();
    BuildAbout();
    lv_obj_add_flag(about_, LV_OBJ_FLAG_HIDDEN);
    BuildTrim();
    lv_obj_add_flag(trim_, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "settings menu built (hidden)");
}

lv_obj_t* StackySettings::AddRow(const char* text, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_button_create(list_);
    // 🎨 STRIP THE BUILT-IN STYLE FIRST. lv_button_create arrives dressed in
    //    LVGL's default theme - a blue fill with a gradient and a shadow - and
    //    setting a background colour on top of that leaves the rest of it in
    //    place. The first build of this menu came out looking like stock LVGL
    //    rather than like this robot. Start from nothing, then say everything.
    lv_obj_remove_style_all(btn);
    lv_obj_set_size(btn, LV_PCT(100), kRowHeight);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, c_panel_, 0);
    lv_obj_set_style_bg_color(btn, c_accent_, LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 0, 0);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, c_text_, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 4, 0);

    static RowCtx ctx;   // one shared context is enough: `self` is a singleton
    ctx.self = this;     // per board, and the callbacks only ever need `self`.
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, this);
    return btn;
}

// The row's label is its first child - see AddRow, which creates exactly one.
// Kept here rather than inline so that assumption lives next to the code that
// makes it true.
void StackySettings::SetRowLabel(lv_obj_t* row, const char* prefix, const char* label) {
    if (row == nullptr || label == nullptr) return;
    lv_obj_t* text = lv_obj_get_child(row, 0);
    if (text != nullptr) {
        lv_label_set_text_fmt(text, "%s: %s", prefix, label);
    }
}

lv_obj_t* StackySettings::AddSlider(const char* text, int value, lv_event_cb_t cb,
                                    lv_obj_t** out_value_label) {
    // ⚠️ A SLIDER IS TALLER THAN ITS TRACK. The knob is drawn centred on the
    //    track and overhangs it by roughly its own radius at top and bottom, so
    //    a row sized to the track clips the knob - which is exactly the part you
    //    are trying to touch. The first version did that. The row now reserves
    //    room for the knob, and the slider sits inside that padding rather than
    //    flush against the bottom edge.
    constexpr int kKnob = 14;   // knob overhang either side of the track
    lv_obj_t* row = lv_obj_create(list_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kRowHeight + kKnob + 16);
    lv_obj_set_style_bg_color(row, c_panel_, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_set_style_pad_ver(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, c_text_, 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t* val = lv_label_create(row);
    lv_label_set_text_fmt(val, "%d", value);
    lv_obj_set_style_text_color(val, c_dim_, 0);
    lv_obj_align(val, LV_ALIGN_TOP_RIGHT, 0, 0);
    if (out_value_label != nullptr) *out_value_label = val;

    lv_obj_t* slider = lv_slider_create(row);
    lv_obj_remove_style_all(slider);
    lv_obj_set_width(slider, LV_PCT(100));
    lv_obj_set_height(slider, 8);
    // Lifted clear of the bottom edge by the knob's overhang.
    lv_obj_align(slider, LV_ALIGN_BOTTOM_MID, 0, -(kKnob / 2));
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);

    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, c_bg_, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, c_accent_, LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, c_text_, LV_PART_KNOB);
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, kKnob / 2, LV_PART_KNOB);

    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, this);
    return slider;
}

// A switch row. The label says what the switch CONTROLS, and the switch reads on
// = the thing is working - never "Mute: on", which leaves you working out what
// the switch being off would mean.
lv_obj_t* StackySettings::AddToggle(const char* text, bool on, lv_event_cb_t cb,
                                    lv_obj_t** out_switch) {
    lv_obj_t* row = lv_obj_create(list_);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), kRowHeight);
    lv_obj_set_style_bg_color(row, c_panel_, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 6, 0);
    lv_obj_set_style_pad_hor(row, 8, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* label = lv_label_create(row);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, c_text_, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t* sw = lv_switch_create(row);
    lv_obj_remove_style_all(sw);
    lv_obj_set_size(sw, 50, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, c_bg_, LV_PART_MAIN);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    // The cast is not cosmetic: LVGL's selector is a part OR'd with a state, but
    // the two are separate enum types, and -Werror rejects mixing them.
    const lv_style_selector_t kIndicatorChecked =
        static_cast<lv_style_selector_t>(LV_PART_INDICATOR) |
        static_cast<lv_style_selector_t>(LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, kIndicatorChecked);
    lv_obj_set_style_bg_color(sw, c_accent_, kIndicatorChecked);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, c_text_, LV_PART_KNOB);
    lv_obj_set_style_radius(sw, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, this);
    if (out_switch != nullptr) *out_switch = sw;
    return sw;
}

void StackySettings::BuildList() {
    list_ = lv_obj_create(root_);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(list_, kPad, 0);
    lv_obj_set_style_pad_row(list_, kPad, 0);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_opa(list_, LV_OPA_TRANSP, 0);
    // More rows than fit on 240px, so it scrolls - vertically only, or a
    // sideways thumb-drag on a slider would carry the whole list with it. The
    // extra bottom padding is so the last row can be dragged clear of the edge
    // rather than sitting half off the screen at the end of the scroll.
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_style_pad_bottom(list_, kRowHeight, 0);

    // A title, so it is obvious this is not part of the conversation.
    lv_obj_t* title = lv_label_create(list_);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_color(title, c_dim_, 0);

    AddRow("Wi-Fi & server", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        // Hide first: entering config mode repaints the screen underneath, and
        // leaving the menu on top of it would strand the user in a dead list.
        self->Hide();
        if (self->actions_.wifi_setup) self->actions_.wifi_setup();
    });

    const int vol = actions_.get_volume ? actions_.get_volume() : 50;
    AddSlider("Volume", vol, [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int v = lv_slider_get_value(slider);
        if (self->volume_value_) lv_label_set_text_fmt(self->volume_value_, "%d", v);
        if (self->actions_.set_volume) self->actions_.set_volume(v);
    }, &volume_value_);

    const int bright = actions_.get_brightness ? actions_.get_brightness() : 75;
    AddSlider("Brightness", bright, [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        auto* slider = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const int v = lv_slider_get_value(slider);
        if (self->bright_value_) lv_label_set_text_fmt(self->bright_value_, "%d", v);
        if (self->actions_.set_brightness) self->actions_.set_brightness(v);
    }, &bright_value_);

    // 🔇 THE PRIVACY SWITCHES, phrased as what they enable rather than what they
    //    suppress. "Microphone: on" is unambiguous from across the room;
    //    "Mute: off" is a double negative you have to stop and unpick, about the
    //    one setting nobody should have to think twice about.
    const bool mic_on = actions_.get_mic_muted ? !actions_.get_mic_muted() : true;
    AddToggle("Microphone", mic_on, [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (self->actions_.set_mic_muted) self->actions_.set_mic_muted(!on);
    }, &mic_switch_);

    const bool cam_on = actions_.get_camera_off ? !actions_.get_camera_off() : true;
    AddToggle("Camera", cam_on, [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        auto* sw = static_cast<lv_obj_t*>(lv_event_get_target(e));
        const bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
        if (self->actions_.set_camera_off) self->actions_.set_camera_off(!on);
    }, &cam_switch_);

    AddRow("Run self-check", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        if (self->actions_.self_check) self->actions_.self_check();
    });

    // 🎭 THE CHARACTER ROW. Tapping steps to the next one and relabels itself,
    //    so which character is in force is readable without opening anything -
    //    the two halves of a character can drift, and being able to see what
    //    this half thinks it is wearing is how anybody notices.
    body_row_ = AddRow("Body", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        if (!self->actions_.next_body) return;
        self->SetRowLabel(self->body_row_, "Body", self->actions_.next_body());
    });
    if (actions_.body_label) SetRowLabel(body_row_, "Body", actions_.body_label());

    mood_row_ = AddRow("Mood", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        if (!self->actions_.next_mood) return;
        self->SetRowLabel(self->mood_row_, "Mood", self->actions_.next_mood());
    });
    if (actions_.mood_label) SetRowLabel(mood_row_, "Mood", actions_.mood_label());

    AddRow("Motion check", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        if (self->actions_.motion_check) self->actions_.motion_check();
    });

    // 🕐 The two idle timings. Worded as what you will SEE rather than as what
    //    the timer is called: "Screen dims" and "Power off", not "sleep" and
    //    "shutdown" - and the second says `on battery` because on USB it never
    //    happens, and a setting that quietly does nothing is worse than one
    //    that is not offered.
    dim_row_ = AddRow("Screen dims", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        if (!self->actions_.next_dim) return;
        self->SetRowLabel(self->dim_row_, "Screen dims", self->actions_.next_dim());
    });
    if (actions_.dim_label) SetRowLabel(dim_row_, "Screen dims", actions_.dim_label());

    off_row_ = AddRow("Power off on battery", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        if (!self->actions_.next_off) return;
        self->SetRowLabel(self->off_row_, "Power off on battery",
                          self->actions_.next_off());
    });
    if (actions_.off_label) SetRowLabel(off_row_, "Power off on battery", actions_.off_label());

    // 📐 Below Motion check, because they are the same question asked twice:
    //    one shows you how he moves, the other fixes where "straight" is.
    AddRow("Head trim", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        if (self->actions_.get_trim) {
            const auto now = self->actions_.get_trim();
            self->trim_was_pan_ = now.first;
            self->trim_was_tilt_ = now.second;
        }
        self->ShowTrim();
        lv_obj_add_flag(self->list_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(self->trim_, LV_OBJ_FLAG_HIDDEN);
        // Centre him as the page opens, so the first thing you see is the pose
        // you are about to judge rather than wherever he happened to be looking.
        if (self->actions_.nudge_trim) self->actions_.nudge_trim(0, 0);
    });

    AddRow("About", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        self->FillAbout();
        lv_obj_add_flag(self->list_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(self->about_, LV_OBJ_FLAG_HIDDEN);
    });

    AddRow("Close", [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        self->Hide();
    });
}

// 📐 THE TRIM PAGE.
//
//    Two rows of [-] value [+], and Save / Back. Deliberately NOT a slider: the
//    whole range is ±40 counts, so a slider spanning the screen moves several
//    counts per pixel of finger - and the adjustment is "one more, one more,
//    stop", not a value you aim at.
//
//    The head moves on every tap, which is the whole point. You are not reading
//    a number, you are looking at a robot and deciding whether he is straight;
//    the number is there so you can write it down.
void StackySettings::BuildTrim() {
    trim_ = lv_obj_create(root_);
    lv_obj_remove_style_all(trim_);
    lv_obj_set_size(trim_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(trim_, kPad, 0);
    lv_obj_set_style_pad_row(trim_, kPad, 0);
    lv_obj_set_flex_flow(trim_, LV_FLEX_FLOW_COLUMN);
    // Opaque, for the reason spelled out in BuildAbout.
    lv_obj_set_style_bg_color(trim_, c_bg_, 0);
    lv_obj_set_style_bg_opa(trim_, LV_OPA_COVER, 0);

    lv_obj_t* hint = lv_label_create(trim_);
    lv_label_set_text(hint, "Nudge until he looks straight at you.");
    lv_obj_set_style_text_color(hint, c_dim_, 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_PCT(100));

    for (int axis = 0; axis < 2; axis++) {
        lv_obj_t* row = lv_obj_create(trim_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                              LV_FLEX_ALIGN_CENTER);

        for (int half = 0; half < 2; half++) {
            if (half == 1) {
                // The label sits between the two buttons, and carries both the
                // axis name and the value: "PAN  +6". One label is enough, and
                // two would have to agree about alignment.
                lv_obj_t* value = lv_label_create(row);
                lv_obj_set_style_text_color(value, c_text_, 0);
                lv_obj_set_flex_grow(value, 1);
                lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_CENTER, 0);
                if (axis == 0) trim_pan_value_ = value; else trim_tilt_value_ = value;
            }
            lv_obj_t* b = lv_button_create(row);
            // See AddRow: the default button style is a blue gradient, and
            // colouring over it leaves the gradient in place.
            lv_obj_remove_style_all(b);
            lv_obj_set_size(b, 56, kRowHeight);
            lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
            lv_obj_set_style_bg_color(b, c_panel_, 0);
            lv_obj_set_style_bg_color(b, c_accent_, LV_STATE_PRESSED);
            lv_obj_set_style_radius(b, 6, 0);
            lv_obj_t* l = lv_label_create(b);
            lv_label_set_text(l, half == 0 ? "-" : "+");
            lv_obj_set_style_text_color(l, c_text_, 0);
            lv_obj_center(l);
            // 🔴 The button has to carry WHICH nudge it is, and LVGL gives a
            //    callback one user-data pointer, which is already `self`. So
            //    the four deltas live here, in static storage, and each button
            //    points at its own. Packing them into the pointer itself was
            //    the first version and it is unreadable a week later.
            static Nudge nudges[4];
            Nudge& n = nudges[axis * 2 + half];
            n.self = this;
            const int step = half == 0 ? -kTrimStep : kTrimStep;
            n.d_pan = axis == 0 ? step : 0;
            n.d_tilt = axis == 1 ? step : 0;
            lv_obj_add_event_cb(b, [](lv_event_t* e) {
                auto* n = static_cast<Nudge*>(lv_event_get_user_data(e));
                if (n->self->actions_.nudge_trim) {
                    n->self->actions_.nudge_trim(n->d_pan, n->d_tilt);
                }
                n->self->ShowTrim();
            }, LV_EVENT_CLICKED, &n);
        }
    }

    lv_obj_t* buttons = lv_obj_create(trim_);
    lv_obj_remove_style_all(buttons);
    lv_obj_set_size(buttons, LV_PCT(100), kRowHeight);
    lv_obj_set_flex_flow(buttons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(buttons, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    auto leave = [this, buttons](const char* text, bool save) {
        lv_obj_t* b = lv_button_create(buttons);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, LV_PCT(48), kRowHeight);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(b, save ? c_accent_ : c_panel_, 0);
        lv_obj_set_style_bg_color(b, c_accent_, LV_STATE_PRESSED);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_t* l = lv_label_create(b);
        lv_label_set_text(l, text);
        lv_obj_set_style_text_color(l, c_text_, 0);
        lv_obj_center(l);
        static Nudge leaves[2];
        Nudge& n = leaves[save ? 1 : 0];
        n.self = this;
        n.save = save;
        lv_obj_add_event_cb(b, [](lv_event_t* e) {
            auto* n = static_cast<Nudge*>(lv_event_get_user_data(e));
            StackySettings* self = n->self;
            if (self->actions_.close_trim) {
                self->actions_.close_trim(n->save, self->trim_was_pan_, self->trim_was_tilt_);
            }
            lv_obj_add_flag(self->trim_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(self->list_, LV_OBJ_FLAG_HIDDEN);
        }, LV_EVENT_CLICKED, &n);
    };
    leave("Back", false);
    leave("Save", true);
}

void StackySettings::ShowTrim() {
    if (trim_pan_value_ == nullptr || !actions_.get_trim) return;
    const auto now = actions_.get_trim();
    lv_label_set_text_fmt(trim_pan_value_, "PAN  %+d", now.first);
    lv_label_set_text_fmt(trim_tilt_value_, "TILT  %+d", now.second);
}

void StackySettings::BuildAbout() {
    about_ = lv_obj_create(root_);
    lv_obj_remove_style_all(about_);
    lv_obj_set_size(about_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_pad_all(about_, kPad, 0);
    lv_obj_set_style_pad_row(about_, kPad, 0);
    lv_obj_set_flex_flow(about_, LV_FLEX_FLOW_COLUMN);
    // Opaque, not transparent. The page it replaces is only hidden, and an
    // overlay you can see through is how a stale row ends up ghosting behind
    // the one you are reading.
    lv_obj_set_style_bg_color(about_, c_bg_, 0);
    lv_obj_set_style_bg_opa(about_, LV_OPA_COVER, 0);

    about_rows_ = lv_obj_create(about_);
    lv_obj_remove_style_all(about_rows_);
    lv_obj_set_width(about_rows_, LV_PCT(100));
    lv_obj_set_flex_grow(about_rows_, 1);
    lv_obj_set_flex_flow(about_rows_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(about_rows_, 6, 0);
    lv_obj_set_scroll_dir(about_rows_, LV_DIR_VER);

    lv_obj_t* back = lv_button_create(about_);
    // 🎨 STRIP IT FIRST, for the reason spelled out in AddRow - this is the
    //    button that proves the point. It was the only object in this file
    //    without this line, and setting bg_color on top of LVGL's default
    //    button style leaves the default GRADIENT and shadow in place, so it
    //    kept rendering stock blue on a black-and-lavender screen. The base
    //    colour below was always right; it was never what you could see.
    lv_obj_remove_style_all(back);
    lv_obj_set_size(back, LV_PCT(100), kRowHeight);
    lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(back, c_panel_, 0);
    lv_obj_set_style_bg_color(back, c_accent_, LV_STATE_PRESSED);
    lv_obj_set_style_radius(back, 6, 0);
    lv_obj_t* blabel = lv_label_create(back);
    lv_label_set_text(blabel, "Back");
    lv_obj_set_style_text_color(blabel, c_text_, 0);
    lv_obj_center(blabel);
    lv_obj_add_event_cb(back, [](lv_event_t* e) {
        auto* self = static_cast<StackySettings*>(lv_event_get_user_data(e));
        lv_obj_add_flag(self->about_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(self->list_, LV_OBJ_FLAG_HIDDEN);
    }, LV_EVENT_CLICKED, this);
}

// 🅰️ NO BOLD FONT SHIPS WITH THIS FIRMWARE - Noto Sans basic at 14/16/20/30 and
//    nothing else - and adding a bold face costs flash for the sake of one
//    screen. Colour does the same job: the label in the saturated accent, the
//    value in the softer text colour. The eye reads saturation as weight.
//
//    Two columns rather than one wrapped line, because a value that wraps across
//    the whole screen loses its label. A long URL now wraps inside its own
//    column with the label still beside it.
void StackySettings::FillAbout() {
    if (about_rows_ == nullptr) return;
    lv_obj_clean(about_rows_);   // rebuilt on every open: the IP can change
    if (!actions_.about_rows) return;

    for (const auto& [label, value] : actions_.about_rows()) {
        lv_obj_t* row = lv_obj_create(about_rows_);
        lv_obj_remove_style_all(row);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* l = lv_label_create(row);
        lv_label_set_text(l, label.c_str());
        lv_obj_set_style_text_color(l, c_accent_, 0);
        // ⚠️ WIDE ENOUGH FOR THE LONGEST LABEL, and CLIP rather than wrap. At 86px
        //    "Firmware" broke across two lines as "Firmwar" / "e", which cost a
        //    whole row of vertical space and pushed the last field off the
        //    screen. A label that does not fit should be cut, never reflowed -
        //    reflowing is how one narrow column eats the page.
        lv_obj_set_width(l, 104);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);

        lv_obj_t* v = lv_label_create(row);
        lv_label_set_text(v, value.c_str());
        lv_obj_set_style_text_color(v, c_text_, 0);
        lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
        lv_obj_set_flex_grow(v, 1);
    }
}

void StackySettings::Show() {
    if (root_ == nullptr || visible_) return;
    // Always open on the list, never on whatever page was left showing.
    lv_obj_add_flag(about_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(list_, LV_OBJ_FLAG_HIDDEN);
    // Values may have changed by voice since this was last opened.
    if (actions_.get_volume && volume_value_) {
        lv_label_set_text_fmt(volume_value_, "%d", actions_.get_volume());
    }
    if (actions_.get_brightness && bright_value_) {
        lv_label_set_text_fmt(bright_value_, "%d", actions_.get_brightness());
    }
    lv_obj_move_foreground(root_);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    visible_ = true;
}

void StackySettings::Hide() {
    if (root_ == nullptr || !visible_) return;
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    visible_ = false;
}
