/*
	使用步骤：
	命令行输入 get_pcm /media/sda1/1.pcm  得到pcm数据
	命令行输入 recorded_audio /media/sda1/1.avi 得到录制的avi文件
*/

static  uint8_t* pcm_pkg=NULL;
static size_t total_len;
#include <kernel/lib/console.h>
static int get_pcm(int argc,char *argv[])  //get_pcm /media/sda1/1.pcm 
{
    FILE* file = fopen(argv[1], "rb");
    if (file == NULL) {
        perror("Failed to open file");
        return -1;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);
    total_len = ftell(file);
    rewind(file); // 回到文件开头

    // 分配内存来存储文件内容
    if(pcm_pkg ==NULL){
        pcm_pkg = (uint8_t*)malloc(total_len);
        if (pcm_pkg == NULL) {
            perror("Failed to allocate memory");
            fclose(file);
            return -1;
        }
    }
    
    // 读取文件内容到内存
    size_t bytes_read = fread(pcm_pkg, 1, total_len, file);
    if (bytes_read != total_len) {
        perror("Failed to read file");
        free(pcm_pkg);
        fclose(file);
        return -1;
    }
    fclose(file);

    puts(" get_pcm success \n");
}
CONSOLE_CMD(get_pcm, NULL, get_pcm, CONSOLE_CMD_MODE_SELF, "set rotation.")

static int recorded_audio(int argc, char *argv[])  //命令： recorded_audio /media/sda1/1.avi
{
#if 0       //测试代码，验证得到的pcm_pkg数据是否正确
    FILE * pcm_file = fopen("/media/sda1/cba.pcm", "wb"); // "wb" 表示以二进制写模式打开文件
    if (pcm_file == NULL) {
        perror("无法打开文件");
        // return 1; // 如果文件打开失败，退出程序
    }

    size_t written = fwrite(pcm_pkg, 1, total_len, pcm_file);
    if (written != total_len) {
        perror("写入文件失败");
    }else{
        puts("写入文件成功");
    }
    fclose(pcm_file);
#endif

    // 初始化 FFmpeg 库
    int ret = avformat_network_init();
    if (ret) {
        printf("initialization failure\n");
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

    // 创建音频流
    AVStream *audio_stream = avformat_new_stream(output_ctx, NULL);
    if (!audio_stream) {
        fprintf(stderr, "无法创建音频流\n");
        return -1;
    }

    // 设置音频编码器
    AVCodec *audio_codec = avcodec_find_encoder(AV_CODEC_ID_PCM_S16LE);
    if (!audio_codec) {
        fprintf(stderr, "找不到音频编码器\n");
        return -1;
    }

    AVCodecContext *audio_codec_ctx = avcodec_alloc_context3(audio_codec);
    if (!audio_codec_ctx) {
        fprintf(stderr, "无法分配音频编码器上下文\n");
        return -1;
    }

    // 设置音频编码器参数
    audio_codec_ctx->codec_id = AV_CODEC_ID_PCM_S16LE;
    audio_codec_ctx->codec_type = AVMEDIA_TYPE_AUDIO;
    audio_codec_ctx->sample_rate = 44100; // 采样率
    audio_codec_ctx->channels = 1; // 通道数，修改为单声道
    audio_codec_ctx->channel_layout = AV_CH_LAYOUT_MONO; // 修改为单声道布局
    audio_codec_ctx->sample_fmt = AV_SAMPLE_FMT_S16;
    audio_codec_ctx->time_base = (AVRational){1, audio_codec_ctx->sample_rate};

    // 打开音频编码器
    if (avcodec_open2(audio_codec_ctx, audio_codec, NULL) < 0) {
        fprintf(stderr, "无法打开音频编码器\n");
        return -1;
    }

    // 将音频编码器参数复制到音频流中
    avcodec_parameters_from_context(audio_stream->codecpar, audio_codec_ctx);

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
    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "无法分配 AVPacket\n");
        return -1;
    }

    pkt->data = NULL;
    pkt->size = 0;


    // 初始化帧计数器
    int64_t video_pts = 0; // 用于记录视频帧的时间戳
    int64_t audio_pts = 0; // 用于记录音频帧的时间戳

    // 写入音频帧
    // int frame_size = audio_codec_ctx->frame_size;
    int frame_size = 44100 *2; // 根据你的需求设置
    int samples_per_frame = frame_size * audio_codec_ctx->channels;
    int bytes_per_sample = av_get_bytes_per_sample(audio_codec_ctx->sample_fmt);
    int audio_data_size = samples_per_frame * bytes_per_sample;

    for (int i = 0; i < 3; i++) { // 写入 3次 PCM 数据
        printf("frame_size: %d\n", frame_size);
        printf("sample_rate: %d\n", audio_codec_ctx->sample_rate);
        printf("time_base: %d/%d\n", audio_stream->time_base.num, audio_stream->time_base.den);
        printf("total_len: %d\n", total_len);

        printf("av_rescale_q result=%d\n",av_rescale_q(88200, (AVRational){1, 44100}, (AVRational){1, 44100}));
        av_new_packet(pkt, total_len);
        memcpy(pkt->data, pcm_pkg, total_len);

        // 根据时间基设置 PTS 和 DTS，时间戳单位是时间基的倍数
        pkt->pts = audio_pts;
        pkt->dts = audio_pts;
        
        pkt->duration = 1;//av_rescale_q(frame_size, (AVRational){1, audio_codec_ctx->sample_rate}, audio_stream->time_base);
        printf("pkt->duration =%ld\n",pkt->duration);
        pkt->pos = -1; // 不记录数据位置

        // 写入音频帧
        pkt->stream_index = audio_stream->index;

        if (av_interleaved_write_frame(output_ctx, pkt) < 0) {
            fprintf(stderr, "写入音频帧失败\n");
            av_packet_free(&pkt);
            return -1;
        }

        // 更新音频时间戳，每帧增加 frame_size 个时间基单位（根据采样率）
        audio_pts += av_rescale_q(frame_size, (AVRational){1, audio_codec_ctx->sample_rate}, audio_stream->time_base);

        printf("Audio PTS: %ld\n", audio_pts);
        printf("Packet Size: %d\n", pkt->size);
        av_packet_unref(pkt); // 取消引用，以便重用
    }

    // 写入文件尾
    av_write_trailer(output_ctx);

    // 清理
    if (output_ctx && !(output_ctx->oformat->flags & AVFMT_NOFILE)) {
        avio_closep(&output_ctx->pb);
    }
    avcodec_free_context(&audio_codec_ctx);
    avformat_free_context(output_ctx);
    av_packet_free(&pkt);

    return 0;
}
CONSOLE_CMD(recorded_audio, NULL, recorded_audio, CONSOLE_CMD_MODE_SELF, "recorded_audio.")

