#include "cores3_audio_codec.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "CoreS3AudioCodec"

CoreS3AudioCodec::CoreS3AudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    uint8_t aw88298_addr, uint8_t es7210_addr, bool input_reference) {
    duplex_ = true; // full duplex
    input_reference_ = input_reference; // use a reference input, for echo cancellation
    input_channels_ = input_reference_ ? 2 : 1; // number of input channels
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    input_gain_ = 30;

    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Audio Output(Speaker)
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = (i2c_port_t)1,
        .addr = aw88298_addr,
        .bus_handle = i2c_master_handle,
    };
    out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(out_ctrl_if_ != NULL);

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    aw88298_codec_cfg_t aw88298_cfg = {};
    aw88298_cfg.ctrl_if = out_ctrl_if_;
    aw88298_cfg.gpio_if = gpio_if_;
    aw88298_cfg.reset_pin = GPIO_NUM_NC;
    aw88298_cfg.hw_gain.pa_voltage = 5.0;
    aw88298_cfg.hw_gain.codec_dac_voltage = 3.3;
    aw88298_cfg.hw_gain.pa_gain = 1;
    out_codec_if_ = aw88298_codec_new(&aw88298_cfg);
    assert(out_codec_if_ != NULL);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = out_codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(output_dev_ != NULL);

    // Audio Input(Microphone)
    i2c_cfg.addr = es7210_addr;
    in_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(in_ctrl_if_ != NULL);

    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = in_ctrl_if_;
    es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3;
    in_codec_if_ = es7210_codec_new(&es7210_cfg);
    assert(in_codec_if_ != NULL);

    dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    dev_cfg.codec_if = in_codec_if_;
    input_dev_ = esp_codec_dev_new(&dev_cfg);
    assert(input_dev_ != NULL);

    ESP_LOGI(TAG, "CoreS3AudioCodec initialized");
}

CoreS3AudioCodec::~CoreS3AudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);

    audio_codec_delete_codec_if(in_codec_if_);
    audio_codec_delete_ctrl_if(in_ctrl_if_);
    audio_codec_delete_codec_if(out_codec_if_);
    audio_codec_delete_ctrl_if(out_ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

void CoreS3AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din) {
    assert(input_sample_rate_ == output_sample_rate_);

    ESP_LOGI(TAG, "Audio IOs: mclk: %d, bclk: %d, ws: %d, dout: %d, din: %d", mclk, bclk, ws, dout, din);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    i2s_tdm_config_t tdm_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)input_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256,
            .bclk_div = 8,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = i2s_tdm_slot_mask_t(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 | I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
            .ws_width = I2S_TDM_AUTO_WS_WIDTH,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = false,
            .big_endian = false,
            .bit_order_lsb = false,
            .skip_mask = false,
            .total_slot = I2S_TDM_AUTO_SLOT_NUM
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = I2S_GPIO_UNUSED,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &tdm_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    ESP_LOGI(TAG, "Duplex channels created");
}

void CoreS3AudioCodec::SetOutputVolume(int volume) {
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, volume));
    AudioCodec::SetOutputVolume(volume);
}

// 🔴 NOT ESP_ERROR_CHECK. These configure the ES7210 over the SHARED I2C bus,
//    and that bus NACKs under load. Aborting here rebooted the whole robot
//    mid-boot, from PlaySound on the activation-done event, once the boot got
//    busy enough (a TLS client starting at the same time did it).
//
// 🔴 AND AN OPEN THAT RETURNS OK IS NOT PROOF THE MICROPHONE IS LISTENING -
//    exactly as for the amplifier, one function down. `Adev_Codec: Input already
//    open` in the log is the tell: the layer reports success without having
//    written a single ES7210 register, so the chip keeps whatever configuration
//    it had. The robot then shows "listening" on screen, the wake word never
//    fires, and nothing anywhere says why. Observed on real hardware after the
//    device had been idle.
bool CoreS3AudioCodec::TryOpenMic() {
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 2,
        .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
        .sample_rate = (uint32_t)output_sample_rate_,
        .mclk_multiple = 0,
    };
    if (input_reference_) {
        fs.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
    }
    esp_err_t err = esp_codec_dev_open(input_dev_, &fs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mic open failed (%s)", esp_err_to_name(err));
        return false;
    }
    if (!MicResponds()) {
        ESP_LOGW(TAG, "mic opened but the ES7210 does not answer - closing to retry");
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(input_dev_));
        return false;
    }
    err = esp_codec_dev_set_in_channel_gain(input_dev_, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
                                            input_gain_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mic gain not set (%s) - input still usable", esp_err_to_name(err));
    }
    return true;
}

