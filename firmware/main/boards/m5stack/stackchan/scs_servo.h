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

// 🔴 PER-UNIT VALUES, FROM ONE ROBOT. RELEASE BLOCKER.
//
//    Factory centre calibration read from the factory NVS of the reference
//    unit. Another StackChan will almost certainly differ - and the safe travel
//    limits below are computed around this centre, so on a different unit the
//    head would sit off-centre AND the tilt clamp could land closer to a
//    mechanical stop than intended.
//
//    Before any public release these must come from each robot's OWN factory
//    calibration, which survives flashing the app partition. See
//    docs/roadmap.md, "Firmware and platform".
#define SCS_ZERO_POS_PAN    460
#define SCS_ZERO_POS_TILT   620

// Bench trim, measured on the reference unit: with pan commanded to 0 the
// head sat ~4 degrees left of straight ahead. At 300deg/1024 counts that is
// ~14 counts. Kept SEPARATE from the factory value above so the provenance of
// each number stays visible - 460 is what the vendor stored, +14 is what we
// measured. Positive counts move right (negative pan = left, per the tool).
#define SCS_PAN_TRIM_COUNTS   14
#define SCS_TILT_TRIM_COUNTS  0

#define SCS_CENTER_PAN      (SCS_ZERO_POS_PAN  + SCS_PAN_TRIM_COUNTS)
#define SCS_CENTER_TILT     (SCS_ZERO_POS_TILT + SCS_TILT_TRIM_COUNTS)

// Position limits. 0..1023 is the electrical range; these are deliberately
// tighter. Tilt has only ~90 degrees of travel and driving it into its
// mechanical stop is the most plausible way to damage the unit, so it is
// clamped hard around centre until real limits are measured on the bench.
#define SCS_POS_MIN         0
#define SCS_POS_MAX         1023
#define SCS_PAN_SAFE_MIN    (SCS_ZERO_POS_PAN  - 250)
#define SCS_PAN_SAFE_MAX    (SCS_ZERO_POS_PAN  + 250)
#define SCS_TILT_SAFE_MIN   (SCS_ZERO_POS_TILT - 120)
#define SCS_TILT_SAFE_MAX   (SCS_ZERO_POS_TILT + 120)

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

private:
    bool initialized_ = false;

    void WritePacket(uint8_t id, uint8_t instruction,
                     const uint8_t* params, uint8_t param_len);
    int ReadPacket(uint8_t* out, size_t out_len, int timeout_ms = 50);
    static uint8_t Checksum(const uint8_t* buf, size_t len);
};

#endif  // STACKCHAN_SCS_SERVO_H
