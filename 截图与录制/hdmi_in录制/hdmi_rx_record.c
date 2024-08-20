// #include <generated/br2_autoconf.h>
// #include <fcntl.h>
// #include <unistd.h>
// #include <kernel/vfs.h>
// #include <stdio.h>
// #include <kernel/io.h>
// #include <getopt.h>
// #include <malloc.h>
// #include <string.h>
// #include <kernel/lib/console.h>
// #include <kernel/completion.h>
// #include <sys/ioctl.h>
// #include <sys/types.h>
// #include <hcuapi/common.h>
// #include <hcuapi/kshm.h>
// #include <hcuapi/hdmi_rx.h>
// #include <hcuapi/vidmp.h>
// #include <hcuapi/dis.h>
// #include <stdlib.h>
// #include <hcuapi/codec_id.h>
// #include <hcuapi/snd.h>
// #include <hcuapi/audsink.h>
// #include <stdlib.h>
// #include <kernel/lib/crc32.h>
// #include <hcuapi/gpio.h>
// #include <kernel/lib/fdt_api.h>
// #include <kernel/lib/libfdt/libfdt.h>
// #include <kernel/module.h>
// #include <sys/mount.h>
// #include "./avilib.h"

// // static FILE *arecfile = NULL;
// static avi_t *arecfile = NULL;

// static int hdmirx_fd = -1;
// static bool stop_read = 0;
// #include<pthread.h>
// static pthread_t rx_audio_read_thread_id = 0;
// static struct kshm_info rx_audio_read_hdl = { 0 };
// #ifdef __linux__
// static int akshm_fd = -1;
// #endif
// static int select_audio =0;  //1 是音频录制， 0是视频录制

// #include <string.h>
// struct auwave_header_tag {
// 	/* "RIFF" */
// 	char chunk_id[4];
// 	/* Saved file size, not include chunk id and chunk size these 8 bytes*/
// 	unsigned long chunk_size;
// 	/* "WAVE" */
// 	char format[4];
// };

// struct wave_format {
// 	/* "fmt " */
// 	char sub_chunk1_id[4];
// 	/* This sub chunk size, not include sub chunk1 id and size 8 bytes */
// 	unsigned long sub_chunk1_size;
// 	/* PCM = 1 */
// 	unsigned short audio_format;
// 	/* Mono = 1, stereo = 2 */
// 	unsigned short channels_num;
// 	/* Samples per second */
// 	unsigned long sample_rate;
// 	/* Average bytes per second : 
// 	 * (sample_rate *channels_num * bits_per_sample) / 8 */
// 	unsigned long byte_rate;
// 	/* (channels_num * bits_per_sample) / 8 */
// 	unsigned short block_align;
// 	unsigned short bits_per_sample;
// };

// struct wave_data {
// 	/* "data" */
// 	char sub_chunk2_id[4];
// 	/* data size */
// 	unsigned long sub_chunk2_size;
// };

// struct auwave_header {
//     struct auwave_header_tag wav_header_tag;
//     struct wave_format wav_fmt;
//     struct wave_data wav_dat;
// };

// int generate_auwave_header(struct auwave_header *header,
// 				int sample_rate, int bits_per_sample, int channels_num);

// int generate_auwave_header(struct auwave_header *header,
// 				int sample_rate, int bits_per_sample, int channels_num)
// {
// 	unsigned long header_siz = sizeof(struct auwave_header);

// 	memset(header, 0, sizeof(struct auwave_header));
// 	memcpy(header->wav_header_tag.chunk_id, "RIFF", 4);
// 	memcpy(header->wav_header_tag.format, "WAVE", 4);
// 	header->wav_header_tag.chunk_size = header_siz - 8;

// 	memcpy(header->wav_fmt.sub_chunk1_id, "fmt ", 4);
// 	header->wav_fmt.sub_chunk1_size = sizeof(struct wave_format) - 8;
// 	/* PCM = 1 */
// 	header->wav_fmt.audio_format = 1;
// 	header->wav_fmt.channels_num = channels_num;
// 	header->wav_fmt.sample_rate = sample_rate;
// 	header->wav_fmt.bits_per_sample = bits_per_sample;
// 	header->wav_fmt.byte_rate =
// 		(sample_rate * header->wav_fmt.channels_num * bits_per_sample) / 8;
// 	header->wav_fmt.block_align = (header->wav_fmt.channels_num * bits_per_sample) / 8;

