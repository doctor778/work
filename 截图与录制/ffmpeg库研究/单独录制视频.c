/*
	使用步骤：
	命令行输入 get_buf /media/sda1/1.jpg  得到jpg数据
	命令行输入 recorded_vidio /media/sda1/1.avi 得到录制的avi文件
*/

static unsigned char* jpg_buffer=NULL;
static size_t jpg_size;
#include <kernel/lib/console.h>
static int get_buf(int argc,char *argv[])
{
    FILE* file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror("Failed to open file");
        return -1;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    jpg_size = ftell(file);
    rewind(file); // 回到文件开头

    // 分配内存来存储文件内容
    if(jpg_buffer ==NULL){
        jpg_buffer = (unsigned char*)malloc(jpg_size);
        if (jpg_buffer == NULL) {
            perror("Failed to allocate memory");
            fclose(file);
            return -1;
        }
    }
    
    // 读取文件内容到内存
    size_t bytes_read = fread(jpg_buffer, 1, jpg_size, file);
    if (bytes_read != jpg_size) {
        perror("Failed to read file");
        free(jpg_buffer);
        fclose(file);
        return -1;
    }
    fclose(file);

    puts(" get_buf success \n");
}
CONSOLE_CMD(get_buf, NULL, get_buf, CONSOLE_CMD_MODE_SELF, "set rotation.")



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

static int recorded_video(int argc,char *argv[])
{
    // 初始化 FFmpeg 库
    int ret=avformat_network_init();
    if(ret){
        printf("initialization failure \n");
        return -1;
    }

    // 创建输出上下文
    AVFormatContext *output_ctx = NULL;
    AVOutputFormat *ofmt = av_guess_format("avi", NULL, NULL);
    if (!ofmt) {
        fprintf(stderr, "无法获取输出格式\n");
        return -1;
    }
    avformat_alloc_output_context2(&output_ctx, ofmt, NULL, argv[1]);
    if (!output_ctx) {
        fprintf(stderr, "无法创建输出上下文\n");
        return -1;
    }

    // 创建视频流
    AVStream *out_stream = avformat_new_stream(output_ctx, NULL);
    if (!out_stream) {
        fprintf(stderr, "无法创建输出流\n");
        return -1;
    }

    // 设置编码器
    AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        fprintf(stderr, "找不到编码器\n");
        return -1;
    }

    AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "无法分配编码器上下文\n");
        return -1;
    }

    // 设置编码器参数
    codec_ctx->codec_id = AV_CODEC_ID_MJPEG;
    codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    codec_ctx->width = 1280;  // 设置图像宽度
    codec_ctx->height = 720; // 设置图像高度
    codec_ctx->time_base = (AVRational){1, 15}; // 帧率 15fps
    codec_ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;

    // 打开编码器
    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "无法打开编码器\n");
        return -1;
    }

    // 将编码器参数复制到视频流中
    avcodec_parameters_from_context(out_stream->codecpar, codec_ctx);

    // 打开输出文件
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_ctx->pb, argv[1], AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "无法打开输出文件\n");
            return -1;
        }
    }

    // 写入文件头
    if (avformat_write_header(output_ctx, NULL) < 0) {
        fprintf(stderr, "写入文件头失败\n");
        return -1;
    }

    // 准备 AVPacket
    AVPacket *pkt=av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "无法分配 AVPacket\n");
        return -1;
    }

    // av_init_packet(&pkt);
    pkt->data = NULL;
    pkt->size = 0;

    // 设置视频流的时间基
    // out_stream->time_base = codec_ctx->time_base;
printf("codec_ctx->time_base.num =%d\n",codec_ctx->time_base.num);
printf("codec_ctx->time_base.den =%d\n",codec_ctx->time_base.den);
    // 初始化帧计数器
    int64_t pts = 0; // 用于记录当前帧的时间戳
    for (int i = 0; i < 30; i++) { // 写入 30 帧 JPEG 数据
        av_new_packet(pkt, jpg_size);
        memcpy(pkt->data, jpg_buffer, jpg_size);
        
        // 根据时间基设置 PTS 和 DTS，时间戳单位是时间基的倍数
        pkt->pts = pts;
        pkt->dts = pts;
        pkt->duration = 1; // 设置持续时间（可以根据需要调整）
        // pkt->duration = av_rescale_q(1, codec_ctx->time_base, out_stream->time_base); // 设置持续时间
        pkt->pos = -1; // 不记录数据位置

        // 写入帧
        if (av_interleaved_write_frame(output_ctx, pkt) < 0) {
            fprintf(stderr, "写入帧失败\n");
            av_packet_free(&pkt);
            return -1;
        }

         // 更新时间戳，每帧增加 1 个时间基单位（1/30 秒）
        pts += av_rescale_q(1, (AVRational){1, 15}, out_stream->time_base);
        printf("pts =%d\n",pts);
        av_packet_unref(pkt); // 取消引用，以便重用
    }

    // 写入文件尾
    av_write_trailer(output_ctx);

    // 清理
    if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_ctx->pb);
    }
    avcodec_free_context(&codec_ctx);
    avformat_free_context(output_ctx);
    av_packet_free(&pkt);

    return 0;
}
CONSOLE_CMD(recorded_video, NULL, recorded_video, CONSOLE_CMD_MODE_SELF, "set rotation.")

