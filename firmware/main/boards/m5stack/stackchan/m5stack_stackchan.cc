#include "stackchan_head.h"
#include "stackchan_leds.h"
#include "stacky_face.h"
#include "stacky_settings.h"
#include <esp_app_desc.h>
#include <esp_netif.h>
#include "status_source.h"
#include "wifi_board.h"
#include "cores3_audio_codec.h"
#include "display/lcd_display.h"
#include "application.h"
#include "config.h"
#include "power_save_timer.h"
#include "settings.h"
#include "i2c_device.h"
#include "axp2101.h"
#include "assets/lang_config.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>   // screendump only
#include <cmath>
#include <cstring>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_ili9341.h>
#include <esp_timer.h>
// ⚠️ KEPT FOR THE V4L2_PIX_FMT_* CONSTANTS ONLY. The esp_video driver itself
//    is no longer built into this board - see InitializeCamera - but the frame
//    format the camera reports is still compared against these fourccs.
#include "esp_video.h"
#include "esp32_camera.h"
#include "mcp_server.h"
#include "lvgl_display/lvgl_image.h"
#include "lvgl_display/lvgl_theme.h"
// NOTE: do NOT include <linux/videodev2.h> here. esp_video.h already pulls in
// the managed component's copy, and including it again redefines every
// V4L2_PIX_FMT_* constant - which is a hard error under -Werror.

#define TAG "M5StackStackChanBoard"


class Pmic : public Axp2101 {
public:
    // Power Init
    Pmic(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : Axp2101(i2c_bus, addr) {
        uint8_t data = ReadReg(0x90);
        data |= 0b10110100;
        WriteReg(0x90, data);
        WriteReg(0x99, (0b11110 - 5));
        WriteReg(0x97, (0b11110 - 2));
        WriteReg(0x69, 0b00110101);
        WriteReg(0x30, 0b111111);
        WriteReg(0x90, 0xBF);
        WriteReg(0x94, 33 - 5);
        WriteReg(0x95, 33 - 5);
    }

    void SetBrightness(uint8_t brightness) {
        brightness = ((brightness + 641) >> 5);
        WriteReg(0x99, brightness);
    }
};

class CustomBacklight : public Backlight {
public:
    CustomBacklight(Pmic *pmic) : pmic_(pmic) {}

    void SetBrightnessImpl(uint8_t brightness) override {
        pmic_->SetBrightness(target_brightness_);
        brightness_ = target_brightness_;
    }

private:
    Pmic *pmic_;
};

class Aw9523 : public I2cDevice {
public:
    // Exanpd IO Init
    Aw9523(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        WriteReg(0x02, 0b00000111);  // P0
        WriteReg(0x03, 0b10001111);  // P1
        WriteReg(0x04, 0b00011000);  // CONFIG_P0
        WriteReg(0x05, 0b00001100);  // CONFIG_P1
        WriteReg(0x11, 0b00010000);  // GCR P0 port is Push-Pull mode.
        WriteReg(0x12, 0b11111111);  // LEDMODE_P0
        WriteReg(0x13, 0b11111111);  // LEDMODE_P1
    }

    void ResetAw88298() {
        ESP_LOGI(TAG, "Reset AW88298");
        WriteReg(0x02, 0b00000011);
        vTaskDelay(pdMS_TO_TICKS(10));
        WriteReg(0x02, 0b00000111);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    void ResetIli9342() {
        ESP_LOGI(TAG, "Reset IlI9342");
        WriteReg(0x03, 0b10000001);
        vTaskDelay(pdMS_TO_TICKS(20));
        WriteReg(0x03, 0b10000011);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
};

class Ft6336 : public I2cDevice {
public:
    struct TouchPoint_t {
        int num = 0;
        int x = -1;
        int y = -1;
    };
    
    Ft6336(i2c_master_bus_handle_t i2c_bus, uint8_t addr) : I2cDevice(i2c_bus, addr) {
        uint8_t chip_id = ReadReg(0xA3);
        ESP_LOGI(TAG, "Get chip ID: 0x%02X", chip_id);
        read_buffer_ = new uint8_t[6];
    }

    ~Ft6336() {
        delete[] read_buffer_;
    }

    void UpdateTouchPoint() {
        ReadRegs(0x02, read_buffer_, 6);
        tp_.num = read_buffer_[0] & 0x0F;
        tp_.x = ((read_buffer_[1] & 0x0F) << 8) | read_buffer_[2];
        tp_.y = ((read_buffer_[3] & 0x0F) << 8) | read_buffer_[4];
    }

    inline const TouchPoint_t& GetTouchPoint() {
        return tp_;
    }

private:
    uint8_t* read_buffer_ = nullptr;
    TouchPoint_t tp_;
};

// 🧪 Ask the GC0308 what its auto-exposure actually settled on.
//
// The frame comes back at mean luma 29 out of 255 with nothing anywhere near
// clipping, in a lit room, with a clean lens. The sensor has enormous headroom
// it is not using, so the question is whether its AEC is running and out of
// range, or not running at all. Nothing in the driver's V4L2 surface answers
// that - it exposes HFLIP and VFLIP and nothing else - but the sensor is on the
// SAME I2C BUS this board already owns, at SCCB address 0x21, because we pass
// our own bus handle in with init_sccb = false. So we can just ask it.
//
// WHAT IT SAID, and why this namespace now writes as well as reads:
//
//   exp=480  gain=0x14  aec=0x90(on)  targ=72  HB=0x32 VB=0x0C   -> mean 30
//
// The AEC is ON and has been the whole time. Its target is 72 and it delivers
// 30, so it is not broken, it is CLAMPED - and exposure at 480 rows against a
// frame of roughly 488+VB(12) is pinned at very nearly the whole frame. There
// is no exposure left to give at 20fps.
//
// 🔴 SO STOP ASKING FOR 20fps. A still photo does not need a frame rate. For the
//    duration of one shot the sensor is taken off auto, given a much longer
//    frame to expose into, metered against the frame we actually get, and then
//    put back exactly as it was. This is a real exposure - light collected on
//    the sensor - as opposed to the tone curve further down, which can only
//    stretch what already arrived and turns noise into a milky grey when asked
//    to do too much. That was the "bleached out" picture.
//
// 🔴 EVERY WRITE IS RESTORED, ON EVERY PATH. The driver believes it owns this
//    sensor; we are borrowing it between frames. Leaving the AEC off or the
//    frame length stretched would break the next photo and every preview after
//    it, in a way that would look like a fresh bug rather than this one.
//
// 🔴 AND IT ALL FAILS SOFT. I2cDevice would be the natural way to talk to 0x21
//    and it is the wrong one: its constructor is
//    ESP_ERROR_CHECK(i2c_master_bus_add_device) and 0x21 is already registered
//    by the SCCB driver, so a duplicate-address rejection would abort and reboot
//    the robot. That is the exact failure class already fixed twice here. If the
//    bus will not give us the sensor, the photo still happens, just as it did
//    before any of this existed.
namespace gc0308 {

constexpr uint8_t kAddr = 0x21;
// The exposure register pair is twelve bits wide. Not a tuning choice - the
// hardware's limit, and the only one of the old constants that was ever a fact.
constexpr int kExpMax = 4000;

// 📐 WHAT THE LEVERS ARE WORTH, measured on a held scene with the AEC off:
//
//        exp  240=27 480=30 740=34 1200=41 2000=51 3000=60
//        gain 14=34  1C=39  24=43  2C=46   34=49   3C=52
//
// 🔴 THE OUTPUT GOES AS ROUGHLY THE CUBE ROOT OF THE LIGHT. That is the ISP's
//    gamma, and it is why this took three days: twelve times the exposure is
//    barely twice the picture, so EVERY single-register experiment reported "no
//    visible change" and two working levers were written off - gain explicitly,
//    in a comment, as measured noise.
//
// ⚠️ SO CORRECT BY THE CUBE, never by a ratio. A linear correction on a gamma'd
//    measurement creeps so slowly it reads as another dead lever.
//
// ⚠️ AND NEITHER LEVER REACHES A GOOD EXPOSURE ALONE: exposure alone stops at
//    mean 60, gain alone at 52, and a lit room wants about 100. It takes both,
//    which is why every attempt at one knob at a time failed.
// 🔴 0x50 IS THE GLOBAL GAIN, and that was measured rather than looked up.
//
//    gc0308_regs.h names seven registers and none of them is a gain, so a
//    one-shot sweep raised each plausible candidate in turn and reported what
//    happened to the mean. Against a baseline of 62:
//
//        0x50 -> 81      0x53-0x56 -> 83
//        0x70 -> 57      0x71      -> 54
//        0x72 -> 50      0xb1-0xb3 -> 51
//
//    Two levers, four red herrings. 0x50 is the one used here because it is a
//    single register with a single meaning; 0x53-0x56 move together and look
//    like per-channel trims, which is a colour risk for the same brightness.
//
//    ⚠️ THE RESPONSE IS SUBLINEAR - 0x14 to 0x60 is 4.8x on the register and
//       1.3x on the picture - so gain is driven in STEPS with the result
//       measured each time, never by a formula. Assuming it was linear would
//       overshoot wildly on the first correction.
constexpr uint8_t kGainReg = 0x50;
// 🔴 SIX BITS. The old sweep ran this to 0xFF and reported chaos plus four dead
//    frames; more than half of it was simply out of range.
constexpr uint8_t kGainMax = 0x3C;

// ⏱️ HORIZONTAL BLANKING IS A DEAD LEVER HERE, and that is worth writing down so
//    nobody spends an evening on it twice.
//
//    Exposure is counted in ROWS and both row-count levers are spent - VB is 8
//    bits and pinned at 0xFF - so a LONGER ROW looked like the one remaining way
//    to collect more light. It is not. Sweeping register 0x01 with everything
//    else held:
//
//        HB=0x32:42   0x60:42   0xA0:39   0xFF:36
//
//    No improvement, drifting slightly the wrong way. Whatever ties this
//    sensor's integration time to its frame, it is not the row length, so
//    nothing here writes that register.

i2c_master_dev_handle_t Dev(i2c_master_bus_handle_t bus) {
    static i2c_master_dev_handle_t dev = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address = kAddr;
        cfg.scl_speed_hz = 100 * 1000;   // matches the SCCB config in InitializeCamera
        const esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &dev);
        if (err != ESP_OK) {
            dev = nullptr;
            ESP_LOGW(TAG, "sensor access unavailable: %s", esp_err_to_name(err));
        }
    }
    return dev;
}

int Rd(i2c_master_dev_handle_t dev, uint8_t reg) {
    uint8_t v = 0;
    if (dev == nullptr) return -1;
    if (i2c_master_transmit_receive(dev, &reg, 1, &v, 1, 100) != ESP_OK) return -1;
    return v;
}

void Wr(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
    if (dev == nullptr) return;
    uint8_t b[2] = {reg, val};
    i2c_master_transmit(dev, b, 2, 100);
}

void SetExposure(i2c_master_bus_handle_t bus, int rows) {
    auto* dev = Dev(bus);
    if (rows < 16) rows = 16;
    if (rows > kExpMax) rows = kExpMax;
    Wr(dev, 0x03, static_cast<uint8_t>((rows >> 8) & 0x0F));
    Wr(dev, 0x04, static_cast<uint8_t>(rows & 0xFF));
}

// 🔴 BITS 5:4 OF 0x50 MUST NOT BOTH BE ZERO, or the sensor stops producing
//    pixels entirely - it emits a uniform mid-grey frame, which renders as a flat
//    coloured rectangle and meters at exactly 128.
//
//    Measured, across every value a sweep tried, with no exceptions:
//
//        0x40 0x44 0x80 0xC0   bits 5:4 clear   ->  DEAD FRAME
//        0x20 0x60 0x70 0x90 0xB0 0xFF          ->  real image
//
//    This is not a documented fact anywhere we can find; gc0308_regs.h does not
//    even name 0x50 as a gain. It was found because the metering loop stepped
//    0x14 -> 0x2C -> 0x44 and landed on one, and the whole photo came back green.
//
//    So the write is clamped up rather than refused: a caller asking for a value
//    in that hole gets the nearest working one, and a photo, instead of a
//    rectangle. Belt and braces - the metering path no longer drives gain at all
//    (see the loop in RegisterCameraTool) - but this is the layer that knows why.
constexpr uint8_t kGainFieldMask = 0x30;

// 🎨 WE DO NOT PROGRAM THE AWB GAINS, AND THAT IS A FINDING RATHER THAN AN
//    OVERSIGHT.
//
//    M5Stack's esp32-camera driver writes 0x5a/0x5b/0x5c = 0x56/0x40/0x4a,
//    labelled AWB_R/G/B_GAIN, and our mode table does not - which looked like
//    the reason a lit room photographed monochrome. Writing them changed
//    nothing, and reading them back across several photos said why:
//
//        56/40/4A   then   44/40/60   then   61/40/44
//
//    The sensor's own AWB drives those registers continuously. Anything written
//    at init is replaced within a frame or two, so this was a no-op that looked
//    like a fix. The diagnostic still reports both triples, which is where the
//    evidence belongs.
//
//    The picture was never a white-balance problem. See the gain walk in
//    show_photo: exposure was pinned at the ceiling with the amplifier idle.

// 🔆 LENGTHEN THE FRAME SO THE SENSOR'S AEC CAN EXPOSE PROPERLY.
//
//    Exposure cannot exceed the frame, and this mode's frame is about 252 rows
//    (240 plus 0x0C of vertical blanking). Measured on the ISP path, the AEC
//    asks for 480 and is clamped, so a lit room arrives at mean 34 and the
//    ISP's gamma lifts it into a flat, milky picture.
//
//    Stretching vertical blanking roughly doubles the room available. This is
//    the ONE thing the old manual path did that is worth keeping - it also
//    pinned exposure and walked gain, which is what blew the highlights when
//    fed to the sensor's own ISP.
//
// ⚠️ The sensor still decides. We are moving the ceiling, not choosing the
//    exposure, which is the difference between helping its AEC and replacing it.
// 🎯 AND THE AEC TARGET IS LEFT ALONE, MEASURED. Its default is 72 and moving
//    it to 60 - meant to protect highlights that were riding a little hot -
//    made the picture visibly worse, not better.
//
//    The reading: the ISP's gamma and contrast are tuned around the frames the
//    sensor's OWN target produces, so shifting the target fights that curve
//    rather than feeding it. Whatever finally trims the exposure on this path,
//    it is not register 0xd3.
void ExtendFrame(i2c_master_bus_handle_t bus) {
    auto* dev = Dev(bus);
    if (dev == nullptr) return;
    Wr(dev, 0x02, 0xFF);   // vertical blanking, maximum
    ESP_LOGI(TAG, "camera: frame lengthened (VB=0xFF) so the AEC has room");
}

void SetGain(i2c_master_bus_handle_t bus, int g) {
    if (g < 0) g = 0;
    if (g > kGainMax) g = kGainMax;
    if ((g & kGainFieldMask) == 0) {
        g |= 0x10;
    }
    Wr(Dev(bus), kGainReg, static_cast<uint8_t>(g));
}

std::string Describe(i2c_master_bus_handle_t bus) {
    auto* dev = Dev(bus);
    if (dev == nullptr) return "sensor unreadable";

    auto rd = [dev](uint8_t reg) -> int { return Rd(dev, reg); };

    const int page = rd(0xfe);
    const int id = rd(0x00);
    const int hb = rd(0x01);      // horizontal blanking - sets the row time
    const int vb = rd(0x02);      // vertical blanking - sets the frame time, and
                                  // the frame time is the exposure CEILING
    const int exp_h = rd(0x03);
    const int exp_l = rd(0x04);
    const int gain = rd(0x50);    // global gain
    const int aec_mode = rd(0xd2);  // bit 7 is the AEC enable
    const int aec_targ = rd(0xd3);  // target luma the AEC drives towards
    const int aec_win = rd(0xec);
    // 🎨 The two AWB triples the drivers disagree about - see awb-diag. Read,
    //    not assumed: a value written at init can be overwritten by anything
    //    that runs afterwards, and "I wrote it" is not "it is there".
    const int a57 = rd(0x57), a58 = rd(0x58), a59 = rd(0x59);
    const int a5a = rd(0x5a), a5b = rd(0x5b), a5c = rd(0x5c);

    char buf[260];
    snprintf(buf, sizeof(buf),
             "page=0x%02X id=0x%02X exp=%d gain=0x%02X aec=0x%02X(%s) targ=%d win=0x%02X "
             "HB=0x%02X VB=0x%02X awb57=%02X/%02X/%02X awb5a=%02X/%02X/%02X",
             page, id, (exp_h < 0 || exp_l < 0) ? -1 : ((exp_h << 8) | exp_l), gain, aec_mode,
             (aec_mode > 0 && (aec_mode & 0x80)) ? "on" : "OFF", aec_targ, aec_win, hb, vb,
             a57, a58, a59, a5a, a5b, a5c);
    ESP_LOGI(TAG, "sensor: %s", buf);
    return buf;
}

}  // namespace gc0308

// 📊 WHAT IS IN A FRAME. Two measurements, no conversion.
//
//    The sensor's ISP emits RGB565 and the panel takes it unchanged, so nothing
//    here converts anything any more. What used to live in this namespace - a
//    YUV422 to RGB565 conversion, a tone curve, a per-pixel chroma table and a
//    dither - was six hundred lines written to rescue frames that were three
//    stops under. Metering the exposure properly removed the problem they were
//    hiding, so they went. See git history if they are ever wanted again.
namespace frame {

// 📊 WHAT IS ACTUALLY IN AN RGB565 FRAME.
//
//    The sensor's own ISP output goes to the screen untouched, which means none
//    of the YUV path's diagnostics run and a bad picture has no numbers behind
//    it. These three tell apart faults that look the same from a metre away: a
//    genuinely blown frame, a flat one the ISP has lifted, and a channel order
//    that is wrong.
static void Rgb565Stats(const uint8_t* data, size_t len, char* out, size_t out_len) {
    const uint16_t* px = reinterpret_cast<const uint16_t*>(data);
    const size_t n = len / 2;
    if (n == 0) { snprintf(out, out_len, " rgb=empty"); return; }
    uint32_t sr = 0, sg = 0, sb = 0;
    int rlo = 255, rhi = 0, glo = 255, ghi = 0, blo = 255, bhi = 0;
    for (size_t i = 0; i < n; i++) {
        const uint16_t v = px[i];
        const int r = ((v >> 11) & 0x1F) << 3;
        const int g = ((v >> 5) & 0x3F) << 2;
        const int b = (v & 0x1F) << 3;
        sr += r; sg += g; sb += b;
        if (r < rlo) rlo = r;
        if (r > rhi) rhi = r;
        if (g < glo) glo = g;
        if (g > ghi) ghi = g;
        if (b < blo) blo = b;
        if (b > bhi) bhi = b;
    }
    snprintf(out, out_len, " rgb mean=%lu/%lu/%lu range=%d-%d/%d-%d/%d-%d",
             (unsigned long)(sr / n), (unsigned long)(sg / n), (unsigned long)(sb / n),
             rlo, rhi, glo, ghi, blo, bhi);
}

// 📏 ONE NUMBER FOR A SWEEP. Rgb565Stats formats a string; a loop needs a
//    comparable scalar, decoded exactly as Rgb565Stats decodes so the results
//    line up with every mean already written down.
//
//    The buffer is byte-swapped for the JPEG encoder, so the channels are not
//    individually trustworthy here - but the swap is the same for every shot,
//    and a sweep only needs the levers compared against each other.
static int MeanRgb565(const uint8_t* data, size_t len) {
    const uint16_t* px = reinterpret_cast<const uint16_t*>(data);
    const size_t n = len / 2;
    if (data == nullptr || n == 0) return -1;
    uint32_t sum = 0;
    for (size_t i = 0; i < n; i++) {
        const uint16_t v = px[i];
        sum += static_cast<uint32_t>((((v >> 11) & 0x1F) << 3) +
                                     (((v >> 5) & 0x3F) << 2) +
                                     ((v & 0x1F) << 3)) / 3;
    }
    return static_cast<int>(sum / n);
}

}  // namespace frame

class M5StackStackChanBoard : public WifiBoard {
    StackChanHead head_;
    StackChanLeds leds_;
private:
    i2c_master_bus_handle_t i2c_bus_;
    Pmic* pmic_;
    Aw9523* aw9523_;
    Ft6336* ft6336_;
    LcdDisplay* display_;
    // Same object as display_, kept typed so the board can reach the parts of
    // StackyFace that are not on the Display interface (the idle status screen).
    StackyFace* face_ = nullptr;
    // Either driver - see the switch in the camera init. The shared surface
    // (frame accessors, streaming, preview, explain) lives on Camera itself.
    Camera* camera_;
    esp_timer_handle_t touchpad_timer_;

