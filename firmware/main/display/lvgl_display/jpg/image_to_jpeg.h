// image_to_jpeg.h - efficient image-to-JPEG encoding interface
// a JPEG encoder implementation that saves about 8 KB of SRAM
#pragma once
#include "sdkconfig.h"
#ifndef CONFIG_IDF_TARGET_ESP32

#include <stdint.h>
#include <stddef.h>

// Keep this lightweight header independent from esp_video's Linux compatibility
// headers. esp_video 2.0.x and lwIP both define ioctl macros under ESP-IDF 6.
#ifndef V4L2_PIX_FMT_RGB565
#define V4L2_PIX_FMT_RGB565 0x50424752  // 'RGBP'
#define V4L2_PIX_FMT_RGB565X 0x52474250 // 'PRGB'
#define V4L2_PIX_FMT_RGB24 0x33424752   // 'RGB3'
#define V4L2_PIX_FMT_YUYV 0x56595559    // 'YUYV'
#define V4L2_PIX_FMT_YUV422P 0x36315559 // 'YU16'
#define V4L2_PIX_FMT_YUV420 0x32315559  // 'YU12'
#define V4L2_PIX_FMT_GREY 0x59455247    // 'GREY'
#define V4L2_PIX_FMT_UYVY 0x59565955    // 'UYVY'
#define V4L2_PIX_FMT_JPEG 0x4745504A    // 'JPEG'
#endif

typedef uint32_t v4l2_pix_fmt_t;

#ifdef __cplusplus
extern "C"
{
#endif

    // JPEG output callback type
    // arg: user argument, index: current data index, data: JPEG data chunk, len: chunk length
    // returns: number of bytes actually handled
    typedef size_t (*jpg_out_cb)(void *arg, size_t index, const void *data, size_t len);

    /**
     * @brief Efficiently convert an image to JPEG
     *
     * Encodes with an optimised JPEG encoder. Main features:
     * - saves about 8 KB of SRAM (static variables moved to heap allocation)
     * - accepts several input image formats
     * - high-quality JPEG output
     *
     * @param src       source image data
     * @param src_len   source image data length
     * @param width     image width
     * @param height    image height
     * @param format    image format (PIXFORMAT_RGB565, PIXFORMAT_RGB888, etc.)
     * @param quality   JPEG quality (1-100)
     * @param out       output JPEG data pointer (the caller must free it)
     * @param out_len   output JPEG data length
     *
     * @return true on success, false on failure
     */
    bool image_to_jpeg(uint8_t *src, size_t src_len, uint16_t width, uint16_t height,
                       v4l2_pix_fmt_t format, uint8_t quality, uint8_t **out, size_t *out_len);

    /**
     * @brief Convert an image to JPEG (callback version)
     *
     * Hands the JPEG output to a callback, suited to streaming or chunked processing:
     * - saves about 8 KB of SRAM (static variables moved to heap allocation)
     * - supports streaming output, with no large buffer to preallocate
     * - the callback handles the JPEG data chunk by chunk
     *
     * @param src       source image data
     * @param src_len   source image data length
     * @param width     image width
     * @param height    image height
     * @param format    image format
     * @param quality   JPEG quality (1-100)
     * @param cb        output callback
     * @param arg       user argument passed to the callback
     *
     * @return true on success, false on failure
     */
    bool image_to_jpeg_cb(uint8_t *src, size_t src_len, uint16_t width, uint16_t height,
                          v4l2_pix_fmt_t format, uint8_t quality, jpg_out_cb cb, void *arg);

#ifdef __cplusplus
}
#endif

#endif // ndef CONFIG_IDF_TARGET_ESP32
