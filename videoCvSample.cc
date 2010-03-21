#include <unistd.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <time.h>
#include <string.h>
using namespace std;

#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>

#include "cvFltkWidget.hh"

#include "ffmpegInterface.hh"
#include "pthread.h"

#define SOURCE_PERIOD_S  0
#define SOURCE_PERIOD_NS 100000000

static CvFltkWidget* widgetImage;
static CvFltkWidget* widgetImage2;

static bool sourceThread_doTerminate = false;
void* sourceThread(void *pArg)
{
    FrameSource* source = (FrameSource*)pArg;

    while(!sourceThread_doTerminate)
    {
        struct timespec delay;
        delay.tv_sec =  SOURCE_PERIOD_S;
        delay.tv_nsec = SOURCE_PERIOD_NS;
        nanosleep(&delay, NULL);

        uint64_t timestamp_us;

        if( !source->getNextFrame(*widgetImage, &timestamp_us) )
        {
            cerr << "couldn't get frame\n";
            return NULL;
        }

        cvSetImageCOI(*widgetImage, 1);
        cvCopy( *widgetImage, *widgetImage2);
        cvSetImageCOI(*widgetImage, 0);
        cvCanny(*widgetImage2, *widgetImage2, 20, 50);

        Fl::lock();
        if(sourceThread_doTerminate) return NULL;

        widgetImage->redrawNewFrame();
        widgetImage2->redrawNewFrame();
        Fl::unlock();
    }
    return NULL;
}


int main(int argc, char* argv[])
{
    if(argc <= 1)
    {
        fprintf(stderr, "need video file on the cmdline\n");
        return 0;
    }

    Fl::lock();
    Fl::visual(FL_RGB);

    // open the first source. request color
    FrameSource* source = new FFmpegDecoder(argv[1], FRAMESOURCE_COLOR);
    if(! *source)
    {
        fprintf(stderr, "couldn't open source\n");
        delete source;
        return 0;
    }

    Fl_Double_Window window(source->w()*2, source->h());
    widgetImage = new CvFltkWidget(0, 0, source->w(), source->h(),
                                   WIDGET_COLOR);

    widgetImage2 = new CvFltkWidget(source->w(), 0, source->w(), source->h(),
                                    WIDGET_GRAYSCALE);

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

    delete source;
    delete widgetImage;
    delete widgetImage2;

    return 0;
}
