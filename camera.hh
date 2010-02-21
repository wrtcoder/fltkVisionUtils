#ifndef __CAMERA_H__
#define __CAMERA_H__

#include <string>

#include "dc1394/dc1394.h"
#include "frameSource.hh"

// The colormodes that I know about. These are listed in order from least to most desireable
enum colormode_t { COLORMODE_UNWANTED,
                   COLORMODE_MONO16, // FrameSource always uses 8bits per channel, so mono16 does
                                     // nothing for me. Thus it's least desireable
                   COLORMODE_MONO8,
                   COLORMODE_YUV411,
                   COLORMODE_YUV422,
                   COLORMODE_YUV444,
                   COLORMODE_RGB8,

                   // if we wanted grayscale output, then it is more desireable still
                   COLORMODE_MONO8_REQUESTED
};

// The resolutions that I know about. These are listed in order from least to most desireable
enum resolution_t { MODE_UNWANTED,
                    MODE_160x120,
                    MODE_320x240,
                    MODE_640x480,
                    MODE_800x600,
                    MODE_1024x768,
                    MODE_1280x960,
                    MODE_1600x1200};

class Camera : public FrameSource
{
    unsigned             cameraIndex;
    dc1394camera_t*      camera;
    dc1394video_frame_t* cameraFrame;

    std::string          cameraDescription;

    // These describe the whole camera bus, not just a single camera. Thus we keep only one copy by
    // declaring them static
    static dc1394_t*            dc1394Context;
    static dc1394camera_list_t* cameraList;
    static unsigned int         numInitedCameras;

    unsigned char* finishPeek(uint64_t* timestamp_us);
    bool finishGet(unsigned char* buffer);

    // returns desireability of the resolution. Higher is more desireable
    resolution_t getResolutionWorth(dc1394video_mode_t mode);

    // returns desireability of the color mode. Higher is more desireable
    colormode_t getColormodeWorth(dc1394video_mode_t mode);

public:
    Camera(bool _isColor);
    ~Camera();

    // peek...Frame() blocks until a frame is available. A pointer to the internal buffer is
    // returned (NULL on error). This buffer must be given back to the system by calling
    // unpeekFrame(). unpeekFrame() need not be called if peekFrame() failed.
    // peekNextFrame() returns the next frame in the buffer.
    // peekMostRecentFrame() purges the buffer and returns the most recent frame available
    //
    // The peek...Frame() functions return RAW data. No color conversion is attempted. Use with
    // caution
    unsigned char* peekNextFrame      (uint64_t* timestamp_us);
    unsigned char* peekMostRecentFrame(uint64_t* timestamp_us);
    void unpeekFrame(void);

    // these are like the peek() functions, but these convert the incoming data to the desired
    // colorspace (RGB8 or MONO8 depending on isColor). Since these make a copy of the data, calling
    // unpeek() is not needed. false returned on error
    bool getNextFrame      (uint64_t* timestamp_us, unsigned char* buffer);
    bool getMostRecentFrame(uint64_t* timestamp_us, unsigned char* buffer);

    int getCameraIndex(void)                { return cameraIndex;       }
    const std::string& getDescription(void) { return cameraDescription; }

    static bool uninitedCamerasLeft(void)   { return numInitedCameras < cameraList->num; }
};

#endif
