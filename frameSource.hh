#ifndef __FRAME_SOURCE_H__
#define __FRAME_SOURCE_H__

#include <stdint.h>
#include <time.h>
#include <opencv/cv.h>
#include <signal.h>

// user interface color choice. RGB8 or MONO8
enum FrameSource_UserColorChoice  { FRAMESOURCE_COLOR, FRAMESOURCE_GRAYSCALE };
typedef void (FrameSourceCallback_t)(unsigned char* buffer, uint64_t timestamp_us);

static void* sourceThread_global(void *pArg);

// This is the base class for different frame grabbers. The constructor allows color or monochrome
// mode to be selected. For simplicity, color always means 8-bits-per-channel RGB and monochrome
// always means 8-bit grayscale
class FrameSource
{
protected:
    FrameSource_UserColorChoice userColorMode; // color (RGB8) or grayscale (MONO8)
    unsigned int                width, height;

    pthread_t              sourceThread_id;
    FrameSourceCallback_t* sourceThread_callback;
    uint64_t               sourceThread_frameWait_us;
    unsigned char*         sourceThread_buffer;

    unsigned char* getRawBuffer(IplImage* image)
    {
        unsigned char* buffer;
        cvGetRawData(image, &buffer);
        return buffer;
    }
    unsigned char* getRawBuffer(CvMat* image)
    {
        unsigned char* buffer;
        cvGetRawData(image, &buffer);
        return buffer;
    }


public:
    FrameSource (FrameSource_UserColorChoice _userColorMode = FRAMESOURCE_COLOR)
        : userColorMode(_userColorMode), sourceThread_id(0), sourceThread_callback(NULL) { }

    virtual void cleanupThreads(void)
    {
        if(sourceThread_id != 0)
        {
            pthread_kill(sourceThread_id, SIGKILL);
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

    // I want the derived classes to override this
    virtual operator bool() = 0;

    // peek...Frame() blocks until a frame is available. A pointer to the internal buffer is
    // returned (NULL on error). This buffer must be given back to the system by calling
    // unpeekFrame(). unpeekFrame() need not be called if peekFrame() failed.
    // peekNextFrame() returns the next frame in the buffer.
    // peekLatestFrame() purges the buffer and returns the most recent frame available
    // For non-realtime data sources, such as video files, peekNextFrame() and peekLatestFrame() are
    // the same
    //
    // The peek...Frame() functions return RAW data. No color conversion is attempted. Use with
    // caution
    virtual unsigned char* peekNextFrame  (uint64_t* timestamp_us) = 0;
    virtual unsigned char* peekLatestFrame(uint64_t* timestamp_us) = 0;
    virtual void unpeekFrame(void) = 0;

    // these are like the peek() functions, but these convert the incoming data to the desired
    // colorspace (RGB8 or MONO8 depending on the userColorMode). Since these make a copy of the
    // data, calling unpeek() is not needed. false returned on error
    virtual bool getNextFrame  (uint64_t* timestamp_us, unsigned char* buffer) = 0;
    virtual bool getLatestFrame(uint64_t* timestamp_us, unsigned char* buffer) = 0;

    virtual bool getNextFrameCv  (uint64_t* timestamp_us, IplImage* image)
    {
        return getNextFrame(timestamp_us, getRawBuffer(image));
    }
    virtual bool getLatestFrameCv(uint64_t* timestamp_us, IplImage* image)
    {
        return getLatestFrame(timestamp_us, getRawBuffer(image));
    }
    virtual bool getNextFrameCv  (uint64_t* timestamp_us, CvMat* image)
    {
        return getNextFrame(timestamp_us, getRawBuffer(image));
    }
    virtual bool getLatestFrameCv(uint64_t* timestamp_us, CvMat* image)
    {
        return getLatestFrame(timestamp_us, getRawBuffer(image));
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
                           unsigned char* buffer)
    {
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

                result = getLatestFrame(&timestamp_us, sourceThread_buffer);
            }
            else
                // We are not limiting the framerate. Try to return ALL the available frames
                result = getNextFrame(&timestamp_us, sourceThread_buffer);

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
