#include "stackchan_leds.h"

#include "application.h"
#include "status_source.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <esp_rom_sys.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define TAG "StackChanLeds"

namespace {

// Perceived brightness is nonlinear, so a linear fade looks like it snaps off
// near the bottom. Gamma ~2.8 on the ENVELOPE fixes that.
//
// 🔴 Gamma applies to the envelope, NOT to the palette colour. The palette hex
//    values were picked visually; running them through a gamma curve would
//    shift the hue as well as the level, and #a17eff would stop being that
//    purple. So: colour stays as authored, the fade is what gets corrected.
const uint8_t* GammaTable() {
    static uint8_t table[256];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; i++) {
            table[i] = static_cast<uint8_t>(
                std::lround(std::pow(i / 255.0f, 2.8f) * 255.0f));
        }
        built = true;
    }
    return table;
}

RgbColor Scale(RgbColor c, uint8_t envelope) {
    const uint8_t g = GammaTable()[envelope];
    return {static_cast<uint8_t>(c.r * g / 255),
            static_cast<uint8_t>(c.g * g / 255),
            static_cast<uint8_t>(c.b * g / 255)};
}

// Plain RGB565, packed the same way the vendor firmware does it.
uint16_t Rgb565(RgbColor c) {
    return static_cast<uint16_t>(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

struct NamedColor {
    const char* name;
    RgbColor color;
};

const NamedColor kNamedColors[] = {
    {"lavender", palette::kLavender}, {"purple", palette::kLavender},
    {"blue",     palette::kBlue},     {"green",  palette::kGreen},
    {"amber",    palette::kAmber},    {"orange", palette::kAmber},
    {"red",      palette::kRed},      {"grey",   palette::kMuted},
    {"gray",     palette::kMuted},    {"off",    palette::kOff},
    {"black",    palette::kOff},
};

}  // namespace

bool StackChanLeds::LookupColor(const std::string& name, RgbColor& out) {
    std::string key;
    for (char c : name) key += static_cast<char>(std::tolower(c));

    for (const auto& nc : kNamedColors) {
        if (key == nc.name) {
            out = nc.color;
            return true;
        }
    }
    // #rrggbb, so an unforeseen colour does not need a firmware change.
    if (key.size() == 7 && key[0] == '#') {
        char* end = nullptr;
        long v = strtol(key.c_str() + 1, &end, 16);
        if (end && *end == '\0') {
            out = {static_cast<uint8_t>((v >> 16) & 0xFF),
                   static_cast<uint8_t>((v >> 8) & 0xFF),
                   static_cast<uint8_t>(v & 0xFF)};
            return true;
        }
    }
    return false;
}

StackChanLeds::~StackChanLeds() {
    if (timer_ != nullptr) {
        esp_timer_stop(timer_);
        esp_timer_delete(timer_);
    }
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
    }
}

bool StackChanLeds::Initialize(i2c_master_bus_handle_t bus) {
    bus_ = bus;

    i2c_device_config_t cfg = {};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = kIoeAddr;
    cfg.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus_, &cfg, &dev_) != ESP_OK) {
        ESP_LOGE(TAG, "LED controller absent at 0x%02X - ring disabled", kIoeAddr);
        dev_ = nullptr;
        return false;
    }

    esp_timer_create_args_t timer_args = {
        .callback = [](void* arg) {
            auto* self = static_cast<StackChanLeds*>(arg);
            std::lock_guard<std::mutex> lock(self->mutex_);
            if (self->tick_) self->tick_();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "led_anim",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&timer_args, &timer_) != ESP_OK) {
        ESP_LOGE(TAG, "animation timer failed; ring will be static");
        timer_ = nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        // The vendor's own LED bring-up, replayed: reg 19 = 0xFF and reg 20 = 0x1F,
        // i.e. the (19,20) pair read as one 16-bit map with bits 0-12 set and
        // bit 13 clear. Bit 13 is the servo-rail pin, so this reads as a
        // pin-function bitmap that keeps pin 13 plain GPIO. The rail does not
        // need it, but the vendor issues it and it is free to match.
        //
        // ⚠️ This is NOT what makes the second strip light, despite looking like
        //    it when first found - a controlled capture of the factory firmware
        //    with its strips on and off showed reg 19 = 0xFF in both. The fix for
        //    the second strip was register 36 being a pixel COUNT; see
        //    SetMasterBrightnessLocked.
        const esp_err_t e19 = WriteReg(19, 0xFF);
        const esp_err_t e20 = WriteReg(20, 0x1F);
        ESP_LOGI(TAG, "led cfg: reg19=0xFF (%s) reg20=0x1F (%s)",
                 (e19 == ESP_OK) ? "ack" : "NAK", (e20 == ESP_OK) ? "ack" : "NAK");

        RestoreDefaultPinsLocked();
        SetMasterBrightnessLocked(percent_);
        SelfTestLocked();
    }

    ESP_LOGI(TAG, "ring ready: %d LEDs on 0x%02X (reg36 = pixel count), brightness %d%% in software",
             kLedCount, kIoeAddr, percent_);
    return true;
}

