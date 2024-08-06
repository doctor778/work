#include <kernel/elog.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hcuapi/common.h>
#include <hcuapi/kshm.h>
#include <hcuapi/auddec.h>
#include <hcuapi/viddec.h>
#include <hcuapi/vidmp.h>
#include <hcuapi/codec_id.h>
#include <sys/ioctl.h>
#include <hcuapi/snd.h>
#include <hcuapi/dis.h>
#include <hcuapi/fb.h>

#include <errno.h>
#include <kernel/delay.h>
#include <libuvc/libuvc.h>
#include <kernel/lib/console.h>
#include <pthread.h>
#include <lvgl/hc-porting/hc_lvgl_init.h>
#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lvgl/demos/lv_demos.h"

#include "./avilib.h"
#include "com_api.h"

extern lv_obj_t *main_page_scr;
#define ALOGI log_i
#define ALOGE log_e
#define OSD_DEV      "/dev/fb0"
#define DIS_DEV      "/dev/dis"


#define TILE_ROW_SWIZZLE_MASK 3

static avi_t *avi_file = NULL;
static FILE *mjpeg_file= NULL;
static uvc_device_handle_t *devh;
static control_msg_t msg_ret = {0}; 
static pthread_t camera_thread_id;
static uvc_context_t *ctx;
static uvc_device_t *dev;
static void *decode_hld;
static lv_obj_t * child_camera_screen;
static lv_obj_t * osd_ui;
static lv_obj_t * img_pat;
static pthread_mutex_t g_mutex ;
struct timespec pthread_ts;
static pthread_cond_t cover_cond = PTHREAD_COND_INITIALIZER;
static int osd_width=1280;   //osd层的宽为 1280
static int osd_height=720;   //osd层的高为  720
static unsigned char *osd_buf = NULL;
static unsigned char *dis_buf = NULL;
static unsigned char *dis_buf2=NULL;
static unsigned char *osd_dis_buf=NULL;
static unsigned char *bmp_buf = NULL;
static lv_img_dsc_t bmp_src;
static bool write_flag=false;

static uint32_t dis_width;
static uint32_t dis_height;
static uint32_t bmp_buf_size;
struct mjpg_decoder {
    struct video_config cfg;
    int fd;
};

typedef struct {
    unsigned char a;
    unsigned char r;
    unsigned char g;
    unsigned char b;
} ARGBPixel;

static int c_bt709_yuvL2rgbF[3][3] =
{ {298,0 ,  459},
  {298,-55 ,-136},
  {298,541,0} };

typedef struct {    
    char signature[2];
    uint32_t filesize;
    uint32_t reserved;
    uint32_t data_offset;
} __attribute__((packed)) bmp_header_t;

typedef struct {    
    uint32_t info_size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_ppm;
    int32_t y_ppm;
    uint32_t colors;
    uint32_t important_colors;
} __attribute__((packed)) bmp_info_header_t;

static int camera_deinit(void);
__attribute__((weak)) void set_aspect_mode(dis_tv_mode_e ratio,
                       dis_mode_e dis_mode)
{
}
static void *mjpg_decode_init(int width, int height, int fps, uint8_t *extradata, int size,
               int8_t rotate_enable)
{
    printf(" width=%d    height=%d  fps=%d \n",width,height,fps);

    struct mjpg_decoder *p = malloc(sizeof(struct mjpg_decoder));
    memset(&p->cfg, 0, sizeof(struct video_config));

    set_aspect_mode(DIS_TV_16_9, DIS_PILLBOX);

    p->cfg.codec_id = HC_AVCODEC_ID_MJPEG;
    p->cfg.sync_mode = 0;
    ALOGI("video sync_mode: %d\n", p->cfg.sync_mode);
    p->cfg.decode_mode = VDEC_WORK_MODE_KSHM;

    p->cfg.combine_enable=0; 
    p->cfg.pic_width = width;
    p->cfg.pic_height = height;
    p->cfg.frame_rate = fps * 1000;
    ALOGE("frame_rate: %d\n", (int)p->cfg.frame_rate);

    p->cfg.pixel_aspect_x = 1;
    p->cfg.pixel_aspect_y = 1;
    p->cfg.preview = 0;
    // p->cfg.pbp_mode = VIDEO_PBP_2P_ON; 
    // p->cfg.dis_type = DIS_TYPE_HD;  
    // p->cfg.dis_layer = DIS_LAYER_AUXP; 
    p->cfg.codec_tag=MKTAG('m', 'j', 'p', 'g'); 
    p->cfg.extradata_size = size;
    memcpy(p->cfg.extra_data, extradata, size);

    p->cfg.rotate_enable = rotate_enable;
    ALOGI("video rotate_enable: %d\n", p->cfg.rotate_enable);

    p->fd = open("/dev/viddec", O_RDWR);
    if (p->fd < 0) {
        ALOGE("Open /dev/viddec error.");
        return NULL;
    }

    if (ioctl(p->fd, VIDDEC_INIT, &(p->cfg)) != 0) {
        ALOGE("Init viddec error.");
        close(p->fd);
        free(p);
        return NULL;
    }

    ioctl(p->fd, VIDDEC_START, 0);
    ALOGI("fd: %d\n", p->fd);
    return p;
}

