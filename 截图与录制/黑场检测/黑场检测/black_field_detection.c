#ifdef __linux__
#include <signal.h>
#include <termios.h>
#else
#include <kernel/lib/console.h>
#endif
#include "com_api.h"
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <hcuapi/dis.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <assert.h>
#include <hcuapi/fb.h>

#define DIS_DEV      "/dev/dis"
#define TILE_ROW_SWIZZLE_MASK 3
#define sampleValue  50     //   抽样检测的范围，用户自己设定
#define BLACK_THRESHOLD 25 //判定黑色的标准，用户自己设定
#define FRAME_DELAY 3   //帧间延时时长，用户自己设定
static unsigned char *dis_buf = NULL;  
static uint32_t dis_width;
static uint32_t dis_height;
static uint32_t copy_c_buf_size;
static sem_t blackout_thread_wait_sem;
static int blackout_check_run = 0;
static bool blackout_check_thread_wake_flag=false;
static pthread_t blackout_check_thread_id;
static unsigned char *prev_dis_buf;
static uint32_t prev_buf_size;
static uint32_t dis_buf_size;
static int debounce_time;
static uint32_t  dark_spot;
static uint32_t  brightness_value;

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
   
            *p_out++ = cur_y;
            *p_out++ = cur_u;
            *p_out++ = cur_v;
        }
    }
}

