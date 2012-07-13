#include <jni.h>
#include <android/log.h>
#include <stdio.h>

#define LOG_TAG "Talking Spoony libvid"
#define W_VIDEO 320
#define H_VIDEO 525
#define FRAMES_PER_SECOND 25
//#define VIDEO_CODEC CODEC_ID_ MPEG4
#define IN_PIX_FMT		PIX_FMT_YUV420P
#define OUT_PIX_FMT 	PIX_FMT_YUVJ420P
#define FILE_NAME          "/sdcard/output.mp4"
#define INPUT_FILE_FORMAT "/sdcard/spoony/%03d.jpg"
#define INPUT_AUDIO		"/sdcard/spoony/23.mp3"
//#define INPUT_AUDIO		"/sdcard/sample.wav"
#define FRAME_COUNT        100
#define bool int
#define true 1
#define false 0
#define printf(...) __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define MAX_AUDIO_PACKET_SIZE (128 * 1024)


#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libswscale/swscale.h"
#include "libavcodec/avcodec.h"

#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

typedef unsigned long DWORD;
typedef unsigned char BYTE;

// output format.
AVOutputFormat *pOutFormat;
// format context
AVFormatContext *pFormatContext;
// video stream context
AVStream * pVideoStream;
// audio streams context
AVStream * pAudioStream;
// convert context context
struct SwsContext *pImgConvertCtx;
// encode buffer and size
uint8_t * pVideoEncodeBuffer;
int nSizeVideoEncodeBuffer;

// audio buffer and size
uint8_t * pAudioEncodeBuffer;
int nSizeAudioEncodeBuffer;

// count of sample
int audioInputSampleSize;
// current picture
AVFrame *pCurrentPicture;

// audio buffer
char* audioBuffer;
int nAudioBufferSize;
int nAudioBufferSizeCurrent;

const char* outputFilename;

int g_emulate_pts = 0;

AVCodecContext *apCodecCxt;
uint8_t *audio_outbuf;
int audio_outbuf_size;
int16_t *samples;

void fail(const char* message, int err)
{
    LOGE("%s: errno [%d] - %s\n", message, AVERROR(err), strerror(AVERROR(err)));
}

