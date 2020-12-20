#include <iostream>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
}

AVFrame* videoFrame = nullptr;
AVCodecContext* cctx = nullptr;
SwsContext* swsCtx = nullptr;
int frameCounter = 0;
AVFormatContext* ofctx = nullptr;
AVOutputFormat* oformat = nullptr;
AVStream* stream = nullptr;
int fps = 4;
int width = 1920;
int height = 1080;
int bitrate = 2000;

static void pushFrame(uint8_t* data)
{
    int err;
    if (!videoFrame) {
        videoFrame = av_frame_alloc();
        videoFrame->format = AV_PIX_FMT_YUV420P;
        videoFrame->width = cctx->width;
        videoFrame->height = cctx->height;

        if ((err = av_frame_get_buffer(videoFrame, 32)) < 0) {
            std::cout << "Failed to allocate picture" << err << std::endl;
            return;
        }
    }

    if (!swsCtx) {
        swsCtx = sws_getContext(cctx->width, cctx->height, AV_PIX_FMT_RGB24, cctx->width, cctx->height,
                                AV_PIX_FMT_YUV420P, SWS_BICUBIC, 0, 0, 0);
    }

    int inLinesize[1] = {3 * cctx->width};

    // From RGB to YUV
    sws_scale(swsCtx, (const uint8_t* const*)&data, inLinesize, 0, cctx->height, videoFrame->data,
              videoFrame->linesize);
//90k
    videoFrame->pts = (frameCounter++) * stream->time_base.den / (stream->time_base.num * fps);

    //videoFrame->pts = (1.0/fps) * 1000 * (frameCounter++);

    //std::cout << videoFrame->pts << " " << cctx->time_base.num << " " << cctx->time_base.den << " " << frameCounter
      //        << std::endl;

    if ((err = avcodec_send_frame(cctx, videoFrame)) < 0) {
        std::cout << "Failed to send frame" << err << std::endl;
        return;
    }
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    pkt.flags |= AV_PKT_FLAG_KEY;
    int ret = 0;
    if ((ret = avcodec_receive_packet(cctx, &pkt)) == 0) {
        static int counter = 0;
        std::cout << "pkt key: " << (pkt.flags & AV_PKT_FLAG_KEY) << " " << pkt.size << " " << (counter++) << std::endl;
        uint8_t* size = ((uint8_t*)pkt.data);
        std::cout << "first: " << (int)size[0] << " " << (int)size[1] << " " << (int)size[2] << " " << (int)size[3]
                  << " " << (int)size[4] << " " << (int)size[5] << " " << (int)size[6] << " " << (int)size[7]
                  << std::endl;

        av_interleaved_write_frame(ofctx, &pkt);
    }
    std::cout << "push: " << ret << std::endl;
    av_packet_unref(&pkt);
}

static void finish()
{
    // DELAYED FRAMES
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    for (;;) {
        avcodec_send_frame(cctx, NULL);
        if (avcodec_receive_packet(cctx, &pkt) == 0) {
            av_interleaved_write_frame(ofctx, &pkt);
            std::cout << "final push: " << std::endl;
        } else {
            break;
        }
    }

    av_packet_unref(&pkt);

    av_write_trailer(ofctx);
    if (!(oformat->flags & AVFMT_NOFILE)) {
        int err = avio_close(ofctx->pb);
        if (err < 0) {
            std::cout << "Failed to close file" << err << std::endl;
        }
    }
}

static void free()
{
    if (videoFrame) {
        av_frame_free(&videoFrame);
    }

    if (cctx) {
        avcodec_free_context(&cctx);
    }
    if (ofctx) {
        avformat_free_context(ofctx);
    }
    if (swsCtx) {
        sws_freeContext(swsCtx);
    }
}

int main(int argc, char* argv[])
{
    av_register_all();
    avcodec_register_all();

    oformat = av_guess_format(nullptr, "test.webm", nullptr);
    if (!oformat) {
        std::cout << "can't create output format" << std::endl;
        return -1;
    }
    //oformat->video_codec = AV_CODEC_ID_H265;

    int err = avformat_alloc_output_context2(&ofctx, oformat, nullptr, "test.webm");

    if (err) {
        std::cout << "can't create output context" << std::endl;
        return -1;
    }

    AVCodec* codec = nullptr;

    codec = avcodec_find_encoder(oformat->video_codec);
    if (!codec) {
        std::cout << "can't create codec" << std::endl;
        return -1;
    }

    stream = avformat_new_stream(ofctx, codec);

    if (!stream) {
        std::cout << "can't find format" << std::endl;
        return -1;
    }

    cctx = avcodec_alloc_context3(codec);

    if (!cctx) {
        std::cout << "can't create codec context" << std::endl;
        return -1;
    }

    stream->codecpar->codec_id = oformat->video_codec;
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->width = width;
    stream->codecpar->height = height;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;
    stream->codecpar->bit_rate = bitrate * 1000;
    stream->avg_frame_rate = (AVRational){fps, 1};
    avcodec_parameters_to_context(cctx, stream->codecpar);
    cctx->time_base = (AVRational){1, 1};
    cctx->max_b_frames = 2;
    cctx->gop_size = 12;
    cctx->framerate = (AVRational){fps, 1};

    if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
        av_opt_set(cctx, "preset", "ultrafast", 0);
    } else if (stream->codecpar->codec_id == AV_CODEC_ID_H265) {
        av_opt_set(cctx, "preset", "ultrafast", 0);
    }
    else
    {
        av_opt_set_int(cctx, "lossless", 1, 0);
    }

    avcodec_parameters_from_context(stream->codecpar, cctx);

    if ((err = avcodec_open2(cctx, codec, NULL)) < 0) {
        std::cout << "Failed to open codec" << err << std::endl;
        return -1;
    }

    if (!(oformat->flags & AVFMT_NOFILE)) {
        if ((err = avio_open(&ofctx->pb, "test.webm", AVIO_FLAG_WRITE)) < 0) {
            std::cout << "Failed to open file" << err << std::endl;
            return -1;
        }
    }

    if ((err = avformat_write_header(ofctx, NULL)) < 0) {
        std::cout << "Failed to write header" << err << std::endl;
        return -1;
    }

    av_dump_format(ofctx, 0, "test.webm", 1);
    std::cout << stream->time_base.den  << " " << stream->time_base.num << std::endl;

    uint8_t* frameraw = new uint8_t[1920 * 1080 * 4];
    memset(frameraw, 222, 1920 * 1080 * 4);
    for (int i = 0; i < 5; ++i) {
        pushFrame(frameraw);
        usleep(5000000);
    }

    delete[] frameraw;
    finish();
    free();
    return 0;
}
