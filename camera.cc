#include <stdio.h>
#include <limits.h>
#include "camera.hh"

#define BYTES_PER_PIXEL 1
// These describe the whole camera bus, not just a single camera. Thus we keep only one copy by
// declaring them static members
static dc1394_t*            Camera::dc1394Context    = NULL;
static dc1394camera_list_t* Camera::cameraList       = NULL;
static int                  Camera::numInitedCameras = 0;

Camera::Camera(unsigned _cameraIndex)
    : cameraIndex(_cameraIndex), camera(NULL), cameraFrame(NULL)
{
    dc1394error_t err;

    if(dc1394Context == NULL)
    {
        dc1394Context = dc1394_new();
        if (!dc1394Context)
            return;

        err = dc1394_camera_enumerate (dc1394Context, &cameraList);
        DC1394_ERR(err,"Failed to enumerate cameras");

        if(cameraList->num == 0)
        {
            dc1394_log_error("No cameras found");
            return;
        }
    }

    if(cameraIndex >= cameraList->num)
        return;

    camera = dc1394_camera_new(dc1394Context, cameraList->ids[cameraIndex].guid);
    if (!camera)
    {
        dc1394_log_error("Failed to initialize camera with guid %ld", cameraList->ids[cameraIndex].guid);
        return;
    }

    fprintf(stderr, "Using camera with GUID %ld\n", camera->guid);

    // Release the resources that could have been allocated by previous instances of a program using
    // the cameras. I don't know which channels and how much bandwidth was allocated, so release
    // EVERYTHING. This can generate bogus error messages, but these should be ignored
    static bool doneOnce = false;
    if(!doneOnce)
    {
        doneOnce = true;
        fprintf(stderr, "cleaning up ieee1394 stack. This may cause error messages...\n");
        dc1394_iso_release_bandwidth(camera, INT_MAX);
        for (int channel = 0; channel < 64; channel++)
            dc1394_iso_release_channel(camera, channel);
    }


    // get the best video mode and highest framerate
    // get video modes:
    dc1394video_modes_t  video_modes;
    dc1394video_mode_t   video_mode;
    dc1394color_coding_t coding;
    err = dc1394_video_get_supported_modes(camera, &video_modes);
    DC1394_ERR(err, "Can't get video modes");

    // select highest res mode:
    int i;
    for (i = video_modes.num - 1; i >= 0; i--)
    {
        if (!dc1394_is_video_mode_scalable(video_modes.modes[i]))
        {
            dc1394_get_color_coding_from_video_mode(camera, video_modes.modes[i], &coding);
            if( coding == DC1394_COLOR_CODING_MONO8 )
            {
                video_mode = video_modes.modes[i];
                break;
            }
        }
    }
    if (i < 0)
    {
        dc1394_log_error("Could not get a valid MONO8 mode");
        return;
    }

    err = dc1394_get_color_coding_from_video_mode(camera, video_mode, &coding);
    DC1394_ERR(err, "Could not get color coding");

    // get highest framerate
    dc1394framerates_t framerates;
    dc1394framerate_t  framerate;
    err = dc1394_video_get_supported_framerates(camera, video_mode, &framerates);
    DC1394_ERR(err, "Could not get framerates");
    framerate = framerates.framerates[framerates.num - 1];

    // setup capture
    err = dc1394_video_set_operation_mode(camera, DC1394_OPERATION_MODE_1394B);
    DC1394_ERR(err,"Could not set operation mode");

    err = dc1394_video_set_iso_speed(camera, DC1394_ISO_SPEED_800);
    DC1394_ERR(err,"Could not set iso speed");

    err = dc1394_video_set_mode(camera, video_mode);
    DC1394_ERR(err,"Could not set video mode");

    err = dc1394_video_set_framerate(camera, framerate);
    DC1394_ERR(err,"Could not set framerate");

    // I always want only the latest frame available. I.e. I don't really care if I missed some
    // frames. So I use a single DMA buffer
    err = dc1394_capture_setup(camera, 1, DC1394_CAPTURE_FLAGS_DEFAULT);
    DC1394_ERR(err,"Could not setup camera-\nmake sure that the video mode and framerate are\nsupported by your camera");


    // have the camera start sending us data
    err = dc1394_video_set_transmission(camera, DC1394_ON);
    DC1394_ERR(err,"Could not start camera iso transmission");

    dc1394_get_image_size_from_video_mode(camera, video_mode, &width, &height);
    bitsPerPixel = BYTES_PER_PIXEL*8;

    inited = true;
    numInitedCameras++;

    fprintf(stderr, "init done\n");
}

