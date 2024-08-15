#include "app_config.h"

#include <stdio.h>
#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lvgl/demos/lv_demos.h"
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>
/*#include "lv_gpu_hcge.h"*/
#include <fcntl.h>
#include <sys/ioctl.h>

#ifdef __linux__
#include <linux/fb.h>
#include <linux/input.h>
#else
#include <kernel/fb.h>
#include <kernel/lib/console.h>
#include <kernel/module.h>
#endif
#include <hcuapi/input.h>

#include <hcuapi/fb.h>
#include <sys/poll.h>
#include <hcuapi/input-event-codes.h>
#include <hcuapi/standby.h>
#include <hcuapi/dis.h>

#ifdef CAST_SUPPORT
#include <hudi/hudi_audsink.h>
#include <hccast/hccast_com.h>
#endif

#ifdef BLACK_FIELD_DETECTION_SUPPORT
extern int initialize_blackout_detection(int argc, char *argv[]);
#endif

#include "com_api.h"
#include <lvgl/hc-porting/hc_lvgl_init.h>
#include <hcuapi/sys-blocking-notify.h>
#include "setup.h"
#include "screen.h"
#include "factory_setting.h"
#include "./volume/volume.h"
#include "key_event.h"
#include "channel/local_mp/mp_mainpage.h"
#include "channel/local_mp/mp_fspage.h"
#include "channel/local_mp/mp_ctrlbarpage.h"
#include "channel/local_mp/local_mp_ui.h"
#include "channel/local_mp/mp_bsplayer_list.h"
#include "channel/local_mp/mp_ebook.h"
#include "channel/local_mp/backstage_player.h"
#ifdef HUDI_FLASH_SUPPORT
#include "flash_otp.h"
#endif
#include "vmotor.h"
#include "tv_sys.h"
#include "channel/cast/win_cast_root.h"
#include "osd_com.h"
#include "mul_lang_text.h"
#if defined(USBMIRROR_SUPPORT) || defined(AIRCAST_SUPPORT) || defined(MIRACAST_SUPPORT)
#include "channel/cast/cast_api.h"
#endif

#ifdef WIFI_SUPPORT
#include "network_api.h"
#endif
#ifdef HCIPTV_YTB_SUPPORT
#include "channel/webplayer/webdata_mgr.h"
#include "channel/webplayer/win_webservice.h"
#include "channel/webplayer/win_webplay.h" 
#endif
#ifdef MEMMORY_PLAY_SUPPORT
#include "channel/local_mp/memmory_play.h"
#endif
#ifdef CVBSIN_SUPPORT
#include "./channel/cvbs_in/cvbs_rx.h"
#endif
#ifdef HDMI_RX_CEC_SUPPORT
#include "channel/hdmi_in/hdmi_rx.h"
#endif

//Initialize to show the start screen first, then initialize other screens,
//so that to fast show first UI.
#define UI_FAST_SHOW_SUPPORT

uint32_t act_key_code = -1;
lv_indev_t* indev_keypad;
static lv_group_t * g;
SCREEN_TYPE_E cur_scr = 0, last_scr=0;

#ifdef __HCRTOS__
static TaskHandle_t projector_thread = NULL;
#endif

void* lv_mem_adr=NULL;
static int key_init(void);
bool is_mute = false;
static bool remote_control_disable = false;

extern int memmory_play_init(void);
void set_remote_control_disable(bool b)
{
    remote_control_disable = b;
}


//static SCREEN_TYPE_E projector_get_cur_screen();

void key_set_group(lv_group_t *key_group)
{
    lv_group_set_default(key_group);
    lv_indev_set_group(indev_keypad, key_group);        
}


static void cur_screen_set(enum SCREEN_TYPE cur, enum SCREEN_TYPE last)
{
    cur_scr = cur;
    last_scr = last;
}

void change_screen(enum SCREEN_TYPE stype)
{
#ifndef MAIN_PAGE_SUPPORT  //disable main page
    if(stype==SCREEN_CHANNEL_MAIN_PAGE)
        return ;
#endif  
#ifndef CVBSIN_SUPPORT  //disable cvbs page
    if(stype==SCREEN_CHANNEL_CVBS)
        return ;
#endif  

    cur_scr = stype;
    switch (stype) {
    case SCREEN_CHANNEL:
    case SCREEN_SETUP:
    case SCREEN_CHANNEL_MAIN_PAGE:
        break; 
#ifdef HDMIIN_SUPPORT       
    case SCREEN_CHANNEL_HDMI:
        break;
#endif      
    case SCREEN_CHANNEL_CVBS:
        break;   
#ifdef WIFI_SUPPORT   
    case SCREEN_WIFI:
        break;        
#endif        
#ifdef HC_FACTORY_TEST
    case SCREEN_FTEST_FULL_COLOR_DIS:
        break;
#endif      
    default:
        key_set_group(g);
        break;
    }
}


static lv_obj_t *m_last_scr = NULL;
void _ui_screen_change(lv_obj_t * target,  int spd, int delay)
{
    m_last_scr = lv_scr_act();

#ifdef USB_MIRROR_FAST_SUPPORT
    um_service_off_by_menu(target);
#endif    
    lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_MOVE_TOP, spd, delay, false);
    
#ifdef USB_MIRROR_FAST_SUPPORT
    um_service_on_by_menu(target);
#endif    

}

void api_scr_go_back(void)
{
    if (m_last_scr)
        _ui_screen_change(m_last_scr, 0, 0);
}

bool ui_mute_get(void)
{
    return is_mute;
}

void ui_mute_set(void)
{
    is_mute = !is_mute;
    int ret =  api_media_mute(is_mute);
    if(ret == API_SUCCESS){
        create_mute_icon();
    }else {
        /* api mute fail do not change this vaule */
        is_mute = !is_mute;
    }
}

