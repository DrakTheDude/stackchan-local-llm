#ifndef CAMERA_H
#define CAMERA_H

#include <string>

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
};

#endif  // CAMERA_H
