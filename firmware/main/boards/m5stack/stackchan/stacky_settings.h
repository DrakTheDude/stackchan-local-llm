/*
 * The on-screen settings menu.
 *
 * Opened by holding the screen for five seconds. The factory firmware had one of
 * these - Change Wi-Fi, Brightness, Volume, Hardware Test, RGB Strip - reached
 * from its launcher, and an owner coming from that firmware will look for it, so
 * this is deliberately the same idea rather than a new one.
 *
 * WHAT IS NOT HERE, and why, is in docs/stock-vs-local.md. Short version: no
 * account, no app catalogue, no over-the-air update. The rows here are the ones
 * that belong to a robot you own.
 *
 * 🎨 EVERY COLOUR COMES FROM THE REGISTERED THEME, never from a literal. That is
 *    not tidiness - it is the whole point. A skin changes the theme, and this
 *    menu has to come with it, including the menu you use to pick the skin.
 *
 * 🔴 FULL SCREEN, AND THE FACE IS HIDDEN BEHIND IT. On a 320x240 panel there is
 *    no room to do both, and a menu drawn over a blinking face is a menu you
 *    cannot read. He comes back when it closes.
 */
#ifndef STACKY_SETTINGS_H
#define STACKY_SETTINGS_H

#include <lvgl.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class StackySettings {
public:
    // Actions the menu cannot perform itself, because they belong to the board:
    // entering Wi-Fi setup, and running the hardware self-check.
    struct Actions {
        std::function<void()> wifi_setup;
        std::function<void()> self_check;
        // 📐 Traces a square with the head, so a character's motion tokens can
        //    be SEEN. See StackChanHead::TraceSquare.
        std::function<void()> motion_check;
        // 🎭 Steps to the next character and returns its label. One row rather
        //    than a submenu: the menu is a column of rows, there are four
        //    characters, and a picker is a bigger change than the feature needs.
        std::function<const char*()> next_body;
        std::function<const char*()> body_label;
        // 🌡️ The other axis. A mood modulates a robot; a body replaces him, so
        //    they step independently and never share a row.
        std::function<const char*()> next_mood;
        std::function<const char*()> mood_label;
        std::function<void(int)> set_volume;      // 0..100
        std::function<int()> get_volume;
        std::function<void(int)> set_brightness;  // 0..100
        std::function<int()> get_brightness;
        // 🔇 The privacy switches. Both persist across a reboot, which is the
        //    honest behaviour: a microphone you turned off should still be off
        //    in the morning.
        std::function<void(bool)> set_mic_muted;
        std::function<bool()> get_mic_muted;
        std::function<void(bool)> set_camera_off;
        std::function<bool()> get_camera_off;
        // 📐 Head trim - the one genuinely per-robot number, and until now the
        //    only setting that needed a rebuilt firmware to change, for
        //    something you decide by looking at him.
        //
        //    `nudge_trim` applies LIVE and does not persist: the head re-centres
        //    on the new value so the change can be seen. `close_trim(true)`
        //    saves; `close_trim(false)` puts back whatever the screen opened
        //    with. Saving on every tap would leave a robot stuck mid-adjustment
        //    if you walked away, which is the one state nobody chooses.
        std::function<std::pair<int, int>()> get_trim;   // pan, tilt, in counts
        std::function<void(int, int)> nudge_trim;        // deltas, applied live
        // true = save. The pair is what the page opened with, handed back so a
        // revert does not need the board to keep state that is only meaningful
        // while one screen is up.
        std::function<void(bool, int, int)> close_trim;
        // The About page, as label/value pairs rather than formatted lines. The
        // page needs to style the two halves differently and wrap the value
        // inside its own column - neither of which is possible once it has been
        // flattened into one string.
        std::function<std::vector<std::pair<std::string, std::string>>()> about_rows;
    };

    // Builds the overlay hidden. Call once, with the LVGL lock held, after the
    // display's own SetupUI has run - it parents onto the active screen.
    void Build(const Actions& actions);

    // Both assume the LVGL lock is held.
    void Show();
    void Hide();
    bool visible() const { return visible_; }

private:
    void BuildList();
    void BuildAbout();
    void BuildTrim();
    // Repaints the two numbers. Called after every nudge and on every open.
    void ShowTrim();
    // Repopulates the About rows. Called on every open, because the address can
    // change under a robot that has been running for a week.
    void FillAbout();
    lv_obj_t* AddRow(const char* text, lv_event_cb_t cb);
    void SetRowLabel(lv_obj_t* row, const char* prefix, const char* label);
    lv_obj_t* AddSlider(const char* text, int value, lv_event_cb_t cb, lv_obj_t** out_value_label);
    lv_obj_t* AddToggle(const char* text, bool on, lv_event_cb_t cb, lv_obj_t** out_switch);

    Actions actions_{};
    bool visible_ = false;

    lv_obj_t* root_ = nullptr;     // full-screen container, hidden by default
    lv_obj_t* list_ = nullptr;     // the scrolling rows
    lv_obj_t* about_ = nullptr;      // the About page, shown in place of the list
    lv_obj_t* about_rows_ = nullptr;  // repopulated each time it is opened
    lv_obj_t* trim_ = nullptr;         // the head trim page, same idea
    lv_obj_t* trim_pan_value_ = nullptr;
    lv_obj_t* trim_tilt_value_ = nullptr;
    // What the trim was when the page opened, so Back can put it back.
    int trim_was_pan_ = 0;
    int trim_was_tilt_ = 0;
    lv_obj_t* volume_value_ = nullptr;
    lv_obj_t* bright_value_ = nullptr;
    lv_obj_t* mic_switch_ = nullptr;
    lv_obj_t* cam_switch_ = nullptr;
    lv_obj_t* body_row_ = nullptr;   // relabelled as the body changes
    lv_obj_t* mood_row_ = nullptr;
    lv_obj_t* toast_ = nullptr;    // transient feedback, e.g. the self-check result

    // Theme colours, read once at Build() so a restyle is one place to change.
    lv_color_t c_bg_{}, c_panel_{}, c_text_{}, c_dim_{}, c_accent_{};
};

#endif  // STACKY_SETTINGS_H
