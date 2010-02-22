#include "ffmpegInterface.hh"

#define LOCAL_PIX_FMT       PIX_FMT_GRAY8
#define OUTPUT_PIX_FMT      PIX_FMT_GRAY8
#define OUTPUT_CODEC        CODEC_ID_RAWVIDEO
#define OUTPUT_GOP_SIZE     0
#define OUTPUT_MAX_B_FRAMES 0
#define OUTPUT_FLAGS        0
#define OUTPUT_FLAGS2       0
#define OUTPUT_THREADS      2

FFmpegTalker::FFmpegTalker()
{
    av_register_all();
    reset();
}
void FFmpegTalker::reset(void)
{
    m_bOpen         = false;
    m_bOK           = true;

    m_pFormatCtx      = NULL;
    m_pCodecCtx       = NULL;
    m_pFrameYUV       = NULL;
    m_pSWSCtx         = NULL;
}

void FFmpegEncoder::reset(void)
{
    m_pOutputFormat   = NULL;
    m_pStream         = NULL;
    m_bufferYUV       = NULL;
    m_bufferYUVSize   = -1;

    FFmpegTalker::reset();
}

void FFmpegDecoder::reset(void)
{
    m_videoStream   = -1;

    FFmpegTalker::reset();
}

FFmpegTalker::~FFmpegTalker()
{
    cerr << "FFmpegTalker::~FFmpegTalker" << endl;
    free();
}
void FFmpegTalker::free(void)
{
    cerr << "FFmpegTalker::free(void)" << endl;
    if(m_pFrameYUV)    av_free(m_pFrameYUV);

    if(m_pSWSCtx)      sws_freeContext(m_pSWSCtx);

    if(m_pCodecCtx)
    {
        if(m_pCodecCtx->codec != NULL)
        {
            cerr << "avcodec_close" << endl;
            avcodec_close(m_pCodecCtx);
        }
    }
}
void FFmpegDecoder::free(void)
{
    cerr << "FFmpegDecoder::free(void)" << endl;
    FFmpegTalker::free();

    if(m_pFormatCtx)
        av_close_input_file(m_pFormatCtx);
    reset();
}
void FFmpegEncoder::free(void)
{
    cerr << "FFmpegEncoder::free(void)" << endl;

    FFmpegTalker::free();

    if(m_bufferYUV)
        av_free(m_bufferYUV);
    if(m_pStream)
        av_free(m_pStream);
    if(m_pCodecCtx)
        av_free(m_pCodecCtx);
    if(m_pFormatCtx)
        av_free(m_pFormatCtx);

    reset();
}

void FFmpegDecoder::close(void)
{
    cerr << "FFmpegDecoder::close(void)" << endl;
    free();
}
void FFmpegEncoder::close(void)
{
    cerr << "FFmpegEncoder::close(void)" << endl;
    if(m_bOpen)
    {
        av_write_trailer(m_pFormatCtx);
        url_fclose(m_pFormatCtx->pb);
    }
    free();
}


bool FFmpegDecoder::open(const char* filename)
{
    if(m_bOpen)
    {
        cerr << "FFmpegDecoder: trying to open a file while we're already open. Doing nothing." << endl;
        return true;
    }
    if(!m_bOK)
    {
        cerr << "FFmpegDecoder: trying to open a file while we're not ok. Reseting and trying again." << endl;
        close();
    }
    m_bOK = false;

    if(av_open_input_file(&m_pFormatCtx, filename, NULL, 0, NULL) != 0)
    {
        cerr << "ffmpeg: couldn't open input file" << endl;
        return false;
    }

    // this dumps info into stdout
    //   if(av_find_stream_info(m_pFormatCtx) < 0)
    //   {
    //     cerr << "ffmpeg: couldn't find stream info" << endl;
    //     return false;
    //   }
    //   dump_format(pFormatCtx, 0, filename, 0);

    // Find the first video stream
    m_videoStream = -1;
    for(unsigned int i=0; i<m_pFormatCtx->nb_streams; i++)
    {
        if(m_pFormatCtx->streams[i]->codec->codec_type == CODEC_TYPE_VIDEO)
        {
            m_videoStream = i;
            break;
        }
    }
    if(m_videoStream == -1)
    {
        cerr << "ffmpeg: couldn't find first video stream" << endl;
        return false;
    }
    m_pCodecCtx = m_pFormatCtx->streams[m_videoStream]->codec;

    AVCodec* pCodec = avcodec_find_decoder(m_pCodecCtx->codec_id);
    if(pCodec == NULL)
    {
        cerr << "ffmpeg: couldn't find decoder" << endl;
        return false;
    }

    if(avcodec_open(m_pCodecCtx, pCodec) < 0)
    {
        cerr << "ffmpeg: couldn't open codec" << endl;
        return false;
    }

    m_pFrameYUV = avcodec_alloc_frame();

    m_bOpen = m_bOK = true;

    width  = m_pCodecCtx->width;
    height = m_pCodecCtx->height;

    return true;
}

