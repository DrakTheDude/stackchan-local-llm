#pragma once
#include "sdkconfig.h"

#include <lvgl.h>
#include <memory>
#include <thread>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "camera.h"
#include "esp_video_init.h"
#include "jpg/image_to_jpeg.h"

struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class EspVideo : public Camera {
private:
    struct FrameBuffer {
        uint8_t* data = nullptr;
        size_t len = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        v4l2_pix_fmt_t format = 0;
    } frame_;
    v4l2_pix_fmt_t sensor_format_ = 0;
#ifdef CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    uint16_t sensor_width_ = 0;
    uint16_t sensor_height_ = 0;
#endif  // CONFIG_XIAOZHI_ENABLE_ROTATE_CAMERA_IMAGE
    int video_fd_ = -1;
    bool streaming_on_ = false;
    struct MmapBuffer {
        void* start = nullptr;
        size_t length = 0;
    };
    std::vector<MmapBuffer> mmap_buffers_;
    std::string explain_url_;
    uint8_t* explain_image_ = nullptr;      // owned; see SetExplainImage
    size_t explain_image_len_ = 0;
    uint16_t explain_image_w_ = 0;
    uint16_t explain_image_h_ = 0;
    v4l2_pix_fmt_t explain_image_format_ = 0;
    std::string explain_token_;
    std::thread encoder_thread_;
    bool auto_preview_ = true;

public:
    EspVideo(const esp_video_init_config_t& config);
    ~EspVideo() override;

    virtual void SetExplainUrl(const std::string& url, const std::string& token);
    virtual bool Capture();
    // flip controls
    virtual bool SetHMirror(bool enabled) override;
    virtual bool SetVFlip(bool enabled) override;
    virtual std::string Explain(const std::string& question);
    virtual bool CanExplain() const { return !explain_url_.empty(); }

    // 🔴 SHOW THE MODEL WHAT THE PERSON IS LOOKING AT.
    //
    //    Explain() encodes the raw sensor frame. On this camera that frame is
    //    dark - measured mean luma 31 in a lit room, because the sensor is at
    //    its exposure ceiling at 20fps - and the picture on screen only looks
    //    right because the board black-points and tone-curves it first.
    //
    //    Sending the raw one meant the owner saw a lit room and the model said
    //    "a dark room". Neither was wrong; they were given different pictures.
    //
    //    Set this to the frame the screen is showing, and Explain() sends that
    //    instead. The buffer is COPIED, because the display takes ownership of
    //    the original. Cleared automatically after the next Explain().
    void SetExplainImage(const uint8_t* data, size_t len, uint16_t w, uint16_t h,
                         v4l2_pix_fmt_t format);

    // Read-only access to the last captured frame.
    //
    // Added for the StackChan board so a photo can be shown on the device's own
    // screen without Explain(), which posts the image to a vision endpoint. On
    // this project that endpoint is a CLOUD vision API - the shipped default
    // still carries the placeholder key - and sending frames from a camera
    // pointed at the room off the LAN is exactly what this build exists to
    // prevent. Capture() plus these accessors keep it entirely local.
    const uint8_t* frame_data() const { return frame_.data; }
    size_t frame_len() const { return frame_.len; }
    uint16_t frame_width() const { return frame_.width; }
    uint16_t frame_height() const { return frame_.height; }
    v4l2_pix_fmt_t frame_format() const { return frame_.format; }

    // Capture() normally pushes its own preview to the display: it converts the
    // frame with esp_imgfx and hands the result straight to SetPreviewImage().
    // Turn that off and the caller owns the preview instead.
    //
    // StackChan turns it off because the built-in conversion is a call into a
    // binary blob whose YUV range convention is not inspectable, and the levels
    // it produced were visibly wrong - see the colour note in the board file.
    // Owning the conversion is what makes those levels fixable at all.
    void SetAutoPreview(bool on) { auto_preview_ = on; }
};
