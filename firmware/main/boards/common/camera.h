#ifndef CAMERA_H
#define CAMERA_H

#include <string>
#include <cstddef>
#include <cstdint>

// One definition, shared. Both drivers had their own, which collides the moment
// a board includes both - which is what comparing them requires.
struct JpegChunk {
    uint8_t* data;
    size_t len;
};

class Camera {
public:
    virtual ~Camera() = default;

    virtual void SetExplainUrl(const std::string& url, const std::string& token) = 0;
    virtual bool Capture() = 0;
    virtual bool SetHMirror(bool enabled) = 0;
    virtual bool SetVFlip(bool enabled) = 0;
    virtual bool SetSwapBytes(bool enabled) { return false; }  // Optional, default no-op
    virtual std::string Explain(const std::string& question) = 0;

    // Whether Explain() has somewhere to go. False until the server offers a
    // vision URL in its capabilities, which it only does when it has a model -
    // so this is also the answer to "does this setup have an eye?".
    //
    // Explain() throws when it does not, and a robot with no vision model is an
    // ordinary configuration rather than an error, so callers ask first.
    virtual bool CanExplain() const { return false; }

    // ---- what a board needs from any camera ------------------------------
    //
    // These began as EspVideo methods and are on the interface now, so a board
    // can be written once and pointed at either driver. The defaults are the
    // honest answers for a camera that does not have the thing being asked for.

    virtual const uint8_t* frame_data() const { return nullptr; }
    virtual size_t frame_len() const { return 0; }
    virtual uint16_t frame_width() const { return 0; }
    virtual uint16_t frame_height() const { return 0; }
    virtual uint32_t frame_format() const { return 0; }

    // Some drivers draw the captured frame to the screen themselves. A board
    // that wants to present it its own way says false.
    virtual void SetAutoPreview(bool) {}

    // Streaming only while a photograph is being taken - the sensor otherwise
    // fills memory over DMA for nobody. A driver with no such control can say
    // it is always ready.
    virtual bool StartStreaming() { return true; }
    virtual void StopStreaming() {}

    // Hand the vision model a prepared frame rather than the raw sensor output.
    // Ignored by a driver whose output is already the prepared picture.
    virtual void SetExplainImage(const uint8_t*, size_t, uint16_t, uint16_t, uint32_t) {}
};

#endif  // CAMERA_H