// 	memcpy(header->wav_dat.sub_chunk2_id, "data", 4);
// 	header->wav_dat.sub_chunk2_size = 0;//file_siz - header_siz;

// 	return 0;
// }

// static int hdmi_rx_stop(int argc , char *argv[])
// {
//     stop_read = 1;
//     if(rx_audio_read_thread_id)
//         pthread_join(rx_audio_read_thread_id , NULL);
//     rx_audio_read_thread_id = 0;
//     if(arecfile){
//         // if(select_audio){
//         //     fclose(arecfile);   
//         //     puts("close success");
//         // }else{
//             AVI_close(arecfile);
//         // }
        
//     }
        
//     arecfile = NULL;
// #ifdef __linux__
//     if(akshm_fd >= 0)
//         close(akshm_fd);
//     akshm_fd = -1;
// #endif
//     if(hdmirx_fd >= 0)
//     {
//         ioctl(hdmirx_fd , HDMI_RX_STOP);
//         ioctl(hdmirx_fd , HDMI_RX_SET_VIDEO_ROTATE_MODE , 0);
//         close(hdmirx_fd);
//     }
//     hdmirx_fd = -1;


//     (void)argc;
//     (void)argv;
//     return 0;
// }


// static void *rx_audio_read_thread(void *args)
// {
//     int data_size = 0;
//     uint8_t *data = NULL;
//     static bool have_header=0;
//  #ifdef __linux__
//     while(!stop_read && akshm_fd >= 0)
// #else
//     while(!stop_read)
// #endif
//     {
//         AvPktHd hdr = { 0 };
//  #ifdef __linux__
//         while(read(akshm_fd, &hdr, sizeof(AvPktHd)) != sizeof(AvPktHd))
//  #else
//  		while(kshm_read((void*)&rx_audio_read_hdl, &hdr, sizeof(AvPktHd)) != sizeof(AvPktHd))
//  #endif
//         {
//             //printf("read audio hdr from kshm err\n");
//             usleep(20 * 1000);
//             if(stop_read)
//             {
//                 goto end;
//             }
//         }
//         printf("apkt size %d\n" , (int)hdr.size);
//         if(data_size < hdr.size)
//         {
//             data_size = hdr.size;
//             if(data)
//             {
//                 data = realloc(data , data_size);
//             }
//             else
//             {
//                 data = malloc(data_size);
//             }
//             if(!data)
//             {
//                 printf("no memory\n");
//                 goto end;
//             }
//         }
//  #ifdef __linux__
//         while(read(akshm_fd, data, hdr.size) != hdr.size)
//  #else
//  		while(kshm_read((void*)&rx_audio_read_hdl, data, hdr.size) != hdr.size)
//  #endif
//         {
//             //printf("read audio data from kshm err\n");
//             usleep(20 * 1000);
//             if(stop_read)
//             {
//                 goto end;
//             }
//         }

//         //printf("adata: 0x%x, 0x%x, 0x%x, 0x%x\n", data[0], data[1], data[2], data[3]);
//         if(arecfile)
//         {
//             // if(select_audio){
//             //     if(!have_header){
//             //         int snd_fd =open("/dev/sndC0i2si",O_RDWR);
//             //         if(snd_fd>=0){
//             //             struct auwave_header header={0};
//             //             struct snd_hw_info hw_info ={0};
//             //             ioctl(snd_fd,SND_IOCTL_GET_HW_INFO,&hw_info);
//             //             close(snd_fd);
//             //             generate_auwave_header(&header,
//             //                 hw_info.pcm_params.rate,hw_info.pcm_params.bitdepth,hw_info.pcm_params.channels);
//             //             fwrite(&header,sizeof(struct auwave_header),1,arecfile);
//             //         }
//             //         have_header =true;
//             //     }
//             // }
            
//             // if(select_audio){
//             //     fwrite(data , hdr.size , 1 , arecfile);
//             // }else{
//                 AVI_write_frame(arecfile,(char *)data,hdr.size,1);
//             // }
//         }
//         usleep(1000);
//     }

// end:
//     if(data)
//         free(data);


//     (void)args;


//     return NULL;
// }


// static int hdmi_rx_start(int argc , char *argv[])
// {
// 	enum HDMI_RX_VIDEO_DATA_PATH vpath = HDMI_RX_VIDEO_TO_KSHM;
// 	enum HDMI_RX_AUDIO_DATA_PATH apath = HDMI_RX_AUDIO_TO_I2SI_AND_KSHM;

