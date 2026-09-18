#include "stacky_face.h"

#include "display/lvgl_display/lvgl_theme.h"
#include "assets/lang_config.h"
#include "status_source.h"

#include <esp_log.h>
#include <esp_random.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define TAG "StackyFace"

namespace {

// Cartoon ink, inverted for a black screen. See the header for why the features
// are monochrome: a cartoon eye is legible because of the VALUE step between a
// bright sclera and a dark pupil, and hue cannot stand in for that.
constexpr uint32_t kInk = 0xF2FAFF;    // sclera, brows, mouth - white, faint cyan
constexpr uint32_t kPupil = 0x07080E;  // near-black, not pure, so it still reads
constexpr uint32_t kGlint = 0xFFFFFF;
// The accent colour, kept as a bloom rather than as a feature.
constexpr uint32_t kGlow = 0xA855FF;

// Layout.
//
// 🔴 THE SCREEN IS A FACE, NOT A TRANSCRIPT. This was three wrapped lines of
//    subtitle taking the bottom 40% of a 2" panel, and the text was not even
//    worth reading - streamed chunks arrive without spaces at the joins, so it
//    rendered "onlyStackycould hear". Meanwhile the thing the hardware exists
//    to show was squeezed into a third of the display.
//
//    So: ONE line, scrolled slowly, and the face takes everything else. Speech
//    is the channel; the text is a caption for glancing at, not for reading.
//
//    The multiline chat bar (CONFIG_USE_MULTILINE_CHAT_MESSAGE, lcd_display.cc
//    :948) is now OFF in sdkconfig.defaults.stackchan. It was turned on to stop
//    a fast marquee being unreadable - the right fix for that was a slower
//    scroll, not more lines. See kScrollMs.
//
//      0..26     status bar
//      26..186   the face
//      ~202..240 the chat bar, one line
constexpr int kChatMaxH = 40;
constexpr int kFaceW = 300;
constexpr int kFaceH = 160;
constexpr int kFaceYOffset = -14;

// 🔴 THE EXPRESSION TABLE IS UNSCALED, AND STAYS THAT WAY. Every entry in
//    kShapes is in the original 128px-tall face's pixels, because 22 rows of
//    hand-tuned geometry is exactly the thing not to retype when the budget
//    changes. Scaled() applies the factor once, where the shape is chosen, so
//    the table stays readable and there is one number to change if the layout
//    moves again.
constexpr int kScaleNum = 5;
constexpr int kScaleDen = 4;   // 1.25x
constexpr int Scaled(int v) { return v * kScaleNum / kScaleDen; }

// All of the following are in FACE coordinates: 0,0 is the centre of face_.
constexpr int kEyeSpread = Scaled(58);
constexpr int kEyeY = Scaled(-6);      // screen y ~99
constexpr int kBrowY = Scaled(-46);    // screen y ~49, clear of the status bar
constexpr int kBrowHalfW = Scaled(29); // brow runs +/- this about the eye centre
constexpr int kBrowThick = Scaled(8);
constexpr int kMouthY = Scaled(44);    // screen y ~161
constexpr int kStrokeW = Scaled(7);    // mouth line thickness

// One full lap of a scrolling caption. LVGL's default is 40px/s, which crosses
// a 320px screen in eight seconds and reads as a ticker you cannot keep up
// with. This is a fixed duration for the whole scroll instead, slow enough to
// follow while he is still saying it.
constexpr int kScrollMs = 22000;

constexpr int kTickMs = 50;

// A blink, as percentages of the eye's open height, one entry per tick. Six
// ticks at 50ms is 300ms - fast enough to be involuntary, slow enough to see.
constexpr int kBlink[] = {70, 30, 8, 8, 35, 75};
constexpr int kBlinkFrames = sizeof(kBlink) / sizeof(kBlink[0]);

// lv_line points are relative to the line widget's own top-left. Every stroke
// here is a full-face-sized widget, so this converts face coordinates into
// point coordinates and the maths above stays readable.
inline void FacePoint(lv_point_precise_t& p, int x, int y) {
    p.x = kFaceW / 2 + x;
    p.y = kFaceH / 2 + y;
}

}  // namespace

// The expression table.
//
//   {left eye, right eye, left brow, right brow, gaze x, gaze y,
//    mouth width, mouth curve, mouth open height}
//
//   eye  = {width, height, corner radius, lid from top, lid from bottom}
//   brow = {inner end, outer end} in px BELOW the baseline; inner is the end
//          nearest the nose. inner-down reads angry, inner-up reads sad.
//   mouth_curve: + smiles, - frowns. mouth_open: 0 uses the line instead.
//
// Eye heights stay at or under 66 so the sclera clears the brows above and the
// mouth below, which in turn clears the capped chat bar.
struct NamedShape {
    const char* name;
    StackyFace::FaceShape shape;
};

// clang-format off
static const StackyFace::FaceShape kNeutral =
    {{58, 62, 26, 0, 0}, {58, 62, 26, 0, 0}, {0, 2}, {0, 2}, 0, 0, 44, 3, 0};

