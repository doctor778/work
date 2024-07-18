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
void transcribe_com_message_process(control_msg_t * ctl_msg);

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
    #ifdef LVGL_MBOX_STANDBY_SUPPORT
        win_open_lvmbox_standby();
        lv_key->state = LV_INDEV_STATE_PR;
    ret = V_KEY_POWER;
    #else
        enter_standby();
    #endif
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


#if defined(AUTO_HTTP_UPGRADE) || defined(MANUAL_HTTP_UPGRADE) 
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
    transcribe_com_message_process(&ctl_msg);
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
    hccast_com_audsink_set(HUDI_AUDSINK_TYPE_I2SO | HUDI_AUDSINK_TYPE_SPO);
  #endif
#endif    

#ifdef WIFI_SUPPORT
    //service would be enabled in cast UI
    network_service_enable_set(false);
    network_connect();
#endif    

#ifdef __linux__
    api_usb_dev_check();
#endif    
    /*Handle LitlevGL tasks (tickless mode)*/

    set_auto_sleep(projector_get_some_sys_param(P_AUTOSLEEP));    

#ifdef BLUETOOTH_SUPPORT
    bluetooth_ioctl(BLUETOOTH_SET_DEFAULT_CONFIG,NULL);
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

    change_screen(projector_get_some_sys_param(P_CUR_CHANNEL));
    //api_dis_show_onoff(0);

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
                hdmi_rx_leave();
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
#include <errno.h>
#include <kernel/delay.h>
#include <libuvc/libuvc.h>
#include "./avilib.h"
static avi_t *avi_file = NULL;
static int animation_effect(char * patch);
static lv_img_dsc_t bmp_src;
static unsigned char *osd_buf = NULL;
static unsigned char *dis_buf = NULL;
static unsigned char *out_buf = NULL;
static unsigned char *jpg_buf = NULL;
static control_msg_t msg_ret = {0}; 
static int animation_width;
static int animation_height;
static int width_one_tenth;
static int width_obj=0;
static int height_obj=0;
static int height_one_tenth;
static pthread_mutex_t g_mutex ;
static pthread_cond_t cover_cond = PTHREAD_COND_INITIALIZER;
struct timespec pthread_ts;
#define osd_size 3686400     //1280 * 720 * 4
static void *decode_hld;
struct mjpg_decoder {
    struct video_config cfg;
    int fd;
};
static int uvc_open_camera(int argc,char *argv[]);
static uint8_t *jpg_data = NULL;
static int jpg_size = 0;
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

typedef struct {
    unsigned char a;
    unsigned char r;
    unsigned char g;
    unsigned char b;
} ARGBPixel;

static ARGBPixel bilinear_interpolation(ARGBPixel tl, ARGBPixel tr, ARGBPixel bl, ARGBPixel br, float x_ratio, float y_ratio) {
    ARGBPixel result;
    result.r = (unsigned char)((tl.r * (1 - x_ratio) * (1 - y_ratio)) + (tr.r * x_ratio * (1 - y_ratio)) + (bl.r * (1 - x_ratio) * y_ratio) + (br.r * x_ratio * y_ratio));
    result.g = (unsigned char)((tl.g * (1 - x_ratio) * (1 - y_ratio)) + (tr.g * x_ratio * (1 - y_ratio)) + (bl.g * (1 - x_ratio) * y_ratio) + (br.g * x_ratio * y_ratio));
    result.b = (unsigned char)((tl.b * (1 - x_ratio) * (1 - y_ratio)) + (tr.b * x_ratio * (1 - y_ratio)) + (bl.b * (1 - x_ratio) * y_ratio) + (br.b * x_ratio * y_ratio));
    result.a = (unsigned char)((tl.a * (1 - x_ratio) * (1 - y_ratio)) + (tr.a * x_ratio * (1 - y_ratio)) + (bl.a * (1 - x_ratio) * y_ratio) + (br.a * x_ratio * y_ratio));
    return result;
}

static lv_obj_t * demo_ui;
static lv_obj_t * demo_test_ui;
#define TILE_ROW_SWIZZLE_MASK 3

#define ALOGI log_i
#define ALOGE log_e

static pthread_t camera_thread_id;
static uvc_device_handle_t *devh;
static uvc_device_t *dev;
static uvc_context_t *ctx;
static FILE *uvc_transcribe;
static lv_obj_t * uvc_screen;
static lv_obj_t * uvc_ui;
static lv_obj_t * img_pat;

struct dd_s
{
  int      infd;     
  int      outfd;     
  uint32_t nsectors;   
  uint32_t sector;    
  uint32_t skip;      
  bool     eof;       
  ssize_t  sectsize;  
  ssize_t  nbytes;   
  uint8_t *buffer;    
};

