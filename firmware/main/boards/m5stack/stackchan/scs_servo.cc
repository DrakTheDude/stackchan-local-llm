#include "scs_servo.h"

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>
#include <algorithm>

#define TAG "ScsServo"

namespace {

// This unit's factory centres. Start as the fallback and are replaced by the
// robot's own values at Initialize(). File-scope rather than members because
// ClampFor is static and is called from the motion task, which has no handle.
int g_zero_pan = SCS_FALLBACK_ZERO_PAN;
int g_zero_tilt = SCS_FALLBACK_ZERO_TILT;
bool g_fallback = true;

// The bench trim. The build setting is the DEFAULT; a stored value replaces it
// at Initialize(). Ours, unlike the factory keys above, so it gets a namespace
// we chose and can open by name.
int g_pan_trim = SCS_PAN_TRIM_COUNTS;
int g_tilt_trim = SCS_TILT_TRIM_COUNTS;
constexpr const char kTrimNs[] = "stacky";

void LoadTrim() {
    nvs_handle_t h;
    if (nvs_open(kTrimNs, NVS_READONLY, &h) != ESP_OK) {
        return;   // never trimmed on this robot; the build setting stands
    }
    int32_t pan = 0, tilt = 0;
    const bool got_pan = nvs_get_i32(h, "pan_trim", &pan) == ESP_OK;
    const bool got_tilt = nvs_get_i32(h, "tilt_trim", &tilt) == ESP_OK;
    nvs_close(h);
    if (got_pan) g_pan_trim = std::clamp<int>(pan, -ScsServo::kMaxTrimCounts,
                                              ScsServo::kMaxTrimCounts);
    if (got_tilt) g_tilt_trim = std::clamp<int>(tilt, -ScsServo::kMaxTrimCounts,
                                                ScsServo::kMaxTrimCounts);
    if (got_pan || got_tilt) {
        ESP_LOGI(TAG, "bench trim from NVS: pan %+d tilt %+d", g_pan_trim, g_tilt_trim);
    }
}

// 🔎 THE KEYS ARE FOUND BY SEARCHING, NOT BY NAMING A NAMESPACE.
//
//    The factory stores the calibration under namespace INDEX 2 - which is an
//    internal NVS detail, not something nvs_open() accepts. The namespace's
//    actual name belongs to the vendor's app and there is no reason to believe
//    it is the same string on every production run.
//
//    So this walks every entry in the partition looking for the two key names,
//    and opens whichever namespace they turn up in. Slower than opening a known
//    namespace, and it runs once at boot; the alternative is a constant that is
//    right on the units we have seen and silently wrong on the rest.
//
//    On the reference unit the namespace turns out to be called "servo" and the
//    values read 460/620 - the same numbers the disassembly of its factory
//    backup produced, which is what makes this a confirmation rather than a
//    hopeful substitution. The name is recorded here as a fact about one robot,
//    NOT hard-coded: one sample is not a naming convention.
bool FindCalibrationNamespace(char* out_ns, size_t out_len) {
    nvs_iterator_t it = nullptr;
    esp_err_t err = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_I32, &it);
    while (err == ESP_OK && it != nullptr) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (strcmp(info.key, "zero_pos_1") == 0) {
            strncpy(out_ns, info.namespace_name, out_len - 1);
            out_ns[out_len - 1] = '\0';
            nvs_release_iterator(it);
            return true;
        }
        err = nvs_entry_next(&it);
    }
    if (it != nullptr) nvs_release_iterator(it);
    return false;
}