static int mjpg_decode(void *phandle, uint8_t *video_frame, size_t packet_size,
        uint32_t rotate_mode)
{
    struct mjpg_decoder *p = (struct mjpg_decoder *)phandle;
    AvPktHd pkthd = { 0 };
    pkthd.pts = -1;
    pkthd.dur = 0;
    pkthd.size = packet_size;
    pkthd.flag = AV_PACKET_ES_DATA;
    pkthd.video_rotate_mode = rotate_mode;

    // ALOGI("video_frame: %p, packet_size: %d", video_frame, packet_size);
    if (write(p->fd, (uint8_t *)&pkthd, sizeof(AvPktHd)) !=
        sizeof(AvPktHd)) {
        ALOGE("Write AvPktHd fail\n");
        return -1;
    }

    if (write(p->fd, video_frame, packet_size) != (int)packet_size) {
        ALOGE("Write video_frame error fail\n");
        float tmp = 0;
        ioctl(p->fd, VIDDEC_FLUSH, &tmp);
        return -1;
    }
    return 0;
}

static void uvc_cb(uvc_frame_t *frame, void *ptr)
{
    uvc_error_t ret;
    //   printf("callback! frame_format = %d, width = %ld, height = %ld, length = %lu, ptr = %p\n",
    //     frame->frame_format, frame->width, frame->height, frame->data_bytes, ptr);
    switch (frame->frame_format) {
    case UVC_FRAME_FORMAT_H264:
        break;
    case UVC_COLOR_FORMAT_MJPEG:
        mjpg_decode(decode_hld, frame->data, frame->data_bytes, 0);
	if(write_flag == true){
            if (mjpeg_file) {
                fwrite(frame->data, frame->data_bytes, 1, mjpeg_file);
            } 
        
            if(avi_file){
                AVI_write_frame(avi_file,(char *)frame->data,frame->data_bytes,1);
            }
        }
	
        break;
    case UVC_COLOR_FORMAT_YUYV:
        break;
    default:
        break;
    }
}

static void mjpg_decoder_destroy(void *phandle)
{
    struct mjpg_decoder *p = (struct mjpg_decoder *)phandle;
    set_aspect_mode(DIS_TV_AUTO, DIS_PILLBOX);
    if (!p)
        return;

    if (p->fd > 0) {
        close(p->fd);
    }
    free(p);
}

static int camera_deinit(void)
{
    camera_thread_id=0;
    if(devh){
        uvc_stop_streaming(devh);
        printf("Done streaming.");

        uvc_close(devh);
        printf("Device closed");

        devh=NULL;
    }
    if(dev){
        uvc_unref_device(dev);
        dev=NULL;
    }
    if(ctx){
        uvc_exit(ctx);
        printf("UVC exited");
        ctx=NULL;
    }
    if(decode_hld){
        mjpg_decoder_destroy(decode_hld);
        printf("mjpg display decoder destroy");
        decode_hld=NULL;
    }
    lv_scr_load_anim(main_page_scr, LV_SCR_LOAD_ANIM_MOVE_TOP, 0, 0, false);
}

