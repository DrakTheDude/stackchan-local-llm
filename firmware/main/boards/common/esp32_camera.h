#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <thread>
#include <memory>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "esp_camera.h"
#include "jpg/image_to_jpeg.h"

class Esp32Camera : public Camera
{
private:
    bool streaming_on_ = false;
    bool swap_bytes_enabled_ = true;  // Swap pixel byte order for RGB565, enabled by default
    std::string explain_url_;
    std::string explain_token_;
    std::thread encoder_thread_;
    camera_fb_t *current_fb_ = nullptr;
    uint8_t *encode_buf_ = nullptr;  // Buffer for JPEG encoding (with optional byte swap)
    size_t encode_buf_size_ = 0;

public:
    Esp32Camera(const camera_config_t &config);
    ~Esp32Camera();

    virtual void SetExplainUrl(const std::string &url, const std::string &token) override;
    virtual bool Capture() override;
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual bool SetSwapBytes(bool enabled) override;
    virtual std::string Explain(const std::string &question) override;

    // ---- the surface EspVideo offers, so a board can use either ----------
    //
    // This driver keeps its frame in a private camera_fb_t and draws its own
    // preview; EspVideo hands the frame to the board and lets it decide. A
    // board written against one could not compile against the other, which is
    // why they had never been compared on the same hardware.

    // 🔴 THE PREPARED BUFFER, NOT THE RAW ONE. Capture() byte-swaps RGB565
    //    into encode_buf_ and leaves current_fb_->buf as the sensor sent it.
    //    Handing back the raw buffer means a board does its own conversion on
    //    data that has not had the driver's - which produced a picture with
    //    every shape intact and every colour wrong.
    const uint8_t* frame_data() const {
        if (encode_buf_ != nullptr && encode_buf_size_ > 0) return encode_buf_;
        return current_fb_ ? current_fb_->buf : nullptr;
    }
    size_t frame_len() const { return current_fb_ ? current_fb_->len : 0; }
    uint16_t frame_width() const { return current_fb_ ? current_fb_->width : 0; }
    uint16_t frame_height() const { return current_fb_ ? current_fb_->height : 0; }

    // Translated to the V4L2 fourccs the board already switches on, so the
    // conversion code does not need a second vocabulary.
    //
    // ⚠️ Defined in the .cc, NOT here. <linux/videodev2.h> is pulled in by
    //    esp_video.h, and a board that includes both headers - which is
    //    precisely what comparing the two drivers does - gets every
    //    V4L2_PIX_FMT_* redefined, which is a hard error under -Werror.
    uint32_t frame_format() const;

    // 📷 Capture() draws the preview itself. The board wants that decision -
    //    it centres the head, meters, tone-maps and shows the result at full
    //    screen - so this switches the driver's own preview off.
    void SetAutoPreview(bool enabled) { auto_preview_ = enabled; }

    // The camera is stopped between photographs; see the note in EspVideo.
    // esp32-camera has no stream on/off, so these gate Capture() instead -
    // the sensor keeps running, but nothing is read or converted.
    bool StartStreaming() { streaming_on_ = true; return true; }
    void StopStreaming() { streaming_on_ = false; }
    bool streaming() const { return streaming_on_; }

    virtual bool CanExplain() const override { return !explain_url_.empty(); }

    // EspVideo can be handed a prepared frame to send to the vision model. Here
    // the frame IS the sensor's own processed picture, which is what we would
    // have prepared, so this is deliberately a no-op rather than a second copy.
    void SetExplainImage(const uint8_t*, size_t, uint16_t, uint16_t, uint32_t) {}

private:
    bool auto_preview_ = true;
};
