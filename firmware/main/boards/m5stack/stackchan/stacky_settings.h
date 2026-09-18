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

class StackySettings {
public:
    // Actions the menu cannot perform itself, because they belong to the board:
    // entering Wi-Fi setup, and running the hardware self-check.
    struct Actions {
        std::function<void()> wifi_setup;
        std::function<void()> self_check;
        std::function<void(int)> set_volume;      // 0..100
        std::function<int()> get_volume;
        std::function<void(int)> set_brightness;  // 0..100
        std::function<int()> get_brightness;
        // One line each for the About page: firmware, address, server.
        std::function<std::string()> about_text;
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
    lv_obj_t* AddRow(const char* text, lv_event_cb_t cb);
    lv_obj_t* AddSlider(const char* text, int value, lv_event_cb_t cb, lv_obj_t** out_value_label);

    Actions actions_{};
    bool visible_ = false;

    lv_obj_t* root_ = nullptr;     // full-screen container, hidden by default
    lv_obj_t* list_ = nullptr;     // the scrolling rows
    lv_obj_t* about_ = nullptr;    // the About page, shown in place of the list
    lv_obj_t* about_label_ = nullptr;
    lv_obj_t* volume_value_ = nullptr;
    lv_obj_t* bright_value_ = nullptr;
    lv_obj_t* toast_ = nullptr;    // transient feedback, e.g. the self-check result

    // Theme colours, read once at Build() so a restyle is one place to change.
    lv_color_t c_bg_{}, c_panel_{}, c_text_{}, c_dim_{}, c_accent_{};
};

#endif  // STACKY_SETTINGS_H
