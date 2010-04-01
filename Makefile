CXXFLAGS += -g -Wall -Wextra -pedantic -MMD
LDFLAGS  += -g -lX11 -lXft -lXinerama

FFMPEG_LIBS = -lavformat -lavcodec -lswscale -lavutil
LDLIBS += -lfltk -lcv -lpthread -ldc1394 $(FFMPEG_LIBS)

all: fltkVisionUtils.a sample

# all non-sample .cc files
LIBRARY_SOURCES = $(shell ls *.cc | grep -v -i sample)

fltkVisionUtils.a: $(patsubst %.cc, %.o, $(LIBRARY_SOURCES));
	ar rcvu $@ $^

sample: ffmpegInterface.o cameraSource.o sample.o
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@


clean:
	rm -f *.o *.a sample

-include *.d