    // --- on-screen settings -------------------------------------------------
    StackySettings settings_ui_;
    bool settings_built_ = false;

    // 🔇 The privacy switches. Restored from NVS at boot - see
    //    LoadPrivacySettings - because a mute that quietly lapses overnight is
    //    worse than no mute at all.
    bool mic_muted_ = false;
    bool camera_off_ = false;

    // 🔴 LVGL HAS NO POINTER ON THIS BOARD, and that is deliberate rather than an
    //    omission. Touch is polled on a timer and turned into gestures; nothing
    //    in the conversation UI is meant to be tapped, and registering a pointer
    //    would let stray taps scroll and click widgets that were never designed
    //    for it.
    //
    //    The settings menu does need one. So there is an input device, it is
    //    fed from the poll below rather than reading I2C on the LVGL task - one
    //    reader for one chip - and it is DISABLED except while the menu is open.
    lv_indev_t* touch_indev_ = nullptr;
    volatile int32_t touch_x_ = 0, touch_y_ = 0;
    volatile bool touch_down_ = false;
    PowerSaveTimer* power_save_timer_;
    // Optional ambient status - see status_source.h. Null in this build.
    StatusSource* status_ = nullptr;
    // The level he has already reacted to, which is NOT the same thing as the
    // level the source last read - see AnnounceStatusIfChanged.
    StatusSource::Level announced_level_ = StatusSource::Level::kUnknown;

    // Dim after 90s in BOTH power states; only ever power off on battery.
    //
    // 🔴 The upstream design was "disable the whole timer while on USB", driven
    //    from GetBatteryLevel(). That never worked on this board - see the
    //    stale-edge bug noted at GetBatteryLevel() below - so a plugged-in robot
    //    shut itself off after five minutes. It is also the wrong shape: with the
    //    timer disabled on USB there is no screen timeout at all, and the screen
    //    timeout is what the idle status screen hangs off.
    //
    //    So the timer runs ALWAYS, and the power-state decision lives in the
    //    shutdown callback. Sleep is a display concern; shutdown is a battery
    //    concern.
    void InitializePowerSaveTimer() {
        // 90s to dim. cpu_max_freq -1 keeps the CPU/wake-word path untouched -
        // he must still hear his name while the screen is down.
        power_save_timer_ = new PowerSaveTimer(-1, 90, 300);
        power_save_timer_->OnEnterSleepMode([this]() {
            // SetPowerSaveMode is what raises StackyFace's idle status screen.
            GetDisplay()->SetPowerSaveMode(true);
            // 25, not 10. At 10 the panel is a faint glow - fine for a blank
            // sleep screen, useless for a status screen carrying numbers. Still
            // well under the waking level, so it reads as "resting".
            GetBacklight()->SetBrightness(25);
        });
        power_save_timer_->OnExitSleepMode([this]() {
            GetDisplay()->SetPowerSaveMode(false);
            GetBacklight()->RestoreBrightness();
        });
        power_save_timer_->OnShutdownRequest([this]() {
            // Asked once a second once the counter passes 300, so keep it cheap
            // and idempotent. On USB this is simply never the right answer: 550
            // mAh needs protecting, a wall socket does not.
            if (!pmic_->IsDischarging()) {
                return;
            }
            ESP_LOGI(TAG, "Idle on battery, powering off");
            pmic_->PowerOff();
        });
        power_save_timer_->SetEnabled(true);
    }