/**
 * @brief 功能类函数，作用：截osd层图，生成argb格式图片
 * 
 * @return int
 *  
 * @note crop_osd_layer
 */
static int crop_osd_layer(void)  
{    
    struct dd_s dd;
    char *infile = NULL;
    int i;
    uint64_t elapsed;

    memset(&dd, 0, sizeof(struct dd_s));

    infile="/dev/fb0";

    dd.sectsize=1024;   
    dd.nsectors=3600;   
    dd.skip=0;          

    dd.buffer = malloc(dd.sectsize);    
    memset(dd.buffer, 0, dd.sectsize); 
    dd.infd = open(infile, O_RDONLY);
    if(dd.infd< 0)
    {
      printf(" dd.infd open  fail ");
      return -1;
    }

    if(!osd_buf){
        printf("11 osd_buf malloc\n");
        osd_buf=malloc(osd_size);
        memset(osd_buf, 0, osd_size); 
    }
    dd.sector = 0;  

    long osd_nbytes=0;
    while (!dd.eof && dd.nsectors > 0)  
    {
        uint8_t *buffer = dd.buffer;   
        ssize_t nbytes;
        dd.nbytes = 0;
        do
        {
            nbytes = read(dd.infd, buffer, dd.sectsize - dd.nbytes);    
            if(nbytes < 0)  
            {   
                printf("nbytes read fail");
            }

            dd.nbytes += nbytes;   
            buffer+= nbytes;   
            }
        while (dd.nbytes < dd.sectsize && nbytes > 0);  
        dd.eof |= (dd.nbytes == 0); 

        if (!dd.eof)    
        {
          for (i = dd.nbytes; i < dd.sectsize; i++)
            {
              dd.buffer[i] = 0;
            }

          if (dd.sector >= dd.skip) 
            {
                uint8_t *buffer = dd.buffer;
                ssize_t written ;
                ssize_t nbytes;
                written = 0;    
                do
                {
                    if(osd_nbytes <= osd_size){
                        memcpy(osd_buf+ osd_nbytes, buffer, dd.sectsize - written);
                    }
                    
                    osd_nbytes+=dd.sectsize - written;
                    nbytes=dd.sectsize - written;

                    written += nbytes;  
                    buffer  += nbytes;  

                }
                while (written < dd.sectsize);  

                dd.nsectors--;  
            }
          dd.sector++; 
        }
    }
    close(dd.infd);
    free(dd.buffer);
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
	display_info->info.layer = DIS_PIC_LAYER_AUX;
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

static int c_bt709_yuvL2rgbF[3][3] =
{ {298,0 ,  459},
  {298,-55 ,-136},
  {298,541,0} };
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


/**
 * @brief 功能类函数，作用：截dis层图，生成argb格式的图片
 * 
 * @return int
 *  
 * @note crop_dis_layer（display_info）
 */
static int crop_dis_layer(struct dis_display_info * display_info) 
{
  
    int buf_size = 0;
    void *buf = NULL;
    int ret = 0;
    unsigned char *p_buf_y = NULL;
    unsigned char *p_buf_c = NULL;
    unsigned char *buf_rgb = NULL;
    int fd = -1;

    fd = open("/dev/dis" , O_RDWR);
    if(fd < 0)
    {
        return -1;
    }

    ret = dis_get_display_info(display_info);
    if(ret <0)
    {
        return -1;
    }

    buf_size = display_info->info.y_buf_size * 4;
    buf_rgb = malloc(buf_size);
    memset(buf_rgb, 0, buf_size); 

    printf("buf =0x%x buf_rgb = 0x%x rgb_buf_size = 0x%x\n", (int)buf, (int)buf_rgb, buf_size);

    YUV420_RGB((unsigned char *)(display_info->info.y_buf),
               (unsigned char *)(display_info->info.c_buf) ,
               buf_rgb ,
               display_info->info.pic_width ,
               display_info->info.pic_height,
               display_info->info.de_map_mode);

    if(!dis_buf){
        dis_buf=malloc(osd_size);
        memset(dis_buf, 0, osd_size);
        printf("malloc dis_buf \n");
        
    }
    if(buf_rgb != NULL){
        memcpy(dis_buf, buf_rgb, osd_size);
    }   
    close(fd);
    free(buf_rgb);
    return 0;

}

/**
 * @brief 功能类函数，作用：将任意分辨率的图像缩放到另一个分辨率
 * 
 * @return int
 *  
 * @note bilinear_interpolate_resize("/media/sda/input.raw","/media/sda/output.raw",1920,1080,1280,720)
 */
static int bilinear_interpolate_resize(char *input_file,char *output_file,int input_width,int input_height,int output_width,int output_height) 
{
    FILE *in_fp = fopen(input_file, "rb");
    if (!in_fp) {
        printf("Error: Unable to open input file.\n");
        return 1;
    }

    FILE *out_fp = fopen(output_file, "wb");
    if (!out_fp) {
        printf("Error: Unable to open output file.\n");
        fclose(in_fp);
        return 1;
    }

    ARGBPixel *input_data = (ARGBPixel *)malloc(input_width * input_height * sizeof(ARGBPixel));
    fread(input_data, sizeof(ARGBPixel), input_width * input_height, in_fp);

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
            ARGBPixel tl = input_data[y_int * input_width + x_int];
            ARGBPixel tr = input_data[y_int * input_width + x_int + 1];
            ARGBPixel bl = input_data[(y_int + 1) * input_width + x_int];
            ARGBPixel br = input_data[(y_int + 1) * input_width + x_int + 1];
            ARGBPixel result = bilinear_interpolation(tl, tr, bl, br, x_frac, y_frac);
            fwrite(&result, sizeof(ARGBPixel), 1, out_fp);
        }
    }
    fclose(in_fp);
    fclose(out_fp);
    free(input_data);

    printf("Image resized successfully.\n");
    return 0;
}