bool CoreS3AudioCodec::BringUpMic() {
    if (!TryOpenMic()) {
        return false;
    }
    AudioCodec::EnableInput(true);
    return true;
}

void CoreS3AudioCodec::SetMicMuted(bool muted) {
    if (mic_muted_ == muted) {
        return;
    }
    mic_muted_ = muted;
    if (muted) {
        if (input_enabled_) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(input_dev_));
            AudioCodec::EnableInput(false);
        }
        // WARN, not INFO: a muted robot looks identical to a broken one, and the
        // log is where somebody goes to tell them apart.
        ESP_LOGW(TAG, "microphone MUTED - input closed, wake word will not fire");
    } else {
        ESP_LOGI(TAG, "microphone unmuted");
        if (input_wanted_) {
            EnableInput(true);
        }
    }
}

void CoreS3AudioCodec::EnableInput(bool enable) {
    input_wanted_ = enable;
    // Remembered, not obeyed. The application goes on asking for input as it
    // changes state, and every one of those has to bounce off the mute rather
    // than quietly reopening the microphone behind it.
    if (mic_muted_ && enable) {
        return;
    }
    if (enable == input_enabled_) {
        return;
    }
    if (!enable) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(input_dev_));
        AudioCodec::EnableInput(false);
        return;
    }
    // Same bounded retry as the speaker, for the same bus stall.
    constexpr int kAttempts = 4;
    for (int attempt = 0; attempt < kAttempts; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        if (BringUpMic()) {
            if (attempt > 0) {
                ESP_LOGI(TAG, "mic came up on attempt %d", attempt + 1);
            }
            return;
        }
    }
    ESP_LOGE(TAG, "ES7210 unreachable after %d attempts - mic left closed, "
                  "retrying while the robot listens", kAttempts);
}

