/*
 * Feetech SCS serial-bus servo driver for the M5Stack StackChan base.
 *
 * Parameters recovered from the factory firmware by disassembly and confirmed
 * against the device's own boot log.
 *
 *   UART1, TX = GPIO6, RX = GPIO7, 1 000 000 baud
 *   servo IDs 1 and 2, position range 0..1023 (10-bit -> SCS/SCSCL family)
 *
 * NOT PWM. These are addressable smart servos: each has an ID, is commanded by
 * packet, and reports its position back. Do not reach for LEDC or MCPWM.
 */
#ifndef STACKCHAN_SCS_SERVO_H
#define STACKCHAN_SCS_SERVO_H

// Not decoration: the per-unit bench trim is a BUILD setting
// (CONFIG_STACKCHAN_PAN_TRIM), and this header is included from files that do
// not otherwise pull sdkconfig in.
#include <sdkconfig.h>

#include <driver/uart.h>
#include <driver/gpio.h>
#include <cstdint>

// --- bus wiring (see header comment for provenance) ---
#define SCS_UART_NUM        UART_NUM_1
#define SCS_TX_PIN          GPIO_NUM_6
#define SCS_RX_PIN          GPIO_NUM_7
#define SCS_BAUD_RATE       1000000
#define SCS_BUF_SIZE        1024

// --- protocol ---
#define SCS_HEADER          0xFF
#define SCS_INST_PING       0x01
#define SCS_INST_READ       0x02
#define SCS_INST_WRITE      0x03

// SCSCL register map. Goal/present position are 16-bit pairs.
#define SCS_REG_GOAL_POSITION   42
#define SCS_REG_GOAL_TIME       44
#define SCS_REG_GOAL_SPEED      46
#define SCS_REG_PRESENT_POS     56

// Servo IDs, from "[ScsServo] id: 1 ... id: 2" in the factory boot log.
#define SCS_ID_PAN          1
#define SCS_ID_TILT         2

// 🔴 THE CENTRE IS PER-UNIT AND IS READ FROM THE ROBOT'S OWN NVS.
//
//    The factory sets each head's mechanical zero individually and stores it in
//    NVS as `zero_pos_1` (pan) and `zero_pos_2` (tilt), as I32, in SCS 0-1023
//    position units. They are in no datasheet, they differ between units, and
//    they cannot be recovered once the flash is overwritten.
//
//    Flashing the APP partition leaves NVS alone, so every robot still carries
//    its own values - including robots this project has never seen. They are read
//    at Initialize(); see LoadFactoryCentres() in scs_servo.cc.
//
// ⚠️ THIS IS A SAFETY MATTER, NOT COSMETICS. The travel limits are computed
//    AROUND the centre. Using another unit's centre makes the head sit crooked,
//    which is obvious - but it also shifts the tilt clamp, which is not, and
//    tilt has only ~90 degrees before it reaches a mechanical stop.
//
//    The values below are the FALLBACK, for a robot whose factory calibration
//    has been erased. They are the reference unit's, so on any other robot they
//    are a guess - which is why the firmware says so, loudly, when it uses them.
#define SCS_FALLBACK_ZERO_PAN   460
#define SCS_FALLBACK_ZERO_TILT  620

// Bench trim: the small correction for an assembly that still sits off straight
// ahead once centred on its own factory zero. Kept SEPARATE from the factory
// value so each number's provenance stays visible - the centre is what the
// vendor measured, the trim is what you measured. Per-unit, so it is a build
// setting rather than a constant; see CONFIG_STACKCHAN_PAN_TRIM in
// Kconfig.projbuild. Positive counts move the head right.
#define SCS_PAN_TRIM_COUNTS   CONFIG_STACKCHAN_PAN_TRIM
#define SCS_TILT_TRIM_COUNTS  CONFIG_STACKCHAN_TILT_TRIM

// Travel limits, as spans either side of whatever this unit's centre turns out
// to be. 0..1023 is the electrical range; these are deliberately tighter. Tilt
// has only ~90 degrees of travel and driving it into its mechanical stop is the
// most plausible way to damage the unit.
#define SCS_POS_MIN         0
#define SCS_POS_MAX         1023
#define SCS_PAN_SAFE_SPAN   250
#define SCS_TILT_SAFE_SPAN  120

class ScsServo {
public:
    ScsServo();
    ~ScsServo();

    // Brings up UART1 on the pins above. Safe to call once from the board ctor.
    bool Initialize();

    // Absolute position in raw SCS units, clamped to the safe window for that
    // servo. time_ms is the servo-side move duration (0 = as fast as it likes).
    bool WritePosition(uint8_t id, int position, uint16_t time_ms = 0);

    // Reads the servo's actual position. Returns -1 on timeout/checksum error.
    // These servos have feedback; use it rather than assuming the move landed.
    int ReadPosition(uint8_t id);

    // Is a servo answering at all?
    bool Ping(uint8_t id);

    // Convenience: centre both servos on the factory calibration.
    void CenterAll();

    // Clamp helper, exposed so callers can report what they actually did.
    static int ClampFor(uint8_t id, int position);

    // This unit's centre for a servo: its factory zero plus the configured
    // bench trim. Valid after Initialize(); before that it is the fallback.
    static int CenterFor(uint8_t id);

    // False when the factory calibration could not be read and the fallback
    // centre is in use - which means the travel limits are a guess too.
    static bool calibration_is_fallback();

    // 📐 THE BENCH TRIM, AT RUNTIME.
    //
    //    It began as a build setting because it is per-unit - and that made it
    //    the one number every owner had to set for their own robot by editing a
    //    Kconfig and rebuilding the firmware. A toolchain, for a number you
    //    arrive at by looking at the robot and deciding he is not quite straight.
    //
    //    So it lives in NVS now, with the build setting as the DEFAULT rather
    //    than the value. A robot that has never been trimmed behaves exactly as
    //    before, and nothing in the build has to change to trim one.
    static int PanTrim();
    static int TiltTrim();
    // Live, not persisted: what each nudge on the trim screen calls. The head is
    // re-centred after it so the change can be SEEN.
    //
    // 🔴 Clamped to kMaxTrimCounts. A bench correction is small by definition,
    //    and the travel limits are spans around the FACTORY zero rather than
    //    around the trimmed centre - so a trim big enough to re-aim the head
    //    would quietly eat one side of the tilt limit, which is the limit that
    //    exists because tilt reaches a mechanical stop.
    static void SetTrim(int pan_counts, int tilt_counts);
    // Writes the current pair to NVS. Leaving the screen with Back instead calls
    // SetTrim with whatever it opened with, so nothing persists by accident.
    static void SaveTrim();
    static constexpr int kMaxTrimCounts = 40;   // ~12 degrees either way

private:
    bool initialized_ = false;

    void WritePacket(uint8_t id, uint8_t instruction,
                     const uint8_t* params, uint8_t param_len);
    int ReadPacket(uint8_t* out, size_t out_len, int timeout_ms = 50);
    static uint8_t Checksum(const uint8_t* buf, size_t len);
};

#endif  // STACKCHAN_SCS_SERVO_H
