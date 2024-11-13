/**
 * @file com_api.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2022-01-20
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef __COM_API_H__
#define __COM_API_H__


#include "lvgl/lvgl.h"
#include <hcuapi/dis.h>
#include <hcuapi/input-event-codes.h>
#include <stdbool.h> //bool
#include <stdint.h>  //uint32_t
#include <stdio.h>   //printf()
#include <stdlib.h>
#include <string.h> //memcpy()
#include <unistd.h> //usleep()
#include <ffplayer.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define CTL_MSG_COUNT 100

#define INVALID_VALUE_32 (0xFFFFFFFF)


    typedef enum
    {
        USB_STAT_MOUNT,
        USB_STAT_UNMOUNT,
        USB_STAT_MOUNT_FAIL,
        USB_STAT_UNMOUNT_FAIL,
        USB_STAT_CONNECTED,
        USB_STAT_DISCONNECTED,
        USB_STAT_INVALID,
        SD_STAT_MOUNT,
        SD_STAT_UNMOUNT,
        SD_STAT_MOUNT_FAIL,
        SD_STAT_UNMOUNT_FAIL,
    } USB_STATE;


    typedef enum
    {
        MSG_TYPE_KEY = 0,

        MSG_TYPE_MBOX_CREATE,         /*create a mbox*/
        MSG_TYPE_BOARDTEST_SORT_SEND, /*Send the boardtest sort*/
        MSG_TYPE_MBOX_RESULT,         /*Returning the mbox result.*/
        MSG_TYPE_BOARDTEST_EXIT,      /*End of the boardtest*/
        MSG_TYPE_BOARDTEST_AUTO,      /*Start the automated testing process*/
        MSG_TYPE_BOARDTEST_STOP,      /*force stop*/
        MSG_TYPE_MBOX_CLOSE,          /*mbox close*/
        MSG_TYPE_USB_MOUNT,
        MSG_TYPE_USB_UNMOUNT,
        MSG_TYPE_USB_MOUNT_FAIL,
        MSG_TYPE_USB_UNMOUNT_FAIL,
        MSG_TYPE_SD_MOUNT,
        MSG_TYPE_SD_UNMOUNT,
        MSG_TYPE_SD_MOUNT_FAIL,
        MSG_TYPE_SD_UNMOUNT_FAIL,
        MSG_TYPE_LV_CLEAR,
        MSG_TYPE_LV_CREAT,
        MSG_TYPE_LV_COVER_NEXT,
        MSG_TYPE_LV_COVER_PREV,
        MSG_TYPE_WINDOW_SEQUENCE,
        MSG_TYPE_WINDOW_RANDOM,
        MSG_TYPE_WINDOW_DETHUMBNAIL,
        MSG_TYPE_WINDOW_DETHUMBNAIL_CLEAR,
        MSG_TYPE_LV_COVER2_PREV,
        MSG_TYPE_LV_COVER2_NEXT,
        BOARDTEST_SORT,

    } msg_type_t;

    typedef struct
    {
        msg_type_t msg_type;
        uint32_t msg_code;
        char * file_path;
        lv_obj_t* img_pat;
        lv_img_dsc_t img_image;
    } control_msg_t;


    int api_control_send_msg(control_msg_t *control_msg); // 消息队列发送
    int api_control_receive_msg(control_msg_t *control_msg);//消息队列接收
    int api_control_send_key(uint32_t key);

    int boardtest_control_send_msg(control_msg_t *control_msg);
    int boardtest_control_receive_msg(control_msg_t *control_msg);

    void boardtest_read_ini_init(void);
    void boardtest_read_ini_exit(void);

    void photo_notifier_init(void);
    void photo_notifier_exit(void);

    void api_sleep_ms(uint32_t ms);

    #define     DEV_HDMI        "/dev/hdmi"


    uint8_t *api_rsc_string_get(uint32_t string_id);
    uint32_t api_sys_tick_get(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

int api_pic_effect_enable(bool enable);
int api_dis_show_onoff(bool on_off);
int api_system_init(void);
void api_set_display_zoom(int s_x, int s_y, int s_w, int s_h, int d_x, int d_y, int d_w, int d_h);
int api_set_display_zoom2(dis_zoom_t *diszoom_param);
void api_set_display_area(dis_tv_mode_e ratio); // void set_aspect_ratio(dis_tv_mode_e ratio)
void api_set_display_aspect(dis_tv_mode_e ratio, dis_mode_e dis_mode);
int watchdog_init(void);
void watchdog_feed(void);
void api_sys_clock_time_check_start(void);
long api_sys_clock_time_check_get(int64_t *last_clock);
int api_set_backlight_brightness(int val);
void osd_zoom(int x, int y, int w, int h);


#endif
