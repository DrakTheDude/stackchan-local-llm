/*
 * The 12x WS2812C LEDs on the StackChan base.
 *
 * The LEDs are NOT on an ESP32 GPIO - they hang off the PY32 co-processor at
 * I2C 0x6F, the same chip that drives the servo rail. So none of xiaozhi's
 * existing led/ backends apply: CircularStrip drives WS2812s over RMT from a
 * GPIO, and there is no GPIO to give it. This is a backend that speaks the
 * PY32's LED protocol, recovered from the factory firmware and verified on
 * hardware.
 *
 * 🔴 THE CHIP IS AT 0x6F. The vendor's boot log prints "PY32IOExpander:
 *    Version: 0x41" - that 0x41 is a VERSION VALUE, not an address.
 *
 * Protocol:
 *
 *   pixel   reg 48 + 2*i, uint16 LITTLE-ENDIAN RGB565, i in 0..31
 *   count   reg 36 <- how many pixels to clock out (12 on this robot)
 *   latch   reg 36 <- count | 0x40
 *
 * 🔴 REGISTER 36 IS A PIXEL COUNT, NOT A BRIGHTNESS. It was first decoded as
 *    brightness, and writing a brightness-shaped value there is exactly what
 *    made only half the LEDs light. There is no hardware brightness control on
 *    this chip that we have found; brightness is applied in software.
 */
#ifndef STACKCHAN_LEDS_H
#define STACKCHAN_LEDS_H

#include "led/led.h"   // main/ is on the include path; main/led/ is not

#include <driver/i2c_master.h>
#include <esp_timer.h>

#include <functional>
#include <mutex>
#include <string>

class StatusSource;

struct RgbColor {
    uint8_t r = 0, g = 0, b = 0;
};

// The project palette. The lavender is deliberately a SATURATED purple rather
// than the pale #c9a9ff used on screen: an LED is a light source, and a pale
// colour reads as white-with-a-hint at any real brightness.
namespace palette {
constexpr RgbColor kLavender = {161, 126, 255};  // #a17eff
constexpr RgbColor kBlue     = {127, 195, 255};  // #7fc3ff - "working"
constexpr RgbColor kGreen    = {126, 224, 168};  // #7ee0a8 - ok
constexpr RgbColor kAmber    = {255, 207, 142};  // #ffcf8e - warn
constexpr RgbColor kRed      = {255, 142, 142};  // #ff8e8e - alert
constexpr RgbColor kMuted    = {154, 164, 178};  // #9aa4b2 - stale / no contact
constexpr RgbColor kOff      = {0, 0, 0};
}  // namespace palette

class StackChanLeds : public Led {
public:
    // Twelve pixels: two 6-LED strips on one chain, exactly as the vendor drives
    // them. Slots 0-5 are one side, 6-11 the other.
    static constexpr int kLedCount = 12;
    // The two strips run down the LEFT and RIGHT of the head - they are
    // PARALLEL, not a ring. So an effect that walks 0..11 crosses from one side
    // to the other and reads as a snake changing sides, not as motion.
    static constexpr int kStripLen = 6;

    // 🔴 The second strip is wired in REVERSE. The chain runs down one side and
    //    back up the other, so slot 6 sits opposite slot 5, not opposite slot 0.
    //    Pairing i with i+6 therefore lights opposite ENDS and the two dots
    //    visibly cross; pairing i with 11-i lines them up.
    //
    //    Observed, not derived - it is the same wiring that made a plain 0..11
    //    walk look like a continuous ring rather than a zig-zag.
    static constexpr int MirrorOf(int i) { return kLedCount - 1 - i; }
    static constexpr uint8_t kIoeAddr = 0x6F;

    ~StackChanLeds();

    // Registers a LONG-LIVED device handle on the bus. i2c_master_bus_reset()
    // must never be called while it is registered - a reset with any handle
    // registered breaks the bus for the rest of the boot.
    bool Initialize(i2c_master_bus_handle_t bus);
    void RegisterMcpTools();

    bool ready() const { return dev_ != nullptr; }

    // Led interface: reflect the conversation state on the ring.
    void OnStateChanged() override;

    // Forwards that same edge to the board.
    //
    // The framework notifies the LED object on every device state change and
    // notifies nothing else, so this is the only place the edge exists. The
    // board needs it because a status alert raised while he was mid-sentence is
    // deferred until he is idle, and without this the only thing that would
    // retry it is the next status reading. Forwarding one callback beats
    // standing up a second state poller to watch the same variable.
    void SetOnDeviceStateChanged(std::function<void()> cb) { on_state_ = std::move(cb); }

    // 0..100. Kept low on purpose: WS2812s at full duty are unpleasant at desk
    // distance.
    void SetMasterBrightness(int percent);

    void Solid(RgbColor c);
    void Off();
    // min_env floors the envelope (0..255) so the colour never fully vanishes.
    void Breathe(RgbColor c, int period_ms, uint8_t min_env = 0);
    void Comet(RgbColor c, int interval_ms);
    void Blink(RgbColor c, int interval_ms);

    // One lavender sweep down both strips, then hand back to OnStateChanged().
    void BootSweep();
    // The resting ring. Coloured by the status source when one is attached,
    // and muted when its reading is stale - see the note in the .cc.
    void IdleAmbient();

    // Optional. When set, the IDLE ring breathes the status colour instead of a
    // fixed lavender - green, amber, or a red blink. May be null.
    void SetStatusSource(StatusSource* s) { status_ = s; }

    static bool LookupColor(const std::string& name, RgbColor& out);

private:
    // Caller must hold mutex_ for all of these.
    esp_err_t WriteReg(uint8_t reg, uint8_t val);
    esp_err_t WritePixel(int index, RgbColor c);
    void PushAndLatch(const RgbColor px[kLedCount]);
    void SetMasterBrightnessLocked(int percent);
    void SelfTestLocked();
    // Puts the PY32's pins back to their known-good state. Runs every boot -
    // the PY32 keeps its state independently of the ESP32.
    void RestoreDefaultPinsLocked();
    void StopAnimationLocked();
    // Takes mutex_ itself, and runs the first frame before starting the timer.
    void StartAnimation(int interval_ms, std::function<void()> tick);

    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    esp_timer_handle_t timer_ = nullptr;
    std::function<void()> tick_;
    std::mutex mutex_;

    // Applied in SOFTWARE, per pixel - register 36 is the pixel count.
    //
    // 🔴 30%, and the reason is COLOUR RESOLUTION rather than wanting it
    //    brighter. The pixels are RGB565, so green has 6 bits and red/blue 5.
    //    Scaling to 20% first squeezes a breathing green into about NINE
    //    distinct output levels, and a slow cycle then holds each one long
    //    enough to be seen, quite correctly, as a low frame rate. Brightness
    //    here buys steps, not glare. Solid colours at rest would look fine lower.
    int percent_ = 30;
    int step_ = 0;                  // animation frame counter, owned by tick_
    int64_t last_nak_log_us_ = 0;   // rate-limit for the failure warning
    // Why the last pixel write failed. Log the error code, not just "NAK":
    // ESP_FAIL / ESP_ERR_INVALID_RESPONSE mean the PY32 refused, ESP_ERR_TIMEOUT
    // means we never got the bus. Theories built without it were wrong.
    esp_err_t last_err_ = ESP_OK;
    StatusSource* status_ = nullptr;  // ambient status, may be null
    std::function<void()> on_state_;  // board's device-state hook, may be null
};

#endif  // STACKCHAN_LEDS_H