// Does the chip actually TAKE what we write? Writing a known pattern into
// pixel 0's register pair and reading it back distinguishes three cases that
// otherwise look identical from the outside:
//   ack + correct readback -> protocol right, any darkness is optical/wiring
//   ack + wrong readback   -> the register is not what we think it is
//   NAK                    -> not talking to the LED controller at all
// The pattern is restored to black immediately afterwards.
void StackChanLeds::SelfTestLocked() {
    const RgbColor probe = {255, 0, 0};              // 0xF800 - distinctive
    const esp_err_t we = WritePixel(0, probe);
    uint8_t reg = 48, back[2] = {0, 0};
    const esp_err_t re =
        i2c_master_transmit_receive(dev_, &reg, 1, back, 2, pdMS_TO_TICKS(100));
    const uint16_t got = static_cast<uint16_t>(back[0] | (back[1] << 8));
    ESP_LOGI(TAG, "selftest: write reg48=0x%04X %s, readback %s 0x%04X%s",
             Rgb565(probe), (we == ESP_OK) ? "ack" : "NAK",
             (re == ESP_OK) ? "=" : "unreadable", got,
             (re == ESP_OK && got == Rgb565(probe)) ? "  <- MATCH" : "");
    WritePixel(0, palette::kOff);
}

// 🔴 Puts the PY32's pins back to their known-good state. MUST run on every
//    boot, not once.
//
// The PY32 keeps its own power and its own register state for as long as
// anything feeds it, so the ESP32 rebooting or halting does not clear what was
// written before. During bring-up, driving a spare pin OUTPUT+HIGH left one strip
// latched ON permanently - it survived the power button, and only went out when
// USB was unplugged. One of the spare pins gates LED power directly, and a robot
// whose lights cannot be turned off is not acceptable.
//
// 🔴 FOUR BLIND WRITES, no reads. A read-modify-write per bit (56 transactions)
//    NACKed early and wedged the bus for the rest of init, and a NACK cannot be
//    recovered here: i2c_master_bus_reset() is forbidden once dev_ is registered.
//
//    The target values are what a healthy boot measures and what the vendor's
//    own bring-up produces:
//        reg 3  = 0x01   pin 0 output
//        reg 4  = 0x20   pin 13 output (the servo rail)
//        reg 9  = 0x01   pin 0 high
//        reg 10 = 0x20   pin 13 high
//    Writing them outright restores that state and clears every stray pin, in
//    four transactions with nothing to NACK on a read.
void StackChanLeds::RestoreDefaultPinsLocked() {
    WriteReg(3, 0x01);
    WriteReg(4, 0x20);
    WriteReg(9, 0x01);
    WriteReg(10, 0x20);
}

esp_err_t StackChanLeds::WriteReg(uint8_t reg, uint8_t val) {
    const uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(dev_, buf, 2, pdMS_TO_TICKS(100));
}

esp_err_t StackChanLeds::WritePixel(int index, RgbColor c) {
    const uint16_t rgb = Rgb565(c);
    // reg 48 + 2*i, uint16 little-endian, low byte first.
    const uint8_t buf[3] = {static_cast<uint8_t>(48 + index * 2),
                            static_cast<uint8_t>(rgb & 0xFF),
                            static_cast<uint8_t>(rgb >> 8)};
    return i2c_master_transmit(dev_, buf, 3, pdMS_TO_TICKS(100));
}