AVStream* AddVideoStream(AVFormatContext *pContext, enum CodecID codec_id)
{
    AVCodecContext *pCodecCxt = NULL;
    AVStream *st = NULL;

    st = av_new_stream(pContext, 0);
    if (!st)
    {
        printf("Cannot add new video stream\n");
        return NULL;
    }

    pCodecCxt = st->codec;
    pCodecCxt->codec_id = codec_id;
    pCodecCxt->codec_type = CODEC_TYPE_VIDEO;
    pCodecCxt->frame_number = 0;
    // Put sample parameters.
    pCodecCxt->bit_rate = 2000000;
    // Resolution must be a multiple of two.
    pCodecCxt->width = W_VIDEO;
    pCodecCxt->height = H_VIDEO;
    /* time base: this is the fundamental unit of time (in seconds) in terms
     of which frame timestamps are represented. for fixed-fps content,
     timebase should be 1/framerate and timestamp increments should be
     identically 1. */
    pCodecCxt->time_base.den = FRAMES_PER_SECOND;
    pCodecCxt->time_base.num = 1;
    pCodecCxt->gop_size = 12; // emit one intra frame every twelve frames at most

    pCodecCxt->pix_fmt = PIX_FMT_YUV420P;

    // Some formats want stream headers to be separate.
    if (pContext->oformat->flags & AVFMT_GLOBALHEADER)
    {
        pCodecCxt->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    return st;
}

AVStream* AddAudioStream(AVFormatContext *pContext, enum CodecID codec_id)
{
    AVStream *pStream = NULL;

    // Try create stream.
    pStream = av_new_stream(pContext, 1);
    if (!pStream)
    {
        printf("Cannot add new audio stream\n");
        return NULL;
    }

    // Codec.
    apCodecCxt = pStream->codec;
    apCodecCxt->codec_id = codec_id;
    apCodecCxt->codec_type = CODEC_TYPE_AUDIO;
    // Set format
    //apCodecCxt->bit_rate = 128000;
    apCodecCxt->sample_rate = 44100;
	//apCodecCxt->sample_rate = 128000;
    apCodecCxt->channels = 2;
    apCodecCxt->sample_fmt = SAMPLE_FMT_S16;

    nSizeAudioEncodeBuffer = 4 * MAX_AUDIO_PACKET_SIZE;
    if (pAudioEncodeBuffer == NULL)
    {
        pAudioEncodeBuffer = (uint8_t *) av_malloc(nSizeAudioEncodeBuffer);
    }

    // Some formats want stream headers to be separate.
    if (pContext->oformat->flags & AVFMT_GLOBALHEADER)
    {
        apCodecCxt->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }

    audio_outbuf_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
    audio_outbuf = av_malloc(audio_outbuf_size);
    samples = av_malloc(audioInputSampleSize * 2 * apCodecCxt->channels);

    return pStream;
}

bool OpenVideo(AVFormatContext *oc, AVStream *pStream)
{
    AVCodec *pCodec;
    AVCodecContext *pContext;

    pContext = pStream->codec;

    // Find the video encoder.
    pCodec = avcodec_find_encoder(pContext->codec_id);
    if (!pCodec)
    {
        printf("Cannot found video codec\n");
        return false;
    }

    // Open the codec.
    if (avcodec_open(pContext, pCodec) < 0)
    {
        printf("Cannot open video codec\n");
        return false;
    }

    pVideoEncodeBuffer = NULL;
    if (!(pFormatContext->oformat->flags & AVFMT_RAWPICTURE))
    {
        /* allocate output buffer */
        nSizeVideoEncodeBuffer = 10000000;
        pVideoEncodeBuffer = (uint8_t *) av_malloc(nSizeVideoEncodeBuffer);
    }

    return true;
}

void CloseVideo(AVFormatContext *pContext, AVStream *pStream)
{
    avcodec_close(pStream->codec);
    if (pCurrentPicture)
    {
        if (pCurrentPicture->data)
        {
            av_free(pCurrentPicture->data[0]);
            pCurrentPicture->data[0] = NULL;
        }
        av_free(pCurrentPicture);
        pCurrentPicture = NULL;
    }

    if (pVideoEncodeBuffer)
    {
        av_free(pVideoEncodeBuffer);
        pVideoEncodeBuffer = NULL;
    }
    nSizeVideoEncodeBuffer = 0;
}

bool OpenAudio(AVFormatContext *pContext, AVStream *pStream)
{
    AVCodecContext *pCodecCxt = NULL;
    AVCodec *pCodec = NULL;
    pCodecCxt = pStream->codec;

    // Find the audio encoder.
    pCodec = avcodec_find_encoder(pCodecCxt->codec_id);
    if (!pCodec)
    {
        printf("Cannot open audio codec\n");
        return false;
    }

    // Open it.
    if (avcodec_open(pCodecCxt, pCodec) < 0)
    {
        printf("Cannot open audio codec\n");
        return false;
    }

    if (pCodecCxt->frame_size <= 1)
    {
        // Ugly hack for PCM codecs (will be removed ASAP with new PCM
        // support to compute the input frame size in samples.
        audioInputSampleSize = nSizeAudioEncodeBuffer / pCodecCxt->channels;
        switch (pStream->codec->codec_id)
        {
        case CODEC_ID_PCM_S16LE:
        case CODEC_ID_PCM_S16BE:
        case CODEC_ID_PCM_U16LE:
        case CODEC_ID_PCM_U16BE:
            audioInputSampleSize >>= 1;
            break;
        default:
            break;
        }
        pCodecCxt->frame_size = audioInputSampleSize;
    }
    else
    {
        audioInputSampleSize = pCodecCxt->frame_size;
    }

    return true;
}

void CloseAudio(AVFormatContext *pContext, AVStream *pStream)
{
    avcodec_close(pStream->codec);
    if (pAudioEncodeBuffer)
    {
        av_free(pAudioEncodeBuffer);
        pAudioEncodeBuffer = NULL;
    }
    nSizeAudioEncodeBuffer = 0;
}

size_t min(size_t a, size_t b)
{
    if (a > b)
        return b;
    return a;
}

void Free()
{
    if (pFormatContext)
    {
        // close video stream
        if (pVideoStream)
        {
            CloseVideo(pFormatContext, pVideoStream);
        }

        // close audio stream.
        if (pAudioStream)
        {
            CloseAudio(pFormatContext, pAudioStream);
        }

        // Free the streams.
        size_t i;
        for (i = 0; i < pFormatContext->nb_streams; i++)
        {
            av_freep(&pFormatContext->streams[i]->codec);
            av_freep(&pFormatContext->streams[i]);
        }

        if (!(pFormatContext->flags & AVFMT_NOFILE) && pFormatContext->pb)
        {
            url_fclose(pFormatContext->pb);
        }

        // Free the stream.
        av_free(pFormatContext);
        pFormatContext = NULL;
    }
}

bool InitFile(const char* filename)
{
    bool res = false;

    outputFilename = filename;

    // Init constants
    nAudioBufferSize = 1024 * 1024 * 4;
    audioBuffer      = (char*) av_malloc(nAudioBufferSize);

    // Initialize libavcodec
    av_register_all();

    pOutFormat = av_guess_format(NULL, filename, NULL);
    //pOutFormat->video_codec = CODEC_ID_H263;
    //pOutFormat->flags |= AVFMT_RAWPICTURE;

    if (pOutFormat)
    {
        // allocate context
        pFormatContext = avformat_alloc_context();

		// сомнительно
        pFormatContext->max_delay = (int)(0.8 * AV_TIME_BASE);
        pFormatContext->preload = (int)(0.3 * AV_TIME_BASE);

        if (pFormatContext)
        {
            pFormatContext->oformat = pOutFormat;
            memcpy(pFormatContext->filename, filename, min(strlen(filename),
                    sizeof(pFormatContext->filename)));

            // Add video and audio stream
            pVideoStream = AddVideoStream(pFormatContext, pOutFormat->video_codec);
            pAudioStream = AddAudioStream(pFormatContext, pOutFormat->audio_codec);

            // Set the output parameters (must be done even if no
            // parameters).
            if (av_set_parameters(pFormatContext, NULL) >= 0)
            {
                dump_format(pFormatContext, 0, filename, 1);

                // Open Video and Audio stream
                res = false;
                if (pVideoStream)
                {
                    res = OpenVideo(pFormatContext, pVideoStream);
                    res ? LOGI("OpenVideo: OK") : LOGE("OpenVideo: FAIL");
                }

                res = OpenAudio(pFormatContext, pAudioStream);
                res ? LOGI("OpenAudio: OK") : LOGE("OpenAudio: FAIL");

                if (res && !(pOutFormat->flags & AVFMT_NOFILE))
                {
                    url_fopen(&pFormatContext->pb, filename, URL_WRONLY) < 0;
                    //res ? LOGI("url_fopen: OK") : LOGE("url_fopen: FAIL");
                }

                if (res)
                {
                    av_write_header(pFormatContext);
                    LOGI("av_write_header: OK");
                }
                else
                {
                    LOGE("Error while init output file");
                }
            }
        }
    }

    if (!res)
    {
        Free();
    }

    res ? LOGI("InitFile: OK") : LOGE("InitFile: FAIL");

    return res;
}

bool NeedConvert()
{
    bool res = false;
    if (pVideoStream && pVideoStream->codec)
    {
        res = (pVideoStream->codec->pix_fmt != PIX_FMT_RGB24);
    }
    return res;
}

bool AddVideoFrame(AVFrame * pOutputFrame, AVCodecContext *pVideoCodec)
{
    bool res = false;

    if (pFormatContext->oformat->flags & AVFMT_RAWPICTURE)
    {
        LOGI("AVFMT_RAWPICTURE");
        // Raw video case. The API will change slightly in the near
        // futur for that.
        AVPacket pkt;
        av_init_packet(&pkt);

        pkt.flags |= PKT_FLAG_KEY;
        pkt.stream_index = pVideoStream->index;
        pkt.data = (uint8_t *) pOutputFrame;
        pkt.size = sizeof(AVPicture);

        res = av_interleaved_write_frame(pFormatContext, &pkt);
        if (res < 0)
        {
            fail("RAWPICTURE 'av_interleaved_write_frame' failed", res);
            res = false;
        }
        res = true;
    }
    else
    {
        // Encode
        LOGI("Encoding\n");
        int nOutputSize = avcodec_encode_video(pVideoCodec, pVideoEncodeBuffer,	nSizeVideoEncodeBuffer, pOutputFrame);
        if (nOutputSize >= 0)
        //if (res >=0)
        {
            AVPacket pkt;
            av_init_packet(&pkt);

            if (pVideoCodec->coded_frame->pts != AV_NOPTS_VALUE)
            {
                pkt.pts = av_rescale_q(pVideoCodec->coded_frame->pts,
                        pVideoCodec->time_base, pVideoStream->time_base);
            }

            if (pVideoCodec->coded_frame->key_frame)
            {
                pkt.flags |= PKT_FLAG_KEY;
            }
            pkt.stream_index = pVideoStream->index;
            pkt.data = pVideoEncodeBuffer;
            pkt.size = nOutputSize;

            // Write frame
            res = av_interleaved_write_frame(pFormatContext, &pkt);
            if (res < 0)
            {
                fail("'av_interleaved_write_frame' failed", res);
                res = false;
            }
        }
        else
        {
            fail("'avcodec_encode_video' failed", nOutputSize);
            res = false;
        }
    }

    return res;
}

bool AddAudioSample(AVFormatContext *pFormatContext, AVStream *pStream, const char* soundBuffer, int soundBufferSize)
{
    AVCodecContext *pCodecCxt;
    bool res = true;

    pCodecCxt = pStream->codec;
    memcpy(audioBuffer + nAudioBufferSizeCurrent, soundBuffer, soundBufferSize);
    nAudioBufferSizeCurrent += soundBufferSize;

    BYTE * pSoundBuffer = (BYTE *) audioBuffer;
    int nCurrentSize = nAudioBufferSizeCurrent;

    // Size of packet on bytes.
    // FORMAT s16
    DWORD packSizeInSize = 2 * audioInputSampleSize;

    while (nCurrentSize >= packSizeInSize)
    {
        AVPacket pkt;
        av_init_packet(&pkt);

        pkt.size = avcodec_encode_audio(pCodecCxt, pAudioEncodeBuffer,
                nSizeAudioEncodeBuffer, (const short *) pSoundBuffer);

        if (pCodecCxt->coded_frame && pCodecCxt->coded_frame->pts
                != AV_NOPTS_VALUE)
        {
            pkt.pts = av_rescale_q(pCodecCxt->coded_frame->pts,
                    pCodecCxt->time_base, pStream->time_base);
        }

        pkt.flags |= PKT_FLAG_KEY;
        pkt.stream_index = pStream->index;
        pkt.data = pAudioEncodeBuffer;

        // Write the compressed frame in the media file.
        if (av_interleaved_write_frame(pFormatContext, &pkt) != 0)
        {
            res = false;
            break;
        }

        nCurrentSize -= packSizeInSize;
        pSoundBuffer += packSizeInSize;
    }

    // save excess
    memcpy(audioBuffer, audioBuffer + nAudioBufferSizeCurrent - nCurrentSize,
            nCurrentSize);
    nAudioBufferSizeCurrent = nCurrentSize;

    return res;
}

bool Finish()
{
    bool res = true;

    if (pFormatContext)
    {
        av_write_trailer(pFormatContext);
        Free();
    }

    if (audioBuffer)
    {
        //delete[] audioBuffer;
        free(audioBuffer);
        audioBuffer = NULL;
    }

    return res;
}

void SaveRGB(AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char szFilename[32];
    int  y;

    // Open file
    sprintf(szFilename, "frame%d.ppm", iFrame);
    LOGI("Saving frame '%s'", szFilename);
    pFile=fopen(szFilename, "wb");
    if(pFile==NULL)
    {
        LOGE("Cannot create '%s'\n", szFilename);
        return;
    }

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for(y=0; y<height; y++)
        fwrite(pFrame->data[0]+y*pFrame->linesize[0], 1, width*3, pFile);

    // Close file
    fclose(pFile);
    LOGI("PPM written\n");
}

int SavePicture(AVFrame* pFrame, int numBytes, uint8_t *buffer)
{
    AVCodec *pOCodec;
    AVCodecContext *pOCodecCtx = avcodec_alloc_context();
    if (!pOCodecCtx)
    {
        LOGE("Could not allocate codec\n");
        return 1;
    }

    //pOCodecCtx->bit_rate = pCodecCtx->bit_rate;
    pOCodecCtx->width = W_VIDEO;
    pOCodecCtx->height = H_VIDEO;
    pOCodecCtx->pix_fmt = OUT_PIX_FMT;
    pOCodecCtx->color_range = AVCOL_RANGE_MPEG;
    pOCodecCtx->codec_id = CODEC_ID_MJPEG;
    pOCodecCtx->codec_type = CODEC_TYPE_VIDEO;
    pOCodecCtx->time_base.num = 1; //pCodecCtx->time_base.num;
    pOCodecCtx->time_base.den = 1; //pCodecCtx->time_base.den;

    pOCodec = avcodec_find_encoder(pOCodecCtx->codec_id);
    if (!pOCodec)
    {
        LOGE("Codec not found\n");
        return 1;
    }

    int err = avcodec_open(pOCodecCtx, pOCodec);
    if (err < 0)
    {
        LOGE("Could not open output codec: errno %d - %s\n", AVERROR(err), strerror(AVERROR(err)));
        return 1;
    }

    pOCodecCtx->qmin = pOCodecCtx->qmax = 3;
    pOCodecCtx->mb_lmin = pOCodecCtx->lmin = pOCodecCtx->qmin * FF_QP2LAMBDA;
    pOCodecCtx->mb_lmax = pOCodecCtx->lmax = pOCodecCtx->qmax * FF_QP2LAMBDA;
    //pOCodecCtx->flags |= CODEC_FLAG_QSCALE;

    LOGI("trace\n");
    int szBufferActual = avcodec_encode_video(pOCodecCtx, buffer, numBytes, pFrame);
    if(szBufferActual < 0)
    {
        LOGE("avcodec_encode_video error. return value = %d\n",szBufferActual);
        return 1;
    }

    FILE *fdJPEG = fopen("meatball.jpg", "wb");
    int bRet = fwrite(buffer, sizeof(uint8_t), szBufferActual, fdJPEG);
    fclose(fdJPEG);

    if (bRet != szBufferActual)
    {
        LOGE("Error writing jpeg file\n");
        return 1;
    }
    else
        LOGI("jpeg file was written\n");
    return 0;
}

AVFrame* OpenImage(const char* imageFileName)
{
    AVFormatContext *pFormatCtx;

    if(av_open_input_file(&pFormatCtx, imageFileName, NULL, 0, NULL)!=0)
    {
        LOGE("Can't open image file '%s'\n", imageFileName);
        return NULL;
    }
    else
    {
        LOGI("Image file was opened\n");
        dump_format(pFormatCtx, 0, imageFileName, false);
    }

    AVCodecContext *pCodecCtx;

    pCodecCtx = pFormatCtx->streams[0]->codec;
    pCodecCtx->width = W_VIDEO;
    pCodecCtx->height = H_VIDEO;
    pCodecCtx->pix_fmt = IN_PIX_FMT;
    //pCodecCtx->flags |= CODEC_FLAG_QSCALE;

    // Find the decoder for the video stream
    AVCodec *pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (!pCodec)
    {
        LOGE("Codec not found\n");
        return NULL;
    }

    // Open codec
    if(avcodec_open(pCodecCtx, pCodec)<0)
    {
        LOGE("Could not open codec\n");
        return NULL;
    }

    AVFrame *pFrame = avcodec_alloc_frame();

    if (!pFrame)
    {
        LOGE("Can't allocate memory for AVFrame\n");
        return NULL;
    }

    int frameFinished;
    int numBytes;

    // Determine required buffer size and allocate buffer
    numBytes = avpicture_get_size(OUT_PIX_FMT, pCodecCtx->width, pCodecCtx->height);
    uint8_t *buffer = (uint8_t *) av_malloc(numBytes * sizeof(uint8_t));

    avpicture_fill((AVPicture *) pFrame, buffer, OUT_PIX_FMT, pCodecCtx->width, pCodecCtx->height);

    // Read frames
    AVPacket packet;

    int framesNumber = 0;
    if (av_read_frame(pFormatCtx, &packet) >= 0)
    {
        int ret = avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);
        if (ret > 0)
        {
            LOGI("Frame is decoded, size %d\n", ret);
            pFrame->quality = 4;

            //SaveFrame(pFrameRGB, W_VIDEO, H_VIDEO, 1);

            //SavePicture(pFrame, numBytes, buffer);

            // Saving PPM image
            /*AVFrame* pFrameRGB = avcodec_alloc_frame();
            avpicture_fill((AVPicture*)pFrameRGB, buffer, PIX_FMT_RGB24, W_VIDEO, H_VIDEO);

            SwsContext* pSWSContext = sws_getContext(W_VIDEO, H_VIDEO, PIX_FMT_YUV420P, W_VIDEO, H_VIDEO, PIX_FMT_RGB24, SWS_BICUBLIN, 0, 0, 0);
            ret = sws_scale(pSWSContext, pFrame->data, pFrame->linesize, 0, pCodecCtx->height, pFrameRGB->data, pFrameRGB->linesize);
            if (!ret)
                LOGE("SWS_Scale failed!\n", ret);
            else
                SaveRGB(pFrameRGB, W_VIDEO, H_VIDEO, 1);*/

            return pFrame;
        }
        else
        {
            LOGE("Error [%d] while decoding frame: %s\n", ret, strerror(AVERROR(ret)));
        }
    }

        /*else
        {
            printf("Cannot read frame\n");
            return NULL;
        }*/
}

