#include "lv_drivers/display/fbdev.h"
#include "lvgl/demos/lv_demos.h"
#include "src/draw/lv_img_buf.h"
#include "lvgl/lvgl.h"  //按键模块的头文件
#include "sys/errno.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <hcuapi/snd.h>
#include <hcuapi/fb.h>
#include <hcuapi/persistentmem.h>

#ifdef __linux__
#include <linux/fb.h>
#include <linux/input.h>
#else
#include <kernel/fb.h>
#include <kernel/lib/console.h>
#include <kernel/module.h>
#endif

#include <lvgl/hc-porting/hc_lvgl_init.h>
#include "./include/com_api.h" 
#include "./include/key_event.h" 
#include "./include/osd_com.h" 
#include "./include/os_api.h"  
#include "./include/factory_setting.h"  



static int fd_key;
static int fd_adc_key = -1;
lv_group_t *g;
static uint32_t act_key_code = -1;     //存储按键代码
lv_indev_t *indev_keypad;
#ifdef __HCRTOS__
static TaskHandle_t camera_thread = NULL;
#endif

/* ir key code map to lv_keys*/ //将红外键码映射为LVGL键码的操作
static uint32_t keypad_key_map2_lvkey(uint32_t act_key)
{
    switch(act_key) {
    case KEY_UP:    //上移 103
        act_key = LV_KEY_UP;
        break;
    case KEY_DOWN://下移 108
        act_key = LV_KEY_DOWN;
        break;
    case KEY_LEFT: //左移105
        act_key = LV_KEY_LEFT;
        break;
    case KEY_RIGHT: //右移106
        act_key = LV_KEY_RIGHT; 
        break;
    case KEY_OK:    //确认352  0x160   //KEY_ENTER  28 (钜弘)
        act_key = LV_KEY_ENTER;
        break;
    case KEY_ENTER:
        act_key = LV_KEY_ENTER;
        break;
    // case KEY_NEXT:
    //     act_key = LV_KEY_NEXT;
    //     break;
    // case KEY_PREVIOUS:
    //     act_key = LV_KEY_PREV;
        // break;
    // case KEY_VOLUMEUP:   //音量+
    //     act_key = V_KEY_V_UP;
    //     break;
    // case KEY_VOLUMEDOWN:  //音量-
    //     act_key = V_KEY_V_DOWN;
    //     break;
    case KEY_EXIT: //BACK 返回
        act_key = LV_KEY_ESC;
        break;
    case KEY_ESC:
        act_key = LV_KEY_ESC;
        break;
    // case KEY_EPG: //EPG
    //     act_key = LV_KEY_HOME;
    //     break;
    #ifdef PROJECTOR_VMOTOR_SUPPORT
    case KEY_CAMERA_FOCUS:
        ctrlbar_reset_mpbacklight();    //reset backlight with key 
        vMotor_Roll_cocus();
        act_key = 0;
        break;
    case KEY_FORWARD:
    case KEY_BACK:
        ctrlbar_reset_mpbacklight();    //reset backlight with key
        vMotor_set_step_count(192);
        if(act_key==KEY_FORWARD){
            vMotor_set_direction(BMOTOR_STEP_FORWARD);
        }else{
            vMotor_set_direction(BMOTOR_STEP_BACKWARD);
        }
        act_key = 0;
        break;
    #endif
    default:
        act_key = 0;//0x10000/*USER_KEY_FLAG */| act_key;
        // act_key = USER_KEY_FLAG | act_key;
        break;
    }
    return act_key;
}

static uint32_t key_preproc(uint32_t act_key) //按键预处理
{
    uint32_t ret = 0;
    if(act_key != 0)
   {    
        act_key_code = act_key;
        printf("act_key = %d\n",act_key);
        if(KEY_OK == act_key){         
        }
        else if(KEY_EXIT == act_key){         
        }
         else if(KEY_DOWN == act_key){         
        }
         else if(KEY_UP == act_key){        
        }
         else if(KEY_LEFT == act_key){        
        }
         else if(KEY_RIGHT == act_key){          
        }
        else if(KEY_VOLUMEUP == act_key){  
        }
        else if(KEY_VOLUMEDOWN == act_key){  
        }
        else {          //其他情况下，将按键值传递给LVGL，并返回该按键值。
            printf(">>lvgl keys: %d\n", (int)act_key);//map2lvgl_key(act_key); //打印红外键值，
            ret = act_key;
        }  
         ret = act_key; 
    }

    return ret;
}