/* Description:
*        hotkeys proc, such as POWER/MENU/SETUP keys etc.
 * return 0:  hotkeys consumed by app, don't send to lvgl;  others: lvgl keys
 */
static uint32_t key_preproc(uint32_t act_key)
{
    uint32_t ret = 0;
        
    //check if the key is disabled for hot key
    if (!api_hotkey_enable_get(act_key))
        return act_key;

    if(act_key != 0)
    {    
        autosleep_reset_timer();
        act_key_code = act_key;
        if(remote_control_disable){
            
        }
        else if(KEY_POWER == act_key){
        /*
        #ifdef LVGL_MBOX_STANDBY_SUPPORT
            win_open_lvmbox_standby();
            ret = V_KEY_POWER;
        #else
            enter_standby();
        #endif
        */

        }
        else if(KEY_MENU/*KEY_CHANNEL*/ == act_key){
            printf(">>Channel key\n");
        #ifdef MAIN_PAGE_SUPPORT
            change_screen(SCREEN_CHANNEL);
        #endif
        }
        else if(KEY_EPG/*KEY_SETUP*/ == act_key){
            printf(">>Setup key\n");        
            change_screen(SCREEN_SETUP);
            
            if(lv_scr_act() == setup_scr){
                ret = act_key;
            }
        }
        else if(KEY_VOLUMEUP == act_key|| KEY_VOLUMEDOWN == act_key){
            //printf(">>Volume key\n");
            ctrlbar_reset_mpbacklight(); //key to reset backlight 
            if(ui_mute_get()){
                ui_mute_set();

                create_volume();

                ret = act_key;
            }else if(volume_bar){
                ret = act_key;
            }
            else{ 
                create_volume();                     
                ret = act_key;
            }  
        }else if(KEY_MUTE == act_key){
            ui_mute_set();
            
            ctrlbar_reset_mpbacklight();

            ret = act_key;
        }
        else if(KEY_ROTATE_DISPLAY  == act_key || act_key == KEY_FLIP){
            printf("act_key %d\n", (int)act_key);
            api_set_next_flip_mode();
        }else if(KEY_KEYSTONE == act_key && cur_scr != SCREEN_SETUP){
        #ifdef KEYSTONE_SUPPORT            
            change_keystone();
        #endif
        }
    #ifdef MIRROR_ES_DUMP_SUPPORT
        //dump mirror ES data to U-disk
        else if(KEY_BLUE == act_key)
        {
            extern void api_mirror_dump_enable_set(bool enable);
            static bool dump_enable = true;
            if (USB_STAT_MOUNT == mmp_get_usb_stat())
            {
                if (dump_enable)
                    create_message_box("Enable mirror ES dump!");
                else
                    create_message_box("Disable mirror ES dump!");
            }
            else
            {
                dump_enable = false;
                create_message_box("No USB-disk, disable mirror ES dump!");
            }
            api_mirror_dump_enable_set(dump_enable);
            dump_enable = !dump_enable;
        }
    #endif
        else if(act_key == KEY_HOME)
        {
            change_screen(SCREEN_CHANNEL_MAIN_PAGE);
            ret = act_key;
        }
        else {
            printf(">>lvgl keys: %d\n", (int)act_key);//map2lvgl_key(act_key);
            ret = act_key;
        }   
    }

    return ret;
}


/* ir key code map to lv_keys*/
static uint32_t keypad_key_map2_lvkey(uint32_t act_key)
{
    switch(act_key) {
    case KEY_UP:
        act_key = LV_KEY_UP;
        break;
    case KEY_DOWN:
        act_key = LV_KEY_DOWN;
        break;
    case KEY_LEFT:
        act_key = LV_KEY_LEFT;
        break;
    case KEY_RIGHT:
        act_key = LV_KEY_RIGHT;
        break;
    case KEY_OK:
        act_key = LV_KEY_ENTER;
        break;
    case KEY_ENTER:
        act_key = LV_KEY_ENTER;
        break;
    case KEY_NEXT:
        act_key = LV_KEY_NEXT;
        break;
    case KEY_PREVIOUS:
        act_key = LV_KEY_PREV;
        break;
    // case KEY_VOLUMEUP:
    //     act_key = LV_KEY_UP;
    //     break;
    // case KEY_VOLUMEDOWN:
    //     act_key = LV_KEY_DOWN;
    //     break;
    case KEY_EXIT:
        act_key = LV_KEY_ESC;
        break;
    case KEY_ESC:
        act_key = LV_KEY_ESC;
        break;
    case KEY_EPG:
        act_key = LV_KEY_HOME;
        break;
    #ifdef PROJECTOR_VMOTOR_SUPPORT
    case KEY_CAMERA_FOCUS:
        ctrlbar_reset_mpbacklight();    //reset backlight with key 
        vMotor_Roll_cocus();
        act_key = 0;
        break;
    case KEY_FORWARD:
    case KEY_BACK:
        ctrlbar_reset_mpbacklight();    //reset backlight with key
        if(act_key==KEY_FORWARD){
            vMotor_set_direction(BMOTOR_STEP_FORWARD);
        }else{
            vMotor_set_direction(BMOTOR_STEP_BACKWARD);
        }
        vMotor_set_step_count(12);
        act_key = 0;
        break;
    #endif
    default:
        // act_key = 0;//0x10000/*USER_KEY_FLAG */| act_key;
        act_key = USER_KEY_FLAG | act_key;
        break;
    }
    return act_key;
}