// 🔴 AN OPEN THAT RETURNS OK IS NOT PROOF THE AMPLIFIER IS LISTENING, and
//    believing it cost a silent robot twice.
//
//    This board shares one I2C bus between the PMIC, the PY32, the touch
//    controller, the camera's SCCB and both audio chips, and about ten seconds
//    into EVERY boot - as Wi-Fi associates and the wake-word engine starts - it
//    NAKs for a few hundred milliseconds. Measured on two consecutive boots:
//        E (10364) I2C_If: Fail to read from dev 6c      <- the amp
//        E (10384) I2C_If: Fail to read from dev 80      <- the mic codec
//        E (10424) speaker open failed (ESP_ERR_NOT_SUPPORTED)
//    It is not occasional. It is every boot, and the only question is what else
//    is happening at the time.
//
//    esp_codec_dev_open can report success in that state. Its register writes
//    went nowhere, so the amp keeps whatever configuration it had - which after a
//    soft reset can be "muted" - and every later write is accepted, decoded and
//    played into a chip that is not listening. From the room: he hears you, he
//    answers on screen, and he is silent.
//
//    So this asks the amp whether it is actually there, and reports failure
//    honestly rather than leaving the caller believing it has a speaker.
bool CoreS3AudioCodec::TryOpenSpeaker(bool allow_reset) {
    // Play 16bit 1 channel
    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .channel_mask = 0,
        .sample_rate = (uint32_t)output_sample_rate_,
        .mclk_multiple = 0,
    };
    esp_err_t err = esp_codec_dev_open(output_dev_, &fs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "speaker open failed (%s)", esp_err_to_name(err));
        return false;
    }
    if (!AmpResponds()) {
        // The open lied: its register writes went nowhere. Close, so the next
        // attempt re-runs aw88298_open - that is what actually configures the
        // chip, and it has to happen while the bus is free or it achieves
        // nothing.
        //
        // ⚠️ THE RESET IS A LAST RESORT, NOT THE FIRST MOVE. Pulsing reset while
        //    I2S is clocking leaves internal state the configuration registers do
        //    not show: it was audible as a boot chime that came out louder on the
        //    boots where this fired, while the registers read byte-identical. A
        //    plain reopen fixes the common case - the bus was simply busy - so
        //    the chip only gets disturbed once retrying alone has failed.
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(output_dev_));
        if (allow_reset && amp_reset_) {
            ESP_LOGW(TAG, "amp still not answering after retries - resetting it");
            amp_reset_();
        }
        return false;
    }
    // See the note in EnableInput. This is the one that actually crashed:
    // esp_codec_dev_set_out_vol writes the AW88298's volume over the shared I2C
    // bus, and a single NACK there was taking the device down. Deliberately
    // AFTER the liveness check - there is no point writing a volume to a chip
    // that has already been shown not to answer.
    err = esp_codec_dev_set_out_vol(output_dev_, output_volume_);
    if (err != ESP_OK) {
        // Wrong volume is a nuisance; a lost reply is the bug being fixed.
        ESP_LOGW(TAG, "volume not set (%s) - audio still plays", esp_err_to_name(err));
    }
    return true;
}

// ⚠️ THE ONCE-PER-BOOT RESET IS NOT HERE ANY MORE, and moving it was the fix for
//    a second symptom. It used to fire lazily on the first EnableOutput - which
//    is AFTER I2S is already clocking - and resetting an amplifier mid-stream
//    leaves internal state that its configuration registers do not show. Six
//    consecutive boots read back byte-identical REG61/REG0C while the boot chime
//    was audibly louder on every other one.
//
//    The board now resets it during init, before this object exists and before
//    anything is clocking, so the chip is configured once from a known state.
//    See InitializeAw9523 in the board file. What remains here is RECOVERY: the
//    reset in TryOpenSpeaker, for an amp that has gone unreachable at runtime.
bool CoreS3AudioCodec::BringUpSpeaker(bool allow_reset) {
    if (!TryOpenSpeaker(allow_reset)) {
        return false;
    }
    // AFTER the open, never before: esp_codec_dev_open re-runs aw88298_open,
    // which writes REG61 back to boost-disabled. Setting it earlier would be
    // silently undone.
    SetSpeakerBoost(speaker_boost_);
    AudioCodec::EnableOutput(true);
    return true;
}

void CoreS3AudioCodec::EnableOutput(bool enable) {
    // Recorded even when the call is a no-op: Write() needs to know what was
    // asked for, not what succeeded.
    output_wanted_ = enable;
    if (enable == output_enabled_) {
        return;
    }
    if (!enable) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(output_dev_));
        AudioCodec::EnableOutput(false);
        return;
    }
    // ⚠️ RETRY FOR LONGER THAN THE GLITCH LASTS. One attempt was not enough: the
    //    bus stall runs a few hundred milliseconds, and a single failed open at
    //    the moment he starts speaking threw away the whole reply. Four attempts
    //    across ~450ms costs a barely perceptible late start in the bad case and
    //    nothing at all in the good one.
    constexpr int kAttempts = 4;
    for (int attempt = 0; attempt < kAttempts; attempt++) {
        if (attempt > 0) {
            vTaskDelay(pdMS_TO_TICKS(150));
        }
        // Plain retries first; the reset line only from the third attempt, once
        // "the bus was simply busy" has been ruled out.
        if (BringUpSpeaker(attempt >= 2)) {
            if (attempt > 0) {
                ESP_LOGI(TAG, "speaker came up on attempt %d", attempt + 1);
            }
            return;
        }
    }
    // Not fatal, and deliberately not pretended away: Write() keeps trying while
    // there is audio to play, so this costs the start of one reply rather than
    // every reply until a power cycle.
    ESP_LOGE(TAG, "amp unreachable after %d attempts - speaker left closed, "
                  "retrying while audio plays (%s)", kAttempts, DescribeAmp().c_str());
}

