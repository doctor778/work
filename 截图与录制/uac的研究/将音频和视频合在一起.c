#define AVI_HEADER_SIZE 64
#define AVI_STREAM_HEADER_SIZE 64
#define FORMAT_HEADER_SIZE 40
static int read_avi_file(int argc, char **argv) {
    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror("Error opening file");
        return -1;
    }

    // 读取 RIFF 头
    char riffHeader[12];
    fread(riffHeader, 1, 12, file);
    printf("RIFF Header: %.4s\n", riffHeader);

    // 读取 AVI 头
    fseek(file, 12, SEEK_SET); // 跳过 RIFF 头
    char aviHeader[AVI_HEADER_SIZE];
    fread(aviHeader, 1, AVI_HEADER_SIZE, file);
    printf("AVI Header: %.4s\n", aviHeader);

    // 读取视频流头
    fseek(file, 12 + AVI_HEADER_SIZE, SEEK_SET); // 跳过 RIFF 头和 AVI 头
    char streamHeader[AVI_STREAM_HEADER_SIZE];
    fread(streamHeader, 1, AVI_STREAM_HEADER_SIZE, file);
    printf("Stream Header: %.4s\n", streamHeader);

    // 读取格式头（如果存在）
    char formatHeader[FORMAT_HEADER_SIZE];
    fread(formatHeader, 1, FORMAT_HEADER_SIZE, file);
    printf("Format Header: %.4s\n", formatHeader);

     // 检测是否有音轨
    // 假设音频流头在视频流头之后
    fseek(file, 12 + AVI_HEADER_SIZE + AVI_STREAM_HEADER_SIZE + FORMAT_HEADER_SIZE, SEEK_SET);
    char audioStreamHeader[AVI_STREAM_HEADER_SIZE];
    fread(audioStreamHeader, 1, AVI_STREAM_HEADER_SIZE, file);

    // 检查音频流头标识
    if (strncmp(audioStreamHeader, "strh", 4) == 0) {
        printf("Audio Stream Header: %.4s\n", audioStreamHeader);

        // 读取音频格式头
        char audioFormatHeader[FORMAT_HEADER_SIZE];
        fread(audioFormatHeader, 1, FORMAT_HEADER_SIZE, file);
        printf("Audio Format Header: %.4s\n", audioFormatHeader);
    }else{
        puts("没有音轨 ");
    }

    // 读取视频数据（示例中略）
    // 可以继续读取视频数据块和索引块（如果有）

    fclose(file);
}


#define AUDIO_FILE "/media/mmcblk0p2/111.pcm"
#define VIDEO_FILE "/media/mmcblk0p2/1.avi"
#define OUTPUT_FILE "/media/mmcblk0p2/2.avi"
static int aaa(int argc, char **argv)
{
    FILE *audioFile = fopen(AUDIO_FILE, "rb");
    if(!audioFile){
        perror("audioFile opening files");
    }
    FILE *videoFile = fopen(VIDEO_FILE, "rb");
    if(!videoFile){
        perror("videoFile opening files");
    }
    FILE *outputFile = fopen(OUTPUT_FILE, "wb");
    if(!outputFile){
        perror("outputFile opening files");
    }

    if (!audioFile || !videoFile || !outputFile) {
        perror("Error opening files");
        return 1;
    }

    // 读取视频文件头部（假设不需要处理）
    char buffer[2048];
    size_t bytesRead;
puts("视频文件开始读");
    // 读取视频文件内容到输出文件
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), videoFile)) > 0) {
        fwrite(buffer, 1, bytesRead, outputFile);
    }
puts("视频文件读完");
    // 读取音频文件内容到输出文件
    while ((bytesRead = fread(buffer, 1, sizeof(buffer), audioFile)) > 0) {
        fwrite(buffer, 1, bytesRead, outputFile);
    }
puts("音频文件读完");

    fclose(audioFile);
    fclose(videoFile);
    fclose(outputFile);

    printf("Files have been merged successfully.\n");
    return 0;
}