    void InitializeI2c() {
        // Initialize I2C peripheral
        i2c_master_bus_config_t i2c_bus_cfg = {
            .i2c_port = (i2c_port_t)1,
            .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
            .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .intr_priority = 0,
            .trans_queue_depth = 0,
            .flags = {
                .enable_internal_pullup = 1,
            },
        };
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));
    }

    void InitializeAxp2101() {
        ESP_LOGI(TAG, "Init AXP2101");
        pmic_ = new Pmic(i2c_bus_, 0x34);
    }

    void InitializeAw9523() {
        ESP_LOGI(TAG, "Init AW9523");
        aw9523_ = new Aw9523(i2c_bus_, 0x58);
        vTaskDelay(pdMS_TO_TICKS(50));

        // 🔴 RESET THE AMPLIFIER HERE - EARLY, AND ON EVERY BOOT.
        //
        //    The AW88298 has its own supply and keeps its registers across a soft
        //    reset: the ESP32 rebooting means nothing to it. So without this, the
        //    first open of a boot inherits whatever the previous boot left
        //    behind, and a fault that depends on inherited state alternates
        //    between reboots - which is exactly how this was found ("every other
        //    reboot works", then "every other ding sounds boosted").
        //
        // ⚠️ AND THE TIMING IS THE POINT, not just the reset. Doing this lazily
        //    at the first EnableOutput - which is after I2S is already clocking -
        //    left the chime audibly louder on every other boot while six
        //    consecutive boots read back byte-identical REG61/REG0C. Resetting an
        //    amplifier mid-stream leaves internal state its configuration
        //    registers do not show. Here, nothing is clocking yet and the codec
        //    object does not exist, so it is configured once from a known state.
        //
        //    Upstream's CoreS3 board defines ResetAw88298 and never calls it.
        //
        // 🔴 BUT WAIT FOR THE CHIP TO EXIST FIRST. A reset pulse aimed at a part
        //    whose supply has not come up yet does nothing at all, silently.
        //
        //    That is the difference between the two ways this robot starts:
        //
        //      ordinary reboot     the amp's rail was never cut, so it is
        //                          already up when this runs at ~200ms
        //      wake from power-off the PMIC is bringing that rail up right now,
        //                          and the amp answers some time later
        //
        //    Which is exactly the reported symptom - silent coming out of
        //    hibernation, fine on the next reboot from there - and why it looked
        //    intermittent rather than like the ordering bug it is. Probing costs
        //    nothing on the common path, where the first probe succeeds.
        const int waited = WaitForAmp();
        if (waited < 0) {
            ESP_LOGE(TAG, "amplifier never appeared on I2C - resetting it anyway");
        } else if (waited > 0) {
            ESP_LOGW(TAG, "amplifier took %d ms to appear - its supply was still "
                          "coming up (waking from power-off?)", waited);
        }
        aw9523_->ResetAw88298();
    }

    // Milliseconds waited for the AW88298 to acknowledge its address, 0 if it was
    // already there, -1 if it never answered. A bare address probe, because the
    // codec object does not exist yet at this point in the boot.
    //
    // ⚠️ THE >> 1 IS NOT OPTIONAL. esp_codec_dev takes 8-BIT addresses - which is
    //    why the logs read "dev 6c" and "dev 80" - and i2c_master_probe takes
    //    7-bit ones. Passing the constant straight through probes an address
    //    nothing lives at, and reports a perfectly healthy amplifier missing on
    //    every boot. It did exactly that, which is how this comment exists.
    static constexpr uint8_t kAmpAddr7 = AUDIO_CODEC_AW88298_ADDR >> 1;

    int WaitForAmp() {
        constexpr int kTimeoutMs = 1500;
        constexpr int kStepMs = 25;
        for (int waited = 0; waited <= kTimeoutMs; waited += kStepMs) {
            if (i2c_master_probe(i2c_bus_, kAmpAddr7, kStepMs) == ESP_OK) {
                return waited;
            }
            vTaskDelay(pdMS_TO_TICKS(kStepMs));
        }
        return -1;
    }

    // Three gestures, deliberately far apart in length so none of them can be
    // reached on the way to another:
    //
    //   touch at all      wake the screen
    //   tap, under 500ms  start or stop a conversation
    //   hold 5 SECONDS    enter Wi-Fi configuration mode
    //
    // 🔑 THE HOLD IS THE ONLY WAY BACK TO THE SERVER ADDRESS. The address lives
    //    in NVS and is set on the configuration portal, but the portal is only
    //    served in config mode - and config mode is otherwise entered only when
    //    there is no Wi-Fi saved, or when connecting times out. Without a
    //    gesture, changing servers on a working robot means deliberately
    //    breaking its Wi-Fi first. Every other board in the tree binds this to a
    //    button; this one has a touchscreen instead.
    //
    // ⚠️ Five seconds is long ON PURPOSE. It is not a shortcut, it is a
    //    deliberate act: it drops the conversation and takes the robot off the
    //    network. Nobody should reach it by resting a thumb on the screen.
    void PollTouchpad() {
        static bool was_touched = false;
        static bool hold_fired = false;
        static int64_t touch_start_time = 0;
        constexpr int64_t kTapMs = 500;          // above this is not a tap
        constexpr int64_t kConfigHoldMs = 5000;  // and this is the deliberate hold

        ft6336_->UpdateTouchPoint();
        auto& touch_point = ft6336_->GetTouchPoint();
        const int64_t now_ms = esp_timer_get_time() / 1000;

        // Hand the coordinates to LVGL's input device. Only this poll ever talks
        // to the FT6336; the read callback just reports what was last seen, so
        // the chip has exactly one reader and none of it happens on the LVGL
        // task. Harmless when the device is disabled, which is most of the time.
        touch_x_ = touch_point.x;
        touch_y_ = touch_point.y;
        touch_down_ = touch_point.num > 0;

        // While the menu is open the screen belongs to LVGL: taps are buttons
        // and drags are sliders, not conversation gestures. The hold is still
        // read below so it can also CLOSE the menu.
        const bool in_settings = settings_ui_.visible();

        // touch started
        if (touch_point.num > 0 && !was_touched) {
            was_touched = true;
            hold_fired = false;
            touch_start_time = now_ms;
            // Touching the robot wakes him. It did not before: touch only
            // reached ToggleChatState(), so waking depended on the app changing
            // power save level as a side effect. Poking a dark screen and
            // having nothing happen is the obvious thing a person tries first.
            power_save_timer_->WakeUp();
        }
        // still held - has it been long enough to mean it?
        else if (touch_point.num > 0 && was_touched && !hold_fired) {
            if (now_ms - touch_start_time >= kConfigHoldMs) {
                hold_fired = true;
                // 🔴 NOT FROM HERE. This is the esp_timer task, whose stack is
                //    CONFIG_ESP_TIMER_TASK_STACK_SIZE (3584 bytes), and building
                //    or showing the menu is LVGL work with std::string in it.
                //    Doing that here has crashed this device before. Hand it to
                //    the application loop instead.
                Application::GetInstance().Schedule([this]() { ToggleSettings(); });
            }
        }
        // touch released
        else if (touch_point.num == 0 && was_touched) {
            was_touched = false;
            // The hold already acted, and acting again on release would toggle
            // the chat state of a robot that is now leaving the network.
            if (hold_fired) {
                return;
            }
            const int64_t touch_duration = now_ms - touch_start_time;

            // While the menu is up, a tap is a button press - LVGL has already
            // dealt with it. Toggling the conversation as well would start him
            // talking every time you moved a slider.
            if (in_settings) {
                return;
            }

            // only a short tap toggles chat
            if (touch_duration < kTapMs) {
                auto& app = Application::GetInstance();
                if (app.GetDeviceState() == kDeviceStateStarting) {
                    EnterWifiConfigMode();
                    return;
                }
                app.ToggleChatState();
            }
        }
    }

    // 🔇 One place that puts the mute into effect, so the codec, the badge and
    //    the stored value can never disagree. Called from the settings switch
    //    (persist = true) and once at boot from the stored value (persist =
    //    false, since writing back what was just read is pointless wear).
    void ApplyMicMute(bool muted, bool persist) {
        // 🔴 STORED FIRST. In the first version the write came last, after a call
        //    that deadlocked - so the robot hung AND forgot the setting. For a
        //    privacy switch the durable part is the part that must not depend on
        //    everything after it succeeding.
        if (persist) {
            Settings settings("stackchan", true);
            settings.SetInt("mic_muted", muted ? 1 : 0);
        }
        mic_muted_ = muted;
        static_cast<CoreS3AudioCodec*>(GetAudioCodec())->SetMicMuted(muted);
        if (face_ != nullptr) {
            face_->SetMuted(muted);   // a flag; the LVGL task draws it
        }
    }

    // Read once at boot, before anything opens the microphone.
    // The server address as the firmware ACTUALLY resolves it: NVS first,
    // compiled-in value as the fallback. Mirrors Ota::GetCheckVersionUrl, and
    // exists so the About row and the NO LLM badge read the same thing - two
    // places computing "which server" separately is two places to drift.
    static std::string EffectiveServerUrl() {
        Settings settings("wifi", false);
        std::string server = settings.GetString("ota_url");
        if (server.empty()) server = CONFIG_OTA_URL;
        return server;
    }

    // 🧠 A ROBOT NOBODY HAS GIVEN A SERVER LOOKS EXACTLY LIKE A WORKING ONE.
    //
    //    The release binary ships pointing at a `.invalid` host, which can never
    //    resolve - deliberately, so a forgotten setting fails closed instead of
    //    dialling a stranger. Application says so once at startup and then stops
    //    trying, which is right: an address that cannot resolve will not start
    //    resolving on the fourth attempt. But it leaves an owner with a robot
    //    that wakes, listens, blinks and does nothing, and the only explanation
    //    scrolled off the screen minutes ago.
    //
    // ⚠️ SCOPE: this is "no server has ever been set", not "the server is
    //    down". The placeholder is a fact about configuration, knowable without
    //    a network, and it is checked here rather than inferred from a failed
    //    connection - a robot whose server is merely unreachable is a different
    //    state and deserves a different message, not this one worn as a lie.
    void UpdateNoServerBadge() {
        if (face_ == nullptr) return;
        const std::string url = EffectiveServerUrl();
        const bool unset = url.empty() || url.find(".invalid") != std::string::npos;
        ESP_LOGI(TAG, "server %s (%s)", unset ? "NOT SET - showing the NO LLM badge" : "configured",
                 url.c_str());
        face_->SetNoServer(unset);
    }

    void LoadPrivacySettings() {
        Settings settings("stackchan", false);
        mic_muted_ = settings.GetInt("mic_muted", 0) != 0;
        camera_off_ = settings.GetInt("camera_off", 0) != 0;
        if (mic_muted_ || camera_off_) {
            ESP_LOGW(TAG, "privacy settings restored: microphone %s, camera %s",
                     mic_muted_ ? "MUTED" : "on", camera_off_ ? "OFF" : "on");
        }
    }

    // Built on FIRST USE, not at boot. SetupUI() has not run when this board is
    // constructed, so there is no screen to parent onto yet - and a menu nobody
    // opens should not cost memory. By the time a finger has been held down for
    // five seconds, the display is long since up.
    void ToggleSettings() {
        DisplayLockGuard lock(GetDisplay());
        if (settings_ui_.visible()) {
            settings_ui_.Hide();
            if (touch_indev_ != nullptr) lv_indev_enable(touch_indev_, false);
            ESP_LOGI(TAG, "settings closed");
            return;
        }
        EnsureTouchIndev();
        if (!settings_built_) {
            settings_built_ = true;
            settings_ui_.Build(MakeSettingsActions());
        }
        settings_ui_.Show();
        if (touch_indev_ != nullptr) lv_indev_enable(touch_indev_, true);
        ESP_LOGI(TAG, "settings opened");
    }

    StackySettings::Actions MakeSettingsActions() {
        StackySettings::Actions a;
        a.wifi_setup = [this]() { EnterWifiConfigMode(); };
        a.self_check = [this]() {
            // The same check the boot chime reports, on demand - which is what
            // the factory firmware's "Hardware Test" was for. It takes seconds
            // and ends in a sound, so it runs on its own task rather than
            // freezing the menu.
            xTaskCreate([](void* arg) {
                static_cast<M5StackStackChanBoard*>(arg)->BootSelfCheck();
                vTaskDelete(nullptr);
            }, "self_check", 4096, this, 3, nullptr);
        };
        a.get_volume = [this]() { return GetAudioCodec()->output_volume(); };
        a.set_volume = [this](int v) { GetAudioCodec()->SetOutputVolume(v); };
        a.get_brightness = [this]() { return (int)GetBacklight()->brightness(); };
        // permanent: the point of setting it here is that it survives the next
        // time he dims and wakes.
        a.set_brightness = [this](int v) {
            GetBacklight()->SetBrightness((uint8_t)v, true);
        };
        // 🔇 Persisted, because a microphone you switched off should still be off
        //    in the morning. Written immediately rather than on close: the way
        //    people test a mute is to switch it off and pull the power.
        a.get_mic_muted = [this]() { return mic_muted_; };
        a.set_mic_muted = [this](bool muted) { ApplyMicMute(muted, true); };
        a.get_camera_off = [this]() { return camera_off_; };
        a.set_camera_off = [this](bool off) {
            camera_off_ = off;
            Settings settings("stackchan", true);
            settings.SetInt("camera_off", off ? 1 : 0);
            ESP_LOGW(TAG, "camera %s", off ? "OFF" : "on");
        };

        a.about_rows = [this]() {
            // "Which server is he really using" is the question this answers,
            // so it asks the same helper the NO LLM badge does.
            std::string server = EffectiveServerUrl();
            // Trimmed to what somebody standing in front of the robot is
            // actually checking: which machine, on which port.
            //
            // The scheme carries no information here, and the path is the same
            // on every install - but only ALMOST always, so a non-standard path
            // is still shown rather than hidden. Between them they were two
            // wrapped lines of nothing, which is what pushed the last field off
            // a 240px screen.
            for (const char* scheme : {"http://", "https://"}) {
                const size_t n = strlen(scheme);
                if (server.compare(0, n, scheme) == 0) {
                    server.erase(0, n);
                    break;
                }
            }
            const std::string kUsualPath = "/xiaozhi/ota/";
            if (server.size() > kUsualPath.size() &&
                server.compare(server.size() - kUsualPath.size(), kUsualPath.size(),
                               kUsualPath) == 0) {
                server.erase(server.size() - kUsualPath.size());
            }
            // Asked of esp_netif rather than of the Wi-Fi component: this is the
            // address the router actually handed out, and it does not depend on
            // whatever that component's API happens to look like.
            char ip[24] = "not connected";
            esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
            esp_netif_ip_info_t info;
            if (netif != nullptr && esp_netif_get_ip_info(netif, &info) == ESP_OK &&
                info.ip.addr != 0) {
                snprintf(ip, sizeof(ip), IPSTR, IP2STR(&info.ip));
            }
            return std::vector<std::pair<std::string, std::string>>{
                {"Firmware", esp_app_get_description()->version},
                {"Address", ip},
                {"Server", server},
                {"Servos", ScsServo::calibration_is_fallback() ? "FALLBACK calibration"
                                                               : "factory calibration"},
            };
        };
        return a;
    }

    void InitializeFt6336TouchPad() {
        ESP_LOGI(TAG, "Init FT6336");
        ft6336_ = new Ft6336(i2c_bus_, 0x38);
        
        // poll every 20 ms
        esp_timer_create_args_t timer_args = {
            .callback = [](void* arg) {
                M5StackStackChanBoard* board = (M5StackStackChanBoard*)arg;
                board->PollTouchpad();
            },
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "touchpad_timer",
            .skip_unhandled_events = true,
        };
        
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &touchpad_timer_));
        ESP_ERROR_CHECK(esp_timer_start_periodic(touchpad_timer_, 20 * 1000));

    }

    // 🔴 CREATED LATE, UNDER THE LVGL LOCK, AND BOUND TO A DISPLAY EXPLICITLY.
    //
    //    The first version built this in the board constructor, and the result
    //    was a menu that drew perfectly and could not be touched: the screen
    //    still woke on a tap - so the poll was clearly running - but LVGL never
    //    saw a pointer.
    //
    //    Two reasons, either enough on its own. The constructor runs before
    //    SetupUI, so there was no default display for the new device to attach
    //    itself to; and it ran outside the port lock, which is not a safe place
    //    to be mutating LVGL's device list. Built here instead, on first use,
    //    where the caller already holds the lock and the display certainly
    //    exists.
    //
    //    The read callback does no I2C: it reports what the 20ms poll last saw,
    //    so the FT6336 keeps exactly one reader and the LVGL task never waits on
    //    the shared bus.
    void EnsureTouchIndev() {
        if (touch_indev_ != nullptr) {
            return;
        }
        touch_indev_ = lv_indev_create();
        lv_indev_set_type(touch_indev_, LV_INDEV_TYPE_POINTER);
        lv_indev_set_user_data(touch_indev_, this);
        lv_indev_set_display(touch_indev_, lv_display_get_default());
        lv_indev_set_read_cb(touch_indev_, [](lv_indev_t* indev, lv_indev_data_t* data) {
            auto* board = static_cast<M5StackStackChanBoard*>(lv_indev_get_user_data(indev));
            data->point.x = board->touch_x_;
            data->point.y = board->touch_y_;
            data->state = board->touch_down_ ? LV_INDEV_STATE_PRESSED
                                             : LV_INDEV_STATE_RELEASED;
        });
        // Disabled by default: while the conversation UI is on screen, taps are
        // gestures and nothing is meant to be clicked.
        lv_indev_enable(touch_indev_, false);
        ESP_LOGI(TAG, "touch pointer registered for the settings menu");
    }

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = GPIO_NUM_37;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = GPIO_NUM_36;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeIli9342Display() {
        ESP_LOGI(TAG, "Init IlI9342");

        // The display reads its theme by name during construction and defaults
        // to "light" (lcd_display.cc:81) - which is the white screen. Selecting
        // "dark" has to happen BEFORE that read, and writing the setting is the
        // only way to influence it without touching live LVGL objects.
        // Stacky's face is black-and-lavender; a white screen is not the look.
        {
            Settings settings("display", true);
            if (settings.GetString("theme", "light") != "dark") {
                settings.SetString("theme", "dark");
                ESP_LOGI(TAG, "display theme -> dark");
            }
        }

        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;

        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_3;
        io_config.dc_gpio_num = GPIO_NUM_35;
        io_config.spi_mode = 2;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
        panel_config.bits_per_pixel = 16;
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
        
        esp_lcd_panel_reset(panel);
        aw9523_->ResetIli9342();

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, true);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

        // StackyFace, not SpiLcdDisplay: same screen furniture, but the centre
        // is drawn eyes on a timer instead of an emoji glyph. See stacky_face.h.
        // It only builds anything during SetupUI(), which Application::Start
        // calls long after this constructor - nothing here touches LVGL.
        face_ = new StackyFace(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
        display_ = face_;
    }

    // The project look: pure black with lavender. Restyles the registered "dark"
    // theme in place rather than adding a new theme name - `self.screen.set_theme`
    // takes a name from the model, and teaching it a third one it may or may not
    // use is worse than making the one it already asks for correct.
    //
    // 🔴 Background is PURE BLACK, not a dark grey. That contrast is most of the
    //    look, and the stock dark theme's 0x1F1F1F washes it out on an IPS panel
    //    at desk distance.
    //
    // 🔴 DO NOT CALL display_->SetTheme() FROM HERE.
    //
    //    LcdDisplay::SetTheme restyles live LVGL objects, and SetupUI() has not
    //    run yet during board construction - so it dereferences a null content_
    //    and the device boot-loops with `Guru Meditation Error: LoadProhibited`
    //    right after "Reset IlI9342".
    //
    //    Instead: restyle the registered theme's colours in place. The display
    //    picks its theme by NAME at construction (default "light") and applies
    //    the colours later in SetupUI(), so edits made here - after the themes
    //    are registered, before SetupUI - are simply what gets painted. No live
    //    objects are touched.
    void InitializeTheme() {
        auto* theme = LvglThemeManager::GetInstance().GetTheme("dark");
        if (theme == nullptr) {
            ESP_LOGW(TAG, "no dark theme registered; leaving the stock look alone");
            return;
        }
        theme->set_background_color(lv_color_hex(0x000000));
        theme->set_chat_background_color(lv_color_hex(0x000000));
        theme->set_text_color(lv_color_hex(0xC9A9FF));         // lavender
        theme->set_system_text_color(lv_color_hex(0x98A2B3));  // muted grey
        theme->set_assistant_bubble_color(lv_color_hex(0x1A1430));
        theme->set_user_bubble_color(lv_color_hex(0x5933AB));  // deep purple
        theme->set_system_bubble_color(lv_color_hex(0x000000));
        theme->set_border_color(lv_color_hex(0xA17EFF));       // saturated purple
        theme->set_low_battery_color(lv_color_hex(0xFF8E8E));  // soft red
        ESP_LOGI(TAG, "theme applied: black + lavender");
    }

    // 🔴 BOOT CRASH FIX. Wait until a VSYNC pulse has just gone by before
    //    letting the DVP driver arm its interrupt.
    //
    //    The panic this prevents, decoded from the backtrace:
    //
    //      dvp_vsync_isr -> xQueueGenericSendFromISR -> assert -> abort
    //      ...fired from inside gpio_isr_handler_add(), called by
    //         esp_cam_new_dvp_ctlr_ext (esp_cam_ctlr_dvp_cam.c:891)
    //
    //    The driver arms the VSYNC ISR before the queue that ISR posts to is
    //    ready. A frame edge landing in that window sends to a null queue and
    //    the assert takes the whole device down.
    //
    //    Why it only sometimes happens - and it is the same lesson the PY32
    //    taught on the servo rail. Look at the pins:
    //
    //      CAMERA_PIN_XCLK  GPIO_NUM_NC   <- 20MHz external crystal, not ours
    //      CAMERA_PIN_RESET GPIO_NUM_NC   <- no reset line
    //      CAMERA_PIN_PWDN  GPIO_NUM_NC   <- no power-down line
    //
    //    The GC0308 clocks itself and we have no way to stop it. Once it has
    //    been configured it FREE-RUNS, and a soft reset does not touch it. So
    //    on a cold boot the sensor is idle and init is safe, while on a soft
    //    reset - which is what both flashing and esp_restart() produce - VSYNC
    //    is still pulsing and it is a coin flip. That is the "crashes now and
    //    then, but comes back" behaviour exactly.
    //
    //    We cannot silence the sensor and we should not patch a managed
    //    component. But we can choose WHEN to arm the interrupt: sit on the
    //    VSYNC pin, wait for an edge, and start immediately after one. At the
    //    GC0308's frame rate that buys a full frame period of headroom for a
    //    setup path that needs microseconds.
    //
    //    No edge inside the timeout means the sensor is idle - a cold boot -
    //    which is the case that was never at risk.
    void WaitForVsyncGap() {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << CAMERA_PIN_VSYNC;
        io.mode = GPIO_MODE_INPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
        io.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io.intr_type = GPIO_INTR_DISABLE;
        if (gpio_config(&io) != ESP_OK) {
            return;
        }

        const int start = gpio_get_level(CAMERA_PIN_VSYNC);
        // 120ms covers well over one frame at any rate this sensor runs at.
        const int64_t deadline = esp_timer_get_time() + 120000;
        while (esp_timer_get_time() < deadline) {
            if (gpio_get_level(CAMERA_PIN_VSYNC) != start) {
                ESP_LOGI(TAG, "camera: sensor is free-running, starting just after a VSYNC edge");
                return;
            }
        }
        ESP_LOGI(TAG, "camera: no VSYNC seen, sensor idle (cold boot)");
    }

     // 📸 DEV-ONLY SCREEN CAPTURE, over the serial cable and nowhere else.
    //
    //    self.screen.snapshot did this and was removed: it took a URL from the
    //    caller and uploaded to it, which on a device driven by a microphone
    //    with no confirmation step is exfiltration waiting for a misheard
    //    sentence. The CAPABILITY was never the problem - SnapshotToJpeg is
    //    still there - the caller-supplied destination was.
    //
    //    So the frame goes to the serial console, to a cable somebody is
    //    physically holding. No network, no URL, no tool, no arguments.
    //
    // ⚠️ OFF, and it belongs off. Every interval this allocates a 150KB draw
    //    buffer and JPEG-encodes it. Turn it on to look at the interface for an
    //    afternoon, then turn it off.
    //
    //    deploy/capture-screen.ps1 collects the frames and writes .jpg files.
    static constexpr bool kDumpScreenToSerial = false;
    static constexpr int kDumpIntervalMs = 6000;

    void StartScreenDumpTask() {
        if (!kDumpScreenToSerial) {
            return;
        }
        // Its own task: SnapshotToJpeg takes the display lock itself, so this
        // must not be called from anything already holding it - and it needs a
        // real stack, which rules out the esp_timer task (3584 bytes).
        xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<M5StackStackChanBoard*>(arg);
                vTaskDelay(pdMS_TO_TICKS(10000));   // let the UI settle first
                for (;;) {
                    std::string jpeg;
                    // The board's own member, not GetDisplay(): SnapshotToJpeg
                    // lives on LcdDisplay, and the base Display has no idea.
                    auto* display = self->display_;
                    if (display != nullptr && display->SnapshotToJpeg(jpeg, 80) &&
                        !jpeg.empty()) {
                        printf("\nSCREENDUMP BEGIN %u\n",
                               static_cast<unsigned>(jpeg.size()));
                        // 57 raw bytes -> 76 base64 chars, a comfortable line.
                        constexpr size_t kRaw = 57;
                        unsigned char line[128];
                        for (size_t off = 0; off < jpeg.size(); off += kRaw) {
                            const size_t n = std::min(kRaw, jpeg.size() - off);
                            size_t out_len = 0;
                            if (mbedtls_base64_encode(
                                    line, sizeof(line), &out_len,
                                    reinterpret_cast<const unsigned char*>(jpeg.data() + off),
                                    n) == 0) {
                                line[out_len] = '\0';
                                printf("SD:%s\n", reinterpret_cast<char*>(line));
                            }
                        }
                        printf("SCREENDUMP END\n");
                    } else {
                        ESP_LOGW(TAG, "screendump: snapshot failed");
                    }
                    vTaskDelay(pdMS_TO_TICKS(kDumpIntervalMs));
                }
            },
            "screendump", 6144, this, 1, nullptr);
        ESP_LOGW(TAG, "screendump: ON - frames go to the serial console every %d ms",
                 kDumpIntervalMs);
    }

    void InitializeCamera() {
        WaitForVsyncGap();

        camera_config_t cam = {};
        cam.pin_d0 = CAMERA_PIN_D0;
        cam.pin_d1 = CAMERA_PIN_D1;
        cam.pin_d2 = CAMERA_PIN_D2;
        cam.pin_d3 = CAMERA_PIN_D3;
        cam.pin_d4 = CAMERA_PIN_D4;
        cam.pin_d5 = CAMERA_PIN_D5;
        cam.pin_d6 = CAMERA_PIN_D6;
        cam.pin_d7 = CAMERA_PIN_D7;
        cam.pin_pclk = CAMERA_PIN_PCLK;
        cam.pin_vsync = CAMERA_PIN_VSYNC;
        cam.pin_href = CAMERA_PIN_HREF;
        // ⚠️ -1, not a pin. The sensor has its own 20 MHz crystal, so the
        //    ESP32 must not drive a clock at it, and SCCB reuses the I2C
        //    bus this board already owns - port 1 - instead of installing a
        //    second driver on top of the amp, the codec and the PY32.
        cam.pin_xclk = -1;
        cam.pin_sccb_sda = -1;
        cam.pin_sccb_scl = -1;
        cam.sccb_i2c_port = 1;
        cam.pin_pwdn = -1;
        cam.pin_reset = -1;
        cam.xclk_freq_hz = XCLK_FREQ_HZ;
        // The vendor's own settings, from their StackChan example and from
        // upstream's AtomS3R-CAM board file - the same GC0308.
        cam.pixel_format = PIXFORMAT_RGB565;
        cam.frame_size = FRAMESIZE_QVGA;
        cam.jpeg_quality = 12;
        // 2, as M5Stack's own StackChan example uses. Capture() fetches
        // twice per call to discard a stale frame, and with one buffer under
        // GRAB_WHEN_EMPTY that is a wait for a frame that cannot arrive -
        // the robot froze after its first photograph.
        cam.fb_count = 2;
        cam.fb_location = CAMERA_FB_IN_PSRAM;
        cam.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

        auto* cam_drv = new Esp32Camera(cam);
        // Our show_photo draws the picture full-screen itself.
        cam_drv->SetAutoPreview(false);
        camera_ = cam_drv;

        // 🔆 EXPOSURE COMPENSATION, through the driver rather than by hand.
        //
        //    Measured on the saved frame: mean 28 with a range of 16-224.
        //    Correctly coloured, full contrast, about two stops under.
        //
        //    set_ae_level writes AEC_TARGET_Y (0xd3) as 0x48 + level*8, so
        //    +2 moves the sensor's target from 72 to 88. That is the same
        //    register this project poked by hand yesterday - downwards, on
        //    the theory that highlights needed protecting, which made it
        //    worse. The frame is dark; it wants the other direction.
        //
        // ⚠️ NOT set_brightness. That applies a digital offset, which lifts
        //    the black point and produces the milky picture we have twice
        //    mistaken for a white balance or byte-order fault.
        constexpr int kAeLevel = 2;   // -2..+2; 0 is the sensor's default
        if (sensor_t* sen = esp_camera_sensor_get()) {
            if (sen->set_ae_level != nullptr && sen->set_ae_level(sen, kAeLevel) == 0) {
                ESP_LOGI(TAG, "camera: AE level %+d (target %d)", kAeLevel,
                         0x48 + kAeLevel * 8);
            } else {
                ESP_LOGW(TAG, "camera: AE level %+d refused", kAeLevel);
            }
        }
        camera_->SetHMirror(false);
        // We convert and preview the frame ourselves - see the yuv422 note.
        camera_->SetAutoPreview(false);
    }