void CoreS3AudioCodec::SetSpeakerBoost(bool enable) {
    speaker_boost_ = enable;
    if (out_ctrl_if_ == nullptr) {
        return;
    }
    // REG61 BSTCTRL2. 0x6673 is the chip's own reset default (boost active);
    // 0x0673 is what esp_codec_dev writes to disable it. Values are 16-bit,
    // big-endian on the wire - the same shape as aw88298_write_reg.
    constexpr uint8_t kReg = 0x61;
    const uint16_t value = enable ? 0x6673 : 0x0673;
    uint8_t data[2] = {static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
    // Not ESP_ERROR_CHECK. This shares the I2C bus with the PMIC, the touch
    // controller and the PY32, and a NAK here must never be more than a warning
    // - that lesson cost two debugging sessions on this board.
    const int ret = out_ctrl_if_->write_reg(out_ctrl_if_, kReg, 1, data, 2);
    if (ret != 0) {
        ESP_LOGW(TAG, "speaker boost %s failed (%d) - audio still plays",
                 enable ? "on" : "off", ret);
        return;
    }
    ESP_LOGI(TAG, "speaker boost %s (REG61=0x%04X) -> %s", enable ? "ON" : "off", value,
             DescribeAmp().c_str());
}

// One register read, retried a couple of times. REG0C always reads back
// something on a live AW88298; an unreachable chip returns an error every time.
//
// Deliberately NOT a value check: what "correct" looks like depends on volume
// and boost, and a wrong-looking value still proves the chip is talking - which
// is the question being asked here.
bool CoreS3AudioCodec::AmpResponds() {
    if (out_ctrl_if_ == nullptr) {
        return false;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t v[2] = {0, 0};
        if (out_ctrl_if_->read_reg(out_ctrl_if_, 0x0C, 1, v, 2) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

// The ES7210's chip-ID register. Any successful read proves the chip is on the
// bus and answering; the ID value itself is not checked, because an unexpected
// value would still mean it is talking, which is the question.
bool CoreS3AudioCodec::MicResponds() {
    if (in_ctrl_if_ == nullptr) {
        return false;
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        uint8_t v = 0;
        if (in_ctrl_if_->read_reg(in_ctrl_if_, 0xFD, 1, &v, 1) == 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

std::string CoreS3AudioCodec::DescribeAmp() {
    if (out_ctrl_if_ == nullptr) return "amp unreachable";
    auto rd = [this](uint8_t reg) -> int {
        uint8_t v[2] = {0, 0};
        if (out_ctrl_if_->read_reg(out_ctrl_if_, reg, 1, v, 2) != 0) return -1;
        return (v[0] << 8) | v[1];
    };
    // REG0C's HIGH byte is the volume, 0x00 = 0dB and 0xC0 = -96dB. Reading
    // 0x00 there means the codec is already wide open and no amount of asking
    // for more volume can do anything - which is exactly what it read.
    char buf[80];
    snprintf(buf, sizeof(buf), "REG61=0x%04X REG0C=0x%04X vol=%d", rd(0x61), rd(0x0C),
             output_volume_);
    return buf;
}

int CoreS3AudioCodec::Read(int16_t* dest, int samples) {
    // 🔴 THIS USED TO RETURN `samples` WITHOUT TOUCHING THE BUFFER when input was
    //    closed - it claimed it had filled it. The wake-word engine then fed on
    //    whatever was already in that memory, for ever. From the room: the screen
    //    says "listening", you talk to him, and nothing is ever picked up. No
    //    error, no log line, no way to tell it apart from a broken microphone.
    //
    //    So: keep trying while something is listening, and when there is nothing
    //    to give, hand over SILENCE rather than stale memory.
    // Checked before the recovery path below, or the retry would fight the mute
    // and reopen the microphone every 250ms.
    if (mic_muted_) {
        memset(dest, 0, samples * sizeof(int16_t));
        // 🔴 AND IT HAS TO TAKE AS LONG AS A REAL READ. Returning instantly is
        //    what a muted microphone looks like to this function, and it starves
        //    the device: esp_codec_dev_read normally blocks until the samples
        //    exist, which is what paces the audio input task. Hand back silence
        //    with no delay and that task spins as fast as the CPU allows, which
        //    took the 20ms touch poll with it - the screen still drew, the menu
        //    still appeared, and nothing could be pressed or scrolled. The
        //    watchdog reboot came from the same place.
        //
        //    So sleep for the time the samples represent. Silence still costs
        //    what sound costs.
        const int ms = input_sample_rate_ > 0
                           ? (samples * 1000) / input_sample_rate_
                           : 20;
        vTaskDelay(pdMS_TO_TICKS(ms > 0 ? ms : 1));
        return samples;
    }
    if (!input_enabled_ && input_wanted_) {
        const int64_t now = esp_timer_get_time();
        if (now - last_in_retry_us_ >= 250000) {
            last_in_retry_us_ = now;
            if (BringUpMic()) {
                ESP_LOGW(TAG, "mic recovered - he can hear you again");
            }
        }
    }
    if (input_enabled_) {
        if (esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t)) != ESP_OK) {
            memset(dest, 0, samples * sizeof(int16_t));
        }
    } else {
        memset(dest, 0, samples * sizeof(int16_t));
    }
    return samples;
}

int CoreS3AudioCodec::Write(const int16_t* data, int samples) {
    // 🔴 SILENTLY DROPPING SAMPLES IS HOW A CLOSED SPEAKER BECAME A ROBOT THAT
    //    NEVER SPOKE AGAIN. `if (output_enabled_)` alone means a failed open at
    //    the moment he starts talking discards every sample of that reply, and
    //    nothing tries again until the next state change - so the user's whole
    //    question goes unanswered with no sound and no explanation.
    //
    //    If output was ASKED for and is not up, keep trying. One attempt per
    //    250ms: the audio task must not be blocked, and the retries only happen
    //    while there is something to play, so an idle robot is not poking a dead
    //    chip forever.
    if (!output_enabled_ && output_wanted_) {
        const int64_t now = esp_timer_get_time();
        if (now - last_open_retry_us_ >= 250000) {
            last_open_retry_us_ = now;
            if (BringUpSpeaker()) {
                ESP_LOGW(TAG, "speaker recovered mid-playback - the start of this "
                              "reply was lost, the rest will be audible");
            }
        }
    }
    if (output_enabled_) {
        // 🔴 NO DIGITAL GAIN HERE, AND IT IS NOT AN OVERSIGHT. A multiplier was
        //    tried and removed the same hour, because on THIS board it does not
        //    fail as distortion - it fails as the robot interrupting itself.
        //
        //    config.h: AUDIO_INPUT_REFERENCE is false. There is no echo
        //    reference wired from the amplifier back into the ES7210, so the
        //    AFE has nothing to subtract his own voice with. Every dB louder is
        //    a dB more of himself arriving at his own mics. At 250% he heard
        //    himself, concluded he had been interrupted, and sent an abort
        //    mid-sentence - which from the room sounds like the voice breaking
        //    up and then cutting out entirely.
        //
        //    Loudness on this hardware is therefore NOT free, and the ceiling is
        //    acoustic rather than electrical. Anything that raises it has to
        //    deal with self-hearing first.
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)));
    }
    return samples;
}