void StackChanLeds::PushAndLatch(const RgbColor px[kLedCount]) {
    if (dev_ == nullptr) return;
    // 🔴 Count the NAKs. Fire-and-forget I2C writes are how the servo rail
    //    investigation lost weeks: the code looked right, the bus looked alive,
    //    and nothing was landing. Logged at most once a second so a genuinely
    //    dead ring is loud without an animation spamming the console.
    uint16_t failed = 0;
    for (int i = 0; i < kLedCount; i++) {
        // Master brightness is applied HERE now, not in a register - reg 36 is
        // the pixel count. Linear, because the palette colours are already
        // chosen visually and gamma belongs on animation envelopes only.
        const RgbColor lit = {static_cast<uint8_t>(px[i].r * percent_ / 100),
                              static_cast<uint8_t>(px[i].g * percent_ / 100),
                              static_cast<uint8_t>(px[i].b * percent_ / 100)};
        // The pixel writes NAK only once the Wi-Fi and audio stacks are up, and
        // the same indices write cleanly earlier in boot. That is contention on
        // a shared bus, not a bad LED - so retrying is right, and a bus reset
        // would be badly wrong (never reset with a handle registered).
        //
        // 🔴 DO NOT PACE THIS LOOP. Tried and measured: the failures clustered
        //    at the last pixels, which looked exactly like a slave-side FIFO
        //    filling up. Inserting 150us between writes made it WORSE - the
        //    failures scattered across the ring, got more frequent, and the
        //    PMIC read on another task started failing too. A longer burst is a
        //    wider window to collide with everything else on this bus.
        //
        //    The fix for contention is to hold the bus for LESS time, not to be
        //    gentler while holding it.
        esp_err_t err = ESP_OK;
        bool ok = false;
        for (int attempt = 0; attempt < 3 && !ok; attempt++) {
            if (attempt > 0) {
                esp_rom_delay_us(attempt * 400);
            }
            err = WritePixel(i, lit);
            ok = err == ESP_OK;
        }
        if (!ok) {
            failed |= (1u << i);
            last_err_ = err;
        }
    }
    // 🔴 SECOND PASS, well after the first. With the error code logged, these
    //    are ESP_ERR_INVALID_RESPONSE - a real NACK from the PY32 - and they
    //    arrive as CONTIGUOUS RUNS ENDING AT THE LAST PIXEL (0xF00, 0xFC0,
    //    0x800). Once it starts refusing it
    //    refuses for the rest of the frame, then is fine on the next one.
    //
    //    That is the chip busy for a window of its own, not a bus we cannot
    //    get. The in-loop retries at 400us and 800us are far too quick to
    //    outlast it, which is why retrying harder never worked. Waiting a few
    //    milliseconds and re-sending only the refused pixels does, and it costs
    //    nothing on the overwhelming majority of frames where failed == 0.
    if (failed != 0) {
        esp_rom_delay_us(3000);
        uint16_t still_failed = 0;
        for (int i = 0; i < kLedCount; i++) {
            if ((failed & (1u << i)) == 0) continue;
            const RgbColor lit = {static_cast<uint8_t>(px[i].r * percent_ / 100),
                                  static_cast<uint8_t>(px[i].g * percent_ / 100),
                                  static_cast<uint8_t>(px[i].b * percent_ / 100)};
            const esp_err_t err = WritePixel(i, lit);
            if (err != ESP_OK) {
                still_failed |= (1u << i);
                last_err_ = err;
            }
        }
        failed = still_failed;
    }

    if (failed != 0) {
        const int64_t now = esp_timer_get_time();
        if (now - last_nak_log_us_ > 1000000) {
            last_nak_log_us_ = now;
            // WHICH ones matters: a contiguous half points at a second strip
            // that needs enabling, scattered ones point at bus contention.
            //
            // And WHICH ERROR matters just as much - it was missing, and two
            // wrong theories were built on its absence. ESP_FAIL is the PY32
            // genuinely refusing the address; ESP_ERR_TIMEOUT is us failing to
            // get the bus at all, which is somebody else's transaction and not
            // an LED problem in any sense.
            ESP_LOGW(TAG, "pixel writes failed, index mask 0x%03X, last error %s", failed,
                     esp_err_to_name(last_err_));
        }
    }
    // Commit: set the pixel COUNT in register 36, then set the latch bit on the
    // same value - the vendor's setter followed by its show(). kLedCount is 12,
    // which is what makes the chip clock out far enough to reach the second
    // strip.
    //
    // (Registers 0x70-0x8F follow the LED state too, but they are the chip's own
    // bookkeeping while a frame shifts out - a symptom, not an input. Writing
    // them changes nothing.)
    //
    // 🔴 Both writes are retried, in MILLISECONDS. The latch lands right after
    //    the last pixel, when the chip is most likely to be in its busy window,
    //    and a NAKed latch silently drops an entire frame.
    for (int attempt = 0; attempt < 3; attempt++) {
        if (WriteReg(36, static_cast<uint8_t>(kLedCount)) == ESP_OK) break;
        esp_rom_delay_us(3000);
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        if (WriteReg(36, static_cast<uint8_t>(kLedCount | 0x40)) == ESP_OK) break;
        esp_rom_delay_us(3000);
    }
}