// 	pthread_attr_t attr;
//     pthread_attr_init(&attr);
//     pthread_attr_setstacksize(&attr , 0x1000);

//     select_audio=atoi(argv[2]);
//     if(select_audio){
//         // arecfile = fopen("/media/sda/1.wav" , "wb");      
//         // arecfile = fopen(argv[1], "wb");   
//     }else{
//         // arecfile = fopen("/media/sda/1.mjpg" , "wb");      
//         // arecfile = fopen(argv[1], "wb");   
//             arecfile = AVI_open_output_file(argv[1]);  
//             // AVI_set_video(arecfile, 960, 402, 30.000, "mjpeg");
//             AVI_set_video(arecfile, 1280, 720, 30.000, "mjpg");
//     }

//     if(!arecfile)
//     {
//     	goto err;
//     }

//     hdmirx_fd = open("/dev/hdmi_rx" , O_RDWR);
//     if(hdmirx_fd < 0)
//     {
//         goto err;
//     }
//     ioctl(hdmirx_fd , HDMI_RX_SET_VIDEO_DATA_PATH , vpath); 
//     ioctl(hdmirx_fd , HDMI_RX_SET_AUDIO_DATA_PATH , apath);

//     if(select_audio){
//         ioctl(hdmirx_fd , HDMI_RX_AUDIO_KSHM_ACCESS , &rx_audio_read_hdl);
//     }else{
//         ioctl(hdmirx_fd , HDMI_RX_VIDEO_KSHM_ACCESS , &rx_audio_read_hdl);
//     }
    
//     printf("get audio hdl, kshm desc 0x%x\n" , (int)rx_audio_read_hdl.desc);
    
// #ifdef __linux__
//     akshm_fd = open("/dev/kshmdev" , O_RDONLY);         
//     if(akshm_fd < 0)
//     {
//     	goto err;
//     }
//     ioctl(akshm_fd , KSHM_HDL_SET , &rx_audio_read_hdl);
// #endif


//     stop_read = 0;
//     if(pthread_create(&rx_audio_read_thread_id, &attr, rx_audio_read_thread, NULL))
//     {
//         printf("audio kshm recv thread create failed\n");
//         goto err;
//     }

//     ioctl(hdmirx_fd , HDMI_RX_START);
//     printf("hdmi_rx start ok```\n");
//     return 0;
// err:
//     hdmi_rx_stop(0 , 0);
//     (void)argc;
//     (void)argv;
//     return -1;
// }

// CONSOLE_CMD(hdmi_start, NULL ,hdmi_rx_start, CONSOLE_CMD_MODE_SELF , "start hdmi rx")
// CONSOLE_CMD(hdmi_stop, NULL ,hdmi_rx_stop, CONSOLE_CMD_MODE_SELF , "stop hdmi rx")






#include <generated/br2_autoconf.h>
#include <fcntl.h>
#include <unistd.h>
#include <kernel/vfs.h>
#include <stdio.h>
#include <kernel/io.h>
#include <getopt.h>
#include <malloc.h>
#include <string.h>
#include <kernel/lib/console.h>
#include <kernel/completion.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <hcuapi/common.h>
#include <hcuapi/kshm.h>
#include <hcuapi/hdmi_rx.h>
#include <hcuapi/vidmp.h>
#include <hcuapi/dis.h>
#include <stdlib.h>
#include <hcuapi/codec_id.h>
#include <hcuapi/snd.h>
#include <hcuapi/audsink.h>
#include <stdlib.h>
#include <kernel/lib/crc32.h>
#include <hcuapi/gpio.h>
#include <kernel/lib/fdt_api.h>
#include <kernel/lib/libfdt/libfdt.h>
#include <kernel/module.h>
#include <sys/mount.h>

static FILE *arecfile = NULL;

static int hdmirx_fd = -1;
static bool stop_read = 0;
#include<pthread.h>
static pthread_t rx_audio_read_thread_id = 0;
static struct kshm_info rx_audio_read_hdl = { 0 };
#ifdef __linux__
static int akshm_fd = -1;
#endif
static int select_audio =0;  //1 是音频录制， 0是视频录制

#include <string.h>
struct auwave_header_tag {
	/* "RIFF" */
	char chunk_id[4];
	/* Saved file size, not include chunk id and chunk size these 8 bytes*/
	unsigned long chunk_size;
	/* "WAVE" */
	char format[4];
};

