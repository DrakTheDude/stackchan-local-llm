#ifndef _BOX_AUDIO_CODEC_H
#define _BOX_AUDIO_CODEC_H

#include "audio_codec.h"

#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>

#include <functional>
#include <string>

class CoreS3AudioCodec : public AudioCodec {
private:
    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* out_ctrl_if_ = nullptr;
    const audio_codec_if_t* out_codec_if_ = nullptr;
    const audio_codec_ctrl_if_t* in_ctrl_if_ = nullptr;
    const audio_codec_if_t* in_codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;

    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    CoreS3AudioCodec(void* i2c_master_handle, int input_sample_rate, int output_sample_rate,
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
        uint8_t aw88298_addr, uint8_t es7210_addr, bool input_reference);
    virtual ~CoreS3AudioCodec();

    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;

    // 🔊 The AW88298 is a BOOST-CONVERTER class-D amp: it steps its own supply
    //    up to drive the speaker hard. esp_codec_dev turns that off at init
    //    (aw88298.c, REG61 <- 0x0673, where the chip's own default is 0x6673),
    //    and its digital volume range is -96dB..0dB - pure attenuation, with no
    //    gain available anywhere. So "volume 100" means "not turned down", and
    //    on a 1W speaker at desk distance that is a robot you lean in to hear.
    //
    //    This puts the chip back on its default. It has to be re-applied after
    //    every EnableOutput(true), because esp_codec_dev_open re-runs
    //    aw88298_open, which writes 0x0673 again.
    //
    // ⚠️ Boost drives real power into a small speaker. If it distorts on loud
    //    passages this is the first thing to turn back off - hence the runtime
    //    toggle rather than a constant.
    void SetSpeakerBoost(bool enable);
    bool speaker_boost() const { return speaker_boost_; }

    // Reads REG61 (boost) and REG0C (volume) back off the chip. Returns e.g.
    // "REG61=0x6673 REG0C=0x0064". The point is to tell "the write did not
    // land" apart from "the write landed and did nothing", which sound
    // identical from across the room.
    std::string DescribeAmp();

    // Does the amplifier answer on I2C at all? Used to tell a genuinely open
    // output apart from one that opened while the amp was unreachable - see
    // EnableOutput.
    bool AmpResponds();

    // How the board resets the amplifier. The AW88298's reset line is on a
    // different chip (the AW9523 IO expander), which the codec does not own, so
    // the board hands in a closure. Optional: without it, recovery is limited to
    // closing and reopening.
    void SetAmpResetHook(std::function<void()> cb) { amp_reset_ = std::move(cb); }

    // 🔴 There is deliberately NO digital output gain. It was tried, it made
    //    things worse rather than louder, and the reason is in Write().

private:
    std::function<void()> amp_reset_;
    // 🔴 OFF BY DEFAULT, and it was briefly true. Turning the boost on was
    //    MEASURED to change nothing audible - REG61 read back as 0x6673, so the
    //    write landed and simply does not move this speaker - and the popping
    //    and dropouts started around the same time. A boost converter draws
    //    current in bursts; a supply dip on this board sounds exactly like that.
    //
    //    A change that buys nothing and plausibly costs something does not get
    //    to stay on. The toggle remains so it can be re-tested deliberately.
    bool speaker_boost_ = false;
};

#endif // _BOX_AUDIO_CODEC_H