/*Get the currently being pressed key.  0 if no key is pressed*/
static uint32_t keypad_read_key(lv_indev_data_t  *lv_key)
{
    /*Your code comes here*/
    struct input_event *t;
    uint32_t ret = 0;
    KEY_MSG_s *key_msg = NULL;

    key_msg = api_key_msg_get();
    if (!key_msg){
        return 0;
    }
    t = &key_msg->key_event;

    // printf("key_type = %d t->value =%d adc_key_count =%d\n",key_msg ->key_type,t->value,adc_key_count);
    if(t->value == 1)// pressed
    {
        ret = key_preproc(t->code);
        lv_key->state = (ret==0)?LV_INDEV_STATE_REL:LV_INDEV_STATE_PR;
    }
    else if(t->value == 0)// released
    {
        ret = t->code;
        lv_key->state = LV_INDEV_STATE_REL;
    }

    //power key is valid while key release.    
    if(t->code == KEY_POWER && t->value == 0 && !remote_control_disable){
    // #ifdef LVGL_MBOX_STANDBY_SUPPORT
    //     win_open_lvmbox_standby();
    //     lv_key->state = LV_INDEV_STATE_PR;
    // ret = V_KEY_POWER;
    // #else
    //     enter_standby();
    // #endif
    win_um_play_open(NULL);
    }    

    lv_key->key = keypad_key_map2_lvkey(ret);
    return ret;
}

/*Will be called by the library to read the mouse*/
 void keypad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    /*Get whether the a key is pressed and save the pressed key*/
    keypad_read_key(data);
}
static int key_init(void)
{
    static lv_indev_drv_t keypad_driver;

    lv_indev_drv_init(&keypad_driver);
    keypad_driver.type = LV_INDEV_TYPE_KEYPAD;
    keypad_driver.read_cb = keypad_read;
    indev_keypad = lv_indev_drv_register(&keypad_driver);

    g = lv_group_create();
    lv_group_set_default(g);
    lv_indev_set_group(indev_keypad, g);

    return 0;
}

static void sys_reboot_func(void *user_data)
{
    (void)user_data;
    api_system_reboot();
}

void stroage_hotplug_handle(int msg_type);
static bool m_wifi_plug_out = true;
static lv_obj_t *demo_label = NULL;