struct wave_format {
	/* "fmt " */
	char sub_chunk1_id[4];
	/* This sub chunk size, not include sub chunk1 id and size 8 bytes */
	unsigned long sub_chunk1_size;
	/* PCM = 1 */
	unsigned short audio_format;
	/* Mono = 1, stereo = 2 */
	unsigned short channels_num;
	/* Samples per second */
	unsigned long sample_rate;
	/* Average bytes per second : 
	 * (sample_rate *channels_num * bits_per_sample) / 8 */
	unsigned long byte_rate;
	/* (channels_num * bits_per_sample) / 8 */
	unsigned short block_align;
	unsigned short bits_per_sample;
};

struct wave_data {
	/* "data" */
	char sub_chunk2_id[4];
	/* data size */
	unsigned long sub_chunk2_size;
};

struct auwave_header {
    struct auwave_header_tag wav_header_tag;
    struct wave_format wav_fmt;
    struct wave_data wav_dat;
};

int generate_auwave_header(struct auwave_header *header,
				int sample_rate, int bits_per_sample, int channels_num);

int generate_auwave_header(struct auwave_header *header,
				int sample_rate, int bits_per_sample, int channels_num)
{
	unsigned long header_siz = sizeof(struct auwave_header);

	memset(header, 0, sizeof(struct auwave_header));
	memcpy(header->wav_header_tag.chunk_id, "RIFF", 4);
	memcpy(header->wav_header_tag.format, "WAVE", 4);
	header->wav_header_tag.chunk_size = header_siz - 8;

	memcpy(header->wav_fmt.sub_chunk1_id, "fmt ", 4);
	header->wav_fmt.sub_chunk1_size = sizeof(struct wave_format) - 8;
	/* PCM = 1 */
	header->wav_fmt.audio_format = 1;
	header->wav_fmt.channels_num = channels_num;
	header->wav_fmt.sample_rate = sample_rate;
	header->wav_fmt.bits_per_sample = bits_per_sample;
	header->wav_fmt.byte_rate =
		(sample_rate * header->wav_fmt.channels_num * bits_per_sample) / 8;
	header->wav_fmt.block_align = (header->wav_fmt.channels_num * bits_per_sample) / 8;

	memcpy(header->wav_dat.sub_chunk2_id, "data", 4);
	header->wav_dat.sub_chunk2_size = 0;//file_siz - header_siz;

	return 0;
}

/***************************************************
 * 函数原型：static int hdmi_rx_stop(int argc , char *argv[])
 * 函数功能：停止并退出录制hdmi_rx 的数据内容
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/8/20
 * 示范： hdmi_stop
 * *************************************************/
static int hdmi_rx_stop(int argc , char *argv[])
{
    stop_read = 1;
    if(rx_audio_read_thread_id)
        pthread_join(rx_audio_read_thread_id , NULL);
    rx_audio_read_thread_id = 0;
    if(arecfile){
        fclose(arecfile);   
        puts("close success");
    }
        
    arecfile = NULL;
#ifdef __linux__
    if(akshm_fd >= 0)
        close(akshm_fd);
    akshm_fd = -1;
#endif
    if(hdmirx_fd >= 0)
    {
        ioctl(hdmirx_fd , HDMI_RX_STOP);
        ioctl(hdmirx_fd , HDMI_RX_SET_VIDEO_ROTATE_MODE , 0);
        close(hdmirx_fd);
    }
    hdmirx_fd = -1;


    (void)argc;
    (void)argv;
    return 0;
}