static const NamedShape kShapes[] = {
    //                  left eye              right eye             brow L     brow R    gz    mouth
    {"neutral",     {{58, 62, 26,  0,  0}, {58, 62, 26,  0,  0}, {  0,  2}, {  0,  2},  0,  0, 44,   3,  0}},
    {"happy",       {{58, 54, 26,  0, 10}, {58, 54, 26,  0, 10}, { -4, -2}, { -4, -2},  0, -1, 60,  14,  0}},
    {"laughing",    {{58, 40, 20,  0, 14}, {58, 40, 20,  0, 14}, { -8, -4}, { -8, -4},  0, -2, 54,   0, 26}},
    {"funny",       {{58, 44, 22,  0, 12}, {58, 58, 26,  0,  8}, { -8, -3}, { -2,  0},  2, -1, 56,  12,  0}},
    {"loving",      {{58, 50, 25,  0, 12}, {58, 50, 25,  0, 12}, { -5, -3}, { -5, -3},  0, -1, 56,  13,  0}},
    {"kissy",       {{58, 46, 23,  0, 12}, {58, 46, 23,  0, 12}, { -5, -2}, { -5, -2},  0, -1, 22,   6,  0}},
    {"delicious",   {{58, 48, 24,  0, 12}, {58, 48, 24,  0, 12}, { -4, -2}, { -4, -2},  0, -1, 50,  11,  0}},
    {"sad",         {{58, 56, 26, 12,  0}, {58, 56, 26, 12,  0}, { -9,  6}, { -9,  6},  0,  6, 42, -10,  0}},
    {"crying",      {{58, 54, 26, 14,  0}, {58, 54, 26, 14,  0}, {-11,  7}, {-11,  7},  0,  9, 40, -12,  0}},
    // Inner ends driven DOWN. This is the whole of "angry" and it needs no
    // help from the eyes.
    {"angry",       {{60, 46, 20, 14,  0}, {60, 46, 20, 14,  0}, { 12, -6}, { 12, -6},  0,  2, 44,  -8,  0}},
    {"confident",   {{58, 44, 20, 12,  0}, {58, 44, 20, 12,  0}, {  5, -5}, { -3, -1},  0,  0, 48,   8,  0}},
    {"cool",        {{58, 40, 18, 14,  0}, {58, 40, 18, 14,  0}, {  4, -4}, {  4, -4},  0,  0, 46,   6,  0}},
    {"relaxed",     {{58, 40, 18, 12,  0}, {58, 40, 18, 12,  0}, { -2,  0}, { -2,  0},  0,  1, 44,   6,  0}},
    {"sleepy",      {{58, 24, 11, 10,  0}, {58, 24, 11, 10,  0}, {  0,  4}, {  0,  4},  0,  4, 32,  -3,  0}},
    {"surprised",   {{62, 66, 30,  0,  0}, {62, 66, 30,  0,  0}, {-10, -8}, {-10, -8},  0, -2, 26,   0, 20}},
    {"shocked",     {{64, 66, 31,  0,  0}, {64, 66, 31,  0,  0}, {-13,-10}, {-13,-10},  0, -3, 30,   0, 26}},
    // One brow up, one down, eyes uneven, looking away. The cheapest way to
    // look like you are processing something.
    {"thinking",    {{54, 34, 16,  0,  0}, {56, 52, 22,  0,  0}, {  6, -2}, { -9, -6}, -8, -7, 34,  -2,  0}},
    {"confused",    {{52, 42, 20,  8,  0}, {60, 60, 28,  0,  0}, {  5, -1}, {-10, -6},  6,  2, 34,  -4,  0}},
    {"embarrassed", {{56, 36, 17,  0,  0}, {56, 36, 17,  0,  0}, { -4,  3}, { -4,  3},  9,  4, 36,   5,  0}},
    {"winking",     {{58, 62, 26,  0,  0}, {58,  8,  4,  0,  0}, { -3, -1}, { -7, -4},  0, -1, 54,  12,  0}},
    {"silly",       {{58,  8,  4,  0,  0}, {58, 54, 26,  0, 10}, { -7, -4}, { -3, -1},  0, -1, 54,   0, 22}},
};
// clang-format on

// Applies kScaleNum/kScaleDen to a table entry. The ONE place the table's
// original 128px-face pixels become screen pixels.
static StackyFace::FaceShape ScaleShape(const StackyFace::FaceShape& s) {
    auto eye = [](const StackyFace::EyeShape& e) {
        return StackyFace::EyeShape{
            static_cast<uint8_t>(Scaled(e.w)),        static_cast<uint8_t>(Scaled(e.h)),
            static_cast<uint8_t>(Scaled(e.radius)),   static_cast<uint8_t>(Scaled(e.lid_top)),
            static_cast<uint8_t>(Scaled(e.lid_bottom))};
    };
    auto brow = [](const StackyFace::BrowShape& b) {
        return StackyFace::BrowShape{static_cast<int8_t>(Scaled(b.inner)),
                                     static_cast<int8_t>(Scaled(b.outer))};
    };
    return StackyFace::FaceShape{
        eye(s.left),
        eye(s.right),
        brow(s.brow_l),
        brow(s.brow_r),
        static_cast<int8_t>(Scaled(s.gaze_x)),
        static_cast<int8_t>(Scaled(s.gaze_y)),
        static_cast<uint8_t>(Scaled(s.mouth_w)),
        static_cast<int8_t>(Scaled(s.mouth_curve)),
        static_cast<uint8_t>(Scaled(s.mouth_open)),
    };
}

StackyFace::FaceShape StackyFace::ShapeFor(const char* emotion) {
    if (emotion != nullptr) {
        for (const auto& s : kShapes) {
            if (strcmp(s.name, emotion) == 0) {
                return ScaleShape(s.shape);
            }
        }
    }
    // Deliberately silent. The model invents emotion names, and the framework
    // itself asks for "robot_2" at application.cc:414 - a name in no font
    // either. An unknown mood is a neutral face, not a log line.
    return ScaleShape(kNeutral);
}