#ifdef CAST_SUPPORT
void projector_demo_show()
{
    char demo_content[32] = {0};
    int cast_is_demo = 0;

    if (NULL == demo_label)
    { 
        demo_label = lv_label_create(lv_layer_top());
        lv_obj_set_style_text_font(demo_label, &lv_font_montserrat_22, 0); 
        lv_obj_set_style_text_color(demo_label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(demo_label, LV_ALIGN_TOP_LEFT, 30, 30);
    }
    
    if (cast_air_is_demo())
    {
        cast_is_demo = 1;
        strncat(demo_content, "a", sizeof(demo_content));
    }

    if (cast_dial_is_demo())
    {
        cast_is_demo = 1;
        strncat(demo_content, "d", sizeof(demo_content));
    }

    if (cast_um_is_demo())
    {
        cast_is_demo = 1;
        strncat(demo_content, "u", sizeof(demo_content));
    }

#ifdef HCIPTV_YTB_SUPPORT
    extern bool iptv_is_demo(void);
    if (iptv_is_demo())
    {
        cast_is_demo = 1;
        strncat(demo_content, "y", sizeof(demo_content));
    }
#endif

    if (cast_is_demo)
        lv_label_set_text_fmt(demo_label, "demo(%s)", demo_content);
    else
        lv_label_set_text(demo_label, "");
}
#endif

#ifdef BLUETOOTH_CHANNEL_OPTIMIZE
static uint16_t g_p2p_fuse_map = 0;
static uint8_t g_bt_enable_map[10] = {0};

static void projector_bt_p2p_channel_calc(int wifi_ch)
{
    memset(g_bt_enable_map, 0, 10);
    g_p2p_fuse_map = 0;

    switch (wifi_ch)
    {
        case 1:
        case 2:
        case 3:
            g_p2p_fuse_map = 0x1FE0;    //1,2,3,4,5
            g_bt_enable_map[5] = 0x3F;
            g_bt_enable_map[6] = 0xFF;
            g_bt_enable_map[7] = 0xFF;
            g_bt_enable_map[8] = 0xFF;
            g_bt_enable_map[9] = 0xFF;
            break;
        case 4:
            g_p2p_fuse_map = 0x1FC1;    //2,3,4,5,6
            g_bt_enable_map[0] = 0xF0;
            g_bt_enable_map[5] = 0x01;
            g_bt_enable_map[6] = 0xFF;
            g_bt_enable_map[7] = 0xFF;
            g_bt_enable_map[8] = 0xFF;
            g_bt_enable_map[9] = 0xFF;
            break;
        case 5:
            g_p2p_fuse_map = 0x1F83;    //3,4,5,6,7
            g_bt_enable_map[0] = 0xFF;
            g_bt_enable_map[1] = 0x80;
            g_bt_enable_map[6] = 0x0F;
            g_bt_enable_map[7] = 0xFF;
            g_bt_enable_map[8] = 0xFF;
            g_bt_enable_map[9] = 0xFF;
            break;
        case 6:
            g_p2p_fuse_map = 0x1F07;    //4,5,6,7,8
            g_bt_enable_map[0] = 0xFF;
            g_bt_enable_map[1] = 0xFC;
            g_bt_enable_map[7] = 0x7F;
            g_bt_enable_map[8] = 0xFF;
            g_bt_enable_map[9] = 0xFF;
            break;
        case 7:
            g_p2p_fuse_map = 0x1E0F;    //5,6,7,8,9
            g_bt_enable_map[0] = 0xFF;
            g_bt_enable_map[1] = 0xFF;
            g_bt_enable_map[2] = 0xE0;
            g_bt_enable_map[7] = 0x03;
            g_bt_enable_map[8] = 0xFF;
            g_bt_enable_map[9] = 0xFF;
            break;
        case 8:
            g_p2p_fuse_map = 0x1C1F;    //6,7,8,9,10
            g_bt_enable_map[0] = 0xFF;
            g_bt_enable_map[1] = 0xFF;
            g_bt_enable_map[2] = 0xFF;
            g_bt_enable_map[8] = 0x1F;
            g_bt_enable_map[9] = 0xFF;
            break;
        case 9:
            g_p2p_fuse_map = 0x183F;    //7,8,9,10,11
            g_bt_enable_map[0] = 0xFF;
            g_bt_enable_map[1] = 0xFF;
            g_bt_enable_map[2] = 0xFF;
            g_bt_enable_map[3] = 0xF8;
            g_bt_enable_map[9] = 0xFF;
            break;
        case 10:
            g_p2p_fuse_map = 0x107F;    //8,9,10,11,12
            g_bt_enable_map[0] = 0xFF;
            g_bt_enable_map[1] = 0xFF;
            g_bt_enable_map[2] = 0xFF;
            g_bt_enable_map[3] = 0xFF;
            g_bt_enable_map[4] = 0xC0;
            g_bt_enable_map[9] = 0x07;
            break;
        case 11:
        case 12:
        case 13:
            g_p2p_fuse_map = 0xFF;    //9,10,11,12,13
            g_bt_enable_map[0] = 0xFF;
            g_bt_enable_map[1] = 0xFF;
            g_bt_enable_map[2] = 0xFF;
            g_bt_enable_map[3] = 0xFF;
            g_bt_enable_map[4] = 0xFE;
            break;
    }
}

static int projector_bt_channel_set()
{
    uint8_t bt_channel[10] = {0};

    if (!memcmp(bt_channel, g_bt_enable_map, 10))
    {
        return -1;
    }

    bluetooth_ioctl(BLUETOOTH_GET_CHANNEL_MAP, bt_channel);

    if (memcmp(bt_channel, g_bt_enable_map, 10))
    {
        bluetooth_ioctl(BLUETOOTH_SET_CHANNEL_MAP, g_bt_enable_map);
        printf("Update BT channel map\n");
    }
    else
    {
        printf("Ignore same BT channel map\n");
    }

    return 0;
}
#endif

static void com_message_process(control_msg_t *ctl_msg)
{
    if (!ctl_msg)
    {    
        return;
    }

	media_type_t media_type_tmp=0;
    switch(ctl_msg->msg_type)
    {
#ifdef CAST_SUPPORT
    case MSG_TYPE_AIR_INVALID_CERT:
    case MSG_TYPE_CAST_DIAL_INVALID_CERT:
    {
        projector_demo_show();
        break;
    }
    case MSG_TYPE_HDMI_TX_CHANGED:
    {
        extern void restart_air_service_by_hdmi_change(void);
        restart_air_service_by_hdmi_change();
        break;
    }
#endif
#ifdef HCIPTV_YTB_SUPPORT
    case MSG_TYPE_HCIPTV_INVALID_CERT:
    {
        projector_demo_show();
        break;
    }
#endif 

#ifdef WIFI_SUPPORT    
    case MSG_TYPE_NET_UPGRADE:
    {
        if (ui_network_upgrade && !api_is_upgrade_get()){
            win_upgrade_type_set(ctl_msg->msg_type);
            _ui_screen_change(ui_network_upgrade,0,0);
        }
        break;
    }
#endif
#ifdef WIFI_SUPPORT
    case MSG_TYPE_USB_WIFI_PLUGIN:
        if (m_wifi_plug_out)
        {
#ifdef WIFI_PM_SUPPORT
            if (api_get_wifi_pm_state() == WIFI_PM_STATE_PS)
            {
                api_wifi_pm_plugin_handle();
            }
            else         
#endif
            {
                app_wifi_init();

                if (!network_connect())
                {
                    m_wifi_plug_out = false;
                }    
            }
        }       
        break;
    case MSG_TYPE_USB_WIFI_PLUGOUT:
        if (!api_get_wifi_pm_state())
        {
            hccast_stop_services();
            hccast_wifi_mgr_udhcpc_stop();
            // udhcpd already stop in app_wifi_switch_work_mode(WIFI_MODE_NONE)
            app_wifi_deinit();
        }

        m_wifi_plug_out = true;
        app_set_wifi_init_done(0);

        break;
#endif
#ifdef HC_MEDIA_MEMMORY_PLAY
    case MSG_TYPE_MP_MEMMORY_PLAY:
    {    
        media_type_tmp = (media_type_t)projector_get_some_sys_param(P_MEM_PLAY_MEDIA_TYPE);
        api_wifi_pm_open();
        //Use change_screen(), so that it can back to mainmenu
        if(media_type_tmp == MEDIA_TYPE_TXT)
            change_screen(SCREEN_CHANNEL_MP_EBOOK);
        else
            change_screen(SCREEN_CHANNEL_MP_PLAYBAR);

        break;
    }
#endif
    // for test usb msg
    case MSG_TYPE_USB_MOUNT:
    case MSG_TYPE_USB_UNMOUNT:
    case MSG_TYPE_USB_UNMOUNT_FAIL:
    case MSG_TYPE_SD_MOUNT:
    case MSG_TYPE_SD_UNMOUNT:
    case MSG_TYPE_SD_UNMOUNT_FAIL:
    {
        partition_info_update(ctl_msg->msg_type,(char*)ctl_msg->msg_code);
        /*due to msg_code is malloc in wq pthread */ 
        stroage_hotplug_handle(ctl_msg->msg_type);
        /*hotplug msg send to ui_screen then do ui_refr opt */
        //api_control_send_msg(ctl_msg);

        break;
    }
    #ifdef BLUETOOTH_SUPPORT
    case MSG_TYPE_BT_CONNECTED:
        create_message_box(api_rsc_string_get(STR_BT_CONNECTED));
        break;
    case MSG_TYPE_BT_DISCONNECTED:
        if(app_bt_is_scanning() || app_bt_is_connecting()){
            break;
        }
        create_message_box(api_rsc_string_get(STR_BT_DISCONNECTED));
        break;
    #endif    
    #ifdef BATTERY_SUPPORT
    case MSG_TYPE_PM_BATTERY_MONITOR:
        lv_event_send(power_label, LV_EVENT_REFRESH, NULL);
        break;
    #endif
    #ifdef WIFI_SUPPORT
    case MSG_TYPE_NETWORK_WIFI_MAY_LIMITED:
        if (!m_wifi_plug_out)
        {
            app_wifi_set_limited_internet(true);
            printf("%s %d: MSG_TYPE_NETWORK_WIFI_MAY_LIMITED\n",__func__,__LINE__);
        }
        break;
    #endif

#ifdef BLUETOOTH_CHANNEL_OPTIMIZE
    case MSG_TYPE_NETWORK_HOSTAP_ENABLE : //AP MODE 
    case MSG_TYPE_NETWORK_WIFI_CONNECTED :  //STA MODE 
        {
            int ch = HCCAST_P2P_LISTEN_CH_DEFAULT;
            
            if(hccast_wifi_mgr_get_current_freq_mode() == HCCAST_WIFI_FREQ_MODE_24G){
                ch = hccast_wifi_mgr_get_current_freq();
                projector_bt_p2p_channel_calc(ch);
                projector_bt_channel_set();
            } else {
                projector_bt_p2p_channel_calc(ch);
            }
        }
        break;
    case MSG_TYPE_CAST_MIRACAST_SSID_DONE : 
        {
            if (0 == g_p2p_fuse_map) {
                projector_bt_p2p_channel_calc(HCCAST_P2P_LISTEN_CH_DEFAULT);
            }
 
            if (projector_get_some_sys_param(P_BT_SETTING) == BLUETOOTH_ON) {
                hccast_wifi_mgr_p2p_channel_fuse(g_p2p_fuse_map);
                projector_bt_channel_set();
            } else {
                hccast_wifi_mgr_p2p_channel_fuse(0);
            }
            
        }
        break;
#endif

    default:
    #ifdef USB_MIRROR_FAST_SUPPORT
        ui_um_fast_proc(ctl_msg->msg_type);
    #endif
        break;
    }

}

static void _media_play_exit()
{
    ctrlbarpage_close(true);
    backstage_player_task_stop(0,NULL);
    ebook_close(true);
}

/**
 * @description: Logic handle for removing and inserting storage devices 
 * @param :
 * @return {*}
 * @author: Yanisin
 */
void stroage_hotplug_handle(int msg_type) 
{
    int updata_state=0;
    partition_info_t * cur_partition_info=mmp_get_partition_info();
    if(cur_partition_info==NULL){
        return ;
    }
    printf(">>!%s ,%d\n",__func__,__LINE__);
    if(msg_type==MSG_TYPE_USB_MOUNT||msg_type==MSG_TYPE_SD_MOUNT){
        if(cur_scr==SCREEN_CHANNEL_MP&&lv_scr_act()==ui_mainpage){
            lv_event_send(mp_statebar,LV_EVENT_REFRESH,0);
        }
    #ifdef USB_AUTO_UPGRADE
        updata_state = sys_upg_usb_check_notify();
    #endif
    #ifdef HC_MEDIA_MEMMORY_PLAY
        if(updata_state == 0)
            memmory_play_init();
    #endif
    }else if(msg_type==MSG_TYPE_USB_UNMOUNT||msg_type==MSG_TYPE_SD_UNMOUNT||msg_type==MSG_TYPE_SD_UNMOUNT_FAIL||msg_type==MSG_TYPE_USB_UNMOUNT_FAIL){
        if(cur_scr == SCREEN_CHANNEL_MP){
            if(lv_scr_act()==ui_mainpage){
                lv_event_send(mp_statebar,LV_EVENT_REFRESH,0);
            }else if(lv_scr_act()==ui_subpage){
                _ui_screen_change(ui_mainpage,0, 0);
            }else if(lv_scr_act()==ui_fspage||lv_scr_act()==ui_ctrl_bar||lv_scr_act()==ui_ebook_txt){
                if(api_check_partition_used_dev_ishotplug()){
                    _ui_screen_change(ui_mainpage,0, 0);
                    app_media_list_all_free();
                    clear_all_bsplayer_mem();
                }
            }
        }else if(cur_scr ==  SCREEN_SETUP || cur_scr == SCREEN_CHANNEL){
            if(api_check_partition_used_dev_ishotplug()){
                _media_play_exit();
                app_set_screen_submp(SCREEN_SUBMP0);
            }
        }
        // refresh the filelist_t in other scene
        if(api_check_partition_used_dev_ishotplug()){
            app_media_list_all_free();
            clear_all_bsplayer_mem(); 
        }
        bool is_devoinfo_state=api_storage_devinfo_state_get();
        if(msg_type==MSG_TYPE_USB_UNMOUNT && is_devoinfo_state==false){
            win_msgbox_msg_open_on_top(STR_USB_READ_ERROR, 3000, NULL, NULL);
            win_msgbox_msg_set_pos(LV_ALIGN_CENTER,0,0);
            api_storage_devinfo_state_set(true);
        }
    }
}

static void loop_watchdog_feed(void)
{
    static int counter = 0;
    if (counter ++ > 80){
    #ifdef __HCRTOS__
      #ifndef WATCHDOG_KERNEL_FEED
        api_watchdog_feed();
      #endif
    #else
        api_watchdog_feed();
    #endif
        counter = 0;
    }
}

static lv_obj_t *old_scr = NULL;
static bool screen_is_changed(void)
{
    bool change = false;
    lv_obj_t *screen = NULL;

    screen = lv_scr_act();
    if (old_scr && old_scr != screen)
        change = true;
    else
        change = false;

    old_scr = screen;
    return change;
}

static void message_ctrl_process(void)
{
    control_msg_t ctl_msg = {0,};
    screen_ctrl ctrl_fun = NULL;
//    lv_disp_t * dispp = lv_disp_get_default();
    lv_obj_t *screen;
    int ret = -1;

//    screen = dispp->act_scr;
    screen = lv_scr_act();
    do
    {
        ret = api_control_receive_msg(&ctl_msg);
        if (0 != ret){
            if (0 != ret)
            break;
        }
        if (screen)
        {
            ctrl_fun = api_screen_get_ctrl(screen);
            if (ctrl_fun)
                ctrl_fun((void*)&ctl_msg, NULL);
        }
    }while(0);

    com_message_process(&ctl_msg);

    loop_watchdog_feed();

	//exit pop up message box if change screen
    if (screen_is_changed()){
    #ifdef LVGL_MBOX_STANDBY_SUPPORT
        win_del_lvmbox_standby();
    #endif    
    #ifdef USB_AUTO_UPGRADE
        del_upgrade_prompt();
    #endif

    }
}

//init UI screens
static void ui_screen_init(void)
{
    if (!setup_scr)
        setup_screen_init();

    if (!channel_scr)
        channel_screen_init();

#ifdef WIFI_SUPPORT 
    if (!ui_network_upgrade)   
        ui_network_upgrade_init();
#endif    

#ifdef DLNA_SUPPORT
    if (!ui_cast_dlna)
        ui_cast_dlna_init();
#endif

#if defined(MIRACAST_SUPPORT) || defined(AIRCAST_SUPPORT) 
    if (!ui_wifi_cast_root)
        ui_wifi_cast_init();
    if (!ui_cast_play)
        ui_cast_play_init();
#endif    

    // lv_disp_load_scr(channel_scr); 
    // lv_group_set_default(channel_g);
    // lv_indev_set_group(indev_keypad, channel_g);
#ifdef  USBMIRROR_SUPPORT
    if (!ui_um_play)
        ui_um_play_init();
  #ifdef USB_MIRROR_FAST_SUPPORT
    if (!ui_um_fast)
        ui_um_fast_init();
  #endif
#endif
    if (!hdmi_scr)
        hdmi_screen_init();
    #ifdef CVBSIN_SUPPORT
    if (!cvbs_scr)
        cvbs_screen_init();
    #endif
#ifdef WIFI_SUPPORT  
    if (!wifi_scr)        
        wifi_screen_init();    
#endif  
#ifdef HCIPTV_YTB_SUPPORT

    if (!webservice_scr)
        webservice_screen_init();

    if (!webplay_scr)
        webplay_screen_init();
#endif  
    if (!main_page_scr)
        main_page_init();

    if (!volume_scr)
        volume_screen_init();

    //local mp screen 
    if (!ui_mainpage)
        ui_mainpage_screen_init();
    if (!ui_subpage)
        ui_subpage_screen_init();
    if (!ui_fspage)
        ui_fspage_screen_init();
    if (!ui_ctrl_bar)
        ui_ctrl_bar_screen_init();
    if (!ui_ebook_txt)
        ui_ebook_screen_init();
#ifdef HC_FACTORY_TEST  
    if (!factory_settings_scr)
        ui_factory_settings_init();
#endif  

#ifdef BATTERY_SUPPORT
    battery_screen_init();
#endif

}

//init some UI/LVGL system.
static void ui_sys_init(void)
{
    lv_disp_t *dispp = NULL;   
    lv_theme_t *theme = NULL;

    api_key_get_init();
#ifdef __linux__
    extern void lv_fb_hotplug_support_set(bool enable);
    lv_fb_hotplug_support_set(false);
#endif    

    hc_lvgl_init();
    key_init();
    dispp = lv_disp_get_default();   
    theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE),  \
                                        lv_palette_main(LV_PALETTE_RED),  false, LV_FONT_DEFAULT);
    lv_disp_set_theme(dispp, theme);