void StackChanLeds::SetMasterBrightness(int percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    SetMasterBrightnessLocked(percent);
}

// 🔴 REGISTER 36 IS A PIXEL COUNT, NOT A BRIGHTNESS.
//
// This was first decoded as "brightness, clamped min(v, 32)". The clamp is the tell: 32 is arbitrary for a
// brightness, and exactly the size of the pixel bank (32 slots at 0x30-0x6F).
// It is a LENGTH - how many pixels to clock out - and show() being
// `read(36) | 0x40` then makes sense as "set the trigger bit, keep the count".
//
// The vendor writes 12. Writing percent * 32 / 100 = 6 at a 20% setting is
// precisely how many LEDs ever lit. Every "strip B is dark"
// symptom follows from that one line: the colours reached the chip, and the
// chip was told to shift out six pixels.
//
// So brightness has to be done in software, by scaling the colours before they
// are packed to RGB565. Which also explains why the ring never looked like it
// was running at 20% - it was at full output the whole time.
void StackChanLeds::SetMasterBrightnessLocked(int percent) {
    percent_ = std::clamp(percent, 0, 100);
    // No register write. Kept as a scale factor applied per pixel.
}

void StackChanLeds::StartAnimation(int interval_ms, std::function<void()> tick) {
    std::lock_guard<std::mutex> lock(mutex_);
    StopAnimationLocked();
    step_ = 0;
    tick_ = std::move(tick);
    if (tick_) tick_();  // first frame immediately, not one interval late
    if (timer_ != nullptr) {
        esp_timer_start_periodic(timer_, interval_ms * 1000);
    }
}

void StackChanLeds::StopAnimationLocked() {
    if (timer_ != nullptr) esp_timer_stop(timer_);
    tick_ = nullptr;
}

void StackChanLeds::Solid(RgbColor c) {
    std::lock_guard<std::mutex> lock(mutex_);
    StopAnimationLocked();
    RgbColor px[kLedCount];
    for (int i = 0; i < kLedCount; i++) px[i] = c;
    PushAndLatch(px);
}

void StackChanLeds::Off() {
    Solid(palette::kOff);
}

// The effects capture their colour BY VALUE rather than reading color_. The
// previous animation's timer callback is still live until StartAnimation takes
// the lock, so a member written here would be read by the outgoing effect.
// min_env is the FLOOR of the envelope, 0..255, not a percentage.
//
// 🔴 A breath that reaches zero is dark for most of its cycle. That is right
//    for decoration and wrong for a status light: a status colour was
//    invisible across the desk until this existed, because it only touched
//    peak brightness for an instant every six seconds. Give the ambient states
//    a floor so the colour is always readable and the movement is what varies.
void StackChanLeds::Breathe(RgbColor c, int period_ms, uint8_t min_env) {
    const int period = std::max(period_ms, 200);
    const int interval = 40;
    StartAnimation(interval, [this, c, period, interval, min_env]() {
        const float t = static_cast<float>((step_ * interval) % period) / period;
        const float wave = (1.0f - std::cos(t * 2.0f * static_cast<float>(M_PI))) * 0.5f;
        const uint8_t env = static_cast<uint8_t>(
            std::lround(min_env + (255.0f - min_env) * wave));
        const RgbColor lit = Scale(c, env);
        RgbColor px[kLedCount];
        for (int i = 0; i < kLedCount; i++) px[i] = lit;
        PushAndLatch(px);
        step_ = (step_ + 1) % (period / interval + 1);
    });
}

