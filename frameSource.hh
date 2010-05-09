#ifndef __FRAME_SOURCE_H__
#define __FRAME_SOURCE_H__

#include <stdint.h>
#include <time.h>
#include <opencv/cv.h>
#include "threadUtils.hh"

// user interface color choice. RGB8 or MONO8
enum FrameSource_UserColorChoice  { FRAMESOURCE_COLOR, FRAMESOURCE_GRAYSCALE };

typedef void (FrameSourceCallback_t)(IplImage* buffer, uint64_t timestamp_us);
static void* sourceThread_global  (void *pArg);

// This is the base class for different frame grabbers. The constructor allows color or monochrome
// mode to be selected. For simplicity, color always means 8-bits-per-channel RGB and monochrome
// always means 8-bit grayscale
class FrameSource
{
protected:
    FrameSource_UserColorChoice userColorMode; // color (RGB8) or grayscale (MONO8)
    unsigned int                width, height;

    pthread_t              sourceThread_id;
    uint64_t               sourceThread_frameWait_us;

    FrameSourceCallback_t* sourceThread_callback;
    IplImage*              sourceThread_buffer;

    // I use a condition to control the "running" state of the frame source. Every get..Frame() call
    // checks this conditon and waits for it to trigger, if necessary
    MTcondition isRunningNow;

private:
    // These are the internal APIs called only by the external function definitions below
    virtual void _restartStream(void) = 0;
    virtual void _resumeStream (void) = 0;
    virtual bool _getNextFrame  (IplImage* image, uint64_t* timestamp_us = NULL) = 0;
    virtual bool _getLatestFrame(IplImage* image, uint64_t* timestamp_us = NULL) = 0;
    virtual void _stopStream(void) = 0;

public:
    FrameSource (FrameSource_UserColorChoice _userColorMode = FRAMESOURCE_COLOR)
        : userColorMode(_userColorMode), sourceThread_id(0)
    {
        // we're not yet initialized and thus not able to serve data
        isRunningNow.reset();
    }

    virtual void cleanupThreads(void)
    {
        if(sourceThread_id != 0)
        {
            pthread_cancel(sourceThread_id);
            pthread_join(sourceThread_id, NULL);
            sourceThread_id = 0;
        }
    }

    virtual ~FrameSource()
    {
        // This should be called from the most derived class since the thread calls a virtual
        // function. Just in case we call it here also. This will do nothing if the derived class
        // already did this, but may crash if it didn't
        cleanupThreads();
    }

    // I want the derived classes to override this. It indicates whether the class is initialized
    // and ready to use
    virtual operator bool() = 0;

    // getNextFrame() returns the next frame in the buffer.
    // getLatestFrame() purges the buffer and returns the most recent frame available
    // For non-realtime data sources, such as video files, getNextFrame() and getLatestFrame() are
    // the same.
    bool getNextFrame  (IplImage* image, uint64_t* timestamp_us = NULL)
    {
        isRunningNow.waitForTrue();
        return _getNextFrame(image, timestamp_us);
    }
    bool getLatestFrame(IplImage* image, uint64_t* timestamp_us = NULL)
    {
        isRunningNow.waitForTrue();
        return _getLatestFrame(image, timestamp_us);
    }

    // tell the source to stop sending data. Any queued, but not processed frames are discarded
    void stopStream(void)
    {
        isRunningNow.reset();
        _stopStream();
    }

    // re-activate the stream, rewinding to the beginning if asked (restart) and if possible. This
    // is context-dependent. Video sources can rewind, but cameras cannot, for instance
    void resumeStream (void)
    {
        _resumeStream();
        isRunningNow.setTrue();
    }

    void restartStream(void)
    {
        _restartStream();
        isRunningNow.setTrue();
    }

    // Instead of accessing the frame with blocking I/O, the frame source can spawn a thread to wait
    // for the frames, and callback when a new frame is available. The thread can optionally wait
    // some amount of time between successive frames to force-limit the framerate by giving a
    // non-zero frameWait_us. In this case only the latest available frame will be returned with the
    // rest thrown away. Otherwise, we try not to miss frames by returning the next available frame.
    //
    // The callback is passed the buffer where the data was stored. Note that this is the same
    // buffer that was originally passed to startSourceThread. Note that this buffer is accessed
    // asynchronously, so the caller can NOT assume it contains valid data outside of the callback

    void startSourceThread(FrameSourceCallback_t* callback, uint64_t frameWait_us,
                           IplImage* buffer)
    {
        if(sourceThread_id != 0)
            return;

        sourceThread_callback     = callback;
        sourceThread_frameWait_us = frameWait_us;
        sourceThread_buffer       = buffer;

        if(pthread_create(&sourceThread_id, NULL, &sourceThread_global, this) != 0)
        {
            sourceThread_id = 0;
            fprintf(stderr, "couldn't start source thread\n");
        }
    }

    void sourceThread(void)
    {
        while(1)
        {
            uint64_t timestamp_us;
            bool result;
            if(sourceThread_frameWait_us != 0)
            {
                // We are limiting the framerate. Sleep for a bit, then return the newest frame in
                // the buffer, throwing away the rest
                struct timespec delay;
                delay.tv_sec  = sourceThread_frameWait_us / 1000000;
                delay.tv_nsec = (sourceThread_frameWait_us - delay.tv_sec*1000000) * 1000;
                nanosleep(&delay, NULL);

                result = getLatestFrame(sourceThread_buffer, &timestamp_us);
            }
            else
            {
                // We are not limiting the framerate. Try to return ALL the available frames
                result = getNextFrame(sourceThread_buffer, &timestamp_us);
            }

            if(!result)
            {
                fprintf(stderr, "thread couldn't get frame\n");
                return;
            }

            (*sourceThread_callback)(sourceThread_buffer, timestamp_us);
        }
    }

    unsigned int w() { return width;  }
    unsigned int h() { return height; }
};

static void* sourceThread_global(void *pArg)
{
    FrameSource* source = (FrameSource*)pArg;
    source->sourceThread();
    return NULL;
}

#endif