void write_audio_frame(AVFormatContext *oc, AVStream *st, int frameIndex)
{
    AVCodecContext *c;
    AVPacket pkt;
    av_init_packet(&pkt);

    c = st->codec;

    pkt.size = avcodec_encode_audio(c, audio_outbuf, audio_outbuf_size, samples);
    //pkt.pts = au_pts++;
    //pkt.pts = av_rescale_q(c->coded_frame->pts, c->time_base, st->time_base);
    pkt.flags |= PKT_FLAG_KEY;
    pkt.stream_index = st->index;
    pkt.data = audio_outbuf;

    /* write the compressed frame to the media file */
    int ret = av_interleaved_write_frame(oc, &pkt);
    if (ret != 0)
    {
        fail("Error while writing audio frame", ret);
        exit(1);
    }
}

int ReadAudio(const char* audioFileName, int time)
{
    AVFormatContext *pFormatCtx;

    if(av_open_input_file(&pFormatCtx, audioFileName, NULL, 0, NULL)!=0)
    {
        LOGE("Can't open audio file '%s'\n", audioFileName);
        return 1;
    }
    else
    {
        LOGI("Audio file was opened successfully\n");
        dump_format(pFormatCtx, 0, audioFileName, false);
    }

    // Find codec
    AVCodecContext* pCodecCxt = pFormatCtx->streams[0]->codec;

    AVCodec* apCodec = avcodec_find_decoder(pCodecCxt->codec_id);
    if (!apCodec)
    {
        LOGE("Audio decoder is not found");
        return 1;
    }

    // Open codec
    int res = avcodec_open(pCodecCxt, apCodec);
    if(res != 0)
    {
        fail("Couldn't open audio codec\n", res);
        return 1;
    }
    else
        LOGI("Codec is found: %s\n", apCodec->name);

    AVPacket packet;

    // Read data
    res = av_read_frame(pFormatCtx, &packet);
    LOGI("Reading audio frame result: [%d]\n", res);


    samples = av_malloc(audioInputSampleSize * 2 * pCodecCxt->channels);

    int max_frames = time / pCodecCxt->ticks_per_frame;
    LOGI("sample rate: %d, max frames: %d\n", pCodecCxt->ticks_per_frame, max_frames);

    //int audio_outbuf_size =  AVCODEC_MAX_AUDIO_FRAME_SIZE;
    LOGI("Decoding audio...\n");

    int n = 0;
    while (res >= 0)
    {
        n++;
        //if (n < 10)
        {
            audio_outbuf_size = AVCODEC_MAX_AUDIO_FRAME_SIZE;
            audio_outbuf = av_malloc(audio_outbuf_size);
            //This is an ugly hack for the ugly hacking
            //of our "audio_outbuf_size"
            int result = avcodec_decode_audio3(pCodecCxt, samples, &audio_outbuf_size, &packet);
            LOGI("Done: %d\n", result);
            if (result > 0)
            {
                write_audio_frame(pFormatContext, pAudioStream, n);
            }
            else if (result < 0)
                fail("Error while decode audio", result);
            else
                LOGE("No data in frame\n");
        }

        res = av_read_frame(pFormatCtx, &packet);
    }
    LOGI("Frames detected: %d\n", n);
    LOGI("Audio is ready\n");
}