// #include "avformat.h"
#include "libavformat/avformat.h"
#define PCM_DATA_SIZE 4096
#define TARGET_FRAME_RATE 30
static int mixture(int argc, char **argv)
{
    const char *video_filename = "/media/mmcblk0p2/1.avi";
    const char *audio_filename = "/media/mmcblk0p2/111.pcm";
    const char *output_filename = "/media/mmcblk0p2/3.avi";
    
    AVFormatContext *video_ctx = NULL;
    AVFormatContext *output_ctx = NULL;
    AVStream *video_stream = NULL;
    AVStream *out_video_stream = NULL;
    AVStream *out_audio_stream = NULL;
    AVPacket pkt;
    int ret;
    FILE *audio_file = NULL;
    uint8_t audio_buffer[PCM_DATA_SIZE];
    int64_t audio_pts = 0;

      printf("FFmpeg version: %s\n", av_version_info());  //FFmpeg version: 4.4
    // 初始化库
    ret=avformat_network_init();
    if(ret){
        printf("initialization failure \n");
    }

    // 打开视频文件
    if (avformat_open_input(&video_ctx, video_filename, NULL, NULL) < 0) {
        fprintf(stderr, "Unable to open video file.\n");
        return -1;
    }

    // 获取流信息
    if (avformat_find_stream_info(video_ctx, NULL) < 0) {
        fprintf(stderr, "Failed to retrieve stream info.\n");
        return -1;
    }

  //创建输出文件
    if ((ret=avformat_alloc_output_context2(&output_ctx, NULL, NULL, "/media/mmcblk0p2/3.avi") )< 0) {
        fprintf(stderr, "Creation failure\n");
        return -1;
    }
  // 打开视频流
    for (int i = 0; i < video_ctx->nb_streams; i++) {
        if (video_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream = video_ctx->streams[i];
            break;
        }
    }
    // 将视频流添加到输出文件
    out_video_stream = avformat_new_stream(output_ctx, NULL);
    if (!out_video_stream) {
        fprintf(stderr, "Creation failure fail\n");
        return -1;
    }
    avcodec_parameters_copy(out_video_stream->codecpar, video_stream->codecpar);
    
    // 设置目标帧率
    out_video_stream->r_frame_rate = (AVRational){TARGET_FRAME_RATE, 1};
    // 设置time_base使其符合30fps的要求
    out_video_stream->time_base = (AVRational){1, TARGET_FRAME_RATE}; 

  // 将音频流添加到输出文件
      out_audio_stream = avformat_new_stream(output_ctx, NULL);
    if (!out_audio_stream) {
        fprintf(stderr, "Could not allocate output audio stream\n");
        return -1;
    }

  // 假设 PCM 音频参数
    out_audio_stream->time_base = (AVRational){1, 44100}; // 假设采样率为 44100
    out_audio_stream->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    out_audio_stream->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
    out_audio_stream->codecpar->sample_rate = 44100;
    out_audio_stream->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
    out_audio_stream->codecpar->channels = 1;
    out_audio_stream->codecpar->bits_per_coded_sample = 16;
  // 打开输出文件
    if (!(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&output_ctx->pb, output_filename, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "avio_open fail\n");
            return -1;
        }
    }
// 写入输出文件的头部
    if (avformat_write_header(output_ctx, NULL) < 0) {
        fprintf(stderr, "avformat_write_header fail\n");
        return -1;
    }
// 打开 PCM 音频文件
    audio_file = fopen(audio_filename, "rb");
    if (!audio_file) {
        fprintf(stderr, "fopen fail\n");
        return -1;
    }
// 读取 PCM 数据并写入到输出文件
    while (1) {
        size_t bytes_read = fread(audio_buffer, 1, sizeof(audio_buffer), audio_file);
        if (bytes_read <= 0)
            break;
// 创建音频包
        AVPacket *audio_pkt = av_packet_alloc();
        if (!audio_pkt) {
            fprintf(stderr, "Could not allocate AVPacket\n");
            break;
        }
         // 复制音频数据
        audio_pkt->data = (uint8_t*)av_malloc(bytes_read);
        if (!audio_pkt->data) {
            fprintf(stderr, "Could not allocate audio data\n");
            av_packet_free(&audio_pkt);
            break;
        }
        memcpy(audio_pkt->data, audio_buffer, bytes_read);
        audio_pkt->size = bytes_read;
        audio_pkt->stream_index = out_audio_stream->index;

        // 计算duration
        int sample_size = 2; // 每个样本的字节数
        int channels = 1; // 通道数
        int samples_per_packet = bytes_read / (sample_size * channels);     //样本总数
        audio_pkt->duration = samples_per_packet * (AV_TIME_BASE / out_audio_stream->codecpar->sample_rate);    //音频包的持续时间

        // 更新时间戳
        audio_pkt->pts = audio_pts;     //呈现时间戳
        audio_pkt->dts = audio_pts;     //解码时间戳
        audio_pts += samples_per_packet;

        printf("audio_pkt->pts =%lu  audio_pts=%lu \n",audio_pkt->pts,audio_pts);//audio_pkt->pts =2166876280  audio_pts=0 
        // 写入音频包   
        ret = av_interleaved_write_frame(output_ctx, audio_pkt);
        if (ret < 0) {
            fprintf(stderr, "Error while writing audio frame\n");
        }
// 释放 AVPacket
        av_free(audio_pkt->data);
        av_packet_free(&audio_pkt);
    }
    fclose(audio_file);
// 读取并写入视频数据
    while (1) {
        if (av_read_frame(video_ctx, &pkt) < 0)
            break;

        if (pkt.stream_index == video_stream->index) {
            pkt.stream_index = out_video_stream->index;
            pkt.pts = av_rescale_q(pkt.pts, video_stream->time_base, out_video_stream->time_base);
            pkt.dts = av_rescale_q(pkt.dts, video_stream->time_base, out_video_stream->time_base);
            pkt.duration = av_rescale_q(pkt.duration, video_stream->time_base, out_video_stream->time_base);
            ret = av_interleaved_write_frame(output_ctx, &pkt);
            if (ret < 0) {
                fprintf(stderr, "Error while writing video frame\n");
            }
        }
        av_packet_unref(&pkt);
    }
// 写入输出文件的尾部
    av_write_trailer(output_ctx);
// 清理
    avformat_close_input(&video_ctx);
     if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_ctx->pb);
    avformat_free_context(output_ctx);
    return 0;
}
CONSOLE_CMD(mixture, NULL, mixture, CONSOLE_CMD_MODE_SELF, "libuac example")
CONSOLE_CMD(aaa, NULL, aaa, CONSOLE_CMD_MODE_SELF, "libuac example")
CONSOLE_CMD(read_avi_file, NULL, read_avi_file, CONSOLE_CMD_MODE_SELF, "libuac example")
