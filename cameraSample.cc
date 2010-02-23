#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <string.h>
using namespace std;

#include <FL/Fl.H>
#include <FL/Fl_Window.H>

#include "flWidgetImage.hh"
#include "ffmpegInterface.hh"

#include "camera.hh"
#include "pthread.h"

#define SOURCE_PERIOD_NS 1000000000

static FlWidgetImage* widgetImage;
static FFmpegEncoder videoEncoder;

static bool sourceThread_doTerminate = false;
void* sourceThread(void *pArg)
{
    FrameSource* source = (FrameSource*)pArg;

    while(!sourceThread_doTerminate)
    {
        struct timespec delay;
        delay.tv_sec = 0;
        delay.tv_nsec = SOURCE_PERIOD_NS;
        nanosleep(&delay, NULL);

        uint64_t timestamp_us;
        if( !source->getNextFrame(&timestamp_us, widgetImage->getBuffer()) )
        {
            cerr << "couldn't get frame\n";
            source->unpeekFrame();
            return NULL;
        }

        if(!videoEncoder)
        {
            fprintf(stderr, "Couldn't encode frame!\n");
            return NULL;
        }
        videoEncoder.writeFrameGrayscale(widgetImage->getBuffer());
        if(!videoEncoder)
        {
            fprintf(stderr, "Couldn't encode frame!\n");
            return NULL;
        }

        Fl::lock();
        if(sourceThread_doTerminate) return NULL;

        widgetImage->redrawNewFrame();
        Fl::unlock();
    }
    return NULL;
}


int main(void)
{
    Fl::lock();
    Fl::visual(FL_RGB);

    // open the first source. request color
    Camera* source = new Camera(FRAMESOURCE_GRAYSCALE);
    if(! *source)
    {
        fprintf(stderr, "couldn't open source\n");
        delete source;
        return 0;
    }
    cout << source->getDescription();

    videoEncoder.open("capture.avi", source->w(), source->h(), 15);
    if(!videoEncoder)
    {
        fprintf(stderr, "Couldn't initialize the video encoder\n");
        return 0;
    }

    Fl_Window window(source->w(), source->h());
    widgetImage = new FlWidgetImage(0, 0, source->w(), source->h(),
                                  WIDGET_GRAYSCALE, FAST_REDRAW);

    window.resizable(window);
    window.end();
    window.show();

    pthread_t sourceThread_id;
    if(pthread_create(&sourceThread_id, NULL, &sourceThread, source) != 0)
    {
        cerr << "couldn't start source thread" << endl;
        return 0;
    }

    while (Fl::wait())
    {
    }

    Fl::unlock();
    sourceThread_doTerminate = true;

    pthread_join(sourceThread_id, NULL);

    videoEncoder.close();

    delete source;
    delete widgetImage;

    return 0;
}
