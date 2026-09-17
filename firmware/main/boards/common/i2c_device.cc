#include "i2c_device.h"

#include <esp_log.h>
#include <esp_timer.h>

#include <cstring>

#define TAG "I2cDevice"

I2cDevice::I2cDevice(i2c_master_bus_handle_t i2c_bus, uint8_t addr)
    : i2c_bus_(i2c_bus), device_address_(addr) {
    i2c_device_config_t i2c_device_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400 * 1000,
        .scl_wait_us = 0,
        .flags =
            {
                .disable_ack_check = 0,
            },
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus, &i2c_device_cfg, &i2c_device_));
    assert(i2c_device_ != NULL);
}

// 🔴 A FLAKY I2C TRANSFER MUST NOT REBOOT THE DEVICE.
//
//    These three were ESP_ERROR_CHECK(...), which aborts on any error. On the
//    StackChan that turned an occasional bus glitch into a full restart, from
//    the most routine caller there is:
//
//      Application::Run -> LvglDisplay::UpdateStatusBar -> GetBatteryLevel
//        -> Axp2101::IsCharging -> I2cDevice::ReadReg -> abort()
//
//    The battery gauge is polled for the status bar forever, so a bus that
//    NAKs even rarely is guaranteed to hit it. Meanwhile the LED ring shares
//    that same bus and has always tolerated the identical NAK with a warning -
//    two policies for one failure, and the harsher one was on the path that
//    mattered least.
//
//    Now: retry a couple of times, then log and carry on with a zero. The
//    failure modes of a zero were checked rather than assumed - for the AXP2101
//    it reads as "neither charging nor discharging", which leaves the power
//    save timer NOT shutting down. Wrong in the safe direction.
//
//    The warning is rate-limited because a genuinely wedged bus would otherwise
//    fill the log faster than anything else could print.
namespace {

constexpr int kI2cAttempts = 3;

void LogTransferFailure(const char* op, uint8_t addr, uint8_t reg, esp_err_t err) {
    static int64_t last_us = 0;
    const int64_t now = esp_timer_get_time();
    if (now - last_us < 1000000) {
        return;
    }
    last_us = now;
    ESP_LOGW(TAG, "i2c %s failed after %d tries: addr 0x%02X reg 0x%02X (%s)", op, kI2cAttempts,
             addr, reg, esp_err_to_name(err));
}

}  // namespace

void I2cDevice::WriteReg(uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < kI2cAttempts; i++) {
        err = i2c_master_transmit(i2c_device_, buffer, 2, 100);
        if (err == ESP_OK) {
            return;
        }
    }
    LogTransferFailure("write", device_address_, reg, err);
}

uint8_t I2cDevice::ReadReg(uint8_t reg) {
    uint8_t buffer[1] = {0};
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < kI2cAttempts; i++) {
        err = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, 1, 100);
        if (err == ESP_OK) {
            return buffer[0];
        }
    }
    LogTransferFailure("read", device_address_, reg, err);
    return 0;
}

void I2cDevice::ReadRegs(uint8_t reg, uint8_t* buffer, size_t length) {
    esp_err_t err = ESP_FAIL;
    for (int i = 0; i < kI2cAttempts; i++) {
        err = i2c_master_transmit_receive(i2c_device_, &reg, 1, buffer, length, 100);
        if (err == ESP_OK) {
            return;
        }
    }
    memset(buffer, 0, length);
    LogTransferFailure("read", device_address_, reg, err);
}

esp_err_t I2cDevice::ResetBus(const char* reason) {
    ESP_LOGW(TAG, "Resetting I2C bus: %s", reason ? reason : "unspecified");
    return i2c_master_bus_reset(i2c_bus_);
}