static int get_dis_layer_data(void) 
{
  
    int buf_size = 0;
    void *buf = NULL;
    int ret = 0;
    unsigned char *p_buf_y = NULL;
    unsigned char *p_buf_c = NULL;
    unsigned char *buf_yuv = NULL;
    int fd = -1;

    struct dis_display_info display_info = { 0 };
    
    fd = open("/dev/dis" , O_RDWR);
    if(fd < 0)
    {
        return -1;
    }

    ret = dis_get_display_info(&display_info);
    if(ret <0)
    {
        if(fd>0){
            close(fd);
        }
        return -1;
    }

    dis_width=display_info.info.pic_width;
    dis_height=display_info.info.pic_height;

    if(!dis_width || !dis_height){
        if(fd>0){
            close(fd);
        }
        return -1;
    }

#ifdef __linux__
    uint32_t y_buf_offset = (unsigned long)display_info.info.y_buf % 4096;
    buf_size = display_info.info.y_buf_size *3;
    dis_buf_size=buf_size;
    buf = mmap(0 , buf_size , PROT_READ | PROT_WRITE , MAP_SHARED , fd , 0);
    buf_yuv = malloc(buf_size);
    memset(buf_yuv, 0, buf_size); 

    p_buf_y = buf + y_buf_offset;
    p_buf_c = p_buf_y + (display_info.info.c_buf - display_info.info.y_buf);
    printf("buf =0x%x buf_yuv = 0x%x rgb_buf_size = 0x%x\n", (int)buf, (int)buf_yuv, buf_size);

    unsigned char * copy_p_buf_y = malloc(display_info.info.y_buf_size);
    memset(copy_p_buf_y,0,sizeof(display_info.info.y_buf_size));
    memcpy(copy_p_buf_y, (unsigned char *)p_buf_y, display_info.info.y_buf_size);

    unsigned char * copy_p_buf_c = malloc(display_info.info.c_buf_size);
    memset(copy_p_buf_c,0,sizeof(display_info.info.c_buf_size));
    memcpy(copy_p_buf_c, (unsigned char *)p_buf_c, display_info.info.c_buf_size);

#else
    buf_size = dis_width * dis_height *3;
    dis_buf_size=buf_size;
    buf_yuv = malloc(buf_size);
    memset(buf_yuv, 0, buf_size); 
    printf("buf =0x%x buf_yuv = 0x%x rgb_buf_size = 0x%x\n", (int)buf, (int)buf_yuv, buf_size);

    unsigned char * copy_p_buf_y = malloc(display_info.info.y_buf_size);
    memset(copy_p_buf_y,0,sizeof(display_info.info.y_buf_size));
    memcpy(copy_p_buf_y, (unsigned char *)(display_info.info.y_buf), display_info.info.y_buf_size);

    unsigned char * copy_p_buf_c = malloc(display_info.info.c_buf_size);
    memset(copy_p_buf_c,0,sizeof(display_info.info.c_buf_size));
    memcpy(copy_p_buf_c, (unsigned char *)(display_info.info.c_buf), display_info.info.c_buf_size);
#endif

    YUV420_RGB(copy_p_buf_y,
        copy_p_buf_c,
        buf_yuv ,
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

    if(buf_yuv != NULL){
        memcpy(dis_buf, buf_yuv, buf_size);
    }   
    
    if(buf_yuv){
        free(buf_yuv);
        buf_yuv=NULL;
    }

    if(fd>0){
        close(fd);
    }
    
    return 0;

}

static void get_screen_brightness(void)
{
    hcfb_enhance_t eh = {0};
    int fd = -1;
    int ret = 0;
    fd = open("/dev/fb0" , O_RDWR);
    if( fd < 0){
        printf("open /dev/fb0 failed, ret=%d\n", fd);
    }

    ret = ioctl(fd, HCFBIOGET_ENHANCE, &eh);
    if( ret != 0 ){
        printf("%s:%d: warning: HCFBIOGET_ENHANCE failed\n", __func__, __LINE__);
        close(fd);
        fd = -1;
    }

    printf("eh.brightness =%d\n",eh.brightness);
    brightness_value=eh.brightness;
    close(fd);
}

static int set_screen_brightness(int brightness)
{
    printf("Perform brightness reduction \n");
    hcfb_enhance_t eh = {0};
    struct dis_video_enhance vhance = { 0 };
    int fd = -1;
    int ret = 0;

    fd = open("/dev/fb0" , O_RDWR);
    if( fd < 0){
        printf("open /dev/fb0 failed, ret=%d\n", fd);
    }
    ret = ioctl(fd, HCFBIOGET_ENHANCE, &eh);
    if( ret != 0 ){
        printf("%s:%d: warning: HCFBIOGET_ENHANCE failed\n", __func__, __LINE__);
        close(fd);
        fd = -1;
    }
    eh.brightness=brightness;
    close(fd);

    fd = open("/dev/fb0" , O_RDWR);
    if( fd < 0){
        printf("open /dev/fb0 failed, ret=%d\n", fd);
    }
    ret = ioctl(fd, HCFBIOSET_ENHANCE, &eh);
    if( ret != 0 ){
        printf("%s:%d: warning: HCFBIOSET_ENHANCE failed\n", __func__, __LINE__);
        close(fd);
        fd = -1;
    }
    printf("eh.brightness =%d\n",eh.brightness);
    close(fd);

    
    fd = open("/dev/dis" , O_RDWR);
    if( fd < 0){
        printf("open /dev/dis failed, ret=%d\n", fd);
    }
    vhance.distype = DIS_TYPE_HD;
    vhance.enhance.enhance_type = DIS_VIDEO_ENHANCE_BRIGHTNESS;
    vhance.enhance.grade = eh.brightness;
    ret = ioctl(fd, DIS_SET_VIDEO_ENHANCE , &vhance);
    if(ret != 0){
        printf("%s:%d: warning: HCFBIOSET_VENHANCE failed\n", __func__, __LINE__);
        close(fd);
    }
    close(fd);
}

static void restore_screen_brightness(void)
{
    set_screen_brightness(brightness_value);
}

static int is_pixel_black(int x,int y)
{
    int spot_width=x+dark_spot;
    int spot_height=y+dark_spot;

    printf("x=%d,y=%d,spot_width =%d,spot_height =%d \n",x,y,spot_width,spot_height);
    
    if(!prev_dis_buf){
        return -1;
    }
    for(;y<spot_height;y++){    
        for(x=spot_width-dark_spot ;x< spot_width;x++){
            unsigned char *pixel = prev_dis_buf + (y * dis_width + x) * 3;

            if(pixel[0]<=BLACK_THRESHOLD && pixel[1]==128 && pixel[2]==128){
                ;
            }else{
                printf("Not a black field:%s  %d \n",__func__,__LINE__);
                return -1;
            }
        }
    }
    return 0;
}

static int is_black(void)
{
    int cell_width=dis_width/3;    
    int cell_height=dis_height/3;

    printf("dis_width =%d \n",dis_width);
    printf("cell_width =%d \n",cell_width);
    int x=0;
    int y=0;
    dark_spot=sampleValue;
    if(dark_spot > cell_width || dark_spot>cell_height){
        printf("dark_spot is larger than the length or width of the cell \n");
        if(cell_height > cell_width){
            dark_spot=cell_width;
        }else{
            dark_spot=cell_height;
        }
    }

    int x_1=(cell_width-dark_spot)/2;   
    int y_1=(cell_height-dark_spot)/2; 

    bool black_flag =true;
    for(int i =1;i<10;i++){		//对一帧的数据进行抽样检测，检测是不是黑色。
        switch(i){
            case 1:
                x=x_1;
                y=y_1;
                break;
            case 2:
                x=x_1 + cell_width;   
                y=y_1;
                break;
            case 3:
                x=x_1 + cell_width*2;
                y=y_1;
                break;
            case 4:
                x=x_1;
                y=y_1 + cell_height;
                break;
            case 5:
                x=x_1 + cell_width;
                y=y_1 + cell_height;
                break;
            case 6:
                x=x_1 + cell_width*2;
                y=y_1 + cell_height;
                break;
            case 7:
                x=x_1 ;
                y=y_1 + cell_height*2;
                break;
            case 8:
                x=x_1 + cell_width;
                y=y_1 + cell_height*2;
                break;
            case 9:
                x=x_1 + cell_width*2;
                y=y_1 + cell_height*2;
                break;
            default:
                printf("switch error %s %d\n",__func__,__LINE__);
                break;
        }
        int ret=is_pixel_black(x,y);	//判断这一块的数据是不是黑色
        if(ret <0){
            black_flag =false;
            break;
        }
    }

    if(black_flag == false){
        printf("Not a black field \n");
    }else{
        printf("is Black field \n");
        int brightness=0;   //设置要降低的亮度值
        set_screen_brightness(brightness);//降低亮度
    }
}


static void *blackout_check_thread(void *arg)
{
    get_screen_brightness();        //获取亮度

    int result=0;
    debounce_time=0;
    while(blackout_check_run){
        if(blackout_check_thread_wake_flag == false){
            sem_wait(&blackout_thread_wait_sem);
        }
        
        if(blackout_check_run == 0){
            goto end;
        }
        
        result=get_dis_layer_data();                            //从DE获取一帧的数据
        if(result<0){
            printf("The video layer cannot obtain data \n");
            debounce_time=0;
            restore_screen_brightness(); //亮度复原
            sleep(FRAME_DELAY);
            continue;
        }
        if(!prev_dis_buf){          //第一次获取到帧
            prev_buf_size=dis_buf_size;
            prev_dis_buf=malloc(prev_buf_size);
            memset(prev_dis_buf, 0, prev_buf_size);
            printf("malloc prev_buf_y \n");

            memcpy(prev_dis_buf, dis_buf, prev_buf_size);
            printf("First data acquisition \n");
        }else{
            if(prev_buf_size != dis_buf_size){  //前一帧数据大小 不等于 后一帧数据大小
                printf("The lengths of the data are not equal \n");
                debounce_time=0;
                restore_screen_brightness();
            }else{      
                result = memcmp(prev_dis_buf, dis_buf, dis_buf_size);   
                if (result == 0) {
                    printf("Previous and current frame data are equal\n");
                    debounce_time++;
                } else {    //前一帧数据内容 不等于 后一帧数据内容
                    printf("Previous and current frame data differ\n");
                    debounce_time=0;
                    restore_screen_brightness();
                }
            }

            memcpy(prev_dis_buf, dis_buf, prev_buf_size);
            prev_buf_size=dis_buf_size;
        }

        if(dis_buf){
            free(dis_buf);
            dis_buf=NULL;
        }
        if(debounce_time >= 3){ //判定是暂停状态
            printf("Paused state:\n");
            debounce_time=0;
            is_black();     //判断是不是黑场
        }
        sleep(FRAME_DELAY);      
    }
    end:
        return NULL;
}

int initialize_blackout_detection(int argc, char *argv[])   //黑场检测初始化
{
    int ret=0;
    ret = sem_init(&blackout_thread_wait_sem, 0, 0);
    if (ret != 0) {
        printf("err: sem_init failed!\n");
    }

    blackout_check_run=1;

    if(!blackout_check_thread_id){
        pthread_attr_t attr_blackout;
        pthread_attr_init(&attr_blackout);
        pthread_attr_setstacksize(&attr_blackout, 0x2000);
        pthread_attr_setdetachstate(&attr_blackout, PTHREAD_CREATE_DETACHED);
        if(pthread_create(&blackout_check_thread_id, &attr_blackout, blackout_check_thread, NULL)) {
            printf("pthread_create is fail\n");
            return -1;
        }
    }    
}

int blackout_check_start(int argc, char *argv[])    //黑场检测开始
{
    blackout_check_thread_wake_flag=true;
    sem_post(&blackout_thread_wait_sem);
}

int blackout_check_stop(int argc, char *argv[])     //黑场检测停止
{
    blackout_check_thread_wake_flag=false;
    restore_screen_brightness();
}

#ifdef __HCRTOS__          
CONSOLE_CMD(blackout_check_stop,NULL,  blackout_check_stop, CONSOLE_CMD_MODE_SELF, "CBlack field detection stopped")
CONSOLE_CMD(blackout_check_start,NULL,  blackout_check_start, CONSOLE_CMD_MODE_SELF, "Black field detection start")
CONSOLE_CMD(initialize_blackout_detection,NULL,  initialize_blackout_detection, CONSOLE_CMD_MODE_SELF, "The black field detection is initialized")
#endif