#ifdef HDMI_RX_CEC_SUPPORT
    hdmirx_cec_init();
#endif
#ifdef MULTI_OS_SUPPORT
    avparam_data_init();
#endif
#ifdef SYS_ZOOM_SUPPORT
    printf("sys zoom support\n");
    sys_scala_init();
#endif
    api_get_screen_rotate_info();

#ifdef WIFI_SUPPORT
    wifi_mutex_init();
#endif

#ifdef USB_AUTO_UPGRADE
    sys_upg_usb_check_init();
#endif

#ifdef BLUETOOTH_SUPPORT
    bt_init();
#endif

}

static void *post_sys_init_task(void *arg)
{
    api_system_init();
    app_ffplay_init();
#ifdef HUDI_FLASH_SUPPORT
    flash_otp_data_init();
#endif
    tv_sys_app_start_set(1);

//Init cast here to get cerificated information in advance
#ifdef AIRCAST_SUPPORT    
    hccast_air_service_init(hccast_air_callback_event);
#endif    

#ifdef HCIPTV_YTB_SUPPORT
    hciptv_y2b_service_init(hccast_iptv_callback_event);
#endif 

#ifdef USBMIRROR_SUPPORT  
    cast_usb_mirror_init(); 
#endif

#ifdef CAST_SUPPORT
  #ifdef CONFIG_APPS_PROJECTOR_SPDIF_OUT
    hccast_com_audsink_set(AUDSINK_SND_DEVBIT_I2SO | AUDSINK_SND_DEVBIT_SPO);
  #endif
#endif    

#ifdef WIFI_SUPPORT
    //service would be enabled in cast UI
    // network_service_enable_set(false);
    network_connect();
#endif    

#ifdef __linux__
    api_usb_dev_check();
#endif    
    /*Handle LitlevGL tasks (tickless mode)*/

    set_auto_sleep(projector_get_some_sys_param(P_AUTOSLEEP));    

#ifdef BLUETOOTH_SUPPORT
    BT_first_power_on();
#endif

    return NULL;
}