bool FFmpegDecoder::readFrameGrayscale(unsigned char* pBuffer)
{
    if(!m_bOpen || !m_bOK)
        return false;

    AVPacket packet;
    int frameFinished;

    while(av_read_frame(m_pFormatCtx, &packet) >= 0)
    {
        if(packet.stream_index == m_videoStream)
        {
            if(avcodec_decode_video(m_pCodecCtx, m_pFrameYUV, &frameFinished,
                                    packet.data, packet.size) < 0)
            {
                cerr << "ffmpeg error avcodec_decode_video()" << endl;
                return false;
            }

            if(frameFinished)
            {
                if(m_pSWSCtx == NULL)
                {
                    m_pSWSCtx = sws_getContext(width, height, m_pCodecCtx->pix_fmt,
                                               width, height, LOCAL_PIX_FMT,
                                               SWS_POINT, NULL, NULL, NULL);
                    if(m_pSWSCtx == NULL)
                    {
                        cerr << "ffmpeg: couldn't create sws context" << endl;
                        return false;
                    }
                }

                sws_scale(m_pSWSCtx,
                          m_pFrameYUV->data, m_pFrameYUV->linesize,
                          0, height,
                          &pBuffer, (int*)&width);

                av_free_packet(&packet);
                return true;
            }
        }
    }
    return false;
}