static void *rx_audio_read_thread(void *args)
{
    int data_size = 0;
    uint8_t *data = NULL;
    static bool have_header=0;
 #ifdef __linux__
    while(!stop_read && akshm_fd >= 0)
#else
    while(!stop_read)
#endif
    {
        AvPktHd hdr = { 0 };
 #ifdef __linux__
        while(read(akshm_fd, &hdr, sizeof(AvPktHd)) != sizeof(AvPktHd))
 #else
 		while(kshm_read((void*)&rx_audio_read_hdl, &hdr, sizeof(AvPktHd)) != sizeof(AvPktHd))
 #endif
        {
            //printf("read audio hdr from kshm err\n");
            usleep(20 * 1000);
            if(stop_read)
            {
                goto end;
            }
        }
        printf("apkt size %d\n" , (int)hdr.size);
        if(data_size < hdr.size)
        {
            data_size = hdr.size;
            if(data)
            {
                data = realloc(data , data_size);
            }
            else
            {
                data = malloc(data_size);
            }
            if(!data)
            {
                printf("no memory\n");
                goto end;
            }
        }
 #ifdef __linux__
        while(read(akshm_fd, data, hdr.size) != hdr.size)
 #else
 		while(kshm_read((void*)&rx_audio_read_hdl, data, hdr.size) != hdr.size)
 #endif
        {
            //printf("read audio data from kshm err\n");
            usleep(20 * 1000);
            if(stop_read)
            {
                goto end;
            }
        }

        //printf("adata: 0x%x, 0x%x, 0x%x, 0x%x\n", data[0], data[1], data[2], data[3]);
        if(arecfile)
        {
            if(select_audio){
                if(!have_header){
                    int snd_fd =open("/dev/sndC0i2si",O_RDWR);
                    if(snd_fd>=0){
                        struct auwave_header header={0};
                        struct snd_hw_info hw_info ={0};
                        ioctl(snd_fd,SND_IOCTL_GET_HW_INFO,&hw_info);
                        close(snd_fd);
                        generate_auwave_header(&header,
                            hw_info.pcm_params.rate,hw_info.pcm_params.bitdepth,hw_info.pcm_params.channels);
                        fwrite(&header,sizeof(struct auwave_header),1,arecfile);
                    }
                    have_header =true;
                }
            }
            fwrite(data , hdr.size , 1 , arecfile);
        }
        usleep(1000);
    }

end:
    if(data)
        free(data);


    (void)args;


    return NULL;
}

/***************************************************
 * 函数原型：static int hdmi_rx_start(int argc , char *argv[])
 * 函数功能：录制hdmi_rx 的数据内容
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/8/20
 * 示范：
 *      hdmi_start /media/sda1/1.wav 1
 *      hdmi_start /media/sda1/1.avi 0     （mp4\mjpeg/mjpg...）
 * 备注：hdmi_rx 应该有数据过来，应用上不要打开hdmi_rx,因为本函数中会打开hdmi_rx
 * *************************************************/
static int hdmi_rx_start(int argc , char *argv[])
{
	enum HDMI_RX_VIDEO_DATA_PATH vpath = HDMI_RX_VIDEO_TO_KSHM;
	enum HDMI_RX_AUDIO_DATA_PATH apath = HDMI_RX_AUDIO_TO_I2SI_AND_KSHM;

	pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr , 0x1000);

    select_audio=atoi(argv[2]);
    if(select_audio){
        arecfile = fopen(argv[1], "wb");   //  media/sda/1.wav
    }else{
        arecfile = fopen(argv[1], "wb");    // media/sda/1.mjpg
    }

    if(!arecfile)
    {
    	goto err;
    }

    hdmirx_fd = open("/dev/hdmi_rx" , O_RDWR);
    if(hdmirx_fd < 0)
    {
        goto err;
    }
    ioctl(hdmirx_fd , HDMI_RX_SET_VIDEO_DATA_PATH , vpath); 
    ioctl(hdmirx_fd , HDMI_RX_SET_AUDIO_DATA_PATH , apath);

    if(select_audio){
        ioctl(hdmirx_fd , HDMI_RX_AUDIO_KSHM_ACCESS , &rx_audio_read_hdl);
    }else{
        ioctl(hdmirx_fd , HDMI_RX_VIDEO_KSHM_ACCESS , &rx_audio_read_hdl);
    }
    
    printf("get audio hdl, kshm desc 0x%x\n" , (int)rx_audio_read_hdl.desc);
    
#ifdef __linux__
    akshm_fd = open("/dev/kshmdev" , O_RDONLY);         
    if(akshm_fd < 0)
    {
    	goto err;
    }
    ioctl(akshm_fd , KSHM_HDL_SET , &rx_audio_read_hdl);
#endif


    stop_read = 0;
    if(pthread_create(&rx_audio_read_thread_id, &attr, rx_audio_read_thread, NULL))
    {
        printf("audio kshm recv thread create failed\n");
        goto err;
    }

    ioctl(hdmirx_fd , HDMI_RX_START);
    printf("hdmi_rx start ok```\n");
    return 0;
err:
    hdmi_rx_stop(0 , 0);
    (void)argc;
    (void)argv;
    return -1;
}

CONSOLE_CMD(hdmi_start, NULL ,hdmi_rx_start, CONSOLE_CMD_MODE_SELF , "start hdmi rx")
CONSOLE_CMD(hdmi_stop, NULL ,hdmi_rx_stop, CONSOLE_CMD_MODE_SELF , "stop hdmi rx")































