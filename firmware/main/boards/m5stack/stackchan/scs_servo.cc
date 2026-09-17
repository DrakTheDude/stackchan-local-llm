#include "scs_servo.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <algorithm>

#define TAG "ScsServo"

ScsServo::ScsServo() {}

ScsServo::~ScsServo() {
    if (initialized_) {
        uart_driver_delete(SCS_UART_NUM);
    }
}

bool ScsServo::Initialize() {
    if (initialized_) return true;

    uart_config_t cfg = {};
    cfg.baud_rate = SCS_BAUD_RATE;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity    = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    esp_err_t err = uart_driver_install(SCS_UART_NUM, SCS_BUF_SIZE, SCS_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_param_config(SCS_UART_NUM, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(err));
        return false;
    }
    err = uart_set_pin(SCS_UART_NUM, SCS_TX_PIN, SCS_RX_PIN,
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(err));
        return false;
    }

    initialized_ = true;
    ESP_LOGI(TAG, "bus up: UART%d tx=%d rx=%d @%d baud",
             SCS_UART_NUM, SCS_TX_PIN, SCS_RX_PIN, SCS_BAUD_RATE);

    // Report which servos actually answer. Cheap, and it turns "the head did
    // not move" into a diagnosable fact at boot rather than a mystery later.
    for (uint8_t id : {SCS_ID_PAN, SCS_ID_TILT}) {
        ESP_LOGI(TAG, "ping id %d: %s", id, Ping(id) ? "OK" : "NO REPLY");
    }
    return true;
}

uint8_t ScsServo::Checksum(const uint8_t* buf, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++) sum += buf[i];
    return static_cast<uint8_t>(~sum);
}

void ScsServo::WritePacket(uint8_t id, uint8_t instruction,
                           const uint8_t* params, uint8_t param_len) {
    // 0xFF 0xFF ID LEN INST PARAMS... CHECKSUM     (LEN = param_len + 2)
    uint8_t pkt[32];
    size_t n = 0;
    pkt[n++] = SCS_HEADER;
    pkt[n++] = SCS_HEADER;
    pkt[n++] = id;
    pkt[n++] = static_cast<uint8_t>(param_len + 2);
    pkt[n++] = instruction;
    for (uint8_t i = 0; i < param_len; i++) pkt[n++] = params[i];
    // Checksum covers ID onwards, i.e. everything after the two header bytes.
    pkt[n] = Checksum(&pkt[2], n - 2);
    n++;

    uart_flush_input(SCS_UART_NUM);
    uart_write_bytes(SCS_UART_NUM, reinterpret_cast<const char*>(pkt), n);
    uart_wait_tx_done(SCS_UART_NUM, pdMS_TO_TICKS(20));
}

int ScsServo::ReadPacket(uint8_t* out, size_t out_len, int timeout_ms) {
    int len = uart_read_bytes(SCS_UART_NUM, out, out_len, pdMS_TO_TICKS(timeout_ms));
    if (len < 6) return -1;                       // shortest valid reply
    if (out[0] != SCS_HEADER || out[1] != SCS_HEADER) return -1;
    uint8_t expect = Checksum(&out[2], len - 3);
    if (expect != out[len - 1]) {
        ESP_LOGW(TAG, "checksum mismatch (got 0x%02X want 0x%02X)", out[len - 1], expect);
        return -1;
    }
    return len;
}

int ScsServo::ClampFor(uint8_t id, int position) {
    int lo = (id == SCS_ID_TILT) ? SCS_TILT_SAFE_MIN : SCS_PAN_SAFE_MIN;
    int hi = (id == SCS_ID_TILT) ? SCS_TILT_SAFE_MAX : SCS_PAN_SAFE_MAX;
    lo = std::max(lo, SCS_POS_MIN);
    hi = std::min(hi, SCS_POS_MAX);
    return std::clamp(position, lo, hi);
}

bool ScsServo::WritePosition(uint8_t id, int position, uint16_t time_ms) {
    if (!initialized_) return false;

    int clamped = ClampFor(id, position);
    if (clamped != position) {
        ESP_LOGW(TAG, "id %d: %d outside safe travel, clamped to %d", id, position, clamped);
    }

    // SCSCL is BIG-endian on the wire (high byte first). The STS/SMS family is
    // little-endian; getting this backwards makes the head slam to an extreme,
    // so it is called out rather than left implicit. 10-bit 0..1023 positions
    // identify this as SCSCL.
    uint8_t p[7];
    p[0] = SCS_REG_GOAL_POSITION;
    p[1] = static_cast<uint8_t>((clamped >> 8) & 0xFF);
    p[2] = static_cast<uint8_t>(clamped & 0xFF);
    p[3] = static_cast<uint8_t>((time_ms >> 8) & 0xFF);
    p[4] = static_cast<uint8_t>(time_ms & 0xFF);
    p[5] = 0;   // speed high - 0 means "use goal time"
    p[6] = 0;   // speed low
    WritePacket(id, SCS_INST_WRITE, p, sizeof(p));
    return true;
}

int ScsServo::ReadPosition(uint8_t id) {
    if (!initialized_) return -1;
    uint8_t p[2] = { SCS_REG_PRESENT_POS, 2 };
    WritePacket(id, SCS_INST_READ, p, sizeof(p));

    uint8_t rx[16] = {};
    int len = ReadPacket(rx, sizeof(rx));
    if (len < 8) return -1;
    // 0xFF 0xFF ID LEN ERR DATA_H DATA_L CHK
    return (static_cast<int>(rx[5]) << 8) | rx[6];
}

bool ScsServo::Ping(uint8_t id) {
    if (!initialized_) return false;
    WritePacket(id, SCS_INST_PING, nullptr, 0);
    uint8_t rx[16] = {};
    return ReadPacket(rx, sizeof(rx)) >= 6;
}

void ScsServo::CenterAll() {
    WritePosition(SCS_ID_PAN,  SCS_CENTER_PAN,  500);
    WritePosition(SCS_ID_TILT, SCS_CENTER_TILT, 500);
}
