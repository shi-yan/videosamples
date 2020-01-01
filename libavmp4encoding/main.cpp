#include <iostream>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

AVFrame *videoFrame = nullptr;
AVCodecContext *cctx = nullptr;
SwsContext *swsCtx = nullptr;
int frameCounter = 0;
AVFormatContext *ofctx = nullptr;
AVOutputFormat *oformat = nullptr;
int fps = 30;

static void pushFrame(uint8_t *data){
    //cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    int err;
    if (!videoFrame) {

        videoFrame = av_frame_alloc();
        videoFrame->format = AV_PIX_FMT_YUV420P;
        videoFrame->width = cctx->width;
        videoFrame->height = cctx->height;

        if ((err = av_frame_get_buffer(videoFrame, 32)) < 0) {
            std::cout <<  "Failed to allocate picture" << err << std::endl;
            return;
        }
    }

    if (!swsCtx) {
        swsCtx = sws_getContext(cctx->width, cctx->height, AV_PIX_FMT_RGB24, cctx->width, cctx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, 0, 0, 0);
    }

    int inLinesize[1] = { 3 * cctx->width };

    // From RGB to YUV
    sws_scale(swsCtx, (const uint8_t * const *)&data, inLinesize, 0, cctx->height, videoFrame->data, videoFrame->linesize);

    videoFrame->pts = (double)cctx->time_base.num / cctx->time_base.den * 90 * (frameCounter++);

    if ((err = avcodec_send_frame(cctx, videoFrame)) < 0) {
        std::cout << "Failed to send frame" << err <<std::endl;
        return;
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;
    pkt.flags |= AV_PKT_FLAG_KEY;
    pkt.stream_index = 0;
    pkt.pts = videoFrame->pts;
    pkt.pts = (1/30) * frameCounter; // stupid try to set pts and dts somehow... Working on this...
pkt.dts = (1/30) * (frameCounter);
pkt.pos = frameCounter-1;

    if (avcodec_receive_packet(cctx, &pkt) == 0) {
        static int counter = 0;
                if (counter == 0){
            FILE *fp = fopen("dump_first_frame1.dat", "wb");
            fwrite(pkt.data, pkt.size,1,fp);
            fclose(fp);
        }
        std::cout << "pkt key: " << (pkt.flags & AV_PKT_FLAG_KEY) <<" " << pkt.size << " " << (counter++) << std::endl;
        uint8_t *size = ((uint8_t*)pkt.data);
        std::cout << "first: " << (int)size[0] << " " << (int)size[1] << " " << (int)size[2] << " " << (int)size[3] <<" "  << (int)size[4] << " " << (int)size[5] << " " << (int)size[6] << " " << (int)size[7] << std::endl;

        av_interleaved_write_frame(ofctx, &pkt);
        av_packet_unref(&pkt);
    }
}

static void finish() {
    //DELAYED FRAMES
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.data = NULL;
    pkt.size = 0;

    for (;;) {
        avcodec_send_frame(cctx, NULL);
        if (avcodec_receive_packet(cctx, &pkt) == 0) {
            av_interleaved_write_frame(ofctx, &pkt);

            
            av_packet_unref(&pkt);
        }
        else {
            break;
        }
    }

    av_write_trailer(ofctx);
    if (!(oformat->flags & AVFMT_NOFILE)) {
        int err = avio_close(ofctx->pb);
        if (err < 0) {
            std::cout << "Failed to close file" << err <<std::endl;
        }
    }
}

static void remux() {
    AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
    int err;

    if ((err = avformat_open_input(&ifmt_ctx, "test.h264", 0, 0)) < 0) {
        std::cout << "Failed to open input file for remuxing" <<err<<std::endl;
        //goto end;
        return;
    }

    //std::cout << "=============== size: " << ifmt_ctx->extradata_size<<std::endl;

    if ((err = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
        std::cout <<"Failed to retrieve input stream information"<< err<<std::endl;
        //goto end;
        return;
    }
    if ((err = avformat_alloc_output_context2(&ofmt_ctx, NULL, NULL, "final.mp4"))) {
        std::cout << "Failed to allocate output context" << err<<std::endl;
        //goto end;
        return;
    }

    AVStream *inVideoStream = ifmt_ctx->streams[0];
    AVStream *outVideoStream = avformat_new_stream(ofmt_ctx, NULL);
    if (!outVideoStream) {
        std::cout <<"Failed to allocate output video stream"<< 0<<std::endl;
        //goto end;
        return;
    }
    outVideoStream->time_base = { 1, fps };
    avcodec_parameters_copy(outVideoStream->codecpar, inVideoStream->codecpar);
    outVideoStream->codecpar->codec_tag = 0;

    if (!(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if ((err = avio_open(&ofmt_ctx->pb, "final.mp4", AVIO_FLAG_WRITE)) < 0) {
            std::cout <<"Failed to open output file" <<  err<<std::endl;
            //goto end;
            return;
        }
    }

    if ((err = avformat_write_header(ofmt_ctx, 0)) < 0) {
        std::cout <<"Failed to write header to output file" <<err<<std::endl;
        //goto end;
        return;
    }

    AVPacket videoPkt;
    int ts = 0;
    while (true) {
        if ((err = av_read_frame(ifmt_ctx, &videoPkt)) < 0) {
            break;
        }
        videoPkt.stream_index = outVideoStream->index;
        videoPkt.pts = ts;
        videoPkt.dts = ts;
        videoPkt.duration = av_rescale_q(videoPkt.duration, inVideoStream->time_base, outVideoStream->time_base);
        ts += videoPkt.duration;
        videoPkt.pos = -1;

        static int counter = 0;

        if (counter == 0){
            FILE *fp = fopen("dump_remix_frame1.dat", "wb");
            fwrite(videoPkt.data, videoPkt.size,1,fp);
            fclose(fp);
        }
        std::cout << "pkt key: " << (videoPkt.flags & AV_PKT_FLAG_KEY) <<" " << videoPkt.size << " " << (counter++) << std::endl;
        uint8_t *size = ((uint8_t*)videoPkt.data);
        std::cout << "second: " << (int)size[0] << " " << (int)size[1] << " " << (int)size[2] << " " << (int)size[3] <<" "  << (int)size[4] << " " << (int)size[5] << " " << (int)size[6] << " " << (int)size[7] << std::endl;

        if ((err = av_interleaved_write_frame(ofmt_ctx, &videoPkt)) < 0) {
            //std::cout << "Failed to mux packet" << err << std::endl;
            av_packet_unref(&videoPkt);
            break;
        }

        av_packet_unref(&videoPkt);
    }

    av_write_trailer(ofmt_ctx);

//end:
    if (ifmt_ctx) {
        avformat_close_input(&ifmt_ctx);
    }
    if (ofmt_ctx && !(ofmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&ofmt_ctx->pb);
    }
    if (ofmt_ctx) {
        avformat_free_context(ofmt_ctx);
    }
}

static void free(){
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


int main(int argc, char *argv[])
{
    av_register_all();
    avcodec_register_all();

    oformat = av_guess_format(nullptr, "test.mp4", nullptr);
    if (!oformat)
    {
        std::cout << "can't create output format" << std::endl;
        return -1;
    }
    //oformat->video_codec = AV_CODEC_ID_H265;

    int err = avformat_alloc_output_context2(&ofctx, oformat, nullptr, "test.mp4");

    if (err)
    {
        std::cout << "can't create output context" << std::endl;
        return -1;
    }

    AVCodec *codec = nullptr;

    codec = avcodec_find_encoder(oformat->video_codec);
    if (!codec)
    {
        std::cout << "can't create codec" << std::endl;
        return -1;
    }

    AVStream *stream = avformat_new_stream(ofctx, codec);

    if (!stream)
    {
        std::cout << "can't find format" << std::endl;
        return -1;
    }

    cctx = avcodec_alloc_context3(codec);

    if (!cctx)
    {
        std::cout << "can't create codec context" << std::endl;
        return -1;
    }

    int width = 1920;
    int height = 1080;
    int fps = 30;
    int bitrate = 120000;


    stream->codecpar->codec_id = oformat->video_codec;
    stream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    stream->codecpar->width = width;
    stream->codecpar->height = height;
    stream->codecpar->format = AV_PIX_FMT_YUV420P;
    stream->codecpar->bit_rate = bitrate * 1000;
    stream->time_base = { 1, fps };

    avcodec_parameters_to_context(cctx, stream->codecpar);
    cctx->time_base = { 1, fps };
    cctx->max_b_frames = 2;
    cctx->gop_size = 12;
    if (stream->codecpar->codec_id == AV_CODEC_ID_H264) {
        av_opt_set(cctx, "preset", "ultrafast", 0);
    }
    else if (stream->codecpar->codec_id == AV_CODEC_ID_H265)
    {
        av_opt_set(cctx, "preset", "ultrafast", 0);
    }
    //cctx->extradata = stream->codecpar->extradata;
    //cctx->extradata_size = stream->codecpar->extradata_size;

    //std::cout << "extradata_size: " << stream->codecpar->extradata_size << std::endl;
   if (ofctx->oformat->flags & AVFMT_GLOBALHEADER) {

      //  cctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    avcodec_parameters_from_context(stream->codecpar, cctx);

    if ((err = avcodec_open2(cctx, codec, NULL)) < 0) {
        std::cout << "Failed to open codec" << err << std::endl;
        return -1;
    }
       std::cout << "global header ===================" << std::endl;

            std::cout << "extra data:" << cctx->extradata_size<<std::endl;


  if( cctx->extradata == NULL || cctx->extradata_size == 0 )
      {  
          std::cout << "No codec extradata available!"<<std::endl;
      }
    else
    {
        std::cout << "extra data:" << cctx->extradata_size<<std::endl;
    }

    if (!(oformat->flags & AVFMT_NOFILE)) {
        if ((err = avio_open(&ofctx->pb, "test.mp4", AVIO_FLAG_WRITE)) < 0) {
            std::cout << "Failed to open file" << err << std::endl;
            return -1;
        }
    }

    if ((err = avformat_write_header(ofctx, NULL)) < 0) {
        std::cout << "Failed to write header" << err << std::endl;
        return -1;
    }

  

    av_dump_format(ofctx, 0, "test.mp4", 1);


    uint8_t *frameraw = new uint8_t[1920*1080*4];
    memset(frameraw, 222, 1920*1080*4);
    for(int i =0;i<60;++i){
        pushFrame(frameraw);
    }

    delete [] frameraw;
    finish();
    //remux();
    free();
    return 0;
}
