#ifndef __FL_WIDGET_IMAGE_HH__
#define __FL_WIDGET_IMAGE_HH__

#include <FL/Fl.H>
#include <FL/fl_draw.H>
#include <FL/Fl_RGB_Image.H>
#include <string.h>
#include <stdio.h>

// this class is designed for simple visualization of data passed into the class from the
// outside. Examples of data sources are cameras, video files, still images, processed data, etc.

// Both color and grayscale displays are supported. Incoming data is assumed to be of the desired
// type
enum FlWidgetImage_ColorChoice  { WIDGET_COLOR, WIDGET_GRAYSCALE };

class FlWidgetImage : public Fl_Widget
{
protected:
    int frameW, frameH;

    FlWidgetImage_ColorChoice  colorMode;
    unsigned int bytesPerPixel;

    unsigned char* imageData;
    Fl_RGB_Image*  flImage;

    void cleanup(void)
    {
        if(flImage != NULL)
        {
            delete flImage;
            flImage = NULL;
        }
        if(imageData != NULL)
        {
            delete[] imageData;
            imageData = NULL;
        }
    }

    // called by FLTK to alert this widget about an event. An upper level callback can be triggered
    // here
    virtual int handle(int event)
    {
        switch(event)
        {
        case FL_DRAG:
            if(!Fl::event_inside(this))
                break;
            // fall through if we're inside

        case FL_PUSH:
            do_callback();
            return 1;
        }

        return Fl_Widget::handle(event);
    }

public:
    FlWidgetImage(int x, int y, int w, int h,
                  FlWidgetImage_ColorChoice  _colorMode)
        : Fl_Widget(x,y,w,h),
          frameW(w), frameH(h),
          colorMode(_colorMode),
          imageData(NULL), flImage(NULL)
    {
        bytesPerPixel = (colorMode == WIDGET_COLOR) ? 3 : 1;

        imageData = new unsigned char[frameW * frameH * bytesPerPixel];
        if(imageData == NULL)
            return;

        flImage = new Fl_RGB_Image(imageData, frameW, frameH, bytesPerPixel);
        if(flImage == NULL)
        {
            cleanup();
            return;
        }
    }

    virtual ~FlWidgetImage()
    {
        cleanup();
    }

    // this can be used to render directly into the buffer
    unsigned char* getBuffer(void)
    {
        return imageData;
    }

    void draw()
    {
        // this is the FLTK draw-me-now callback
        flImage->draw(x(), y());
    }

    // this should be called from the main FLTK thread or from any other thread after obtaining an
    // Fl::lock()
    void updateFrame(unsigned char* frame)
    {
        if(!frame)
            return;

        memcpy(imageData, frame, frameW*frameH*bytesPerPixel);
        redrawNewFrame();
    }

    // Used to trigger a redraw if out drawing buffer was already updated.
    void redrawNewFrame(void)
    {
        flImage->uncache();
        redraw();

        // If we're drawing from a different thread, FLTK needs to be woken up to actually do
        // the redraw
        Fl::awake();
    }
};

#endif