static void *open_camera(void *arg)
{
    uvc_stream_ctrl_t ctrl;
    uvc_error_t res;

    res = uvc_init(&ctx, NULL);     
    if (res < 0) {
        uvc_perror(res, "uvc_init");
        camera_deinit();
        return NULL;
    }else{
        printf("UVC initialized");
    }

    res = uvc_find_device(  
        ctx, &dev, 0, 0,
        NULL);
    if (res < 0) {
        uvc_perror(res, "uvc_find_device");
        camera_deinit();
        return NULL;
    }else{
        printf("Device found");
    }
    
    res = uvc_open(dev, &devh);
    if (res < 0) {
        uvc_perror(res, "uvc_open"); 
        camera_deinit();
        return NULL;
    }else{
        printf("Device opened");
    }

    uvc_print_diag(devh, stderr); 

    const uvc_format_desc_t *format_desc =uvc_get_format_descs(devh);
    const uvc_frame_desc_t *frame_desc =format_desc->frame_descs;
    enum uvc_frame_format frame_format;
    int width = 640;
    int height = 480;
    int fps = 30;

    switch (format_desc->bDescriptorSubtype) {
    case UVC_VS_FORMAT_MJPEG:
        frame_format = UVC_COLOR_FORMAT_MJPEG;
        break;
    case UVC_VS_FORMAT_FRAME_BASED:
        frame_format = UVC_FRAME_FORMAT_H264;
        break;
    default:
        frame_format = UVC_FRAME_FORMAT_YUYV;
        break;
    }

    if (frame_desc) {
        width = frame_desc->wWidth;
        height = frame_desc->wHeight;
        fps = 10000000 /
                frame_desc->dwDefaultFrameInterval;
    }
    fps=30;
    width=1280;
    height=720;
    printf("\nFirst format: (%4s) %dx%d %dfps\n",format_desc->fourccFormat, width, height, fps);  //width、height是摄像头的分辨率，fps是帧率

    res = uvc_get_stream_ctrl_format_size(
        devh, &ctrl, 
        frame_format, width, height,
        fps /* width, height, fps */
    );
    if (res < 0) {
        uvc_perror(res, "get_mode"); /* device doesn't provide a matching stream */
    }

    uvc_print_stream_ctrl(&ctrl, stderr);

    decode_hld = mjpg_decode_init(width, height, fps, NULL, 0, 0);
    if (!decode_hld) {
        printf("cannot init mjpg decode\n");
        camera_deinit();
        return NULL;
    }
    printf("mjpg display decoder init");

    res = uvc_start_streaming(devh, &ctrl, uvc_cb, NULL, 0);    //uvc_stop_streaming(devh);
    if (res < 0) {
        uvc_perror(
            res,
            "start_streaming"); 
    }else{
        printf("Streaming...");
    }

    printf("Enabling auto exposure ...");
    const uint8_t
        UVC_AUTO_EXPOSURE_MODE_AUTO = 2;
    res = uvc_set_ae_mode(
        devh,
        UVC_AUTO_EXPOSURE_MODE_AUTO);
    if (res == UVC_SUCCESS) {
        printf(" ... enabled auto exposure");
    } else if (res == UVC_ERROR_PIPE) {
        printf(" ... full AE not supported, trying aperture priority mode");
        const uint8_t
            UVC_AUTO_EXPOSURE_MODE_APERTURE_PRIORITY =
                8;
        res = uvc_set_ae_mode(
            devh,
            UVC_AUTO_EXPOSURE_MODE_APERTURE_PRIORITY);
        if (res < 0) {
            uvc_perror(
                res,
                " ... uvc_set_ae_mode failed to enable aperture priority mode");
        } else {
            printf(" ... enabled aperture priority auto exposure mode");
        }
    } else {
        uvc_perror(
            res,
            " ... uvc_set_ae_mode failed to enable auto exposure mode");
    }
    return NULL;
}

/***************************************************
 * 函数原型：static int uvc_open_camera(int argc,char *argv[])
 * 函数功能：打开摄像头
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int uvc_open_camera(int argc,char *argv[])
{   
    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_type = MSG_TYPE_CHILD_CAMERA_INIT;
    api_control_send_msg(&msg_ret);
    
    struct dis_layer_blend_order vhance = { 0 };
	int fd;
	
	fd = open("/dev/dis" , O_WRONLY);
	if(fd < 0) {
		return -1;
	}
	vhance.distype = DIS_TYPE_HD;
    vhance.gmas_layer = 3;
    vhance.gmaf_layer = 2;
    vhance.main_layer = 1;	
    vhance.auxp_layer = 0;
	ioctl(fd , DIS_SET_LAYER_ORDER , &vhance);
	close(fd);

    if(!camera_thread_id){
        pthread_attr_t attr_camera;
        pthread_attr_init(&attr_camera);
        pthread_attr_setstacksize(&attr_camera, 0x2000);
        pthread_attr_setdetachstate(&attr_camera, PTHREAD_CREATE_DETACHED);
        if(pthread_create(&camera_thread_id, &attr_camera, open_camera, NULL)) {
            printf("pthread_create is fail\n");
            return -1;
        }
    }  
}

/***************************************************
 * 函数原型：static int uvc_close_camera(int argc,char *argv[])
 * 函数功能：关闭摄像头
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int uvc_close_camera(int argc,char *argv[])
{   
    camera_deinit();
}

/***************************************************
 * 函数原型：static int camera_start_record(int argc,char *argv[])
 * 函数功能：开始录制，可以生成mjpeg格式文件，或者 avi格式文件
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * 示例：camera_start_record /media/sda/1.mjpeg  或者  camera_start_record /media/sda/1.avi 
 * *************************************************/
static int camera_start_record(int argc,char *argv[])
{   
    #if 0
    if(argc !=2){
        printf("Enter: camera_start_record /media/sda/1.mjpeg \n");
        return -1;
    }
    mjpeg_file = fopen(argv[1], "wb");
    if(mjpeg_file){
        printf("File created successfully \n");
    }else{
        printf("File created fail\n");
    }
    #else
    if(argc !=2){
        printf("Enter: camera_start_record /media/sda/1.avi \n");
        return -1;
    }
    avi_file = AVI_open_output_file(argv[1]);
    if(avi_file == NULL)
    {
        printf("out_file fail!!\n");
        return -1;
    }
    AVI_set_video(avi_file, 1280, 720, 30.000, "mjpg");
    #endif
    write_flag=true;
}