//Some modules can be initialized late to show UI menu as soon as possible.
static void post_sys_init(void)
{
    pthread_t thread_id = 0;
    pthread_attr_t attr;
    //create the message task
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x2000);
    pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED); //release task resource itself
    pthread_create(&thread_id, &attr, post_sys_init_task, NULL);
    pthread_attr_destroy(&attr);
}
// extern int win_um_play_open(void *arg);

//Set some parameter, for example: fb rotate, ...
static void sys_params_set(void)
{
    set_flip_mode(projector_get_some_sys_param(P_FLIP_MODE));
}

#ifdef __HCRTOS__
static void projector_task(void *pvParameters)
#else
int main(int argc, char *argv[])
#endif
{
    api_sys_clock_time_check_start();

    printf("\n\n ********* Welcome to Hichip world! *********\n\n");    

    api_system_pre_init();
    
   /* system param init*/
    projector_memory_save_init();
    projector_factory_init();
    projector_sys_param_load();

    ui_sys_init();

    sys_params_set();

#ifndef UI_FAST_SHOW_SUPPORT
    ui_screen_init();
#endif    

    // Load default screen,  load from system param later.
    cur_screen_set(SCREEN_CHANNEL, SCREEN_CHANNEL);

    // change_screen(projector_get_some_sys_param(P_CUR_CHANNEL));
    change_screen(SCREEN_CHANNEL_USB_CAST);
    //api_dis_show_onoff(0);

#ifdef BLACK_FIELD_DETECTION_SUPPORT
    initialize_blackout_detection(0,NULL);
#endif

    volatile char first_flag = 1;
    while(1) 
    {
        if(cur_scr != last_scr)
        {
            del_volume();
            del_setup_slave_scr_obj();
            //printf("MAX_SCR_NUM: %d \n", MAX_SCR_NUM);
            switch(cur_scr)
            {
            case SCREEN_CHANNEL:
                if (!channel_scr)
                    channel_screen_init();

                _ui_screen_change(channel_scr,0,0);
                break;
            case SCREEN_SETUP: 
                if (!setup_scr)
                    setup_screen_init();

                _ui_screen_change(setup_scr,0,0);
                break;
         #ifdef  WIFI_SUPPORT    
            case SCREEN_WIFI:
                if (!wifi_scr)        
                    wifi_screen_init();    

                _ui_screen_change(wifi_scr, 0, 0);
                break;

            case SCREEN_CHANNEL_WIFI_CAST:
                if (!ui_wifi_cast_root)
                    ui_wifi_cast_init();
		#ifdef SYS_ZOOM_SUPPORT
				mainlayer_scale_type_set(MAINLAYER_SCALE_TYPE_CAST);
		#endif

                _ui_screen_change(ui_wifi_cast_root, 0, 0);
                break;
        #endif

        #ifdef  USBMIRROR_SUPPORT    
            case SCREEN_CHANNEL_USB_CAST:
                if (!ui_um_play)
                    ui_um_play_init();
                // win_um_play_open(NULL);
                _ui_screen_change(ui_um_play, 0, 0);
                break;
            #endif   
#ifdef HDMIIN_SUPPORT           
            case SCREEN_CHANNEL_HDMI:
        #ifdef HDMI_SWITCH_SUPPORT
            case SCREEN_CHANNEL_HDMI2:
        #endif    
                if (!hdmi_scr)
                    hdmi_screen_init();
            #ifdef HDMI_SWITCH_SUPPORT
                if(projector_get_some_sys_param(P_CUR_CHANNEL) != cur_scr){
                    if(cur_scr == SCREEN_CHANNEL_HDMI)
                        projector_set_some_sys_param(P_CUR_CHANNEL, SCREEN_CHANNEL_HDMI);
                    else
                        projector_set_some_sys_param(P_CUR_CHANNEL, SCREEN_CHANNEL_HDMI2);
                    hdmi_rx_leave();
                }
            #endif
                    _media_play_exit();
            #ifdef CVBSIN_SUPPORT
                    cvbs_rx_stop();
            #endif
            #ifdef SYS_ZOOM_SUPPORT
                    mainlayer_scale_type_set(MAINLAYER_SCALE_TYPE_HDMI);
            #endif
                    _ui_screen_change(hdmi_scr, 0, 0);
                    break;
#endif

#ifdef HCIPTV_YTB_SUPPORT
            case SCREEN_WEBSERVICE:
                if (!webservice_scr)
                    webservice_screen_init();

                _ui_screen_change(webservice_scr,0,0);
                break;
            
            case SCREEN_WEBPLAYER:
                if (!webplay_scr)
                    webplay_screen_init();

                _ui_screen_change(webplay_scr,0,0);
                break;
#endif
#ifdef CVBSIN_SUPPORT
            case SCREEN_CHANNEL_CVBS:
                if (!cvbs_scr)
                    cvbs_screen_init();

                _media_play_exit();
#ifdef HDMIIN_SUPPORT
            #ifdef HDMI_SWITCH_SUPPORT
                hdmi_rx_leave();
            #else
                hdmirx_pause();
            #endif
#endif
#ifdef SYS_ZOOM_SUPPORT
                mainlayer_scale_type_set(MAINLAYER_SCALE_TYPE_CVBS);
#endif
                _ui_screen_change(cvbs_scr, 0, 0);
                break;
#endif
            case SCREEN_CHANNEL_MAIN_PAGE:
	            if (!main_page_scr)
	                main_page_init();

                _ui_screen_change(main_page_scr, 0, 0);
                _media_play_exit();

            #if defined(CAST_SUPPORT)&&defined(WIFI_SUPPORT)
                cast_stop_service();
            #endif
            #ifdef BLUETOOTH_SUPPORT
                /*set spdif audio channel when into media*/
                bluetooth_ioctl(BLUETOOTH_SET_AUDIO_CHANNEL_INPUT,1);
            #endif 
#ifdef HDMIIN_SUPPORT
            #ifdef HDMI_SWITCH_SUPPORT
                hdmi_rx_leave();
            #else
                hdmirx_pause();//hdmi_rx_leave();
            #endif
#endif
            #ifdef CVBSIN_SUPPORT
                cvbs_rx_stop();
            #endif
                break;
#ifdef HC_FACTORY_TEST      
            case SCREEN_FACTORY_TEST:
                if (!factory_settings_scr)
                    ui_factory_settings_init();

                _ui_screen_change(factory_settings_scr, 0, 0);
                break;
#endif              
            case SCREEN_CHANNEL_MP : 
#ifdef HDMIIN_SUPPORT
            #ifdef HDMI_SWITCH_SUPPORT
                hdmi_rx_leave();
            #else
                hdmirx_pause();//hdmi_rx_leave();
            #endif
#endif
            #ifdef CVBSIN_SUPPORT
                cvbs_rx_stop();
            #endif
            #ifdef SYS_ZOOM_SUPPORT
                mainlayer_scale_type_set(MAINLAYER_SCALE_TYPE_LOCAL);
            #endif
                switch(get_screen_submp())
                {
                case SCREEN_SUBMP0 : 
                    if (!ui_mainpage)
                        ui_mainpage_screen_init();

                    _ui_screen_change(ui_mainpage, 0, 0);
                    break;
                case SCREEN_SUBMP1: 
                    if (!ui_subpage)
                        ui_subpage_screen_init();

                    _ui_screen_change(ui_subpage, 0, 0);
                    break;
                case SCREEN_SUBMP2: 
                    if (!ui_fspage)
                        ui_fspage_screen_init();

                    _ui_screen_change(ui_fspage, 0, 0);
                    break;  
                case SCREEN_SUBMP3:
                    if (!ui_ebook_txt)
                        ui_ebook_screen_init();
                    if (!ui_mainpage)
                        ui_mainpage_screen_init();
                    if (!ui_ctrl_bar)
                        ui_ctrl_bar_screen_init();

                    if(mp_get_cur_player_hdl()==NULL)     //from other page enter 
                    {
                        if (get_ebook_fp_state())
                            _ui_screen_change(ui_ebook_txt, 0, 0);
                        else
                            _ui_screen_change(ui_mainpage, 0, 0);
                    }
                    else
                    {
                        _ui_screen_change(ui_ctrl_bar, 0, 0);
                    }    
                    break;                                                                                                      
                }
                break;
        #ifdef HC_MEDIA_MEMMORY_PLAY
            case SCREEN_CHANNEL_MP_PLAYBAR:
            #ifdef SYS_ZOOM_SUPPORT
                mainlayer_scale_type_set(MAINLAYER_SCALE_TYPE_LOCAL);
            #endif

                if (!ui_ctrl_bar)
                    ui_ctrl_bar_screen_init();

                //recover the upper screen
                cur_screen_set(SCREEN_CHANNEL_MP, SCREEN_CHANNEL_MP);
                projector_set_some_sys_param(P_CUR_CHANNEL, SCREEN_CHANNEL_MP);
                _ui_screen_change(ui_ctrl_bar, 0, 0);

                break;
            case SCREEN_CHANNEL_MP_EBOOK:
                if (!ui_ebook_txt)
                    ui_ebook_screen_init();

                //recover the upper screen
                cur_screen_set(SCREEN_CHANNEL_MP, SCREEN_CHANNEL_MP);
                projector_set_some_sys_param(P_CUR_CHANNEL, SCREEN_CHANNEL_MP);
                _ui_screen_change(ui_ebook_txt, 0, 0);

                break;
        #endif
        #ifdef AIRP2P_SUPPORT
            case SCREEN_AIRP2P:
                if (!ui_airp2p_cast_root)
                {   
                    ui_cast_airp2p_init();
                }
                _ui_screen_change(ui_airp2p_cast_root, 0, 0);
                     
                break;
        #endif
            default:
                break;
            }
      
            last_scr = cur_scr;
         
        }
    
        message_ctrl_process(); 
    #ifdef BACKLIGHT_MONITOR_SUPPORT
        api_pwm_backlight_monitor_loop();
    #endif
        lv_task_handler();           

        if (first_flag)        
        {
            //Hide boot logo after UI showed.
            api_dis_show_onoff(0);

            // int64_t show_time = api_sys_clock_time_check_get(NULL);
            // printf("\n************* first UI show time : %lld us! *******************\n\n", show_time);        
            
            //Only run once here.
            //init other modules/screen later here is for fast showing UI.
        #ifdef UI_FAST_SHOW_SUPPORT
            ui_screen_init();
        #endif
            post_sys_init();

            first_flag = 0;
            sleep(3);
            puts("111111111");
            win_um_play_open(NULL);
            puts("22222222");
        }

        usleep(10000);
    }

}

#ifdef __HCRTOS__
static void lvgl_exit(void)
{
    lv_deinit();
}
static int lvgl_stop(int argc, char **argv)
{
    struct fb_var_screeninfo var;
    int fbfd = 0;

    fbfd = open(FBDEV_PATH, O_RDWR);
    if (fbfd == -1)
        return -1;

    ioctl(fbfd, FBIOBLANK, FB_BLANK_NORMAL);
    ioctl(fbfd, FBIOGET_VSCREENINFO, &var);
    var.yoffset = 0;
    var.xoffset = 0;
    ioctl(fbfd, FBIOPUT_VSCREENINFO, &var);
    close(fbfd);

    if (projector_thread != NULL)
        vTaskDelete(projector_thread);
    lvgl_exit();

    fbdev_exit();

    return 0;
}

static int projector_start(int argc, char **argv)
{
    // start projector main task.
    xTaskCreate(projector_task, (const char *)"projector_solution", 0x2000/*configTASK_STACK_DEPTH*/,
            NULL, portPRI_TASK_NORMAL, &projector_thread);
    return 0;
}

static int projector_auto_start(void)
{
    projector_start(0, NULL);
    return 0;
}

__initcall(projector_auto_start)


#endif