StackyFace::StackyFace(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                       int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                       bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {
    // Through ShapeFor, so the starting face is scaled like every other one.
    // Assigning kNeutral directly here would give an unscaled face until the
    // first SetEmotion, which is a subtle enough difference to ship by accident.
    shape_ = ShapeFor("neutral");
}

StackyFace::~StackyFace() {
    if (anim_timer_ != nullptr) {
        DisplayLockGuard lock(this);
        lv_timer_delete(anim_timer_);
        anim_timer_ = nullptr;
    }
}

// A full-face-sized lv_line. Sizing every stroke to the whole face means the
// point maths is in one coordinate system; the cost is that a stroke change
// invalidates the face, which happens once per reply rather than per frame.
lv_obj_t* StackyFace::MakeStroke(lv_obj_t* parent, int width_px) {
    lv_obj_t* line = lv_line_create(parent);
    lv_obj_set_size(line, kFaceW, kFaceH);
    lv_obj_center(line);
    lv_obj_set_style_line_color(line, lv_color_hex(kInk), 0);
    lv_obj_set_style_line_width(line, width_px, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

void StackyFace::MakeEye(lv_obj_t* parent, Eye& eye, int centre_x) {
    auto* theme = static_cast<LvglTheme*>(current_theme_);
    const lv_color_t bg = theme->background_color();

    eye.sclera = lv_obj_create(parent);
    lv_obj_remove_flag(eye.sclera, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(eye.sclera, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(eye.sclera, 0, 0);
    lv_obj_set_style_border_width(eye.sclera, 0, 0);
    lv_obj_set_style_bg_color(eye.sclera, lv_color_hex(kInk), 0);
    lv_obj_set_style_bg_opa(eye.sclera, LV_OPA_COVER, 0);
    // The bloom - a blurred violet shadow with no offset. This is where the
    // accent colour lives, and it costs one style property.
    lv_obj_set_style_shadow_color(eye.sclera, lv_color_hex(kGlow), 0);
    lv_obj_set_style_shadow_width(eye.sclera, 22, 0);
    lv_obj_set_style_shadow_spread(eye.sclera, 1, 0);
    lv_obj_set_style_shadow_offset_x(eye.sclera, 0, 0);
    lv_obj_set_style_shadow_offset_y(eye.sclera, 0, 0);
    lv_obj_set_style_shadow_opa(eye.sclera, LV_OPA_60, 0);
    // Children (pupil, lids) get clipped to the rounded corners rather than
    // squaring them off.
    lv_obj_set_style_clip_corner(eye.sclera, true, 0);
    // The sclera never moves. Only the pupil does - see the header - so this
    // alignment is set once and never touched again.
    lv_obj_align(eye.sclera, LV_ALIGN_CENTER, centre_x, kEyeY);

    eye.pupil = lv_obj_create(eye.sclera);
    lv_obj_remove_flag(eye.pupil, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(eye.pupil, 0, 0);
    lv_obj_set_style_border_width(eye.pupil, 0, 0);
    lv_obj_set_style_bg_color(eye.pupil, lv_color_hex(kPupil), 0);
    lv_obj_set_style_bg_opa(eye.pupil, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(eye.pupil, LV_RADIUS_CIRCLE, 0);

    // The catchlight. Two-thirds of "alive" for six pixels.
    eye.glint = lv_obj_create(eye.pupil);
    lv_obj_remove_flag(eye.glint, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(eye.glint, 0, 0);
    lv_obj_set_style_border_width(eye.glint, 0, 0);
    lv_obj_set_style_bg_color(eye.glint, lv_color_hex(kGlint), 0);
    lv_obj_set_style_bg_opa(eye.glint, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(eye.glint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(eye.glint, 7, 7);
    lv_obj_align(eye.glint, LV_ALIGN_TOP_LEFT, 3, 3);

    // Lids: background-coloured rectangles eating into the sclera from an edge.
    // Created after the pupil so they paint over it - a closing eye has to
    // cover the pupil, not sit behind it.
    for (lv_obj_t** slot : {&eye.lid_top, &eye.lid_bottom}) {
        lv_obj_t* lid = lv_obj_create(eye.sclera);
        lv_obj_remove_flag(lid, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_pad_all(lid, 0, 0);
        lv_obj_set_style_border_width(lid, 0, 0);
        lv_obj_set_style_radius(lid, 0, 0);
        lv_obj_set_style_bg_color(lid, bg, 0);
        lv_obj_set_style_bg_opa(lid, LV_OPA_COVER, 0);
        lv_obj_add_flag(lid, LV_OBJ_FLAG_HIDDEN);
        *slot = lid;
    }
    lv_obj_align(eye.lid_top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_align(eye.lid_bottom, LV_ALIGN_BOTTOM_MID, 0, 0);
}

void StackyFace::BuildFace() {
    auto* theme = static_cast<LvglTheme*>(current_theme_);

    // Cap the chat bar before anything else - see the layout note above.
    // Without this the face has no protected space to live in.
    if (bottom_bar_ != nullptr) {
        lv_obj_set_style_max_height(bottom_bar_, kChatMaxH, 0);
        lv_obj_remove_flag(bottom_bar_, LV_OBJ_FLAG_SCROLLABLE);
    }
    // Slow the caption down. LVGL's default is a SPEED (40px/s), which crosses
    // this screen in about eight seconds and reads as a ticker; setting the
    // style anim duration replaces that with a fixed time for the whole lap
    // (lv_label.c:1117 - a non-zero style duration wins over the default).
    if (chat_message_label_ != nullptr) {
        lv_obj_set_style_anim_duration(chat_message_label_, kScrollMs, 0);
    }

    // Parented to emoji_box_, NOT to the screen. That is load-bearing: the
    // camera preview path (LcdDisplay::SetPreviewImage) hides emoji_box_ to get
    // the photo on screen and un-hides it afterwards. Living inside it means
    // `self.camera.show_photo` keeps working with no changes here.
    lv_obj_t* parent = emoji_box_;
    lv_obj_set_size(parent, kFaceW, kFaceH);
    lv_obj_align(parent, LV_ALIGN_CENTER, 0, kFaceYOffset);

    // The stock face - an emoji glyph and an image slot - is replaced wholesale.
    if (emoji_label_ != nullptr) {
        lv_obj_add_flag(emoji_label_, LV_OBJ_FLAG_HIDDEN);
    }
    if (emoji_image_ != nullptr) {
        lv_obj_add_flag(emoji_image_, LV_OBJ_FLAG_HIDDEN);
    }

    face_ = lv_obj_create(parent);
    lv_obj_remove_flag(face_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(face_, kFaceW, kFaceH);
    lv_obj_center(face_);
    lv_obj_set_style_pad_all(face_, 0, 0);
    lv_obj_set_style_border_width(face_, 0, 0);
    // 🔴 TRANSPARENT. This is a positioning container and nothing else.
    //    Painting it with the theme background looked identical in theory and
    //    wrong in practice: it drew a visible slab across the screen, and the
    //    eye lids - which are also background-coloured - stopped disappearing
    //    into the backdrop and started reading as grey half-discs inside each
    //    eye. One opaque rectangle too many.
    lv_obj_set_style_bg_opa(face_, LV_OPA_TRANSP, 0);

    MakeEye(face_, left_, -kEyeSpread);
    MakeEye(face_, right_, kEyeSpread);

    brow_l_ = MakeStroke(face_, kBrowThick);
    brow_r_ = MakeStroke(face_, kBrowThick);
    mouth_line_ = MakeStroke(face_, kStrokeW);

    // The open mouth is a RING, not a disc: on a black screen an open mouth is
    // a hole, so it is drawn as an outline. Filling it would put a bright blob
    // in the middle of his face.
    mouth_ring_ = lv_obj_create(face_);
    lv_obj_remove_flag(mouth_ring_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(mouth_ring_, 0, 0);
    lv_obj_set_style_bg_color(mouth_ring_, theme->background_color(), 0);
    lv_obj_set_style_bg_opa(mouth_ring_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(mouth_ring_, lv_color_hex(kInk), 0);
    lv_obj_set_style_border_width(mouth_ring_, kStrokeW - 1, 0);
    lv_obj_set_style_radius(mouth_ring_, LV_RADIUS_CIRCLE, 0);
    lv_obj_align(mouth_ring_, LV_ALIGN_CENTER, 0, kMouthY);
    lv_obj_add_flag(mouth_ring_, LV_OBJ_FLAG_HIDDEN);

    blink_countdown_ = 40;
    mouth_h_ = 0;
}

// ---------------------------------------------------------------------------
// The idle status screen: one card at a time.
// ---------------------------------------------------------------------------

namespace {

// 🔴 The 20px font has no glyphs above ASCII, and status text from a server is
//    often full of em dashes: "All healthy — 3 nodes." Rendered as-is that shows
//    boxes, which reads as a bug in the robot rather than punctuation. Replace
//    each multi-byte sequence with a hyphen and move on.
std::string Ascii(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
            i++;
            continue;
        }
        const size_t len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
        out.push_back('-');
        i += len;
    }
    return out;
}

// "28.1/61.1 GB" -> 46. Returns -1 when the value is not a ratio, which is the
// signal to hide the bar rather than draw a meaningless one.
int RatioPercent(const std::string& value) {
    const size_t slash = value.find('/');
    if (slash == std::string::npos) return -1;
    const double used = atof(value.substr(0, slash).c_str());
    const double total = atof(value.substr(slash + 1).c_str());
    if (total <= 0.0 || used < 0.0) return -1;
    const int pct = static_cast<int>((used / total) * 100.0 + 0.5);
    return std::min(100, std::max(0, pct));
}

std::string Ago(int64_t seconds) {
    char buf[48];
    if (seconds < 90) {
        snprintf(buf, sizeof(buf), "updated just now");
    } else if (seconds < 3600) {
        snprintf(buf, sizeof(buf), "updated %d min ago", static_cast<int>(seconds / 60));
    } else {
        snprintf(buf, sizeof(buf), "updated %d h ago", static_cast<int>(seconds / 3600));
    }
    return buf;
}

constexpr int kCardSeconds = 5;
constexpr uint32_t kMuted = 0x9AA4B2;
constexpr uint32_t kCardBg = 0x120C22;

}  // namespace

// 🔴 NONE OF THESE LABELS SET A FONT. They inherit it from the screen, and that
//    is deliberate.
//
//    The first version captured theme->text_font()->font() here at SetupUI
//    time and handed the raw lv_font_t* to each label. That pointer does not
//    stay valid: this build has a dynamic glyph cache and an assets partition
//    that can replace the text font afterwards. By the time the screensaver
//    first appeared the pointer was dangling, and LVGL crashed inside
//    lv_font_get_glyph_width while laying the labels out:
//
//      Guru Meditation Error: InstrFetchProhibited
//      lv_font_get_glyph_width <- lv_text_get_next_line <- lv_label_event
//
//    It survived until then only because hidden objects are never laid out -
//    the bug was created at boot and detonated ninety seconds later. Inherited
//    styles always resolve against the LIVE theme, so they cannot go stale.
void StackyFace::BuildScreensaver() {
    auto* theme = static_cast<LvglTheme*>(current_theme_);

    saver_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_flag(saver_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(saver_, LV_HOR_RES, LV_VER_RES);
    lv_obj_center(saver_);
    lv_obj_set_style_pad_all(saver_, 0, 0);
    lv_obj_set_style_radius(saver_, 0, 0);
    lv_obj_set_style_border_width(saver_, 0, 0);
    lv_obj_set_style_bg_color(saver_, theme->background_color(), 0);
    lv_obj_set_style_bg_opa(saver_, LV_OPA_COVER, 0);
    lv_obj_add_flag(saver_, LV_OBJ_FLAG_HIDDEN);

    // The card: a dark panel, a hairline lavender border and a generous radius.
    lv_obj_t* card = lv_obj_create(saver_);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(card, 296, 158);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -12);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_pad_all(card, 18, 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(kCardBg), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(kGlow), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_opa(card, LV_OPA_50, 0);

    // 🔴 EVERYTHING IS CENTRED, and the vertical positions are explicit rather
    //    than stacked. The first version left-aligned each element and let the
    //    subtitle wrap where it liked, which pushed a three-line sentence into
    //    the value and made the card look like a paragraph rather than a
    //    readout. A stat card is a poster, not a document.
    //
    //    Letter-spaced uppercase does the job a smaller font would: this build
    //    has exactly one text size, so hierarchy has to come from colour,
    //    tracking and space.
    saver_label_ = lv_label_create(card);
    lv_obj_set_style_text_color(saver_label_, lv_color_hex(kMuted), 0);
    lv_obj_set_style_text_letter_space(saver_label_, 3, 0);
    lv_obj_set_style_text_align(saver_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(saver_label_, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_text(saver_label_, "");

    // The health dot, top right of the card. Small, and the only saturated
    // thing on the screen.
    saver_dot_ = lv_obj_create(card);
    lv_obj_remove_flag(saver_dot_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(saver_dot_, 10, 10);
    lv_obj_set_style_radius(saver_dot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(saver_dot_, 0, 0);
    lv_obj_set_style_bg_opa(saver_dot_, LV_OPA_COVER, 0);
    lv_obj_align(saver_dot_, LV_ALIGN_TOP_RIGHT, 0, 6);

    saver_value_ = lv_label_create(card);
    lv_obj_set_style_text_color(saver_value_, lv_color_hex(kInk), 0);
    lv_obj_set_style_text_align(saver_value_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(saver_value_, LV_ALIGN_CENTER, 0, -6);
    lv_label_set_text(saver_value_, "");

    // Only drawn when the value is a ratio - see RatioPercent. A bar with
    // nothing behind it is decoration pretending to be data.
    saver_bar_ = lv_bar_create(card);
    lv_obj_set_size(saver_bar_, 200, 6);
    lv_obj_align(saver_bar_, LV_ALIGN_CENTER, 0, 24);
    lv_obj_set_style_radius(saver_bar_, 4, 0);
    lv_obj_set_style_bg_color(saver_bar_, lv_color_hex(0x241A3A), 0);
    lv_obj_set_style_bg_opa(saver_bar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(saver_bar_, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(saver_bar_, lv_color_hex(kGlow), LV_PART_INDICATOR);
    lv_bar_set_range(saver_bar_, 0, 100);

    saver_sub_ = lv_label_create(card);
    lv_obj_set_style_text_color(saver_sub_, lv_color_hex(kMuted), 0);
    lv_obj_set_width(saver_sub_, 248);
    // One line, ellipsised. Wrapping is what turned the last card into a wall
    // of text, and a subtitle that needs three lines is not a subtitle.
    lv_label_set_long_mode(saver_sub_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(saver_sub_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(saver_sub_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(saver_sub_, "");

    // 🔴 The age line is not decoration. A screensaver confidently showing
    //    "3 healthy" from a reading half an hour old is the confabulation
    //    failure in slow motion, and the only defence is saying how old it is.
    saver_foot_ = lv_label_create(saver_);
    lv_obj_set_style_text_color(saver_foot_, lv_color_hex(kMuted), 0);
    lv_obj_align(saver_foot_, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_label_set_text(saver_foot_, "");
}

void StackyFace::PaintCard() {
    if (saver_ == nullptr) return;

    uint32_t dot = kMuted;
    std::string label = "STATUS";
    std::string value = "no data";
    std::string sub = "waiting for the first reading";
    int pct = -1;
    std::string foot = "not connected yet";

    if (status_ != nullptr) {
        const int64_t age = status_->age_seconds();
        const auto cards = status_->cards();

        if (age > 12 * 60) {
            // Stale. Say so loudly instead of showing numbers that look live.
            label = "NO CONTACT";
            value = "stale";
            sub = Ascii(status_->summary());
            foot = Ago(age);
        } else if (age >= 0) {
            foot = Ago(age);
            switch (status_->level()) {
                case StatusSource::Level::kOk:    dot = 0x7EE0A8; break;
                case StatusSource::Level::kWarn:  dot = 0xFFCF8E; break;
                case StatusSource::Level::kAlert: dot = 0xFF8E8E; break;
                default: break;
            }
            if (!cards.empty()) {
                if (saver_index_ >= static_cast<int>(cards.size())) saver_index_ = 0;
                const auto& c = cards[saver_index_];
                label = c.label;
                value = Ascii(c.value);
                sub = Ascii(c.sub);
                pct = RatioPercent(value);
            } else {
                value = "";
                sub = Ascii(status_->summary());
            }
        }
    }

    lv_label_set_text(saver_label_, label.c_str());
    lv_label_set_text(saver_value_, value.c_str());
    lv_label_set_text(saver_sub_, sub.c_str());
    lv_label_set_text(saver_foot_, foot.c_str());
    lv_obj_set_style_bg_color(saver_dot_, lv_color_hex(dot), 0);

    if (pct >= 0) {
        lv_bar_set_value(saver_bar_, pct, LV_ANIM_OFF);
        lv_obj_remove_flag(saver_bar_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(saver_bar_, LV_OBJ_FLAG_HIDDEN);
    }
}

// 🔴 SETS A FLAG. It does not touch LVGL, and that is not fastidiousness.
//
//    PowerSaveTimer's callbacks run on the shared esp_timer task, whose stack
//    is CONFIG_ESP_TIMER_TASK_STACK_SIZE - 3584 bytes. Raising the screensaver
//    from here would mean running LVGL layout plus the std::string and
//    std::vector work in PaintCard on that stack, alongside the LED animation
//    and the touch poll already living there. That exact mistake crashed this
//    firmware once already with the face animation; doing it again knowingly
//    would be worse.
//
//    Tick() runs in the LVGL task, which has a real stack and already holds
//    the port lock. It picks the change up within 50ms - imperceptible for
//    something triggered by ninety seconds of silence.
void StackyFace::SetPowerSaveMode(bool on) {
    // The base swaps the emotion to "sleepy" and clears the chat line. Harmless
    // while the overlay covers it, and correct the moment it is removed.
    SpiLcdDisplay::SetPowerSaveMode(on);
    saver_want_ = on;
}

// 🔴 SETS A FLAG. DOES NO LVGL WORK. THIS IS NOT FASTIDIOUSNESS - the first
//    version took DisplayLockGuard here and it hung the device.
//
//    The caller is the settings menu's switch callback, which runs inside the
//    LVGL task, which ALREADY HOLDS the port lock. Taking it again deadlocks
//    that task: the screen froze mid-menu, nothing responded, and the watchdog
//    rebooted the robot a couple of seconds later. Worse, the NVS write came
//    after this call, so the mute was never even saved - a privacy switch that
//    appeared to work, hung the robot, and forgot.
//
//    Same rule as Tick() and the screensaver, ten lines down: cross into the
//    LVGL task by leaving a flag for it, never by taking its lock.
void StackyFace::SetMuted(bool muted) {
    muted_ = muted;
    muted_want_ = muted;
}

// Same contract as SetMuted: leave a flag, let the LVGL task draw it. Safe to
// call from any task, including before the face exists.
void StackyFace::SetNoServer(bool no_server) {
    no_server_want_ = no_server;
}

// Runs from Tick(), i.e. inside the LVGL task with the lock already held.
//
// Deliberately a WORD and not only a symbol: a small red dot on a robot's face
// could mean recording just as easily as muted, and getting that backwards is
// the worst possible way to be wrong about a microphone.
void StackyFace::ApplyMuteBadge() {
    const bool want = muted_want_;
    if (want == muted_shown_) {
        return;
    }
    muted_shown_ = want;
    if (mute_badge_ == nullptr) {
        if (!want) {
            return;   // nothing built, nothing to hide
        }
        mute_badge_ = lv_label_create(lv_screen_active());
        lv_label_set_text(mute_badge_, "MIC OFF");
        lv_obj_set_style_text_color(mute_badge_, lv_color_hex(0xFF8E8E), 0);
        lv_obj_set_style_bg_color(mute_badge_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(mute_badge_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(mute_badge_, 3, 0);
        lv_obj_set_style_radius(mute_badge_, 4, 0);
        lv_obj_align(mute_badge_, LV_ALIGN_BOTTOM_LEFT, 4, -4);
    }
    // NOT moved to the foreground: the settings menu is a full-screen overlay
    // that raises itself, and a badge sitting on top of the row you are pressing
    // is how it looked the first time.
    if (want) {
        lv_obj_clear_flag(mute_badge_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(mute_badge_, LV_OBJ_FLAG_HIDDEN);
    }
}

// Runs from Tick(), i.e. inside the LVGL task with the lock already held.
//
// Bottom RIGHT, because the mute badge has the left corner and both can be true
// at once - a muted robot with no server is a perfectly ordinary first boot.
//
// Amber rather than the mute badge's red: nothing is broken and nothing is
// recording, there is simply a setting nobody has filled in. Red would send an
// owner looking for a fault that does not exist.
void StackyFace::ApplyNoServerBadge() {
    const bool want = no_server_want_;
    if (want == no_server_shown_) {
        return;
    }
    no_server_shown_ = want;
    if (no_server_badge_ == nullptr) {
        if (!want) {
            return;   // nothing built, nothing to hide
        }
        no_server_badge_ = lv_label_create(lv_screen_active());
        // The words say what is missing, not what went wrong. "NO LLM" is the
        // sentence an owner can act on; "connection failed" is one they cannot,
        // because there is no connection to fail yet.
        lv_label_set_text(no_server_badge_, "NO LLM");
        lv_obj_set_style_text_color(no_server_badge_, lv_color_hex(0xFFC46B), 0);
        lv_obj_set_style_bg_color(no_server_badge_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(no_server_badge_, LV_OPA_COVER, 0);
        lv_obj_set_style_pad_all(no_server_badge_, 3, 0);
        lv_obj_set_style_radius(no_server_badge_, 4, 0);
        lv_obj_align(no_server_badge_, LV_ALIGN_BOTTOM_RIGHT, -4, -4);
    }
    // Not raised to the foreground, for the reason in ApplyMuteBadge: the
    // settings menu is a full-screen overlay and a badge over the row you are
    // pressing is worse than a badge you cannot see for a moment.
    if (want) {
        lv_obj_clear_flag(no_server_badge_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(no_server_badge_, LV_OBJ_FLAG_HIDDEN);
    }
}

// A photo should be the whole screen, not a stamp in the middle of it.
//
// LcdDisplay::SetupUI creates preview_image_ at width_/2 x height_/2 and
// SetPreviewImage then scales the source to match, so a 320x240 frame on a
// 320x240 panel lands as a 160x120 thumbnail with the face's black background
// all around it. On a 2" screen that reads as a thumbnail of a photo rather
// than as a photo, which is most of what makes the trick land.
//
// The frame is exactly the panel's size, so the fill is 1:1 - scale stays at
// LV_SCALE_NONE and LVGL blits it without pushing the image onto a transformed
// layer. The status bar and the chat line still draw over it: both were created
// after preview_image_ in the base SetupUI, so they are already above it in z
// order, and a caption over a photo is what we want anyway.
void StackyFace::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    const bool showing = image != nullptr;
    SpiLcdDisplay::SetPreviewImage(std::move(image));
    if (!showing || preview_image_ == nullptr) {
        return;
    }

    DisplayLockGuard lock(this);
    auto* dsc = preview_image_cached_ ? preview_image_cached_->image_dsc() : nullptr;
    if (dsc == nullptr || dsc->header.w == 0 || dsc->header.h == 0) {
        return;
    }
    // Fit by the tighter axis so a frame that is not 4:3 is letterboxed rather
    // than stretched.
    const int32_t sx = LV_SCALE_NONE * width_ / dsc->header.w;
    const int32_t sy = LV_SCALE_NONE * height_ / dsc->header.h;
    const int32_t scale = sx < sy ? sx : sy;
    lv_image_set_scale(preview_image_, scale);
    lv_obj_set_size(preview_image_, dsc->header.w * scale / LV_SCALE_NONE,
                    dsc->header.h * scale / LV_SCALE_NONE);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
}

void StackyFace::ApplyEye(const Eye& eye, const EyeShape& s, int open_pct, int gx, int gy,
                          EyeState& cache) {
    int h = s.h * open_pct / 100;
    if (h < 3) h = 3;
    // Clamp the radius: a 26px radius on a 6px-tall eye mid-blink renders as a
    // stray blob rather than a closing lid.
    const int radius = std::min<int>(s.radius, h / 2);
    const int lt = s.lid_top * open_pct / 100;
    const int lb = s.lid_bottom * open_pct / 100;

    // The pupil scales with the sclera so it vanishes into a closing eye
    // instead of hanging there as a dot on a shut lid.
    int pw = s.w * 42 / 100;
    int ph = std::min<int>(pw, h - 10);
    if (ph < 8) {
        pw = 0;
        ph = 0;
    }

    // Nothing moved - do NOT touch LVGL. See the EyeState note in the header.
    if (cache.w == s.w && cache.h == h && cache.radius == radius && cache.lid_top == lt &&
        cache.lid_bottom == lb && cache.pupil_w == pw && cache.pupil_h == ph &&
        cache.pupil_dx == gx && cache.pupil_dy == gy) {
        return;
    }

    if (cache.w != s.w || cache.h != h) {
        lv_obj_set_size(eye.sclera, s.w, h);
    }
    if (cache.radius != radius) {
        lv_obj_set_style_radius(eye.sclera, radius, 0);
    }
    if (cache.lid_top != lt) {
        if (lt > 0) {
            lv_obj_set_size(eye.lid_top, s.w, lt);
            lv_obj_remove_flag(eye.lid_top, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(eye.lid_top, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (cache.lid_bottom != lb) {
        if (lb > 0) {
            lv_obj_set_size(eye.lid_bottom, s.w, lb);
            lv_obj_remove_flag(eye.lid_bottom, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(eye.lid_bottom, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (pw == 0) {
        lv_obj_add_flag(eye.pupil, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (cache.pupil_w != pw || cache.pupil_h != ph) {
            lv_obj_set_size(eye.pupil, pw, ph);
        }
        if (cache.pupil_dx != gx || cache.pupil_dy != gy) {
            lv_obj_align(eye.pupil, LV_ALIGN_CENTER, gx, gy);
        }
        lv_obj_remove_flag(eye.pupil, LV_OBJ_FLAG_HIDDEN);
    }

    cache = {s.w, h, radius, lt, lb, pw, ph, gx, gy};
}

void StackyFace::ApplyBrows(const BrowShape& l, const BrowShape& r) {
    if (last_brow_[0] == l.inner && last_brow_[1] == l.outer && last_brow_[2] == r.inner &&
        last_brow_[3] == r.outer) {
        return;
    }
    // Expression and mode both push the brows, and they STACK: "shocked" already
    // lifts the inner end by 13, and thinking mode lifts it another 7. That put
    // the point above the top of the widget, i.e. at a negative coordinate.
    // Clamp rather than hand-balance the table - the table should stay readable.
    const auto brow_y = [](int off) {
        return std::clamp(kBrowY + off, -kFaceH / 2 + kBrowThick, kFaceH / 2 - kBrowThick);
    };

    // Outer ends point away from the nose; inner ends point toward it.
    FacePoint(brow_l_pts_[0], -kEyeSpread - kBrowHalfW, brow_y(l.outer));
    FacePoint(brow_l_pts_[1], -kEyeSpread + kBrowHalfW, brow_y(l.inner));
    FacePoint(brow_r_pts_[0], kEyeSpread - kBrowHalfW, brow_y(r.inner));
    FacePoint(brow_r_pts_[1], kEyeSpread + kBrowHalfW, brow_y(r.outer));
    lv_line_set_points(brow_l_, brow_l_pts_, 2);
    lv_line_set_points(brow_r_, brow_r_pts_, 2);
    last_brow_[0] = l.inner;
    last_brow_[1] = l.outer;
    last_brow_[2] = r.inner;
    last_brow_[3] = r.outer;
}

void StackyFace::ApplyMouth(int w, int curve, int open_h) {
    if (w == last_mouth_w_ && curve == last_mouth_curve_ && open_h == last_mouth_open_) {
        return;
    }

    if (open_h > 0) {
        lv_obj_set_size(mouth_ring_, std::max(w, open_h + 8), open_h);
        lv_obj_remove_flag(mouth_ring_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
    } else {
        // A parabola through five points. In screen coordinates y grows
        // downward, so a positive curve pushes the CENTRE down and the corners
        // stay put - which is a smile.
        static const int kBow[5] = {0, 75, 100, 75, 0};
        for (int i = 0; i < 5; i++) {
            const int x = -w / 2 + (w * i) / 4;
            FacePoint(mouth_pts_[i], x, kMouthY + curve * kBow[i] / 100);
        }
        lv_line_set_points(mouth_line_, mouth_pts_, 5);
        lv_obj_remove_flag(mouth_line_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(mouth_ring_, LV_OBJ_FLAG_HIDDEN);
    }

    last_mouth_w_ = w;
    last_mouth_curve_ = curve;
    last_mouth_open_ = open_h;
}

void StackyFace::Tick() {
    // No DisplayLockGuard: this is an lv_timer callback, so the LVGL task is
    // already inside the port lock. Taking it again would deadlock.
    if (face_ == nullptr) {
        return;
    }

    // Before the early return below: a muted robot must show it whatever else
    // he is doing, including asleep behind the screensaver. Same for a robot
    // with nowhere to send what he hears - that state outlives every screen he
    // might be showing.
    ApplyMuteBadge();
    ApplyNoServerBadge();

    // Pick up a sleep transition requested from the timer task. All the LVGL
    // work happens HERE, in the LVGL task - see SetPowerSaveMode.
    // With no status source there is nothing to show, so the display just dims
    // like any other board.
    const bool want_saver = saver_want_ && status_ != nullptr;
    if (want_saver != saver_on_) {
        saver_on_ = want_saver;
        if (saver_on_) {
            saver_index_ = 0;
            saver_ticks_ = 0;
            PaintCard();
            lv_obj_move_foreground(saver_);
            lv_obj_remove_flag(saver_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(saver_, LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Asleep: the face is behind an opaque overlay, so animating it would burn
    // CPU on pixels nobody can see. Just rotate the card.
    if (saver_on_) {
        if (++saver_ticks_ >= kCardSeconds * 1000 / kTickMs) {
            saver_ticks_ = 0;
            saver_index_++;
            PaintCard();
        }
        return;
    }

    tick_++;

    // --- blink -------------------------------------------------------------
    int open = 100;
    if (blink_frame_ >= 0) {
        open = kBlink[blink_frame_];
        if (++blink_frame_ >= kBlinkFrames) {
            blink_frame_ = -1;
            // 2.0s - 6.0s. Regular blinking looks mechanical; the jitter is the
            // whole point.
            blink_countdown_ = 40 + static_cast<int>(esp_random() % 80);
        }
    } else if (--blink_countdown_ <= 0) {
        blink_frame_ = 0;
    }

    // --- gaze --------------------------------------------------------------
    // Idle saccades, applied to the PUPILS. A face holding a dead-centre stare
    // reads as switched off, and this is the cheapest fix for that.
    if (--gaze_countdown_ <= 0) {
        gaze_target_x_ = static_cast<int>(esp_random() % 13) - 6;
        gaze_target_y_ = static_cast<int>(esp_random() % 9) - 4;
        gaze_countdown_ = 30 + static_cast<int>(esp_random() % 60);
    }
    if (gaze_x_ < gaze_target_x_) gaze_x_++;
    if (gaze_x_ > gaze_target_x_) gaze_x_--;
    if (gaze_y_ < gaze_target_y_) gaze_y_++;
    if (gaze_y_ > gaze_target_y_) gaze_y_--;

    // --- mode on top of expression ----------------------------------------
    EyeShape l = shape_.left;
    EyeShape r = shape_.right;
    BrowShape bl = shape_.brow_l;
    BrowShape br = shape_.brow_r;
    int mouth_w = shape_.mouth_w;
    int mouth_curve = shape_.mouth_curve;
    int open_target = shape_.mouth_open;

    switch (mode_) {
        case Mode::kListening:
            // Attentive: eyes open wider, brows lift, gaze straight at you.
            l.h = std::min<int>(66, l.h + 6);
            r.h = std::min<int>(66, r.h + 6);
            l.lid_top = r.lid_top = 0;
            bl.outer -= 3; bl.inner -= 3;
            br.outer -= 3; br.inner -= 3;
            gaze_target_x_ = 0;
            gaze_target_y_ = 0;
            break;
        case Mode::kThinking:
            // Narrowed, one brow up, looking away - the universal "working
            // on it".
            l.h = std::max<int>(24, l.h / 2);
            r.h = std::max<int>(30, r.h / 2);
            l.lid_bottom = r.lid_bottom = 0;
            bl.inner += 4;
            br.inner -= 7; br.outer -= 5;
            gaze_target_x_ = -7;
            gaze_target_y_ = -5;
            open_target = 0;
            mouth_curve = -2;
            break;
        case Mode::kSpeaking:
            // Re-rolled every third tick - roughly syllable rate - so the mouth
            // tracks speech rhythm loosely without needing the audio envelope.
            // On the other two ticks the target is the current height, which
            // means no change, which means no repaint at all.
            if (tick_ % 3 == 0) {
                open_target = 8 + static_cast<int>(esp_random() % 20);
            } else {
                open_target = mouth_h_;
            }
            break;
        case Mode::kIdle:
            break;
    }

    ApplyEye(left_, l, open, gaze_x_, gaze_y_, last_l_);
    ApplyEye(right_, r, open, gaze_x_, gaze_y_, last_r_);
    ApplyBrows(bl, br);

    // Ease the mouth rather than snapping it, or speech looks like a strobe.
    mouth_h_ += (open_target - mouth_h_ + 1) / 2;
    if (mouth_h_ < 0) mouth_h_ = 0;
    ApplyMouth(mouth_w, mouth_curve, mouth_h_ >= 6 ? mouth_h_ : 0);
}

void StackyFace::SetupUI() {
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called twice, ignoring");
        return;
    }

    // The base class builds the screen, top bar, status bar and chat bar. Only
    // the centre - the emoji box - is ours.
    SpiLcdDisplay::SetupUI();

    DisplayLockGuard lock(this);
    BuildFace();
    BuildScreensaver();
    anim_timer_ = lv_timer_create(
        [](lv_timer_t* t) { static_cast<StackyFace*>(lv_timer_get_user_data(t))->Tick(); },
        kTickMs, this);
    ESP_LOGI(TAG, "Face up: cartoon eyes and brows at %dms/frame, chat bar capped at %dpx",
             kTickMs, kChatMaxH);
}

void StackyFace::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    shape_ = ShapeFor(emotion);
    // Look up on a change so the expression lands with a small movement rather
    // than appearing fully formed.
    gaze_countdown_ = 8;
}

void StackyFace::SetStatus(const char* status) {
    SpiLcdDisplay::SetStatus(status);
    if (status == nullptr) {
        return;
    }
    bool cleared = false;
    {
        DisplayLockGuard lock(this);
        if (strcmp(status, Lang::Strings::LISTENING) == 0) {
            // 🔴 THINKING OUTRANKS LISTENING HERE, and the order matters.
            //    The device stays in LISTENING for the whole time the model is
            //    working, and any status refresh during that window would
            //    otherwise wipe the thinking face and put him back to
            //    attentively waiting - exactly the wrong thing to show while he
            //    is busy.
            if (!thinking_) {
                mode_ = Mode::kListening;
            }
        } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
            cleared = thinking_;
            SetThinkingInternal(false);
            mode_ = Mode::kSpeaking;
        } else if (strcmp(status, Lang::Strings::CONNECTING) == 0) {
            mode_ = Mode::kThinking;
        } else {
            cleared = thinking_;
            SetThinkingInternal(false);
            mode_ = Mode::kIdle;
        }
    }
    // Outside the lock, and NOT optional: without it the head keeps the
    // thinking pose after he has started speaking. Speaking normally arrives
    // via SetChatMessage("assistant") first, but a reply that is aborted, or
    // one that is all tool call and no text, only ever comes through here.
    if (cleared && on_thinking_) {
        on_thinking_(false);
    }
}

// Caller holds the display lock. The callback is invoked OUTSIDE it by the
// caller's own scoping - see SetChatMessage - because it reaches the head.
void StackyFace::SetThinkingInternal(bool on) {
    if (thinking_ == on) {
        return;
    }
    thinking_ = on;
    if (on) {
        mode_ = Mode::kThinking;
    }
}

void StackyFace::SetChatMessage(const char* role, const char* content) {
    SpiLcdDisplay::SetChatMessage(role, content);
    if (role == nullptr) {
        return;
    }

    bool want = thinking_;
    // "user" is the transcript of what was just said - his turn starts now.
    // An empty body is the framework clearing the line, not a new utterance.
    if (strcmp(role, "user") == 0 && content != nullptr && content[0] != '\0') {
        want = true;
    } else if (strcmp(role, "assistant") == 0) {
        want = false;
    }
    if (want == thinking_) {
        return;
    }

    {
        DisplayLockGuard lock(this);
        SetThinkingInternal(want);
    }
    // Outside the display lock: this ends up commanding servos, and holding
    // the LVGL port lock across a UART transaction is how a face stops
    // repainting while the head moves.
    if (on_thinking_) {
        on_thinking_(want);
    }
}