// Reads this robot's own factory centres. Leaves the fallback in place, and says
// so, if anything is missing - a wrong centre moves the travel limits with it.
void LoadFactoryCentres() {
    // Idempotent, and cheap insurance: this runs from the board constructor and
    // must not report "no calibration" merely because it arrived before NVS was
    // up. A false negative here silently swaps in another unit's travel limits.
    const esp_err_t init = nvs_flash_init();
    if (init != ESP_OK && init != ESP_ERR_NVS_NO_FREE_PAGES &&
        init != ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unavailable (%s) - cannot read factory calibration",
                 esp_err_to_name(init));
    }

    char ns[NVS_KEY_NAME_MAX_SIZE + 8] = {0};
    if (!FindCalibrationNamespace(ns, sizeof(ns))) {
        ESP_LOGE(TAG, "no factory servo calibration in NVS - using the reference "
                      "unit's centres (%d/%d). The head may sit off centre and the "
                      "tilt limits are a GUESS on this robot.",
                 SCS_FALLBACK_ZERO_PAN, SCS_FALLBACK_ZERO_TILT);
        return;
    }
    nvs_handle_t h;
    if (nvs_open(ns, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGE(TAG, "found calibration in namespace '%s' but could not open it", ns);
        return;
    }
    int32_t pan = 0, tilt = 0;
    const esp_err_t e1 = nvs_get_i32(h, "zero_pos_1", &pan);
    const esp_err_t e2 = nvs_get_i32(h, "zero_pos_2", &tilt);
    nvs_close(h);

    // Sanity, because a bad centre is worse than no centre: the value has to sit
    // inside the electrical range with room for the travel limits either side.
    const bool sane = e1 == ESP_OK && e2 == ESP_OK &&
                      pan > SCS_PAN_SAFE_SPAN && pan < SCS_POS_MAX - SCS_PAN_SAFE_SPAN &&
                      tilt > SCS_TILT_SAFE_SPAN && tilt < SCS_POS_MAX - SCS_TILT_SAFE_SPAN;
    if (!sane) {
        ESP_LOGE(TAG, "factory calibration in '%s' unusable (pan=%ld tilt=%ld) - "
                      "keeping the fallback centres", ns, (long)pan, (long)tilt);
        return;
    }
    g_zero_pan = pan;
    g_zero_tilt = tilt;
    g_fallback = false;
    ESP_LOGI(TAG, "factory centres from NVS '%s': pan=%d tilt=%d (trim %+d/%+d)",
             ns, g_zero_pan, g_zero_tilt, g_pan_trim, g_tilt_trim);
}

}  // namespace

int ScsServo::CenterFor(uint8_t id) {
    return (id == SCS_ID_TILT) ? g_zero_tilt + g_tilt_trim
                               : g_zero_pan + g_pan_trim;
}

bool ScsServo::calibration_is_fallback() { return g_fallback; }

int ScsServo::PanTrim() { return g_pan_trim; }
int ScsServo::TiltTrim() { return g_tilt_trim; }

void ScsServo::SetTrim(int pan_counts, int tilt_counts) {
    g_pan_trim = std::clamp(pan_counts, -kMaxTrimCounts, kMaxTrimCounts);
    g_tilt_trim = std::clamp(tilt_counts, -kMaxTrimCounts, kMaxTrimCounts);
}

void ScsServo::SaveTrim() {
    nvs_handle_t h;
    if (nvs_open(kTrimNs, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "could not open NVS to save the trim");
        return;
    }
    nvs_set_i32(h, "pan_trim", g_pan_trim);
    nvs_set_i32(h, "tilt_trim", g_tilt_trim);
    // Committed here rather than left to the handle closing: a trim that
    // survived until the next reboot and no further would look like it saved.
    const esp_err_t err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "bench trim saved: pan %+d tilt %+d (%s)", g_pan_trim, g_tilt_trim,
             esp_err_to_name(err));
}

ScsServo::ScsServo() {}

ScsServo::~ScsServo() {
    if (initialized_) {
        uart_driver_delete(SCS_UART_NUM);
    }
}

bool ScsServo::Initialize() {
    if (initialized_) return true;

    // Before the bus, because everything below - centring, the travel clamps -
    // is expressed relative to this unit's own zero.
    // Trim first, because LoadFactoryCentres reports the pair together and a
    // line that printed the build default next to the stored centre would be
    // wrong in exactly the place somebody checks it.
    LoadTrim();
    LoadFactoryCentres();

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
    // Around THIS unit's factory zero, not around the trimmed centre: the trim
    // is a cosmetic correction to where "straight ahead" points, and letting it
    // shift the mechanical safety limits as well would be the wrong kind of
    // tidy. The span is what the mechanism can take; the trim is where we aim
    // inside it.
    const int zero = (id == SCS_ID_TILT) ? g_zero_tilt : g_zero_pan;
    const int span = (id == SCS_ID_TILT) ? SCS_TILT_SAFE_SPAN : SCS_PAN_SAFE_SPAN;
    const int lo = std::max(zero - span, SCS_POS_MIN);
    const int hi = std::min(zero + span, SCS_POS_MAX);
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
    WritePosition(SCS_ID_PAN,  CenterFor(SCS_ID_PAN),  500);
    WritePosition(SCS_ID_TILT, CenterFor(SCS_ID_TILT), 500);
}