/**
 * @brief 功能类函数，作用：将2个相同大小的分辨率，osd.raw、dis.raw合成一个merge.raw文件
 * 
 * @return int
 *  
 * @note merge_raw_files(1280,720)
 */
static int merge_raw_files(int width,int height ) {
    printf("sizeof(ARGBPixel)*width * height =%d\n",sizeof(ARGBPixel)*width * height);
    
    ARGBPixel *osd_data =(ARGBPixel *)osd_buf;
    ARGBPixel *dis_data =(ARGBPixel *)dis_buf;
    for (int i = 0; i < width * height; ++i) {
        if (osd_data[i].a == 0) {
            osd_data[i] = dis_data[i];
        }
    }
    if(dis_buf){
        free(dis_buf);
        dis_buf=NULL;
        dis_data=NULL;
    }
    if(!out_buf){
        out_buf=malloc(osd_size);
        memset(out_buf, 0, osd_size);
        printf("malloc out_buf \n");
    }
    if(osd_data !=NULL){
        memcpy(out_buf, osd_data, osd_size);
    }
        
    if(osd_buf){
        free(osd_buf);
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
    hcfb_lefttop_pos_t start_pos={x1,y1}; //osd层的起始位置
    // hcfb_lefttop_pos_t start_pos={x,y}; //osd层的起始位置是（x/2，y/2）
    hcfb_scale_t scale_param={1280,720,w,h};    //前面两个数是固定的1280，720是lvgl中的。后面两个是要设置的屏幕的宽和高。备注：放大超过一定值会导致出现logo
    ioctl(fd_fb, HCFBIOSET_SET_LEFTTOP_POS, &start_pos);
    ioctl(fd_fb, HCFBIOSET_SCALE, &scale_param); 

    close(fd_fb);
}
static int zoom_animation(int argc,char * argv[])
{
    osd_zoom_animation(atoi(argv[1]),atoi(argv[2]),atoi(argv[3]),atoi(argv[4]));
}
/**
 * @brief 接口类函数，作用：动画效果
 * 
 * @return int
 *  
 * @note animation_effect /media/sda/1.jpg
 */
#if 0
static int animation_effect(char * patch)
{
    uint8_t *bmp_data = NULL;
	int bmp_size = 0;
	int ret;    

    memset(&bmp_src,0,sizeof(lv_img_dsc_t));

    HCPlayerTranscodeArgs transcode_args4 = {0};
    transcode_args4.url = patch;
    transcode_args4.render_width = 640;
    transcode_args4.render_height = 360;
    transcode_args4.start_time = 0;
    transcode_args4.transcode_mode = 1;
    transcode_args4.transcode_format = 1;
    transcode_args4.dis_layer=1; 
    ret = hcplayer_pic_transcode2(&transcode_args4);
    if (ret < 0) {
        return ret;
    }

    bmp_data=transcode_args4.out;
    bmp_size=transcode_args4.out_size;

    bmp_src.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    bmp_src.header.w = 640;
    bmp_src.header.h = 360;
    bmp_src.data = bmp_data;
    bmp_src.data_size = bmp_size;
    
    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=3;
    msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);

    if (transcode_args4.read_data) {
        free(transcode_args4.read_data);
        transcode_args4.read_data = NULL;
    }
    if (bmp_data) {
        free(bmp_data);
        bmp_data = NULL;
    }
    
    int i=0;
    animation_width=bmp_src.header.w;
    animation_height=bmp_src.header.h;
    width_obj=0;
    height_obj=0;
    for(i=0;i<8;i++){
        width_one_tenth=animation_width/(10-i);
        height_one_tenth=animation_height/(10-i);
        animation_width=animation_width-width_one_tenth;
        animation_height=animation_height-height_one_tenth;

        transcode_args4.render_width = animation_width;
        transcode_args4.render_height = animation_height;
        ret = hcplayer_pic_transcode2(&transcode_args4);
        if (ret < 0) {
            return ret;
        }

        bmp_data=transcode_args4.out;
        bmp_size=transcode_args4.out_size;

        bmp_src.header.w = animation_width;
        bmp_src.header.h = animation_height;
        bmp_src.data = bmp_data;
        bmp_src.data_size = bmp_size;
       
        width_obj+=width_one_tenth;
        height_obj+=-height_one_tenth;

        memset(&msg_ret, 0, sizeof(control_msg_t));
        msg_ret.msg_code=4;
        msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
        api_control_send_msg(&msg_ret);
        pthread_mutex_lock(&g_mutex);
        clock_gettime(CLOCK_REALTIME, &pthread_ts);
        pthread_ts.tv_nsec += 0;
        pthread_ts.tv_sec += 2; 
        pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
        pthread_mutex_unlock(&g_mutex);

        if (transcode_args4.read_data) {
            free(transcode_args4.read_data);
            transcode_args4.read_data = NULL;
        }
        if (bmp_data) {
            free(bmp_data);
            bmp_data = NULL;
        }
    }
    // uvc_open_camera(1,NULL);
    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=5;
    msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);

    printf("exit animation_effect");
}
#else
static int animation_effect(char * patch)
{
    #if 1
    memset(&bmp_src,0,sizeof(lv_img_dsc_t));
    bmp_src.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    bmp_src.header.w = 1280;
    bmp_src.header.h = 720;
    bmp_src.data = jpg_data;
    bmp_src.data_size = jpg_size;

    

    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=6;
    msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);
    
    int i=0;
    animation_width=960;
    animation_height=540;
    width_obj=0;
    height_obj=0;

    int osd_width=480;
    int osd_height=270;
    int one_tenth_width=0;
    int one_tenth_height=0;
    osd_zoom_animation(480,270,960,540);
    usleep(100*1000);
    for(i=0;i<9;i++){
        printf("\n i=%d \n",i);
        
        width_one_tenth=animation_width/(10-i);
        height_one_tenth=animation_height/(10-i);

        animation_width=animation_width-width_one_tenth;
        animation_height=animation_height-height_one_tenth;

        // osd_width+=width_one_tenth;
        // osd_height+=-height_one_tenth;
        osd_width=(1920-animation_width/2)-animation_width;
        osd_height=animation_height/2;
        printf("osd_width =%d\n",osd_width);
        printf("osd_height =%d\n",osd_height);
        osd_zoom_animation(osd_width,osd_height,animation_width,animation_height);
        usleep(100*1000);
    }
    // osd_zoom_animation(20,20,960-96,540-54);   // (960-96)/1920 *1280=576   (540-54)/1080 *720=324 
    puts("\n 2222 \n");
    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=5;
    msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);
    printf("exit animation_effect");

    #endif
}