/*
 * Video encoding example
 */
jint Java_com_ifree_spvid_MainActivity_encode(JNIEnv * env, jobject this, jint framesCount)
{
    LOGI("Native code started");

    if (InitFile(FILE_NAME))
    {
        LOGI("File is init");

        int i;
        int c;
        for (c = 1; c <= framesCount; ++c)
        {
            LOGI("c = %d", c);
            char fname[100];// = "/sdcard/DCIM/meatball1.jpg";
            sprintf(fname, INPUT_FILE_FORMAT, c);
            LOGI("filename: %s\n", fname);

            AVFrame* fframe = OpenImage(fname);
            if (fframe != NULL)
            {
                //CreateSample((short *) sample, nSampleSize / 2);
                //for (i = 0; i < 4; ++i)
                {
                    //int res = AddFrame(frame, sample, nSampleSize);
                    fframe->pts = g_emulate_pts++;
                    int res = AddVideoFrame(fframe, pVideoStream->codec);
                    //printf("Frame %d\n", i);

                    if (res)
                    {
                        LOGE("Cannot write frame\n");
                    }
                }
            }
            else
            {
                LOGE("Cannot proceed file '%s'", fname);
            }
            av_free(fframe);
        }
        ReadAudio(INPUT_AUDIO, 8);

        Finish();
        LOGI("Ready\n");
        //av_free(frame->data[0]);
        //av_free(frame);
        //free(sample);
        //sample = NULL;
    }
    else
    {
        LOGE("Cannot open file " FILE_NAME "\n");
    }
}

void Java_com_ifree_spvid_MainActivity_helloLog(JNIEnv * env, jobject this, jstring logThis)
{
    jboolean isCopy;
    const jbyte *str;
    str = (*env)->GetStringUTFChars(env, logThis, NULL);
    if (str == NULL)
    {
        LOGE("Null");
    }
    LOGI("NDK:LC: [%s]", str);
    (*env)->ReleaseStringUTFChars(env, logThis, str);
}