// A dot running DOWN BOTH STRIPS AT ONCE.
//
// Observed on the hardware: the strips are parallel, one down each side of the
// head. Walking 0..11 therefore runs down the left, jumps across, and runs down
// the right - which reads as a snake switching sides rather than as one
// movement. Running 0..5 and painting i together with i+6 makes both sides move
// together, which is what a head that is paying attention should look like.
void StackChanLeds::Comet(RgbColor c, int interval_ms) {
    static const uint8_t kTail[] = {255, 110, 40};
    StartAnimation(std::max(interval_ms, 30), [this, c]() {
        const int head = step_ % kStripLen;
        RgbColor px[kLedCount];
        for (int i = 0; i < kStripLen; i++) {
            const int d = (head - i + kStripLen) % kStripLen;
            const RgbColor v = (d < static_cast<int>(sizeof(kTail)))
                                   ? Scale(c, kTail[d])
                                   : palette::kOff;
            px[i] = v;              // one side
            px[MirrorOf(i)] = v;    // the other - reversed wiring, see header
        }
        PushAndLatch(px);
        step_ = (step_ + 1) % kStripLen;
    });
}

void StackChanLeds::Blink(RgbColor c, int interval_ms) {
    StartAnimation(std::max(interval_ms, 50), [this, c]() {
        const RgbColor lit = (step_ % 2 == 0) ? c : palette::kOff;
        RgbColor px[kLedCount];
        for (int i = 0; i < kLedCount; i++) px[i] = lit;
        PushAndLatch(px);
        step_ = (step_ + 1) % 2;
    });
}

// The idle ring, coloured by the status source when one is attached.
//
// Deliberately calm. Anything faster is a notification, and a resting state must
// not compete with one. The single exception is a real alert, which SHOULD
// compete - that is a blink, and it is the only time this ring blinks red.
//
// 🔴 STALE DATA GETS ITS OWN COLOUR, and this is the important part. A green
//    ring in front of a source that has stopped answering is a robot quietly
//    asserting "all fine" from a reading twenty minutes old. Muted grey means
//    "I do not know", which is true and visibly different from green.
void StackChanLeds::IdleAmbient() {
    constexpr int64_t kStaleAfterSeconds = 12 * 60;
    // Envelope floor. 🔴 The colours below are used at FULL saturation, unlike
    //    a decorative Scale(c, 90), which is 35%, not 90% - Scale() takes
    //    n/255. Full colour, breathing down to about a third rather than to
    //    nothing, is what makes this readable across a desk.
    constexpr uint8_t kFloor = 85;

    // 🔴 Three seconds, not six. RGB565 gives a dim breathing green only a
    //    dozen or so distinct levels, so the CYCLE LENGTH decides how long each
    //    one is held: at six seconds every step lasted the better part of a
    //    second and the whole thing looked like a slow slideshow. Halving the
    //    period halves the dwell without needing a single extra frame on the
    //    I2C bus - which matters, because that bus is the one thing here that
    //    does not have headroom to spare.
    constexpr int kCalm = 3000;

    // No source, or nothing read yet: lavender. Not a status colour, because
    // it is not one.
    if (status_ == nullptr || status_->age_seconds() < 0) {
        Breathe(palette::kLavender, kCalm, kFloor);
        return;
    }

    const int64_t age = status_->age_seconds();
    if (age > kStaleAfterSeconds) {
        ESP_LOGW(TAG, "status colour is %llds old - showing muted, not green",
                 static_cast<long long>(age));
        Breathe(palette::kMuted, kCalm, kFloor);
        return;
    }

    switch (status_->level()) {
        case StatusSource::Level::kOk:
            Breathe(palette::kGreen, kCalm, kFloor);
            break;
        case StatusSource::Level::kWarn:
            // Faster still, because a warning should pull the eye a little.
            Breathe(palette::kAmber, 1600, kFloor);
            break;
        case StatusSource::Level::kAlert:
            Blink(palette::kRed, 600);
            break;
        case StatusSource::Level::kUnknown:
        default:
            Breathe(palette::kMuted, kCalm, kFloor);
            break;
    }
}