#endif
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
    p->cfg.pbp_mode = VIDEO_PBP_2P_ON; 
    p->cfg.dis_type = DIS_TYPE_HD;  
    p->cfg.dis_layer = DIS_LAYER_AUXP; 
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
        printf("p->cfg.pbp_mode = %d\n",p->cfg.pbp_mode);
        ALOGE("Write AvPktHd fail\n");
        return -1;
    }

    if (write(p->fd, video_frame, packet_size) != (int)packet_size) {
        printf("p->cfg.pbp_mode = %d\n",p->cfg.pbp_mode);
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
        if (uvc_transcribe) {
            fwrite(frame->data, frame->data_bytes, 1, uvc_transcribe);
        } 
       
        if(avi_file){
            AVI_write_frame(avi_file,(char *)frame->data,frame->data_bytes,1);
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

static void *open_camera(void *arg)
{
    uvc_stream_ctrl_t ctrl;
    uvc_error_t res;
    uvc_transcribe=NULL;

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

    // width=640;
    // height=480;
    printf("\nFirst format: (%4s) %dx%d %dfps\n",format_desc->fourccFormat, width, height, fps);

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

/**
 * @brief 接口类函数，作用：打开摄像头
 * 
 * @return int
 *  
 * @note uvc_open_camera
 */
static int uvc_open_camera(int argc,char *argv[])
{   
    // if(argc != 1){
    //     printf("Enter: uvc_open_camera \n");
    //     return -1;
    // }
   
    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_INIT;
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

/**
 * @brief 接口类函数，作用：关闭摄像头
 * 
 * @return int
 *  
 * @note uvc_close_camera
 */
static int uvc_close_camera(int argc,char *argv[])
{   
    camera_deinit();
}


/**
 * @brief 接口类函数，作用：开始录制，可以生成mjpeg格式文件，或者 avi格式文件
 * 
 * @return int
 *  
 * @note camera_start_record /media/sda/1.mjpeg  或者  camera_start_record /media/sda/1.avi 
 */
static int camera_start_record(int argc,char *argv[])
{   
    #if 0
    if(argc !=2){
        printf("Enter: camera_start_record /media/sda/1.mjpeg \n");
        return -1;
    }
    uvc_transcribe = fopen(argv[1], "wb");
    if(uvc_transcribe){
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
}

/**
 * @brief 接口类函数，作用：停止录制
 * 
 * @return int
 *  
 * @note camera_stop_record
 */
static int camera_stop_record(int argc,char *argv[])
{   
    #if 0
    if(uvc_transcribe){
        if (fclose(uvc_transcribe) == 0) {
            printf("File closed successfully\n");
        } else {
            printf("File closing failure\n");
        }
        uvc_transcribe=NULL;
    }
    #else
        if(avi_file){
            AVI_close(avi_file);
            avi_file=NULL;
        }
        
    #endif
}

/**
 * @brief 接口类函数，作用：截图
 * 
 * @return int
 *  
 * @note screenshot /media/sda/test.jpg
 */
static int screenshot(int argc,char *argv[])
{   
    if(argc !=2){
        printf("Enter: screenshot /media/sda/test.jpg \n");
        return -1;
    }

    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=1;
    msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);

    int width=1280;
    int height=720;

    crop_osd_layer();

    memset(&msg_ret, 0, sizeof(control_msg_t));
    msg_ret.msg_code=2;
    msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
    api_control_send_msg(&msg_ret);
    pthread_mutex_lock(&g_mutex);
    clock_gettime(CLOCK_REALTIME, &pthread_ts);
    pthread_ts.tv_nsec += 0;
    pthread_ts.tv_sec += 2; 
    pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
    pthread_mutex_unlock(&g_mutex);

    struct dis_display_info display_info = {0};
    crop_dis_layer(&display_info);

    merge_raw_files(width,height);

    bmp_header_t header = {
        .signature="BM",
        .filesize = sizeof(bmp_header_t) + sizeof(bmp_info_header_t) + width * height * 4,
        .reserved = 0,
        .data_offset = sizeof(bmp_header_t) + sizeof(bmp_info_header_t)
    };

    bmp_info_header_t info_header = {
        .info_size = sizeof(bmp_info_header_t),
        .width = width,
        .height = height,
        .planes = 1,
        .bpp = 32,
        .compression = 0,
        .image_size = width * height * 4,
        .x_ppm = 0,
        .y_ppm = 0,
        .colors = 0,
        .important_colors = 0
    };

    if(!jpg_buf){
        jpg_buf=malloc((sizeof(header))+(sizeof(info_header))+(sizeof(uint32_t))*width*height);
        memset(jpg_buf, 0, (sizeof(header))+(sizeof(info_header))+(sizeof(uint32_t))*width*height); 
        printf("malloc jpg_buf \n");
    }
    memcpy(jpg_buf, &header, sizeof(header));
    memcpy(jpg_buf + sizeof(header), &info_header, sizeof(info_header));

    int bytes_per_row = sizeof(uint32_t) * width;
    size_t copy_offset = sizeof(header) + sizeof(info_header);
    size_t max_copy_bytes = sizeof(header) + sizeof(info_header) + sizeof(uint32_t) * width * height;

    for(int y = 0; y < height; y++) {
        if (copy_offset + (height - 1 - y) * bytes_per_row + bytes_per_row > max_copy_bytes) {
            printf("Error: memcpy position out of bounds\n");
            return -1;
        }
        memcpy(jpg_buf + sizeof(header) + sizeof(info_header) + (height - 1 - y) * bytes_per_row, (uint32_t *)out_buf + y * width, bytes_per_row);
    }

    if(out_buf){
        free(out_buf);
        printf("free out_buf \n");
        out_buf=NULL;
    }

    // uvc_close_camera(1,NULL);

    HCPlayerTranscodeArgs transcode_args = {0};
    memset(&transcode_args,0,sizeof(transcode_args));
    transcode_args.read_data=(void *)jpg_buf;
    transcode_args.read_size=(sizeof(header))+(sizeof(info_header))+(sizeof(uint32_t))*width*height;
    transcode_args.render_width = 1280;
    transcode_args.render_height = 720;
    transcode_args.start_time = 0;
    transcode_args.transcode_mode = 1;
    transcode_args.dis_layer=1; 
    transcode_args.transcode_format = 0;

    int ret = hcplayer_pic_transcode2(&transcode_args);
        if (ret < 0) {
            printf("hcplayer_pic_transcode2 is fail");
            if(jpg_buf){
                free(jpg_buf);
                printf("free jpg_buf \n");
                jpg_buf=NULL;
            }
            return -1;
        }else{
            printf("hcplayer_pic_transcode2 is success");
        }

        // uint8_t *jpg_data = NULL;
        // int jpg_size = 0;
        jpg_data=transcode_args.out;
        jpg_size=transcode_args.out_size;
        // FILE *rec = fopen(argv[1], "wb");
        // if (rec) {
        //     fwrite(jpg_data, jpg_size, 1, rec);
        //     fclose(rec);
        //     printf("write pic success\n");
        // } else {
        //     printf("write pic failed\n");
        // }

        if(jpg_buf){
            free(jpg_buf);
            printf("free jpg_buf \n");
            jpg_buf=NULL;
        }

        // if(jpg_data){
        //     free(jpg_data);
        //     printf("free jpg_data \n");
        //     jpg_data=NULL;
        // }
        animation_effect(argv[1]);
        sleep(1);
}

static int aaa(int argc,char *argv[])
{
    int i=0;
    char jpgfile[100];
    char bmpfile[100];

    for(i=2;i<1000;i++){
        sprintf(jpgfile,"/media/sda1/%d.jpg",i);
        printf("jpgfile=%s\n",jpgfile);

        memset(&msg_ret, 0, sizeof(control_msg_t));
        msg_ret.msg_code=1;
        msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
        api_control_send_msg(&msg_ret);
        pthread_mutex_lock(&g_mutex);
        clock_gettime(CLOCK_REALTIME, &pthread_ts);
        pthread_ts.tv_nsec += 0;
        pthread_ts.tv_sec += 2; 
        pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
        pthread_mutex_unlock(&g_mutex);

        int width=1280;
        int height=720;

        crop_osd_layer();

        memset(&msg_ret, 0, sizeof(control_msg_t));
        msg_ret.msg_code=2;
        msg_ret.msg_type = MSG_TYPE_TRANSCRIBE_SCREENSHOT;
        api_control_send_msg(&msg_ret);
        pthread_mutex_lock(&g_mutex);
        clock_gettime(CLOCK_REALTIME, &pthread_ts);
        pthread_ts.tv_nsec += 0;
        pthread_ts.tv_sec += 2; 
        pthread_cond_timedwait(&cover_cond, &g_mutex,&pthread_ts);
        pthread_mutex_unlock(&g_mutex);

        struct dis_display_info display_info = {0};
        crop_dis_layer(&display_info);

        merge_raw_files(width,height);

        bmp_header_t header = {
            .signature="BM",
            .filesize = sizeof(bmp_header_t) + sizeof(bmp_info_header_t) + width * height * 4,
            .reserved = 0,
            .data_offset = sizeof(bmp_header_t) + sizeof(bmp_info_header_t)
        };

        bmp_info_header_t info_header = {
            .info_size = sizeof(bmp_info_header_t),
            .width = width,
            .height = height,
            .planes = 1,
            .bpp = 32,
            .compression = 0,
            .image_size = width * height * 4,
            .x_ppm = 0,
            .y_ppm = 0,
            .colors = 0,
            .important_colors = 0
        };

        if(!jpg_buf){
            jpg_buf=malloc((sizeof(header))+(sizeof(info_header))+(sizeof(uint32_t))*width*height);
            memset(jpg_buf, 0, (sizeof(header))+(sizeof(info_header))+(sizeof(uint32_t))*width*height); 
            printf("malloc jpg_buf \n");
        }
        memcpy(jpg_buf, &header, sizeof(header));
        memcpy(jpg_buf + sizeof(header), &info_header, sizeof(info_header));

        int bytes_per_row = sizeof(uint32_t) * width;
        size_t copy_offset = sizeof(header) + sizeof(info_header);
        size_t max_copy_bytes = sizeof(header) + sizeof(info_header) + sizeof(uint32_t) * width * height;

        for(int y = 0; y < height; y++) {
            if (copy_offset + (height - 1 - y) * bytes_per_row + bytes_per_row > max_copy_bytes) {
                printf("Error: memcpy position out of bounds\n");
                return -1;
            }
            memcpy(jpg_buf + sizeof(header) + sizeof(info_header) + (height - 1 - y) * bytes_per_row, (uint32_t *)out_buf + y * width, bytes_per_row);
        }

        if(out_buf){
            free(out_buf);
            printf("free out_buf \n");
            out_buf=NULL;
        }

        HCPlayerTranscodeArgs transcode_args = {0};
        memset(&transcode_args,0,sizeof(transcode_args));
        transcode_args.read_data=(void *)jpg_buf;
        transcode_args.read_size=(sizeof(header))+(sizeof(info_header))+(sizeof(uint32_t))*width*height;
        transcode_args.render_width = 1280;
        transcode_args.render_height = 720;
        transcode_args.start_time = 0;
        transcode_args.transcode_mode = 1;
        transcode_args.dis_layer=1; 
        transcode_args.transcode_format = 0;

        int ret = hcplayer_pic_transcode2(&transcode_args);
        if (ret < 0) {
            printf("hcplayer_pic_transcode2 is fail");
            if(jpg_buf){
                free(jpg_buf);
                printf("free jpg_buf \n");
                jpg_buf=NULL;
            }
            return -1;
        }else{
            printf("hcplayer_pic_transcode2 is success");
        }

        uint8_t *jpg_data = NULL;
        int jpg_size = 0;
        jpg_data=transcode_args.out;
        jpg_size=transcode_args.out_size;
        FILE *rec = fopen(jpgfile, "wb");
        if (rec) {
            fwrite(jpg_data, jpg_size, 1, rec);
            fclose(rec);
            printf("write pic success\n");
        } else {
            printf("write pic failed\n");
        }

        if(jpg_buf){
            free(jpg_buf);
            printf("free jpg_buf \n");
            jpg_buf=NULL;
        }

        if(jpg_data){
            free(jpg_data);
            printf("free jpg_data \n");
            jpg_data=NULL;
        }
        animation_effect(jpgfile);
        sleep(1);
    }
}

void transcribe_com_message_process(control_msg_t * ctl_msg) 
{
    if(ctl_msg->msg_type == MSG_TYPE_TRANSCRIBE_INIT){
        if(!uvc_screen){
            uvc_screen=lv_obj_create(NULL);
            lv_obj_set_size(uvc_screen,LV_PCT(100),LV_PCT(100));
            lv_obj_set_align(uvc_screen, LV_ALIGN_CENTER); 
            lv_obj_set_scrollbar_mode(uvc_screen,LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_border_width(uvc_screen, 0, 0);
            lv_obj_set_style_pad_all(uvc_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT); 
            lv_obj_clear_flag(uvc_screen, LV_OBJ_FLAG_SCROLLABLE);   
            lv_obj_set_style_outline_width(uvc_screen,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(uvc_screen,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(uvc_screen, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(uvc_screen, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        }
        lv_scr_load_anim(uvc_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 0, 0, false);

        if(!uvc_ui && uvc_screen){
            uvc_ui =lv_obj_create(uvc_screen);
            lv_obj_set_size(uvc_ui,LV_PCT(100),LV_PCT(31));
            lv_obj_set_align(uvc_ui, LV_ALIGN_BOTTOM_MID); 
            lv_obj_set_style_bg_color (uvc_ui, lv_color_hex(0x323232), LV_PART_MAIN|LV_STATE_DEFAULT);
            lv_obj_set_scrollbar_mode(uvc_ui,LV_SCROLLBAR_MODE_OFF);
            lv_obj_set_style_border_width(uvc_ui, 0, 0);
            lv_obj_set_style_pad_all(uvc_ui, 0, LV_PART_MAIN | LV_STATE_DEFAULT); 
            lv_obj_clear_flag(uvc_ui, LV_OBJ_FLAG_SCROLLABLE);   
            lv_obj_set_style_outline_width(uvc_ui,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_shadow_width(uvc_ui,0,LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_text_color(uvc_ui, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_set_style_bg_opa(uvc_ui, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(uvc_ui,LV_OBJ_FLAG_HIDDEN);
        }

        if(!img_pat && uvc_screen){
            img_pat=lv_img_create (uvc_screen);
            lv_obj_set_align(img_pat, LV_ALIGN_CENTER); 
            lv_obj_clear_flag(img_pat, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_style_pad_all(img_pat, 0, 0);
            lv_obj_set_style_bg_opa(img_pat, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
            lv_obj_add_flag(img_pat,LV_OBJ_FLAG_HIDDEN);
        }
    }
    else if(ctl_msg->msg_type == MSG_TYPE_TRANSCRIBE_SCREENSHOT){
        if(ctl_msg->msg_code == 1){
            if(uvc_ui){
                lv_obj_clear_flag(uvc_ui,LV_OBJ_FLAG_HIDDEN);
            }
        }
        else if(ctl_msg->msg_code == 2){
            if(uvc_ui){
                lv_obj_add_flag(uvc_ui,LV_OBJ_FLAG_HIDDEN);
            }
        }
        else if(ctl_msg->msg_code == 3){
            if(img_pat){
                lv_obj_align(img_pat,LV_ALIGN_CENTER,0,0);
                lv_obj_set_size (img_pat, 640, 360);
                lv_img_set_src (img_pat, &bmp_src);
                lv_obj_clear_flag(img_pat,LV_OBJ_FLAG_HIDDEN);
            }
        }
        else if(ctl_msg->msg_code == 4){
            if(img_pat){
                lv_obj_set_size (img_pat, animation_width, animation_height);
                lv_obj_align(img_pat,LV_ALIGN_CENTER,width_obj,height_obj);
                lv_img_set_src (img_pat, &bmp_src);
            }
        }
        else if(ctl_msg->msg_code == 5){
            if(img_pat){
                lv_obj_add_flag(img_pat,LV_OBJ_FLAG_HIDDEN);
                lv_task_handler();
            }
            osd_zoom_animation(0,0,1920,1080);
        }
        else if(ctl_msg->msg_code == 6){
            if(img_pat){
                lv_obj_align(img_pat,LV_ALIGN_CENTER,0,0);
                lv_obj_set_size (img_pat, 1280, 720);
                lv_img_set_src (img_pat, &bmp_src);
                lv_obj_clear_flag(img_pat,LV_OBJ_FLAG_HIDDEN);
            }
            lv_task_handler();    
        }

        pthread_mutex_lock(&g_mutex);
        pthread_cond_signal(&cover_cond);
        pthread_mutex_unlock(&g_mutex);
    }
}

static int bbb(int argc,char *argv[])
{
    uvc_error_t res;
    enum uvc_device_power_mode *mode=NULL;
    int8_t* roll_rel=NULL;
    uint8_t* speed=NULL;

    puts("111111");
    res=uvc_set_roll_rel(devh, 90,0);
    if(res <0 ){
        printf("uvc_set_power_mode is fail \n");
    }
    puts("222");
    res=uvc_get_roll_rel(devh, roll_rel, speed,0x87);
    if(res <0 ){
        printf("uvc_get_power_mode is fail \n");
    }
    puts("333");
    printf("mode =%d\n",*mode);
    puts("444");

/*
111111
E/LNX             [01-01 08:53:32.793 t:02,2] (musb_h_ep0_irq:1202)STALLING ENDPOINT
E/LNX             [01-01 08:53:32.802 t:02,2] (musb_h_ep0_irq:1225)aborting
uvc_set_power_mode is fail 
222
E/LNX             [01-01 08:53:32.826 t:44,0] (musb_h_ep0_irq:1202)STALLING ENDPOINT
E/LNX             [01-01 08:53:32.836 t:44,0] (musb_h_ep0_irq:1225)aborting
uvc_get_power_mode is fail 
333
*/
}

static int ccc(int argc,char *argv[])
{
    char *infile = NULL;
    infile="/dev/fb1";

    unsigned char *buf=malloc(1280*720*4);
    memset(buf,0,1280*720*4);
    int infd = open(infile, O_RDONLY);
    if(infd <0){
        printf("open fail \n");
    }

    int sizenum = read(infd, buf, 1280*720*4);
    if (sizenum != 1280*720*4) {
        printf("read sizenum =%d\n",sizenum);
    }

    FILE *fp = fopen("/media/sda/1.raw", "wb");
    if (fp == NULL) {
        perror("Failed to open file");
        return 1;
    }

    sizenum = fwrite(buf, 1,1280*720*4, fp);
    if (sizenum != 1280*720*4) {
        printf(" fwrite sizenum =%d\n",sizenum);
    }

    free(buf);
    close(infd);
    fclose(fp);
}
CONSOLE_CMD(uvc_open_camera, NULL, uvc_open_camera, CONSOLE_CMD_MODE_SELF, "Turn on the camera")
CONSOLE_CMD(uvc_close_camera, NULL, uvc_close_camera, CONSOLE_CMD_MODE_SELF, "Turn off the camera")
CONSOLE_CMD(camera_start_record, NULL, camera_start_record, CONSOLE_CMD_MODE_SELF, "The camera is recording.")
CONSOLE_CMD(camera_stop_record, NULL, camera_stop_record, CONSOLE_CMD_MODE_SELF, "The camera stops recording.")
CONSOLE_CMD(screenshot, NULL, screenshot, CONSOLE_CMD_MODE_SELF, "truncated operation.")
CONSOLE_CMD(aaa, NULL, aaa, CONSOLE_CMD_MODE_SELF, "truncated operation.")
CONSOLE_CMD(bbb, NULL, bbb, CONSOLE_CMD_MODE_SELF, "truncated operation.")
CONSOLE_CMD(ccc, NULL, ccc, CONSOLE_CMD_MODE_SELF, "truncated operation.")

CONSOLE_CMD(zoom_animation, NULL, zoom_animation, CONSOLE_CMD_MODE_SELF, "truncated operation.")