/***************************************************
 * 函数原型：static int camera_stop_record(int argc,char *argv[])
 * 函数功能：停止录制
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int camera_stop_record(int argc,char *argv[])
{   
    write_flag=false;
#if 0
    if(mjpeg_file){
        if (fclose(mjpeg_file) == 0) {
            printf("File closed successfully\n");
        } else {
            printf("File closing failure\n");
        }
        mjpeg_file=NULL;
    }
#else
    if(avi_file){
        AVI_close(avi_file);
        avi_file=NULL;
    }
#endif
}

/***************************************************
 * 函数原型：static int crop_osd_layer(void)  
 * 函数功能：截osd层图，argb数据保存到osd_buf中
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int crop_osd_layer(void)  
{    
    if(!osd_buf){
        printf(" osd_buf malloc\n");
        osd_buf=malloc(osd_width * osd_height *4);
        memset(osd_buf, 0, osd_width * osd_height *4); 
    }

    int infd = open(OSD_DEV, O_RDONLY);
    if(infd <0){
        printf("open fail \n");
        return -1;
    }

    int sizenum = read(infd, osd_buf, osd_width * osd_height *4);
    if (sizenum != osd_width * osd_height *4) {
        printf("read sizenum =%d\n",sizenum);
    }

    close(infd);
}

static int dis_get_display_info(struct dis_display_info *display_info )
{
    int fd = -1;

    fd = open("/dev/dis" , O_WRONLY);
    if(fd < 0)
    {
        return -1;
    }

    display_info->distype = DIS_TYPE_HD;
    ioctl(fd , DIS_GET_DISPLAY_INFO , display_info);
    close(fd);
    printf("w:%lu h:%lu\n", (long unsigned int)display_info->info.pic_width, (long unsigned int)display_info->info.pic_height);
    printf("y_buf:0x%lx size = 0x%lx\n" , (long unsigned int)display_info->info.y_buf , (long unsigned int)display_info->info.y_buf_size);
    printf("c_buf:0x%lx size = 0x%lx\n" , (long unsigned int)display_info->info.c_buf , (long unsigned int)display_info->info.c_buf_size);
    printf("display_info->info.de_map_mode :%d\n",display_info->info.de_map_mode);
    return 0;
}

static inline int swizzle_tile_row(int dy)
{
    return (dy & ~TILE_ROW_SWIZZLE_MASK) | ((dy & 1) << 1) | ((dy & 2) >> 1);
}

static inline int calc_offset_mapping1(int stride_in_tile , int x , int y)
{
    int tile_x = x >> 4;
    const int tile_y = y >> 5;
    const int dx = x & 15;
    const int dy = y & 31;
    const int sdy = swizzle_tile_row(dy);
    stride_in_tile >>= 4;
    if(tile_x == stride_in_tile)
        tile_x--;
    return (stride_in_tile * tile_y + tile_x) * 512 + (sdy << 4) + dx;
}

static inline int calc_offset_mapping0(int stride_in_tile , int x , int y)
{
    return (y >> 4) * (stride_in_tile << 4) + (x >> 5) * 512 + (y & 15) * 32 + (x & 31);
}

static void get_yuv(unsigned char *p_buf_y ,
                    unsigned char *p_buf_c ,
                    int ori_width ,
                    int ori_height ,
                    int src_x ,
                    int src_y ,
                    unsigned char *y ,
                    unsigned char *u ,
                    unsigned char *v ,
                    int tile_mode)
{
    int width = 0;
    (void)ori_height;
    unsigned int src_offset = 0;

    if(tile_mode == 1)
    {
        width = (ori_width + 15) & 0xFFFFFFF0;

        src_offset = calc_offset_mapping1(width , src_x , src_y);
        *y = *(p_buf_y + src_offset);

        src_offset = calc_offset_mapping1(width , (src_x & 0xFFFFFFFE) , src_y >> 1);
        *u = *(p_buf_c + src_offset);
        *v = *(p_buf_c + src_offset + 1);
    }
    else
    {
        width = (ori_width + 31) & 0xFFFFFFE0;
        src_offset = calc_offset_mapping0(width , src_x , src_y);
        *y = *(p_buf_y + src_offset);
        src_offset = calc_offset_mapping0(width , (src_x & 0xFFFFFFFE) , src_y >> 1);
        *u = *(p_buf_c + src_offset);
        *v = *(p_buf_c + src_offset + 1);
    }
}

static int Clamp8(int v)
{
    if(v < 0)
    {
        return 0;
    }
    else if(v > 255)
    {
        return 255;
    }
    else
    {
        return v;
    }
}

static int Trunc8(int v)
{
    return v >> 8;
}

static void pixel_ycbcrL_to_argbF(unsigned char y , unsigned char cb , unsigned char cr ,
                                  unsigned char *r , unsigned char *g , unsigned char *b ,
                                  int c[3][3])  
{
    int  red = 0 , green = 0 , blue = 0;

    const int y1 = (y - 16) * c[0][0];
    const int pr = cr - 128;
    const int pb = cb - 128;
    red = Clamp8(Trunc8(y1 + (pr * c[0][2])));
    green = Clamp8(Trunc8(y1 + (pb * c[1][1] + pr * c[1][2])));
    blue = Clamp8(Trunc8(y1 + (pb * c[2][1])));

    *r = red;
    *g = green;
    *b = blue;
}

static void YUV420_RGB(unsigned char *p_buf_y ,
                unsigned  char *p_buf_c ,
                unsigned char *p_buf_out ,
                int ori_width ,
                int ori_height,
                int tile_mode)
{
    unsigned char *p_out = NULL;
    unsigned char r , g , b;
    unsigned char cur_y , cur_u , cur_v;

    int x = 0;
    int y = 0;

    p_out = p_buf_out;

    for(y = 0;y < ori_height;y++)
    {
        for(x = 0;x < ori_width;x++)
        {
            get_yuv(p_buf_y , p_buf_c ,
                    ori_width , ori_height ,
                    x , y ,
                    &cur_y , &cur_u , &cur_v ,
                    tile_mode);
            
            pixel_ycbcrL_to_argbF(cur_y , cur_u , cur_v , &r , &g , &b , c_bt709_yuvL2rgbF);
            *p_out++ = b;
            *p_out++ = g;
            *p_out++ = r;
            *p_out++ = 0xFF;
        }
    }
}

/***************************************************
 * 函数原型：static int get_dis_layer_data(void) 
 * 函数功能：截视频层图，argb数据保存到dis_buf中
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int get_dis_layer_data(void) 
{
  
    int buf_size = 0;
    void *buf = NULL;
    int ret = 0;
    unsigned char *p_buf_y = NULL;
    unsigned char *p_buf_c = NULL;
    unsigned char *buf_rgb = NULL;
    int fd = -1;

    struct dis_display_info display_info = { 0 };

    fd = open(DIS_DEV , O_RDWR);
    if(fd < 0)
    {
        return -1;
    }

    ret = dis_get_display_info(&display_info);
    if(ret <0)
    {
        return -1;
    }

    dis_width=display_info.info.pic_width;
    dis_height=display_info.info.pic_height;

    if(!dis_width || !dis_height){
        return -1;
    }

    buf_size = dis_width * dis_height *4;
    buf_rgb = malloc(buf_size);
    memset(buf_rgb, 0, buf_size); 

    printf("buf =0x%x buf_rgb = 0x%x rgb_buf_size = 0x%x\n", (int)buf, (int)buf_rgb, buf_size);

    unsigned char *copy_p_buf_y = malloc(display_info.info.y_buf_size);
    memcpy(copy_p_buf_y, (unsigned char *)(display_info.info.y_buf), display_info.info.y_buf_size);

    unsigned char *copy_p_buf_c = malloc(display_info.info.c_buf_size);
    memcpy(copy_p_buf_c, (unsigned char *)(display_info.info.c_buf), display_info.info.c_buf_size);

    YUV420_RGB(copy_p_buf_y,
            copy_p_buf_c,
            buf_rgb ,
            display_info.info.pic_width ,
            display_info.info.pic_height,
            display_info.info.de_map_mode);

    if(copy_p_buf_y){
        free(copy_p_buf_y);
        copy_p_buf_y=NULL;
    }

    if(copy_p_buf_c){
        free(copy_p_buf_c);
        copy_p_buf_c=NULL;
    }

    if(!dis_buf){
        dis_buf=malloc(buf_size);
        memset(dis_buf, 0, buf_size);
        printf("malloc dis_buf \n");
        
    }
    if(buf_rgb != NULL){
        memcpy(dis_buf, buf_rgb, buf_size);
    }   
    printf("buf_size =%d\n",buf_size);
    close(fd);

    if(buf_rgb){
        free(buf_rgb);
        buf_rgb=NULL;
    }

    return 0;
}

/***************************************************
 * 函数原型：static int merge_raw_files(int width,int height )
 * 函数功能：将2个相同大小的分辨率，合并成一个
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int merge_raw_files(int width,int height ) 
{
    printf("sizeof(ARGBPixel)*width * height =%ld\n",sizeof(ARGBPixel)*width * height);
    
    ARGBPixel *osd_data =(ARGBPixel *)osd_buf;
    ARGBPixel *dis_data=NULL;
    if(dis_buf){
        dis_data=(ARGBPixel *)dis_buf;
    }else if(dis_buf2){
        dis_data =(ARGBPixel *)dis_buf2;
    }else{
        printf(" dis buf and dis buf2 do not exist \n");
        return -1;
    }
    
    for (int i = 0; i < width * height; ++i) {
        if (osd_data[i].a == 0) {
            osd_data[i] = dis_data[i];
        }
    }

    if(dis_buf){
        free(dis_buf);
        printf("free dis_buf \n");
        dis_buf=NULL;
        dis_data=NULL;
    }else if(dis_buf2){
        free(dis_buf2);
        printf("free dis_buf2 \n");
        dis_buf2=NULL;
        dis_data=NULL;
    }

    if(!osd_dis_buf){
        osd_dis_buf=malloc(osd_width * osd_height *4);
        memset(osd_dis_buf, 0, osd_width * osd_height *4);
        printf("malloc osd_dis_buf \n");
    }
    if(osd_data !=NULL){
        memcpy(osd_dis_buf, osd_data, osd_width * osd_height *4);
    }
        
    if(osd_buf){
        free(osd_buf);
        printf("free osd_buf \n");
        osd_data=NULL;
        osd_buf=NULL;
    }
    printf("Image processed successfully.\n");
    return 0;
}

static void osd_zoom_animation(int x, int y, int w, int h){
    int fd_fb = open( "/dev/fb0" , O_RDWR);
    if (fd_fb < 0) {
        printf("%s(), line:%d. open device: %s error!\n", 
            __func__, __LINE__,  "/dev/fb0");
        return;
    }
    int x1 = x*OSD_MAX_WIDTH/w;
    int y1 = y*OSD_MAX_HEIGHT/h;
    hcfb_lefttop_pos_t start_pos={x1,y1}; 
    hcfb_scale_t scale_param={1280,720,w,h};    
    ioctl(fd_fb, HCFBIOSET_SET_LEFTTOP_POS, &start_pos);
    ioctl(fd_fb, HCFBIOSET_SCALE, &scale_param); 

    close(fd_fb);
}

static ARGBPixel bilinear_interpolation(ARGBPixel tl, ARGBPixel tr, ARGBPixel bl, ARGBPixel br, float x_ratio, float y_ratio) {
    ARGBPixel result;
    result.r = (unsigned char)((tl.r * (1 - x_ratio) * (1 - y_ratio)) + (tr.r * x_ratio * (1 - y_ratio)) + (bl.r * (1 - x_ratio) * y_ratio) + (br.r * x_ratio * y_ratio));
    result.g = (unsigned char)((tl.g * (1 - x_ratio) * (1 - y_ratio)) + (tr.g * x_ratio * (1 - y_ratio)) + (bl.g * (1 - x_ratio) * y_ratio) + (br.g * x_ratio * y_ratio));
    result.b = (unsigned char)((tl.b * (1 - x_ratio) * (1 - y_ratio)) + (tr.b * x_ratio * (1 - y_ratio)) + (bl.b * (1 - x_ratio) * y_ratio) + (br.b * x_ratio * y_ratio));
    result.a = (unsigned char)((tl.a * (1 - x_ratio) * (1 - y_ratio)) + (tr.a * x_ratio * (1 - y_ratio)) + (bl.a * (1 - x_ratio) * y_ratio) + (br.a * x_ratio * y_ratio));
    return result;
}

/***************************************************
 * 函数原型：static int resize_with_bilinear_interpolation(unsigned char *input_data, unsigned char *output_data,
                                       int input_width, int input_height,
                                       int output_width, int output_height)
 * 函数功能：将任意分辨率的图像缩放为指定的分辨率
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int resize_with_bilinear_interpolation(unsigned char *input_data, unsigned char *output_data,
                                       int input_width, int input_height,
                                       int output_width, int output_height) {
    float x_ratio = (float)input_width / output_width;
    float y_ratio = (float)input_height / output_height;
    
    for (int y = 0; y < output_height; ++y) {
        for (int x = 0; x < output_width; ++x) {
            float x_src = x * x_ratio;
            float y_src = y * y_ratio;
            int x_int = (int)x_src;
            int y_int = (int)y_src;
            float x_frac = x_src - x_int;
            float y_frac = y_src - y_int;
            
            int idx_tl = (y_int * input_width + x_int) * sizeof(ARGBPixel);
            int idx_tr = (y_int * input_width + x_int + 1) * sizeof(ARGBPixel);
            int idx_bl = ((y_int + 1) * input_width + x_int) * sizeof(ARGBPixel);
            int idx_br = ((y_int + 1) * input_width + x_int + 1) * sizeof(ARGBPixel);
            
            ARGBPixel tl = *(ARGBPixel *)(input_data + idx_tl);
            ARGBPixel tr = *(ARGBPixel *)(input_data + idx_tr);
            ARGBPixel bl = *(ARGBPixel *)(input_data + idx_bl);
            ARGBPixel br = *(ARGBPixel *)(input_data + idx_br);
            
            ARGBPixel result = bilinear_interpolation(tl, tr, bl, br, x_frac, y_frac);
            
            int idx_out = (y * output_width + x) * sizeof(ARGBPixel);
            memcpy(output_data + idx_out, &result, sizeof(ARGBPixel));
        }
    }
    
    return 0;
}

/***************************************************
 * 函数原型：static int animation_effect(void)
 * 函数功能：动画效果
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * *************************************************/
static int animation_effect(void)
{
    memset(&bmp_src,0,sizeof(lv_img_dsc_t));
    bmp_src.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    bmp_src.header.w = osd_width;
    bmp_src.header.h = osd_height;
    bmp_src.data = osd_dis_buf;
    bmp_src.data_size = bmp_buf_size-54;

    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=3;
    msg_ret.msg_type = MSG_TYPE_CHILD_CAMERA_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);
    dis_screen_info_t dis_area={0};
    int fd;
    fd = open("/dev/dis", O_WRONLY);
    if(fd < 0){
        return -1 ;
    }
    dis_area.distype=1;
    ioctl(fd , DIS_GET_SCREEN_INFO, &dis_area);
    close(fd);
    
    printf("dis_area->area.x =%d\n",dis_area.area.x);
    printf("dis_area->area.y =%d\n",dis_area.area.y);
    printf("dis_area->area.w =%d\n",dis_area.area.w);
    printf("dis_area->area.h =%d\n",dis_area.area.h);

    int i=0;
    int animation_width=dis_area.area.w/2;
    int animation_height=dis_area.area.h/2;
    int x=0;
    int y=0;
    int width_one_tenth=0;
    int height_one_tenth=0;
    
    osd_zoom_animation(dis_area.area.w/4,dis_area.area.h/4,animation_width,animation_height);
    usleep(10*1000);
    for(i=0;i<9;i++){
        printf("\n i=%d \n",i);
        
        width_one_tenth=animation_width/(10-i);
        height_one_tenth=animation_height/(10-i);

        animation_width=animation_width-width_one_tenth;
        animation_height=animation_height-height_one_tenth;

        // x=(1920-animation_width/2)-animation_width;
        x=(dis_area.area.w-animation_width/2)-animation_width;
        y=animation_height/2;
        printf("x =%d\n",x);
        printf("y =%d\n",y);
        osd_zoom_animation(x,y,animation_width,animation_height);
        usleep(10*1000);
    }

    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=4;
    msg_ret.msg_type = MSG_TYPE_CHILD_CAMERA_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);

    osd_zoom_animation(0,0,dis_area.area.w,dis_area.area.h);

    printf("exit animation_effect \n");
}
/***************************************************
 * 函数原型：static int screenshot(int argc,char *argv[])
 * 函数功能：截图
 * 形参：argc ：
 *      argv ：
 * 返回值:int
 * 作者： lxw--2024/7/19
 * 示例：screenshot /media/sda/test.jpg
 * *************************************************/