void StackChanLeds::BootSweep() {
    if (dev_ == nullptr) return;
    // Synchronous, ~420 ms. Cheap, and it makes power-on feel deliberate -
    // it is also the one moment that reveals the physical LED ordering.
    // Down both sides together - see Comet() for why this is 0..5 and mirrored.
    for (int head = 0; head < kStripLen; head++) {
        std::lock_guard<std::mutex> lock(mutex_);
        RgbColor px[kLedCount];
        for (int i = 0; i < kStripLen; i++) {
            const int d = (head - i + kStripLen) % kStripLen;
            const RgbColor v = (d == 0)   ? palette::kLavender
                               : (d == 1) ? Scale(palette::kLavender, 90)
                                          : palette::kOff;
            px[i] = v;
            px[MirrorOf(i)] = v;
        }
        PushAndLatch(px);
        vTaskDelay(pdMS_TO_TICKS(70));
    }
    Off();
}

void StackChanLeds::OnStateChanged() {
    // Forwarded BEFORE the early return. The board's interest in this edge has
    // nothing to do with whether the PY32 came up, and a ring that failed to
    // initialise must not also swallow status alerts.
    if (on_state_) on_state_();
    if (dev_ == nullptr) return;
    auto& app = Application::GetInstance();
    // Logged so "the ring never changes" can be told apart from "the ring is
    // never asked to change" - GetLed() is only consulted if the framework
    // actually routes state transitions here.
    ESP_LOGI(TAG, "state -> %d", static_cast<int>(app.GetDeviceState()));
    switch (app.GetDeviceState()) {
        case kDeviceStateStarting:
            Comet(palette::kLavender, 60);
            break;
        case kDeviceStateWifiConfiguring:
            Blink(palette::kBlue, 500);
            break;
        case kDeviceStateConnecting:
            Breathe(palette::kBlue, 1200);
            break;
        case kDeviceStateIdle:
            // A slow resting pulse, not darkness. Fully off reads as "the robot
            // is broken"; a barely-there breath reads as "waiting" - and it is
            // the only thing on the desk that says he is listening for his name
            // without saying anything.
            //
            // With a status source attached the colour is the STATUS - green,
            // amber or a red blink - so a glance across the desk answers "is
            // everything OK?" with nothing asked and nothing spoken.
            IdleAmbient();
            break;
        case kDeviceStateListening:
        case kDeviceStateAudioTesting:
            Comet(palette::kLavender, 90);
            break;
        case kDeviceStateSpeaking:
            Solid(palette::kLavender);
            break;
        case kDeviceStateUpgrading:
            Blink(palette::kBlue, 200);
            break;
        case kDeviceStateActivating:
            Blink(palette::kLavender, 500);
            break;
        case kDeviceStateFatalError:
            Solid(palette::kRed);
            break;
        default:
            return;
    }
}

void StackChanLeds::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.robot.set_led_color",
        "Set the colour of the light ring on the robot's head. "
        "color: one of lavender, blue, green, amber, red, grey, off - or a #rrggbb hex value. "
        "effect: solid, breathe, blink or comet. "
        "brightness: 0-100 percent; leave at 0 to keep the current level.",
        PropertyList({
            Property("color",      kPropertyTypeString, std::string("lavender")),
            Property("effect",     kPropertyTypeString, std::string("solid")),
            Property("brightness", kPropertyTypeInteger, 0, 0, 100),
        }),
        [this](const PropertyList& p) -> ReturnValue {
            if (dev_ == nullptr) return std::string("light ring unavailable");

            RgbColor c;
            const std::string name = p["color"].value<std::string>();
            if (!LookupColor(name, c)) {
                return std::string("unknown colour '" + name +
                                   "' - use lavender, blue, green, amber, red, grey, off, or #rrggbb");
            }
            const int b = p["brightness"].value<int>();
            if (b > 0) SetMasterBrightness(b);

            const std::string effect = p["effect"].value<std::string>();
            if (effect == "breathe")    Breathe(c, 2000);
            else if (effect == "blink") Blink(c, 400);
            else if (effect == "comet") Comet(c, 90);
            else                        Solid(c);

            ESP_LOGI(TAG, "set_led_color %s %s at %d%%", name.c_str(), effect.c_str(), percent_);
            return true;
        });

    ESP_LOGI(TAG, "registered light ring MCP tool");
}