public:


    // The PY32 co-processor on the StackChan base: a 16-bit GPIO expander plus
    // LED controller, at I2C 0x6F. Its register map was recovered from the
    // factory firmware and verified on hardware.
    //
    //   regs 3 / 4     pin direction. Setting the bit makes the pin an OUTPUT
    //   regs 9 / 10    drive HIGH
    //   regs 11 / 12   drive LOW
    //   regs 5 / 6     configuration touched by the vendor's rail bring-up
    //   reg 36         LED pixel count + latch (see stackchan_leds.h)
    //
    // A SEPARATE chip at 0x41 reports the servo rail in its register 1 - see
    // EnsureServoRail for how that split was found.
    //
    // 🔴 Three things here were wrong for a long time, and all three are easy to
    //    reintroduce, so they are spelled out:
    //
    // 1. The level pairs are the opposite way round from what a first reading
    //    suggests. The vendor's digitalWrite(pin, HIGH) CLEARS the bit in
    //    (11,12) and SETS it in (9,10). Getting this backwards drives the pin
    //    low, which looks exactly like "the write was ignored".
    //
    // 2. The read-modify-write must ALWAYS issue the write, even when the value
    //    is unchanged - the vendor's accessor is an unconditional read/OR/write.
    //    An `if (new != old) write()` optimisation looks free and is not: at
    //    cold boot regs 9 and 10 both read 0xFF, so ORing in the bit for pin 0
    //    or pin 13 changes nothing and the write is skipped entirely. The two
    //    writes that actually power the rail were the two being elided. The
    //    PY32 acts on the write transaction, not on the resulting value.
    // 🔴 3. A FAILED READ IS NOT A VALUE. This returned 0xFF when the PY32 did
    //       not answer, and Py32Bit below ORed the new bit into that 0xFF and
    //       wrote it back - so ONE naked read rewrote every other pin in the
    //       register as an output driven high, and then reported success.
    //
    //       It is not theoretical. About ten seconds into every boot, as Wi-Fi
    //       associates, this shared bus NAKs for a few hundred milliseconds
    //       (the same window that leaves the amplifier silent - see
    //       cores3_audio_codec.cc). A supervisor tick landing in it logged
    //           IOE 0x6F bring-up -> r3=01 r4=20 r5=FF r9=01 r10=20, rail=0xFF
    //       where the vendor's r5 is 0x01, and the rail stayed off for the rest
    //       of the session. The "recovery" was doing the damage.
    //
    //       So reads report failure and the caller decides. The retries are
    //       here because the glitch is transient: by the time three attempts
    //       5 ms apart have failed, the bus is genuinely gone and guessing
    //       would not have helped anyway.
    static constexpr int kPy32Tries = 3;
    bool Py32Read(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t* out) {
        for (int i = 0; i < kPy32Tries; i++) {
            uint8_t v = 0;
            if (i2c_master_transmit_receive(dev, &reg, 1, &v, 1,
                                            pdMS_TO_TICKS(100)) == ESP_OK) {
                *out = v;
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        return false;
    }
    // FOR THE DIAGNOSTIC LINE ONLY, where an unreadable register should print as
    // something rather than suppress the whole message. Never feed this to a
    // read-modify-write - that is the bug described above.
    uint8_t Py32Peek(i2c_master_dev_handle_t dev, uint8_t reg) {
        uint8_t v = 0xFF;
        Py32Read(dev, reg, &v);
        return v;
    }
    bool Py32Write(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t val) {
        const uint8_t buf[2] = { reg, val };
        for (int i = 0; i < kPy32Tries; i++) {
            if (i2c_master_transmit(dev, buf, 2, pdMS_TO_TICKS(100)) == ESP_OK) {
                return true;
            }
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        return false;
    }
    // Bit accessor, as the vendor firmware does it: bit < 8 uses the low
    // register of the pair, bit >= 8 uses the high one with (bit - 8). Never
    // conditional on the resulting VALUE (see note 2), but skipped entirely if
    // the current value is unknown (note 3).
    bool Py32Bit(i2c_master_dev_handle_t dev, uint8_t reg_low, uint8_t reg_high,
                 uint8_t bit, bool set) {
        uint8_t reg  = (bit >= 8) ? reg_high : reg_low;
        uint8_t mask = 1u << (bit >= 8 ? bit - 8 : bit);
        uint8_t v = 0;
        if (!Py32Read(dev, reg, &v)) {
            ESP_LOGW(TAG, "PY32 reg %u unreadable - skipping the write rather "
                          "than writing a guess over the other pins", reg);
            return false;
        }
        return Py32Write(dev, reg, set ? (uint8_t)(v | mask) : (uint8_t)(v & ~mask));
    }
    bool Py32PinMode(i2c_master_dev_handle_t dev, uint8_t pin, bool output) {
        return Py32Bit(dev, 3, 4, pin, output);
    }
    bool Py32DigitalWrite(i2c_master_dev_handle_t dev, uint8_t pin, bool high) {
        if (high) {
            return Py32Bit(dev, 11, 12, pin, false) && Py32Bit(dev, 9, 10, pin, true);
        }
        return Py32Bit(dev, 9, 10, pin, false) && Py32Bit(dev, 11, 12, pin, true);
    }

    // 🔴 THIS RUNS AGAIN, NOT ONLY AT BOOT. Boot-only was a real bug that looked
    //    like a hardware fault.
    //
    //    Seen on the reference unit: plugged back in after running on battery,
    //    he answered the wake word and talked - and did not move a millimetre.
    //    Nothing in the firmware was wrong; the rail was simply OFF. The boot log
    //    showed it exactly: `servo motor rail ON (was OFF, reg1=0xFF->0x00)`. A
    //    reset fixed it, which is the tell - the bring-up only ever ran in the
    //    constructor, so ANY event that dropped the rail after boot was
    //    permanent until a power cycle.
    //
    //    The rail is the servos' supply and the servos are by far the largest
    //    load on a 550 mAh cell, so something below us - the PY32's own logic or
    //    the PMIC - is entitled to shed it. The ESP32 does not reboot when that
    //    happens, and it is never told.
    //
    // ⚠️ Deliberately NOT diagnosing which event drops it. That would need an
    //    unplugged experiment on a device whose only diagnostic channel is the
    //    USB cable being unplugged. Re-asserting a rail is idempotent and costs
    //    one I2C byte read per check, so the supervisor is cheaper than the
    //    investigation and covers causes nobody has thought of.
    //
    // Returns true if the rail is on when this returns. *recovered is set only
    // when it was found down and brought back - the caller uses that to re-init
    // the servo bus, because servos that lost power come back at an unknown
    // position, and a bus that failed to ping at boot left the head disabled.
    //
    // The last state actually READ from the chip, used only when a later check
    // cannot read it at all. Starts optimistic: on the one boot where the very
    // first read fails there is nothing to recover from yet, the servo ping will
    // say so, and the next tick re-reads. Claiming a dead rail on no evidence
    // would print a hardware fault that is really a busy bus.
    bool rail_known_on_ = true;

    bool EnsureServoRail(bool* recovered = nullptr, bool announce = false) {
        if (recovered != nullptr) *recovered = false;
        // Replays the vendor firmware's base bring-up:
        //
        //   pinMode(0, OUTPUT); digitalWrite(0, HIGH);
        //   set bit 0 in the (5,6) pair
        //   delay 20 ms
        //   pinMode(13, OUTPUT); digitalWrite(13, HIGH);
        //
        // 🔴 THE EXPANDER IS AT 0x6F. NOT 0x41.
        //
        //    This was the whole bug. The vendor logs "PY32IOExpander: Version:
        //    0x41" - that 0x41 is the VERSION VALUE, and it was read as an I2C
        //    address. There happens to be a different chip at address 0x41 whose
        //    register 0 also reads 0x41, which made the mistake self-confirming,
        //    and whose register 1 genuinely does report the rail - so the status
        //    read worked perfectly while every write went to the wrong device.
        //
        //    A full register diff of rail-ON against rail-OFF settled it. Device 0x6F:
        //        ON  : EB 41 41 01 20 01 00 00 00 01 20 ...
        //        OFF : EB 41 41 00 00 00 00 -- -- -- -- ...
        //                       r3 r4 r5        r9 r10
        //    reg 3 = 0x01 (pin 0 output), reg 4 = 0x20 (pin 13 output),
        //    reg 5 = 0x01, reg 9 = 0x01 / reg 10 = 0x20 (both driven HIGH).
        //    Every decoded semantic was right; only the address was wrong.
        //    Note 0x6F reg 1 = 0x41 - THAT is the version the vendor prints.
        const uint8_t kIoeAddr    = 0x6F;   // the expander that drives the rail
        const uint8_t kStatusAddr = 0x41;   // reports the rail in its reg 1

        i2c_device_config_t scfg = {};
        scfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        scfg.device_address = kStatusAddr;
        scfg.scl_speed_hz = 100000;
        i2c_master_dev_handle_t dev = nullptr;
        if (i2c_master_bus_add_device(i2c_bus_, &scfg, &dev) != ESP_OK) {
            ESP_LOGE(TAG, "status chip: not present at 0x41");
            return false;
        }
        i2c_device_config_t icfg = scfg;
        icfg.device_address = kIoeAddr;
        i2c_master_dev_handle_t ioe = nullptr;
        if (i2c_master_bus_add_device(i2c_bus_, &icfg, &ioe) != ESP_OK) {
            ESP_LOGE(TAG, "PY32 IO expander: not present at 0x6F");
            i2c_master_bus_rm_device(dev);
            return false;
        }

        uint8_t ver = Py32Peek(ioe, 0x01);   // version lives here, reads 0x41

        // ⚠️ An unreadable status register is NOT a dead rail. 0xFF is what a
        //    NAK used to look like, and 0xFF reads as OFF - so a bus glitch
        //    invented a fault and then "fixed" it by writing over the
        //    expander's configuration. If the chip will not answer, do nothing:
        //    say so, report the last thing actually observed, and let the next
        //    tick thirty seconds later look again.
        uint8_t before = 0;
        if (!Py32Read(dev, 0x01, &before)) {
            ESP_LOGW(TAG, "status chip 0x41 did not answer - rail state unknown, "
                          "leaving it alone (last known %s)",
                     rail_known_on_ ? "ON" : "OFF");
            i2c_master_bus_rm_device(ioe);
            i2c_master_bus_rm_device(dev);
            return rail_known_on_;
        }
        uint8_t rail = before;

        // 🔴 Reg 1 is a BIT FIELD, not a boolean, and only bit 0 is the rail.
        //    Testing `== 0x00` reports a powered rail as dead: a boot that read
        //    0xFE pinged both servos happily. Observed so far:
        //        0xFF  bit0=1  servos dead
        //        0x00  bit0=0  servos alive
        //        0xFE  bit0=0  servos alive
        //    What the other bits mean is not known; they moved when a bus scan
        //    was added, so do not assume they are stable.
        #define RAIL_ON(v) (((v) & 0x01) == 0)

        // The vendor's sequence, now aimed at the expander (ioe) instead of the
        // status chip. Status is still read from `dev` - the two are different
        // parts and each only answers for its own half.
        if (!RAIL_ON(rail)) {
            Py32PinMode(ioe, 0, true);
            Py32DigitalWrite(ioe, 0, true);
            Py32Bit(ioe, 5, 6, 0, true);
            vTaskDelay(pdMS_TO_TICKS(20));
            Py32PinMode(ioe, 13, true);
            Py32DigitalWrite(ioe, 13, true);
            vTaskDelay(pdMS_TO_TICKS(80));

            Py32Read(dev, 0x01, &rail);   // unchanged from `before` if it NAKs now
            ESP_LOGI(TAG, "IOE 0x6F bring-up -> r3=%02X r4=%02X r5=%02X r9=%02X r10=%02X, rail=0x%02X (%s)",
                     Py32Peek(ioe, 3), Py32Peek(ioe, 4), Py32Peek(ioe, 5),
                     Py32Peek(ioe, 9), Py32Peek(ioe, 10),
                     rail, RAIL_ON(rail) ? "ON" : "OFF");
            if (recovered != nullptr) *recovered = RAIL_ON(rail);
        }
        i2c_master_bus_rm_device(ioe);
        i2c_master_bus_rm_device(dev);

        // Announced on the boot call and whenever the rail had actually gone
        // away. A supervisor that logged "rail ON" every thirty seconds would
        // bury the one line that matters in the one place anyone reads.
        if (announce || !RAIL_ON(before)) {
            ESP_LOGI(TAG, "PY32 v0x%02X - servo motor rail %s (was %s, reg1=0x%02X->0x%02X)",
                     ver, RAIL_ON(rail) ? "ON" : "OFF",
                     RAIL_ON(before) ? "ON" : "OFF", before, rail);
        }
        if (!RAIL_ON(rail)) {
            ESP_LOGW(TAG, "servos unpowered - bring-up did not take (reg1=0x%02X)", rail);
        }
        rail_known_on_ = RAIL_ON(rail);
        return rail_known_on_;
    }

    // The boot call: same work, but it always says what it found.
    void InitializePy32() { EnsureServoRail(nullptr, true); }

    // 🔔 THE BOOT CHIME IS A TEST RESULT, NOT A DECORATION.
    //
    //    Upstream plays it the instant activation finishes, which on this board
    //    is the same moment the shared I2C bus stalls and the audio chips go
    //    unreachable. The chime was therefore played into a speaker that was not
    //    open yet, and simply vanished - which is how a boot with a genuinely
    //    dead amplifier and a boot with a busy bus sounded identical: silent.
    //
    //    So it waits until the hardware can answer for itself, and then says
    //    which of two things happened:
    //
    //      success chime      speaker, microphone and servo rail all confirmed
    //      exclamation        something did not come up - and the log says what
    //
    //    That makes the sound worth listening for. A missing chime now means
    //    "the check never finished", which is itself information.
    //
    // ⚠️ It CANNOT wait for output_enabled(): the chime is the first thing that
    //    opens the speaker, so that flag is false until after this runs. It asks
    //    the chips directly instead - which is the better question anyway, since
    //    an open that succeeded while the amp was unreachable is exactly the
    //    failure being guarded against.
    void OnDeviceReady() override {
        // Its own task: the caller is the application main loop, this waits for
        // seconds, and 4096 is room for the I2C work without going near the
        // esp_timer task's 3584-byte stack.
        xTaskCreate([](void* arg) {
            static_cast<M5StackStackChanBoard*>(arg)->BootSelfCheck();
            vTaskDelete(nullptr);
        }, "boot_check", 4096, this, 3, nullptr);
    }

    void BootSelfCheck() {
        auto* codec = static_cast<CoreS3AudioCodec*>(GetAudioCodec());

        // 🔴 ONE GOOD PROBE PROVES NOTHING, and the first version of this check
        //    shipped that mistake. During the stall the bus fails in bursts, so a
        //    single round can land in a gap between two failures - and it did,
        //    verbatim, on a reboot out of the screensaver:
        //
        //      W mic opened but the ES7210 does not answer - closing to retry
        //      I boot self-check PASSED - speaker, mic and servo rail all up
        //      E speaker open failed
        //
        //    A chime that says "all good" while the amplifier is failing is worse
        //    than no chime: it is the one piece of feedback the owner has, and it
        //    was lying. So the bus has to be QUIET FOR A WHILE, not quiet once.
        constexpr int kTimeoutMs = 10000;
        constexpr int kStepMs = 100;
        constexpr int kCleanRoundsNeeded = 5;   // ~500ms of uninterrupted quiet
        int clean = 0;
        bool amp = false, mic = false;
        int waited = 0;
        for (; waited < kTimeoutMs; waited += kStepMs) {
            amp = codec->AmpResponds();
            mic = codec->MicResponds();
            // The microphone is the one flag worth waiting on: the app opens the
            // input early, so it becomes true on its own. The speaker cannot be
            // used the same way - the chime is what opens it - so the amp is
            // judged by whether it answers.
            const bool listening = codec->input_enabled();
            clean = (amp && mic && listening) ? clean + 1 : 0;
            if (clean >= kCleanRoundsNeeded) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(kStepMs));
        }
        if (clean >= kCleanRoundsNeeded) {
            ESP_LOGI(TAG, "audio path steady after %d ms", waited);
        }

        const bool steady = clean >= kCleanRoundsNeeded;
        const bool rail = EnsureServoRail();
        const bool ok = steady && rail;

        if (ok) {
            ESP_LOGI(TAG, "boot self-check PASSED - speaker, mic and servo rail all up");
        } else {
            ESP_LOGE(TAG, "boot self-check FAILED - amp:%s mic:%s listening:%s servo rail:%s",
                     amp ? "ok" : "NO ANSWER", mic ? "ok" : "NO ANSWER",
                     codec->input_enabled() ? "yes" : "NO", rail ? "ok" : "OFF");
        }
        // The exclamation is deliberately a different sound rather than silence.
        // Silence is what a flat battery and a broken amplifier both sound like.
        Application::GetInstance().PlaySound(
            ok ? Lang::Sounds::OGG_SUCCESS : Lang::Sounds::OGG_EXCLAMATION);
    }

    M5StackStackChanBoard() {
        // 🔇 FIRST, before anything can open the microphone. The mute is restored
        //    from NVS, and a robot that listens for a second and a half on every
        //    boot is not muted - it is mostly muted, which is not a thing anyone
        //    wants to be told about their own microphone.
        LoadPrivacySettings();
        InitializePowerSaveTimer();
        InitializeI2c();
        InitializeAxp2101();
        InitializeAw9523();
        InitializePy32();
        // Same chip as the servo rail, so it must come after InitializePy32 -
        // that is what brings the base out of its cold-boot state.
        leds_.Initialize(i2c_bus_);
        InitializeSpi();
        InitializeIli9342Display();
        InitializeTheme();
        InitializeCamera();
        StartScreenDumpTask();
        InitializeFt6336TouchPad();
        GetBacklight()->RestoreBrightness();

        // Now that the codec and the face both exist, put the restored mute into
        // effect: close the input, and mark the screen. Not persisted - this is
        // the value that was just read back.
        if (mic_muted_) {
            ApplyMicMute(true, false);
        }

        head_.Initialize();
        head_.RegisterMcpTools();
        // The rail is on the PY32, on the I2C bus this board owns; the head only
        // has the servos' UART. So it borrows the check from here. Must be set
        // BEFORE StartMotion, since the motion task is where it runs.
        head_.SetRailSupervisor([this](bool* recovered) {
            return EnsureServoRail(recovered);
        });
        // The face is the only thing that can see the model start working - it
        // watches for the user's transcript arriving, because there is no
        // device state for "thinking". The head wants the same signal. The LED
        // ring deliberately does NOT get it: the ring can carry ambient status,
        // and an animation on top of it could mask an alert.
        if (face_ != nullptr) {
            face_->SetOnThinking([this](bool on) { head_.SetThinking(on); });
        }
        // Here rather than in the constructor: face_ does not exist yet when
        // LoadPrivacySettings runs, and a badge set on a null face is a setting
        // that silently does nothing. Config-only, so once is enough - changing
        // the address goes through Wi-Fi config mode, which reboots.
        UpdateNoServerBadge();
        // After the tools, so an early MCP call cannot race the task creation.
        head_.StartMotion();

        leds_.RegisterMcpTools();
        RegisterCameraTool();
        RegisterSpeakerTool();
        leds_.BootSweep();
    }

    // Wires an ambient status source into the LED ring, the idle status screen
    // and spoken alerts. Nothing calls this yet - see status_source.h. Call it
    // at most once, after construction.
    void AttachStatusSource(StatusSource* source) {
        if (source == nullptr || status_ != nullptr) return;
        status_ = source;
        leds_.SetStatusSource(source);
        if (face_ != nullptr) face_->SetStatusSource(source);
        // The moment he stops talking is the moment a deferred alert can finally
        // be delivered. Scheduled rather than run inline so it lands after the
        // state transition has fully settled.
        leds_.SetOnDeviceStateChanged([this]() {
            Application::GetInstance().Schedule([this]() { AnnounceStatusIfChanged(); });
        });
        // Repaint the ring when a reading lands. Without this the colour would
        // only change on a device state transition - the next time somebody
        // spoke to him, the one moment an ambient display is not being looked at.
        source->SetOnUpdate([this]() {
            leds_.OnStateChanged();
            // Hopped onto the main task: the source's task has no business
            // calling Alert(), which touches the display, audio and emotion.
            Application::GetInstance().Schedule([this]() { AnnounceStatusIfChanged(); });
        });
    }

    // 🔔 The status source speaks first, instead of waiting to be asked.
    //
    // When the level CHANGES he does a double-take (if it got worse), the ring
    // follows, a chime plays and the summary is on the screen - with nobody
    // having said a word to him.
    //
    // 🔴 EDGE, NOT LEVEL. Announcing the current level on every reading would
    //    mean a chime every few minutes for as long as something stayed broken,
    //    which is how a useful alert becomes something you unplug.
    //
    // 🔴 AND NOT WHILE HE IS BUSY. Alert() overwrites the chat line and plays a
    //    sound over a reply in progress. If he is not idle, announced_level_ is
    //    not advanced, so the next reading or state change retries - deferred,
    //    not dropped. It re-reads the level rather than being handed one, so a
    //    "went red" that recovered while he was talking is never announced late.
    void AnnounceStatusIfChanged() {
        if (status_ == nullptr) return;
        auto& app = Application::GetInstance();
        const auto now = status_->level();

        // kUnknown means no contact - the ring already mutes for that, and a
        // chime because the Wi-Fi blipped is not information.
        if (now == StatusSource::Level::kUnknown) return;
        if (now == announced_level_) return;
        if (app.GetDeviceState() != kDeviceStateIdle) return;

        // The FIRST reading only establishes the baseline, or he would announce
        // "all healthy" with a chime on every boot.
        if (announced_level_ == StatusSource::Level::kUnknown) {
            announced_level_ = now;
            ESP_LOGI(TAG, "status baseline: %s", LevelName(now));
            return;
        }

        const auto prev = announced_level_;
        const bool worse = now > prev;
        announced_level_ = now;

        const char* emotion = "neutral";
        std::string_view sound = Lang::Sounds::OGG_SUCCESS;
        const char* title = "Status";
        switch (now) {
            case StatusSource::Level::kAlert:
                emotion = "shocked";
                sound = Lang::Sounds::OGG_EXCLAMATION;
                title = "Alert";
                break;
            case StatusSource::Level::kWarn:
                emotion = worse ? "confused" : "relaxed";
                sound = worse ? Lang::Sounds::OGG_POPUP : Lang::Sounds::OGG_SUCCESS;
                title = "Warning";
                break;
            default:
                emotion = "happy";
                title = "Recovered";
                break;
        }

        // Wake first: the idle screen is exactly where he will be when this
        // matters, and an alert nobody can read is not an alert.
        if (power_save_timer_ != nullptr) power_save_timer_->WakeUp();
        if (worse) head_.Startle();

        std::string message = status_->summary();
        if (message.empty()) message = "Status changed.";
        ESP_LOGW(TAG, "status %s -> %s: %s", LevelName(prev), LevelName(now), message.c_str());
        app.Alert(title, message.c_str(), emotion, sound);
        // The ring already follows the level via IdleAmbient; it needs no
        // separate command here, only the repaint the caller has done.
    }

    static const char* LevelName(StatusSource::Level l) {
        switch (l) {
            case StatusSource::Level::kOk:    return "ok";
            case StatusSource::Level::kWarn:  return "warn";
            case StatusSource::Level::kAlert: return "alert";
            default:                          return "unknown";
        }
    }

    // 🔊 A/B the amplifier boost by voice, because "is it louder?" is a
    //    question about a room and not about a register.
    //
    //    The alternative was a constant and a reflash per attempt, which makes
    //    comparing two levels impossible - by the time the second one is
    //    running, nobody can remember exactly how loud the first was. Toggling
    //    it live puts both within a few seconds of each other.
    void RegisterSpeakerTool() {
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool(
            "self.audio_speaker.set_boost",
            "Turn the speaker amplifier's boost converter on or off. On is much louder; "
            "off is the quiet default. Use this when someone asks you to be louder or "
            "quieter and the volume is already at its limit.",
            PropertyList({Property("enabled", kPropertyTypeBoolean, true)}),
            [this](const PropertyList& properties) -> ReturnValue {
                auto* codec = static_cast<CoreS3AudioCodec*>(GetAudioCodec());
                if (codec == nullptr) return std::string("no audio codec");
                const bool on = properties["enabled"].value<bool>();
                codec->SetSpeakerBoost(on);
                // 🧪 Readback in the RESULT, so the server log answers "did the
                //    write land?" without a serial capture. The first attempt
                //    at this changed nothing audible and there was no way to
                //    tell a failed I2C write from a register that simply does
                //    not do what I assumed.
                return std::string("Speaker boost ") + (on ? "on" : "off") + ". [amp " +
                       codec->DescribeAmp() + "]";
            });

        // 🔴 There was a set_gain tool here for about an hour. It is gone on
        //    purpose: a digital multiplier made him loud enough to hear himself
        //    through mics with no echo reference, so he interrupted his own
        //    sentence and aborted. A voice-reachable tool that can make the
        //    robot stop talking mid-answer is not a volume control.
    }

    // 📷 Take a photo and SHOW it, with nothing leaving the network.
    //
    // The stock self.camera.take_photo captures and then calls Explain(), which
    // POSTs the frame to a vision endpoint. The server's shipped default is a
    // CLOUD vision API still carrying a placeholder key, so on a local setup the
    // tool fails - the server logs that the VLLM API key is not set.
    //
    // Adding a key would "fix" it by sending pictures of the room to a third
    // party, which is exactly the exposure this project removes. So the photo
    // goes to the robot's own screen instead. No server, no model, no round trip.
    // Explain() hands back the server's envelope:
    //
    //     {"success":true,"action":"RESPONSE","response":"A red mug on a desk."}
    //
    // Only the sentence is any use. The first version passed the whole thing
    // into the tool result, which is a robot one step away from reading JSON
    // out loud. Hand-parsed rather than adding a dependency for one field, and
    // it falls back to the raw string if the shape ever changes - an odd
    // sentence beats no description at all.
    static std::string VisionSentence(const std::string& raw) {
        const std::string key = "\"response\":\"";
        const size_t start = raw.find(key);
        if (start == std::string::npos) return raw;
        std::string out;
        for (size_t i = start + key.size(); i < raw.size(); i++) {
            const char c = raw[i];
            if (c == '\\' && i + 1 < raw.size()) {
                const char n = raw[++i];
                out += (n == 'n' || n == 't') ? ' ' : n;   // \" and \\ land here too
                continue;
            }
            if (c == '"') break;
            out += c;
        }
        return out.empty() ? raw : out;
    }

    void RegisterCameraTool() {
        auto& mcp = McpServer::GetInstance();
        mcp.AddTool(
            "self.camera.show_photo",
            "Take a photo with the robot's camera and show it on the robot's own screen. "
            "Use this when someone asks you to take a picture or show them what you can see. "
            "The picture appears on the robot own screen the moment this returns: "
            "there is no file and no link, so never write a URL, a filename or "
            "markdown - twice now a placeholder link has been invented and read "
            "aloud, character by character. "
            "If this robot has a vision model, the result carries a `view=` field: "
            "notes about what is in front of the camera, written by a separate "
            "model that cannot speak. The camera faces outward, at whoever is "
            "standing in front of the robot - it is never a picture of you, so "
            "do not call it one. "
            "Always say what you can see, in the SAME reply, "
            "without being asked - announcing the photo and stopping is not enough. "
            "Never repeat the notes as written; they are in nobody's speaking voice. "
            "Say what you noticed, the way somebody would who had just looked up. "
            "For view=Male individual with blonde hair wearing dark clothing, against "
            "a wooden background with a wall clock - you might say: Got you in front of "
            "the cabinet, and is that a clock behind your shoulder? "
            "If there is no `view=` field, the photo simply stays on the device and "
            "you cannot see it.",
            PropertyList(),
            [this](const PropertyList&) -> ReturnValue {
                if (camera_ == nullptr) return std::string("camera unavailable");
                // 🔇 The switch is checked HERE, at the one place a frame is
                //    captured, rather than by tearing the driver down. A refusal
                //    the model can read back and explain beats a mysterious
                //    failure, and it is phrased so it does not apologise for a
                //    deliberate setting.
                if (camera_off_) {
                    return std::string(
                        "The camera is switched off in the robot's settings. "
                        "Nothing was captured. It can be turned back on by holding "
                        "the screen for five seconds.");
                }

                // 📷 METER, then shoot. See the gc0308 namespace note: the
                //    sensor's own AEC is pinned at the frame length and cannot
                //    give us any more light at 20fps, so for one shot we take
                //    it off auto and expose properly.
                //
                //    Each pass costs one frame plus settle time - the sensor
                //    needs a couple of frames to apply a new exposure, and
                //    metering the frame that was in flight when we wrote the
                //    register would just make the loop chase itself.
                // 📷 FACE FRONT AND HOLD, before the sensor is touched. He is
                //    looking at whoever asked - and, less obviously, the long
                //    exposure a still uses is slow enough that an idle glance
                //    part way through smears the frame. Released on every path
                //    below, including the failures, or he would sit frozen.
                // 📷 WAKE THE SENSOR. It is stopped the rest of the time - see
                //    Camera::StartStreaming. The 400ms below, which the servos
                //    need anyway, doubles as its settle time, and Capture()
                //    discards two frames before keeping one.
                if (!camera_->StartStreaming()) {
                    return std::string("the camera would not start");
                }
                head_.HoldStill(true);
                head_.Center();
                vTaskDelay(pdMS_TO_TICKS(400));   // let the servos arrive and stop

                // 🔆 ROOM TO EXPOSE INTO. Vertical blanking to maximum: this
                //    mode's frame is about 252 rows, and exposure cannot exceed
                //    the frame, so without this the 4000-row exposure the
                //    metering loop asks for has nowhere to go.
                //
                // ⚠️ There used to be a five-frame settle loop here, waiting for
                //    the sensor's AEC to find the scene - immediately before the
                //    code below switches that AEC off. It was most of the pause
                //    after the shutter and it never did anything.
                gc0308::ExtendFrame(i2c_bus_);

                // 🔆 WE EXPOSE THE STILL, because the sensor will not.
                //
                //    Its AEC sits at exp=480 with a target of 88 while the frame
                //    meters 30 - through a raised target and a doubled frame
                //    length. It is not out of room, it has stopped.
                //
                //    Calibrated by a sweep on a held scene, AEC off at 0x22:
                //
                //        exp  240=27 480=30 740=34 1200=41 2000=51 3000=60
                //        gain 14=34  1C=39  24=43  2C=46   34=49   3C=52
                //
                // 🔴 THE OUTPUT GOES AS ROUGHLY THE CUBE ROOT OF THE LIGHT - the
                //    ISP's gamma. Twelve times the exposure is twice the picture.
                //    This is why two days of one-register-at-a-time experiments
                //    all reported "no change", and why gain was written off in a
                //    comment as measured noise. Correct by the cube, or the loop
                //    creeps so slowly it looks like another dead lever.
                //
                // ⚠️ Neither lever reaches 100 alone: exposure stops at 60, gain
                //    at 52. It takes both.
                constexpr bool kMeterStill = true;
                constexpr int kTarget = 100;
                constexpr int kExpTop = 4000;        // 0x03/0x04 is 12 bits
                constexpr int kGainTop = 0x3C;       // the field is SIX bits
                std::string sweep_log;
                // Saved here, restored only AFTER the real frame is taken - handing
                // the sensor back first would let its AEC undo the exposure we just
                // worked out, one frame before the photograph.
                int s_22 = -1, s_d2 = -1, s_h = -1, s_l = -1, s_g = -1;
                if (kMeterStill) {
                    auto* dev = gc0308::Dev(i2c_bus_);
                    s_d2 = gc0308::Rd(dev, 0xd2);
                    // 🔴 THE AEC ENABLE IS BIT 0 OF 0x22, not bit 7 of 0xd2. The
                    //    first sweep cleared only 0xd2 and the AEC kept pulling
                    //    exposure back to 480 underneath every write - which is
                    //    why exposure looked like the dead lever and gain looked
                    //    like the live one. bit1 AWB, bit2 AGC; init writes 0x57.
                    s_22 = gc0308::Rd(dev, 0x22);
                    s_h = gc0308::Rd(dev, 0x03);
                    s_l = gc0308::Rd(dev, 0x04);
                    s_g = gc0308::Rd(dev, 0x50);
                    if (dev != nullptr && s_22 >= 0 && s_d2 >= 0 && s_h >= 0 && s_l >= 0 &&
                        s_g >= 0) {
                        // AEC and AGC off; AWB (bit1) left alone, because the
                        // colour is the one thing about this picture that is right.
                        gc0308::Wr(dev, 0x22, static_cast<uint8_t>(s_22 & ~0x05));
                        gc0308::Wr(dev, 0xd2, static_cast<uint8_t>(s_d2 & ~0x80));

                        // Start where the last photograph ended up. The room
                        // rarely changes between two shots, so this usually
                        // converges on the first pass instead of climbing from
                        // the bottom every time - which the user sees as the
                        // pause after the shutter.
                        static int last_exposure = 740, last_gain = 0x14;
                        int exposure = last_exposure, gain = last_gain, m = -1;
                        for (int pass = 0; pass < 6; pass++) {
                            gc0308::SetExposure(i2c_bus_, exposure);
                            gc0308::SetGain(i2c_bus_, gain);
                            // Two frames: the one already in flight was exposed
                            // under the previous setting, and metering it is how
                            // a loop ends up one step behind itself.
                            vTaskDelay(pdMS_TO_TICKS(180));
                            camera_->Capture();
                            vTaskDelay(pdMS_TO_TICKS(120));
                            if (!camera_->Capture()) break;
                            m = frame::MeanRgb565(camera_->frame_data(), camera_->frame_len());
                            char row[40];
                            snprintf(row, sizeof(row), " %d/%02X=%d", exposure, gain, m);
                            sweep_log += row;
                            if (m <= 0) m = 1;
                            if (m >= kTarget * 8 / 10 && m <= kTarget * 13 / 10) break;

                            // How much more LIGHT, by the cube law. 64-bit: the
                            // cube of a target overflows 32 bits at trivial values.
                            const long long want = (long long)kTarget * kTarget * kTarget;
                            const long long have = (long long)m * m * m;
                            if (exposure < kExpTop) {
                                long long next = (long long)exposure * want / have;
                                if (next > kExpTop) next = kExpTop;
                                if (next < 16) next = 16;
                                if (next == exposure) break;   // nothing left to give
                                exposure = (int)next;
                            } else if (gain < kGainTop) {
                                long long next = (long long)gain * want / have;
                                if (next > kGainTop) next = kGainTop;
                                if (next <= gain) break;
                                gain = (int)next;
                            } else {
                                break;   // both levers spent; this is all there is
                            }
                        }
                        last_exposure = exposure;
                        last_gain = gain;
                        ESP_LOGI(TAG, "photo: metered to mean %d (exp=%d gain=0x%02X)",
                                 m, exposure, gain);
                    } else {
                        sweep_log = " meter=sensor-unreadable";
                        s_22 = -1;   // nothing was changed, so nothing is restored
                    }
                }
                const bool got = camera_->Capture();

                // The still is taken; the sensor can have its AEC back. Order
                // matters only in that 0x22 goes last - it is the switch.
                if (s_22 >= 0) {
                    auto* dev = gc0308::Dev(i2c_bus_);
                    gc0308::Wr(dev, 0x03, static_cast<uint8_t>(s_h));
                    gc0308::Wr(dev, 0x04, static_cast<uint8_t>(s_l));
                    gc0308::Wr(dev, 0x50, static_cast<uint8_t>(s_g));
                    gc0308::Wr(dev, 0xd2, static_cast<uint8_t>(s_d2));
                    gc0308::Wr(dev, 0x22, static_cast<uint8_t>(s_22));
                }
                head_.HoldStill(false);
                // Unconditionally, like the two above: every path below returns,
                // and a sensor left streaming is the thing this exists to stop.
                camera_->StopStreaming();
                if (!got) {
                    return std::string("camera did not return a frame");
                }
                const uint8_t* data = camera_->frame_data();
                const uint16_t w = camera_->frame_width();
                const uint16_t h = camera_->frame_height();
                const auto fmt = camera_->frame_format();
                if (data == nullptr || w == 0 || h == 0) {
                    return std::string("camera frame was empty");
                }
                // 🧪 Both measurements ride back in the tool RESULT, not only
                //    into the serial log. xiaozhi logs the result verbatim, so
                //    they can be read out of the container afterwards - which
                //    means diagnosing this no longer needs a serial capture
                //    running at the exact moment somebody asks for a photo.
                //    Three capture windows were missed learning that.
                std::string diag = gc0308::Describe(i2c_bus_);
                diag += sweep_log;

                uint8_t* rgb = nullptr;
                uint16_t out_w = w, out_h = h;
                if (fmt == V4L2_PIX_FMT_RGB565) {
                    // 🎨 NO CHANNEL TRANSPOSE ON THIS DRIVER. esp_video needed
                    //    red and blue traded; esp32-camera does not, and doing
                    //    it anyway gives a cyan picture with every edge sharp -
                    //    which is what transposed channels look like, and what
                    //    sent this project hunting a white-balance fault it
                    //    never had.
                    {
                        char rbuf[96];
                        frame::Rgb565Stats(data, static_cast<size_t>(w) * h * 2,
                                            rbuf, sizeof(rbuf));
                        diag += rbuf;
                    }
                    // Already what the panel wants. Copy anyway: the preview
                    // outlives this call and the driver reuses its frame buffer.
                    const size_t n = static_cast<size_t>(out_w) * out_h * 2;
                    rgb = static_cast<uint8_t*>(
                        heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
                    // 📋 THE BYTE ORDER IS ALREADY RIGHT, for both consumers.
                    //
                    //    esp32-camera byte-swaps RGB565 into its encode buffer
                    //    for the JPEG encoder, and the panel wants that same
                    //    order - swapping here scrambles the screen into
                    //    8-bit-looking colour, and transposing red and blue
                    //    turns it cyan. Both were tried; both are wrong.
                    //
                    // 🔴 AND NEITHER WAS EVER THE BUG. The washed-out screen
                    //    that sent us looking at byte order was the frame being
                    //    three stops under: the sensor's gamma stretches the
                    //    bottom of a starved frame, which lifts the black floor
                    //    and lays a grey veil over the picture. Metering the
                    //    still properly took the black floor from 24 to 6 and
                    //    the veil went with it, on the panel and in the file.
                    if (rgb != nullptr) {
                        memcpy(rgb, data, n);
                    }
                } else {
                    ESP_LOGW(TAG, "frame format 0x%08" PRIx32 " is not one we can convert",
                             (uint32_t)fmt);
                }
                if (rgb == nullptr) {
                    return std::string("photo taken, but it could not be prepared for the screen");
                }

                display_->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
                    rgb, static_cast<size_t>(out_w) * out_h * 2, out_w, out_h, out_w * 2,
                    LV_COLOR_FORMAT_RGB565));
                // 🔴 This tool must never report failure for anything the model
                //    could "fix" by trying again. It cannot see the photo - by
                //    design - so a failure string just makes it apologise and
                //    retry, which is what "I'm still having trouble taking the
                //    photo" was: the tool succeeded and said something that
                //    read like an error.
                // 👁 AND IF THERE IS AN EYE, USE IT. CanExplain() is false unless
                //    the server offered a vision URL, which it only does when a
                //    vision model is configured - so a setup without one takes
                //    this branch never and behaves exactly as before.
                //
                // ⚠️ AFTER the photo is on the screen, deliberately. The picture
                //    appearing is what the person in the room is waiting for;
                //    describing it costs a second or so more, and doing it first
                //    would make the screen look slow to no purpose.
                //
                //    A failure here is NOT reported as one. The photo succeeded
                //    and is visible, and a robot that says "I had trouble" with
                //    a picture sitting on its face is the exact behaviour that
                //    cost an evening before.
                std::string seen;
                if (camera_->CanExplain()) {
                    // The picture the SCREEN is showing, not the raw frame - the
                    // sensor's own output meters at 31 in a lit room and the
                    // model duly called it "a dark room" while its owner was
                    // looking at something perfectly legible. Copied before the
                    // display takes ownership below.
                    camera_->SetExplainImage(rgb, static_cast<size_t>(out_w) * out_h * 2,
                                             out_w, out_h, V4L2_PIX_FMT_RGB565);
                    try {
                        seen = VisionSentence(camera_->Explain("What do you see?"));
                        ESP_LOGI(TAG, "vision: %s", seen.c_str());
                    } catch (const std::exception& e) {
                        ESP_LOGW(TAG, "vision unavailable: %s", e.what());
                    }
                }

                // 🗣️ THE RESULT IS RAW MATERIAL, NOT A LINE TO READ OUT.
                //
                //    It used to say "Photo taken and shown on the robot's
                //    screen. The camera sees: ..." and that is exactly what came
                //    out of the speaker, word for word. The tool description
                //    above already asks for his own words and lost, because the
                //    result itself demonstrated a finished sentence - and on
                //    this project a demonstration beats an instruction every
                //    time.
                //
                //    So nothing here is speakable. Labelled, terse, obviously
                //    data. The other half is in the patch kit, where the vision
                //    model is asked for a description rather than a sentence.
                //
                // 🔬 kPhotoDiag appends the sensor registers. That blob is how
                //    the exposure sweep results got off the robot without a
                //    serial capture running at the right moment - and it was
                //    read aloud twice while it was there. The camera works now,
                //    so it is off, and Describe() still logs the same numbers
                //    over serial whenever a photo is taken.
                constexpr bool kPhotoDiag = false;
                std::string result = "photo_on_screen=yes";
                if (!seen.empty()) {
                    result += "; view=" + seen;
                }
                if (kPhotoDiag) {
                    result += " [diag " + diag + "]";
                }
                return result;
            });
        ESP_LOGI(TAG, "registered local camera preview tool");
    }


    virtual AudioCodec* GetAudioCodec() override {
        static CoreS3AudioCodec audio_codec(i2c_bus_,
            AUDIO_INPUT_SAMPLE_RATE,
            AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_MCLK,
            AUDIO_I2S_GPIO_BCLK,
            AUDIO_I2S_GPIO_WS,
            AUDIO_I2S_GPIO_DOUT,
            AUDIO_I2S_GPIO_DIN,
            AUDIO_CODEC_AW88298_ADDR,
            AUDIO_CODEC_ES7210_ADDR,
            AUDIO_INPUT_REFERENCE);
        // The amplifier's reset line is on the AW9523, which the codec does not
        // own - so it borrows it. Without this, a codec that opened while the
        // amp was unreachable has no way back, and the robot stays silent until
        // someone power-cycles it. See EnableOutput in cores3_audio_codec.cc.
        static bool amp_reset_hooked = false;
        if (!amp_reset_hooked) {
            amp_reset_hooked = true;
            audio_codec.SetAmpResetHook([this]() {
                if (aw9523_ != nullptr) aw9523_->ResetAw88298();
            });
        }
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    // 🔴 nullptr ON PURPOSE, and it is a privacy control rather than a tidy-up.
    //
    //    Board::GetCamera() is reached from exactly two places in the common
    //    code, and both of them are the cloud vision path:
    //
    //      mcp_server.cc:100  registers self.camera.take_photo, whose body is
    //                         Capture() followed by Explain()
    //      mcp_server.cc:337  ParseCapabilities() takes the vision URL and
    //                         token the server offers and stores them on the
    //                         camera, which is what Explain() then POSTs to
    //
    //    Handing back nullptr means the stock tool is never registered and the
    //    explain URL is never even accepted. The camera itself is untouched -
    //    self.camera.show_photo talks to camera_ directly - so the robot can
    //    still take a picture, it just has nowhere off-device to send it.
    //
    //    This is also what the model was tripping over. Both tools were on the
    //    list; it picked take_photo, that failed inside the server with
    //    "VLLM API key not set", and it announced it was having trouble - after
    //    the photo was already on the screen.
    // 👁 THE CAMERA IS HANDED BACK NOW, AND THE PRIVACY CONTROL MOVED RATHER
    //    THAN WENT AWAY.
    //
    //    This returned nullptr for as long as the only vision path on offer was
    //    somebody else's cloud: refusing the handle meant the stock take_photo
    //    tool was never registered AND the explain URL was never even accepted.
    //    One `return nullptr` closed both doors.
    //
    //    With a vision model running on the owner's own machine the second door
    //    is the feature, so the handle comes back - and the first door is now
    //    held by UseStockCameraTool() below instead. The control is the same
    //    size, it is just no longer attached to the wrong thing.
    //
    // 🔑 AND IT IS STILL OFF BY DEFAULT, because the SERVER decides: no vision
    //    model configured means no vision URL in its capabilities, which means
    //    CanExplain() stays false and show_photo behaves exactly as it always
    //    has. Nothing here opts anybody in.
    virtual Camera* GetCamera() override {
        return camera_;
    }

    // Our own show_photo is the camera tool. Upstream's take_photo would be a
    // second one that also takes a picture, and when both were offered the
    // model reached for that one, it failed inside the server, and he
    // apologised for trouble after the photo was already on the screen.
    virtual bool UseStockCameraTool() const override {
        return false;
    }

    // 🔴 This used to enable/disable the power save timer on the discharging
    //    EDGE, and the edge could not fire in the one case that mattered:
    //
    //        static bool last_discharging = false;          // starts false
    //        if (discharging != last_discharging) { ... }   // on USB: also false
    //
    //    The constructor starts the timer ENABLED, so on USB the intended
    //    SetEnabled(false) was never reached and the robot powered itself off
    //    after five minutes while plugged in. Plugged-in was the only broken
    //    case, which is why it read as "the timeouts need tuning".
    //
    //    The timer no longer wants disabling at all - the shutdown callback
    //    checks the power state itself - so this is now just a battery read.
    virtual bool GetBatteryLevel(int &level, bool& charging, bool& discharging) override {
        charging = pmic_->IsCharging();
        discharging = pmic_->IsDischarging();
        level = pmic_->GetBatteryLevel();
        return true;
    }

    virtual void SetPowerSaveLevel(PowerSaveLevel level) override {
        if (level != PowerSaveLevel::LOW_POWER) {
            power_save_timer_->WakeUp();
        }
        WifiBoard::SetPowerSaveLevel(level);
    }

    // Hands the ring to the framework's state machine, so listening / speaking /
    // connecting show up without anything in application.cc knowing about the
    // PY32. Falls back to NoLed if the controller did not answer at 0x6F.
    virtual Led* GetLed() override {
        static NoLed no_led;
        if (!leds_.ready()) return &no_led;
        return &leds_;
    }

    virtual Backlight *GetBacklight() override {
        static CustomBacklight backlight(pmic_);
        return &backlight;
    }
};

DECLARE_BOARD(M5StackStackChanBoard);
