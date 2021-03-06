#pragma once

#include <asm/types.h>
#include <linux/videodev2.h>

#include <string>
#include "frameSource.hh"


#define NUM_STREAMING_BUFFERS_REQUESTED 16


extern "C"
{
#include <libswscale/swscale.h>
#include <libavcodec/avcodec.h>
}


struct v4l2_settings
{
    int control;
    int value;
};

// This is a videoforlinux2 frame source. It is highly immature and may not work. It has only been
// tested on the handful of cameras I have
class CameraSource_V4L2 : public FrameSource
{
    int                        camera_fd;
    v4l2_pix_format            pixfmt;
    bool                       streaming;

    // used if streaming
    int num_streaming_buffers; // could be fewer than NUM_STREAMING_BUFFERS_REQUESTED
    void* mmapped[NUM_STREAMING_BUFFERS_REQUESTED];
    int buf_length[NUM_STREAMING_BUFFERS_REQUESTED];

    unsigned char* buffer;
    int            buffer_bytes_allocated;

    SwsContext*     scaleContext;

    AVCodecContext* codecContext;
    AVFrame*        ffmpegFrame;
    AVPacket        ffmpegPacket;


public:
    CameraSource_V4L2(FrameSource_UserColorChoice _userColorMode,
                      const char* device = "/dev/video0",
                      int requested_width = -1, int requested_height = -1, // <=0 = "as large as possible"
                      int requested_fps = -1, // <=0 = "default"
                      const struct v4l2_settings* settings = NULL, // last element of settings[] must be {<0, ...}
                      CvRect _cropRect = cvRect(-1, -1, -1, -1),
                      double scale = 1.0);

    ~CameraSource_V4L2();

    operator bool() { return camera_fd > 0; }

private:
    void uninit(void);

    bool setupSwsContext(enum AVPixelFormat swscalePixfmt);
    bool findDecoder(void);

    // These functions implement the FrameSource virtuals, and are the main differentiators between
    // the various frame sources, along with the constructor and destructor
private:
    bool _getNextFrame  (IplImage* image, uint64_t* timestamp_us = NULL);
    bool _getLatestFrame(IplImage* image, uint64_t* timestamp_us = NULL);

    bool _stopStream   (void);
    bool _resumeStream (void);

    // There's no concept of rewinding in a real camera, so restart == resume
    bool _restartStream(void)
    {
        return _resumeStream();
    }


    int getFD(void)
    {
        return camera_fd;
    }
};