/*Get the currently being pressed key.  0 if no key is pressed*/ /*获取当前按下的键。如果没有按下键，则为0 */
static uint32_t keypad_read_key(lv_indev_data_t  *lv_key)
{
    struct input_event *t;
    uint32_t ret = 0;
    KEY_MSG_s *key_msg = NULL;

    key_msg = api_key_msg_get();//首先调用 api_key_msg_get() 来获取键盘消息，如果返回的消息为空，则表示没有键被按下，直接返回0。
    if (!key_msg){
        return 0;
    }
    t = &key_msg->key_event;    //若获取到键盘消息，则将消息保存在 t 中
    printf("t->code:%d\n",t->code);
    printf("t->value:%d\n",t->value);

    if(t->value == 1)// 为1，表示键被按下
    {
        ret = key_preproc(t->code); //调用 key_preproc(t->code) 对键码进行预处理
        lv_key->state = (ret==0)?LV_INDEV_STATE_REL:LV_INDEV_STATE_PR;  //LV_INDEV_STATE_REL：LV_INDEV_STATE是释放；LV_INDEV_STATE_PR：LV_INDEV_STATE是按压
    }
    else if(t->value == 0)// t->value 的值为0，表示键被释放
    {
        ret = t->code;
        lv_key->state = LV_INDEV_STATE_REL; //LV_INDEV_STATE是释放
    }

    lv_key->key = keypad_key_map2_lvkey(ret);   //将键码映射为LVGL所需的键码，并将结果保存在 lv_key->key 中，最后返回键码 ret。
    return ret;
}

static void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    /*Get whether the a key is pressed and save the pressed key*/  //获取是否按下a键并保存按下的键
    keypad_read_key(data);
}

static void keypad_init(void)//调用 keypad_init() 函数进行按键初始化
{
    /*Your code comes here*/
    fd_key = open("/dev/input/event0", O_RDONLY);   //红外按键？？
    fd_adc_key = open("/dev/input/event1", O_RDONLY);
}

static int key_init(void)
{
    api_key_get_init();  
    static lv_indev_drv_t keypad_driver;

    keypad_init();     
    lv_indev_drv_init(&keypad_driver);  
    keypad_driver.type = LV_INDEV_TYPE_KEYPAD; 
    keypad_driver.read_cb = keypad_read;    
    indev_keypad = lv_indev_drv_register(&keypad_driver);    

    g = lv_group_create();  
    lv_group_set_default(g);    
    lv_indev_set_group(indev_keypad, g);   

    return 0;
}

static void com_message_process(control_msg_t * ctl_msg)   
{
    if(ctl_msg->msg_type == MSG_TYPE_USB_MOUNT)
    {
        printf("The USB flash drive is mounted !\n");
        // lv_obj_clear_flag(USB_icon,LV_OBJ_FLAG_HIDDEN);
    }
    else if(ctl_msg->msg_type == MSG_TYPE_USB_UNMOUNT)
    {
        printf("The USB flash drive is unmounted !\n");
        // lv_obj_add_flag(USB_icon,LV_OBJ_FLAG_HIDDEN);
    }
}

static void message_ctrl_process(void)
{
    control_msg_t ctl_msg = {0,}; 
    int ret;
    do
    {
        ret = api_control_receive_msg(&ctl_msg);
        if (0 != ret){
            if (0 != ret)
            break;
        }
    }while(0);

    com_message_process(&ctl_msg);
}

#ifdef __HCRTOS__
static void camera_task(void *pvParameters)
#else
int main(int argc, char *argv[])
#endif
{
    printf("\n\n ********* Welcome to Hichip world! *********\n\n");
    api_sys_clock_time_check_start();

    hc_lvgl_init();
    api_system_init();
    key_init(); 

    while(1)
    {
        lv_task_handler();
        message_ctrl_process();
        usleep(1000); 
    }
}

#ifdef __HCRTOS__

static int camera_start(int argc, char **argv)
{
    xTaskCreate(camera_task, (const char *)"camera_solution", 0x2000/*configTASK_STACK_DEPTH*/,
            NULL, portPRI_TASK_NORMAL, &camera_thread);
    return 0;
}

static int camera_auto_start(void)
{
    camera_start(0, NULL);
    
    return 0;
}

__initcall(camera_auto_start)


#endif