static int screenshot(int argc,char *argv[])
{   
    if(argc !=2){
        printf("Enter: screenshot /media/sda/test.jpg \n");
        return -1;
    }

    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=1;
    msg_ret.msg_type = MSG_TYPE_CHILD_CAMERA_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);

    crop_osd_layer();

    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=2;
    msg_ret.msg_type = MSG_TYPE_CHILD_CAMERA_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);

    get_dis_layer_data();

    if(dis_width != osd_width || dis_height != osd_height){   //如果osd层分辨率 不等于 视频层分辨率
        printf("If the osd layer resolution is not equal to the video layer resolution\n");
        if(!dis_buf2){
            dis_buf2=malloc(osd_width*osd_height*4);
            memset(dis_buf2, 0, osd_width*osd_height*4); 
            printf("malloc dis_buf2 \n");
        }
        resize_with_bilinear_interpolation(dis_buf,dis_buf2,dis_width,dis_height,osd_width,osd_height);
        if(dis_buf){
            free(dis_buf);
            dis_buf=NULL;
            printf("free dis_buf \n");
        }
        merge_raw_files(osd_width,osd_height);
    }else{
        printf("If the osd layer resolution is equal to the video layer resolution\n");
        merge_raw_files(osd_width,osd_height);
    }
    
    bmp_header_t header = {
        .signature="BM",
        .filesize = sizeof(bmp_header_t) + sizeof(bmp_info_header_t) + osd_width * osd_height * 4,
        .reserved = 0,
        .data_offset = sizeof(bmp_header_t) + sizeof(bmp_info_header_t)
    };

    bmp_info_header_t info_header = {
        .info_size = sizeof(bmp_info_header_t),
        .width = osd_width,
        .height = osd_height,
        .planes = 1,
        .bpp = 32,
        .compression = 0,
        .image_size = osd_width * osd_height * 4,
        .x_ppm = 0,
        .y_ppm = 0,
        .colors = 0,
        .important_colors = 0
    };

    if(!bmp_buf){
        bmp_buf_size=(sizeof(header))+(sizeof(info_header))+(sizeof(uint32_t))*osd_width*osd_height;
        printf("bmp_buf_size =%d\n",bmp_buf_size);
        bmp_buf=malloc(bmp_buf_size);
        memset(bmp_buf, 0, bmp_buf_size); 
        printf("malloc bmp_buf \n");
    }

    memcpy(bmp_buf, &header, sizeof(header));
    memcpy(bmp_buf + sizeof(header), &info_header, sizeof(info_header));

    int bytes_per_row = sizeof(uint32_t) * osd_width;
    size_t copy_offset = sizeof(header) + sizeof(info_header);
    size_t max_copy_bytes = sizeof(header) + sizeof(info_header) + sizeof(uint32_t) * osd_width * osd_height;

    for(int y = 0; y < osd_height; y++) {
        if (copy_offset + (osd_height - 1 - y) * bytes_per_row + bytes_per_row > max_copy_bytes) {
            printf("Error: memcpy position out of bounds\n");
            return -1;
        }
        memcpy(bmp_buf + sizeof(header) + sizeof(info_header) + (osd_height - 1 - y) * bytes_per_row, (uint32_t *)osd_dis_buf + y * osd_width, bytes_per_row);
    }

    animation_effect();

    if(osd_dis_buf){
        printf("free osd_dis_buf \n");
        free(osd_dis_buf);
        osd_dis_buf=NULL;
    }

    FILE* bmp_file = fopen(argv[1], "wb");
    if (!bmp_file) {
        printf("Error opening BMP file for reading.\n");
        if(bmp_buf){
            free(bmp_buf);
            bmp_buf=NULL;
            printf("free bmp_buf \n");
        }
        return -1;
    }
    fwrite(bmp_buf, bmp_buf_size, 1,bmp_file);
    fclose(bmp_file);

    if(bmp_buf){
        free(bmp_buf);
        bmp_buf=NULL;
        printf("free bmp_buf \n");
    }
}
void child_camera_message_process(control_msg_t * ctl_msg) 
{
    if(ctl_msg->msg_type == MSG_TYPE_CHILD_CAMERA_INIT){
        if(!child_camera_screen){
            child_camera_screen=lv_obj_create(NULL);
            lv_obj_set_size(child_camera_screen,LV_PCT(100),LV_PCT(100));
            lv_obj_set_align(child_camera_screen, LV_ALIGN_CENTER); 
            lv_obj_set_scrollbar_mode(child_camera_screen,LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_border_width(child_camera_screen, 0, 0);
            lv_obj_set_style_pad_all(child_camera_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT); 
            lv_obj_clear_flag(child_camera_screen, LV_OBJ_FLAG_SCROLLABLE);   
            lv_obj_set_style_outline_width(child_camera_screen,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(child_camera_screen,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(child_camera_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(child_camera_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_scr_load_anim(child_camera_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 0, 0, false);

        if(!osd_ui && child_camera_screen){
            osd_ui =lv_obj_create(child_camera_screen);
            lv_obj_set_size(osd_ui,LV_PCT(100),LV_PCT(31));
            lv_obj_set_align(osd_ui, LV_ALIGN_BOTTOM_MID); 
            lv_obj_set_style_bg_color (osd_ui, lv_color_hex(0x323232), LV_PART_MAIN|LV_STATE_DEFAULT);
            lv_obj_set_scrollbar_mode(osd_ui,LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_border_width(osd_ui, 0, 0);
            lv_obj_set_style_pad_all(osd_ui, 0, LV_PART_MAIN | LV_STATE_DEFAULT); 
            lv_obj_clear_flag(osd_ui, LV_OBJ_FLAG_SCROLLABLE);   
            lv_obj_set_style_outline_width(osd_ui,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(osd_ui,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(osd_ui, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(osd_ui, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(osd_ui,LV_OBJ_FLAG_HIDDEN);
        }

        if(!img_pat && child_camera_screen){
            img_pat=lv_img_create (child_camera_screen);
            lv_obj_set_align(img_pat, LV_ALIGN_CENTER); 
            lv_obj_clear_flag(img_pat, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(img_pat, 0, 0);
            lv_obj_set_style_bg_opa(img_pat, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(img_pat,LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if(ctl_msg->msg_type == MSG_TYPE_CHILD_CAMERA_SCREENSHOT){
        if(ctl_msg->msg_code == 1){
            if(osd_ui){
                lv_obj_clear_flag(osd_ui,LV_OBJ_FLAG_HIDDEN);
            }
        }
        else if(ctl_msg->msg_code == 2){
            if(osd_ui){
                lv_obj_add_flag(osd_ui,LV_OBJ_FLAG_HIDDEN);
            }
        }
        else if(ctl_msg->msg_code == 3){
            if(img_pat){
                lv_obj_align(img_pat,LV_ALIGN_CENTER,0,0);
                lv_obj_set_size (img_pat, osd_width, osd_height);
                lv_img_set_src (img_pat, &bmp_src);
                lv_obj_clear_flag(img_pat,LV_OBJ_FLAG_HIDDEN);
            }
            lv_task_handler();
        }
        else if(ctl_msg->msg_code == 4){
            if(img_pat){
                lv_obj_add_flag(img_pat,LV_OBJ_FLAG_HIDDEN);
                lv_task_handler();
            }
        }
        
        pthread_mutex_lock(&g_mutex);
        pthread_cond_signal(&cover_cond);
        pthread_mutex_unlock(&g_mutex);
    }
}

CONSOLE_CMD(uvc_open_camera, NULL, uvc_open_camera, CONSOLE_CMD_MODE_SELF, "Turn on the camera")
CONSOLE_CMD(uvc_close_camera, NULL, uvc_close_camera, CONSOLE_CMD_MODE_SELF, "Turn off the camera")
CONSOLE_CMD(camera_start_record, NULL, camera_start_record, CONSOLE_CMD_MODE_SELF, "The camera is recording.")
CONSOLE_CMD(camera_stop_record, NULL, camera_stop_record, CONSOLE_CMD_MODE_SELF, "The camera stops recording.")
CONSOLE_CMD(screenshot, NULL, screenshot, CONSOLE_CMD_MODE_SELF, "truncated operation.")