bool FFmpegEncoder::open(const char* filename)
{
    if(m_bOpen)
    {
        cerr << "FFmpegEncoder: trying to open a file while we're already open. Doing nothing." << endl;
        return true;
    }
    if(!m_bOK)
    {
        cerr << "FFmpegDecoder: trying to open a file while we're not ok. Reseting and trying again." << endl;
        close();
    }
    m_bOK = false;

    m_pOutputFormat = guess_format(NULL, "blah.avi", NULL);
    if(!m_pOutputFormat)
    {
        cerr << "ffmpeg: guess_format couldn't figure it out" << endl;
        return false;
    }

    m_pFormatCtx = avformat_alloc_context();
    if(m_pFormatCtx == NULL)
    {
        cerr << "ffmpeg: couldn't alloc format context" << endl;
        return false;
    }

    // set the output format and filename
    m_pFormatCtx->oformat = m_pOutputFormat;
    strncpy(m_pFormatCtx->filename, filename, sizeof(m_pFormatCtx->filename));

    m_pStream = av_new_stream(m_pFormatCtx, 0);
    if(m_pStream == NULL)
    {
        cerr << "ffmpeg: av_new_stream failed" << endl;
        return false;
    }

    m_pCodecCtx                = m_pStream->codec;
    m_pCodecCtx->codec_type    = CODEC_TYPE_VIDEO;
    m_pCodecCtx->codec_id      = OUTPUT_CODEC;
    m_pCodecCtx->bit_rate      = 1000000;
    m_pCodecCtx->flags         = OUTPUT_FLAGS;
    m_pCodecCtx->flags2        = OUTPUT_FLAGS2;
    m_pCodecCtx->thread_count  = OUTPUT_THREADS;
    m_pCodecCtx->width         = 640;
    m_pCodecCtx->height        = 480;
    m_pCodecCtx->time_base.num = 1;
    m_pCodecCtx->time_base.den = 15; // frames per second
    m_pCodecCtx->gop_size      = OUTPUT_GOP_SIZE;
    m_pCodecCtx->max_b_frames  = OUTPUT_MAX_B_FRAMES;
    m_pCodecCtx->pix_fmt       = OUTPUT_PIX_FMT;

    AVCodec* pCodec = avcodec_find_encoder(m_pCodecCtx->codec_id);
    if(pCodec == NULL)
    {
        cerr << "ffmpeg: couldn't find encoder. Available:" << endl;

        // I was seeing a linker error with first_avcodec. At some point I should fix that and
        // re-enable this code
//         extern AVCodec *first_avcodec;
//         pCodec = first_avcodec;
//         while(pCodec)
//         {
//             if (pCodec->encode)
//                 cerr << pCodec->id << ": " << pCodec->name << endl;
//             pCodec = pCodec->next;
//         }
        return false;
    }

    if(avcodec_open(m_pCodecCtx, pCodec) < 0)
    {
        cerr << "ffmpeg: couldn't open codec" << endl;
        return false;
    }

    m_pFrameYUV = avcodec_alloc_frame();
    if(m_pFrameYUV == NULL)
    {
        cerr << "ffmpeg: couldn't alloc frame" << endl;
        return false;
    }
    m_bufferYUVSize = avpicture_get_size(m_pCodecCtx->pix_fmt, m_pCodecCtx->width, m_pCodecCtx->height);
    m_bufferYUV     = (uint8_t*)av_malloc(m_bufferYUVSize * sizeof(uint8_t));
    avpicture_fill((AVPicture *)m_pFrameYUV, m_bufferYUV, m_pCodecCtx->pix_fmt,
                   m_pCodecCtx->width, m_pCodecCtx->height);

    // open the file
    if(url_fopen(&m_pFormatCtx->pb, filename, URL_WRONLY) < 0)
    {
        cerr << "ffmpeg: couldn't open file " << filename << endl;
        return false;
    }

    av_write_header(m_pFormatCtx);

    m_pSWSCtx = sws_getContext(m_pCodecCtx->width, m_pCodecCtx->height, LOCAL_PIX_FMT,
                               m_pCodecCtx->width, m_pCodecCtx->height, m_pCodecCtx->pix_fmt,
                               SWS_POINT, NULL, NULL, NULL);
    if(m_pSWSCtx == NULL)
    {
        cerr << "ffmpeg: couldn't create sws context" << endl;
        return false;
    }
  
    m_bOpen = m_bOK = true;
    return true;
}

bool FFmpegEncoder::writeFrameGrayscale(unsigned char* pBuffer)
{
    if(!m_bOpen || !m_bOK)
        return false;

#if 0
    for(int y=0; y<m_pCodecCtx->height; y++)
    {
        unsigned char* pDat = m_pFrameYUV->data[0] + y*m_pFrameYUV->linesize[0];
        for(int x=0; x<m_pCodecCtx->width; x++)
        {
            pDat[4*x] = pDat[4*x + 1] = pDat[4*x + 2] = *pBuffer;
            pDat[4*x + 3] = 255;
            pBuffer++;
        }
    }
#else
    sws_scale(m_pSWSCtx,
              &pBuffer, &m_pCodecCtx->width, 0, m_pCodecCtx->height,
              m_pFrameYUV->data, m_pFrameYUV->linesize);
#endif

    int outsize = avcodec_encode_video(m_pCodecCtx, m_bufferYUV, m_bufferYUVSize, m_pFrameYUV);
    if(outsize <= 0)
    {
        cerr << "ffmpeg: couldn't write grayscale frame" << endl;
        return false;
    }

    AVPacket packet;
    av_init_packet(&packet);
    packet.stream_index = m_pStream->index;
    packet.data         = m_bufferYUV;
    packet.size         = outsize;
    av_write_frame(m_pFormatCtx, &packet);

    av_free_packet(&packet);
    return true;
}