Camera::~Camera(void)
{
    if (camera != NULL)
    {
        dc1394_video_set_transmission(camera, DC1394_OFF);
        dc1394_capture_stop(camera);
        dc1394_camera_free(camera);
        camera = NULL;
    }

    if(inited)
        numInitedCameras--;

    if(dc1394Context != NULL && numInitedCameras <= 0)
    {
        dc1394_free(dc1394Context);
        dc1394Context = NULL;
        dc1394_camera_free_list (cameraList);
    }
}

// Fetches all of the frames in the buffer and throws them away. If we do this we guarantee that the
// next frame read will be the most recent
void Camera::flushFrameBuffer(void)
{
    dc1394error_t err;
    while(true)
    {
        err = dc1394_capture_dequeue(camera, DC1394_CAPTURE_POLICY_POLL, &cameraFrame);

        if( err != DC1394_SUCCESS )
        {
            dc1394_log_warning("%s: in %s (%s, line %d): Could not capture a frame\n",
                               dc1394_error_get_string(err),
                               __FUNCTION__, __FILE__, __LINE__);
            return;
        }
        if(cameraFrame == NULL)
            break;

        err = dc1394_capture_enqueue(camera, cameraFrame);
        if( err != DC1394_SUCCESS )
        {
            dc1394_log_warning("%s: in %s (%s, line %d): Could not enqueue\n",
                               dc1394_error_get_string(err),
                               __FUNCTION__, __FILE__, __LINE__);
            return;
        }
        cameraFrame = NULL;
    }
}

// peekFrame() blocks until a frame is available. A pointer to the internal buffer is returned
// (NULL on error). This buffer must be given back to the system by calling
// unpeekFrame(). unpeekFrame() need not be called if peekFrame() failed
unsigned char* Camera::peekFrame(uint64_t* timestamp_us)
{
    static uint64_t timestamp0 = 0;

    if(cameraFrame != NULL)
    {
        fprintf(stderr, "error: peekFrame() before unpeekFrame()\n");
        return NULL;
    }

    dc1394error_t err;
    err = dc1394_capture_dequeue(camera, DC1394_CAPTURE_POLICY_WAIT, &cameraFrame);

    if( err != DC1394_SUCCESS )
    {
        dc1394_log_warning("%s: in %s (%s, line %d): Could not capture a frame\n",
                           dc1394_error_get_string(err),
                           __FUNCTION__, __FILE__, __LINE__);
        return NULL;
    }

    if(timestamp0 == 0)
        timestamp0 = cameraFrame->timestamp;

    if(timestamp_us != NULL)
        *timestamp_us = cameraFrame->timestamp - timestamp0;

    return cameraFrame->image;
}

void Camera::unpeekFrame(void)
{
    if(cameraFrame == NULL)
        return;

    dc1394error_t err;
    err = dc1394_capture_enqueue(camera, cameraFrame);
    if( err != DC1394_SUCCESS )
    {
        dc1394_log_warning("%s: in %s (%s, line %d): Could not enqueue\n",
                           dc1394_error_get_string(err),
                           __FUNCTION__, __FILE__, __LINE__);
        return;
    }

    cameraFrame = NULL;
}
