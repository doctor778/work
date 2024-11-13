//#include "app_config.h"
#include <hcuapi/dis.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#ifdef __HCRTOS__
#include <hcuapi/watchdog.h>

#include <freertos/FreeRTOS.h>
#include <hcuapi/sys-blocking-notify.h>
#include <kernel/fb.h>
#include <kernel/io.h>
#include <kernel/lib/fdt_api.h>
#include <kernel/notify.h>
#include <linux/notifier.h>
#if !defined(CONFIG_DISABLE_MOUNTPOINT)
#include <sys/mount.h>
#endif

#else
#include <linux/fb.h>
#include <linux/watchdog.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/un.h>
#endif

#include <fcntl.h>
#include <ffplayer.h>
#include <hcuapi/common.h>
#include <hcuapi/hdmi_tx.h>
#include <hcuapi/kumsgq.h>
#include <hcuapi/snd.h>
#include <hcuapi/vidsink.h>
#include <sys/ioctl.h>

// #include "network_api.h"
#include <dirent.h>
#include <hcuapi/fb.h>
#include <hcuapi/standby.h>
#include <lvgl/hc-porting/hc_lvgl_init.h>

#ifdef WIFI_SUPPORT
#include <hccast/hccast_net.h>
#include <hccast/hccast_wifi_mgr.h>
#include <net/if.h>
#endif

#include <hcuapi/input-event-codes.h>
#include <hcuapi/input.h>

#include "lv_drivers/display/fbdev.h"
#include "lvgl/lvgl.h"
#include "lvgl/src/misc/lv_gc.h"
#include "lvgl/src/misc/lv_ll.h"
#include <hcuapi/snd.h>
// #include "gpio_ctrl.h"

// #include "app_config.h"
// #include "factory_setting.h"
// #include "setup.h"

// #include "screen.h"

// #include "tv_sys.h"
#include "../include/com_api.h"
#include "glist.h"
#include "hcuapi/lvds.h"
#include "../include/os_api.h"
#ifdef __HCRTOS__
#include <hcuapi/gpio.h>
#include <nuttx/wqueue.h>
#endif
// #include "com_logo.h"
static uint32_t m_control_msg_id = INVALID_ID;  //消息队列的ID
static uint32_t boardtest_control_msg_id = INVALID_ID;
// static cast_play_state_t m_cast_play_state = CAST_STATE_IDLE;
// static bool m_ffplay_init = false;
static int m_usb_state = USB_STAT_INVALID;

static int partition_info_msg_send(int type, uint32_t code)
{
    control_msg_t msg = {0};
    memset(&msg, 0, sizeof(control_msg_t));
    switch (type)
    {
    case USB_STAT_MOUNT:
        msg.msg_type = MSG_TYPE_USB_MOUNT;
        break;
    case USB_STAT_UNMOUNT:
        msg.msg_type = MSG_TYPE_USB_UNMOUNT;
        break;
    case USB_STAT_MOUNT_FAIL:
        msg.msg_type = MSG_TYPE_USB_MOUNT_FAIL;
        break;
    case USB_STAT_UNMOUNT_FAIL:
        msg.msg_type = MSG_TYPE_USB_UNMOUNT_FAIL;
        break;
    case SD_STAT_MOUNT:
        msg.msg_type = MSG_TYPE_SD_MOUNT;
        break;
    case SD_STAT_UNMOUNT:
        msg.msg_type = MSG_TYPE_SD_UNMOUNT;
        break;
    case SD_STAT_MOUNT_FAIL:
        msg.msg_type = MSG_TYPE_SD_MOUNT_FAIL;
        break;
    case SD_STAT_UNMOUNT_FAIL:
        msg.msg_type = MSG_TYPE_SD_UNMOUNT_FAIL;
        break;
    }
    msg.msg_code = code;
    api_control_send_msg(&msg);
}


#ifdef __HCRTOS__

static int usbd_notify(struct notifier_block *self, unsigned long action, void *dev)//是一个通知回调函数，用于处理 USB 设备状态变化的通知。
{
       switch (action) {
    case USB_MSC_NOTIFY_MOUNT://插入u盘
        if(strstr(dev,"sd")){
            m_usb_state = USB_STAT_MOUNT;
        }else if(strstr(dev,"mmc")){
            m_usb_state = SD_STAT_MOUNT;
        }
        if (dev)
		  printf("USB_STAT_MOUNT: %s\n", (char *)dev);
        break;
    case USB_MSC_NOTIFY_UMOUNT://拔出u盘
        if(strstr(dev,"sd")){
            m_usb_state = USB_STAT_UNMOUNT;
        }else if(strstr(dev,"mmc")){
            m_usb_state = SD_STAT_UNMOUNT;
        }        
        break;
    case USB_MSC_NOTIFY_MOUNT_FAIL:
        if(strstr(dev,"sd")){
            m_usb_state = USB_STAT_MOUNT_FAIL;
        }else if(strstr(dev,"mmc")){
            m_usb_state = SD_STAT_MOUNT_FAIL;
        }
        break;
    case USB_MSC_NOTIFY_UMOUNT_FAIL:
        if(strstr(dev,"sd")){
            m_usb_state = USB_STAT_UNMOUNT_FAIL;
        }else if(strstr(dev,"mmc")){
            m_usb_state = SD_STAT_UNMOUNT_FAIL;
        }
        break;
    default:
    #ifdef WIFI_SUPPORT
        usb_wifi_notify_check(action, dev);
    #endif
        return NOTIFY_OK;
        break;
    }
    char * stroage_devname=strdup(dev);//将字符串 dev 复制到新分配的内存空间中
    /*need to free its memory after rsv message */ 
    partition_info_msg_send(m_usb_state,(uint32_t)stroage_devname);

    // partition_info_update(m_usb_state,dev);

    return NOTIFY_OK;
}


    
static struct notifier_block usb_switch_nb = { //用于注册USB状态变化的通知回调函数。
    .notifier_call = usbd_notify,
};

int api_system_init(void);

// void photo_notifier_init(void)
// {
// #ifdef __HCRTOS__
//     sys_register_notify(&usb_switch_nb);//注册 USB 状态变化的通知回调函数
// #else
//     api_system_init();
// #endif
// }

// void photo_notifier_exit(void)
// {
//     sys_unregister_notify(&usb_switch_nb);//取消注册 USB 状态变化的通知回调函数
// }
#else
#define MX_EVENTS (10)
#define EPL_TOUT (1000)

enum EPOLL_EVENT_TYPE
{
    EPOLL_EVENT_TYPE_KUMSG = 0,
    EPOLL_EVENT_TYPE_HOTPLUG_CONNECT,
    EPOLL_EVENT_TYPE_HOTPLUG_MSG,
};

struct epoll_event_data
{
    int fd;
    enum EPOLL_EVENT_TYPE type;
};

typedef struct
{
    int epoll_fd;
    int hdmi_tx_fd;
    int kumsg_fd;
    int hotplug_fd;
} hotplug_fd_t;

static hotplug_fd_t m_hotplug_fd;
static struct epoll_event_data hotplg_data = {0};
static struct epoll_event_data hotplg_msg_data = {0};
static struct epoll_event_data kumsg_data = {0};

static void process_hotplug_msg(char *msg)
{
    // plug-out: ACTION=wifi-remove INFO=v0BDApF179
    // plug-in: ACTION=wifi-add INFO=v0BDApF179

    control_msg_t ctl_msg = {0};
    const char *plug_msg;

    plug_msg = (const char *)msg;
    if (strstr(plug_msg, "ACTION=wifi"))
    {
#ifdef WIFI_SUPPORT
        // usb wifi plugin/plugout message
        if (strstr(plug_msg, "ACTION=wifi-remove"))
        {
            if (0 == m_wifi_plugin)
                return;

            m_wifi_plugin = 0;
            printf("Wi-Fi plug-out\n");
            network_wifi_module_set(0);
            ctl_msg.msg_type = MSG_TYPE_USB_WIFI_PLUGOUT;
            api_control_send_msg(&ctl_msg);
        }
        else if (strstr(plug_msg, "ACTION=wifi-add"))
        {
            m_wifi_plugin = 1;
            if (strstr(plug_msg, "INFO=v0BDApF179"))
            {
                printf("Wi-Fi probed RTL8188_FTV\n");
                network_wifi_module_set(HCCAST_NET_WIFI_8188FTV);
            }
            else if (strstr(plug_msg, "INFO=v0BDAp0179") ||
                     strstr(plug_msg, "INFO=v0BDAp8179"))
            {
                printf("Wi-Fi probed RTL8188_ETV\n");
                network_wifi_module_set(HCCAST_NET_WIFI_8188FTV);
            }
            else if (strstr(plug_msg, "INFO=v0BDAp8733") ||
                     strstr(plug_msg, "INFO=v0BDApB733") ||
                     strstr(plug_msg, "INFO=v0BDApF72B"))
            {
                printf("Wi-Fi probed RTL8731BU\n");
                network_wifi_module_set(HCCAST_NET_WIFI_8733BU);
            }
            else if (strstr(plug_msg, "INFO=v0BDAp8731"))
            {
                printf("Wi-Fi probed RTL8731AU\n");
                network_wifi_module_set(HCCAST_NET_WIFI_8811FTV);
            }
            else if (strstr(plug_msg, "INFO=v0BDApC811"))
            {
                printf("Wi-Fi probed RTL8811_FTV\n");
                network_wifi_module_set(HCCAST_NET_WIFI_8811FTV);
            }
            else
            {
                printf("Unknown Wi-Fi probed: %s!\n", plug_msg);
                return;
            }

            ctl_msg.msg_type = MSG_TYPE_USB_WIFI_PLUGIN;
            api_control_send_msg(&ctl_msg);
        }
#endif
    }
    else
    {
        // usb disk plugin/plugout message
        uint8_t mount_name[32];
        // usb-disk is plug in (SD??)
        if (strstr(plug_msg, "ACTION=mount"))
        {
            sscanf(plug_msg, "ACTION=mount INFO=%s", mount_name);
            printf("U-disk is plug in: %s\n", mount_name);
            if (strstr(mount_name, "sd") || strstr(mount_name, "hd") || strstr(mount_name, "usb"))
            {
                m_usb_state = USB_STAT_MOUNT;
            }
            else if (strstr(mount_name, "mmc"))
            {
                m_usb_state = SD_STAT_MOUNT;
            }
            // Enter upgrade window if there is upgraded file in USB-disk(hc_upgradexxxx.bin)
            char *stroage_devname = strdup(mount_name);
            /*need to free its memory after rsv message */
            partition_info_msg_send(m_usb_state, (uint32_t)stroage_devname);
        }
        else if (strstr(plug_msg, "ACTION=umount"))
        {
            sscanf(plug_msg, "ACTION=umount INFO=%s", mount_name);
            printf("U-disk is plug out: %s\n", mount_name);
            if (strstr(mount_name, "sd") || strstr(mount_name, "hd") || strstr(mount_name, "usb"))
            {
                m_usb_state = USB_STAT_UNMOUNT;
            }
            else if (strstr(mount_name, "mmc"))
            {
                m_usb_state = SD_STAT_UNMOUNT;
            }
            char *stroage_devname = strdup(mount_name);
            /*need to free its memory after rsv message */
            partition_info_msg_send(m_usb_state, (uint32_t)stroage_devname);
        }
    }
}

static void do_kumsg(KuMsgDH *msg)
{
    switch (msg->type)
    {
    case HDMI_TX_NOTIFY_CONNECT:
        // m_hdmi_tx_plugin = 1;
        printf("%s(), line: %d. hdmi tx connect\n", __func__, __LINE__);
        break;
    case HDMI_TX_NOTIFY_DISCONNECT:
        // m_hdmi_tx_plugin = 0;
        printf("%s(), line: %d. hdmi tx disconnect\n", __func__, __LINE__);
        break;
    // case HDMI_TX_NOTIFY_EDIDREADY:
    // {
    //     struct hdmi_edidinfo *edid = (struct hdmi_edidinfo *)&msg->params;
    //     printf("%s(), best_tvsys: %d\n", __func__, edid->best_tvsys);
    //     _hotplug_hdmi_tx_tv_sys_set();
    //     break;
    // }
    default:
        break;
    }
}

static void *hotplug_receive_event_func(void *arg)
{
    struct epoll_event events[MX_EVENTS];
    int n = -1;
    int i;
    struct sockaddr_in client;
    socklen_t sock_len = sizeof(client);
    int len;

    while (1)
    {
        n = epoll_wait(m_hotplug_fd.epoll_fd, events, MX_EVENTS, EPL_TOUT);
        if (n == -1)
        {
            if (EINTR == errno)
            {
                continue;
            }
            usleep(100 * 1000);
            continue;
        }
        else if (n == 0)
        {
            continue;
        }

        for (i = 0; i < n; i++)
        {
            struct epoll_event_data *d = (struct epoll_event_data *)events[i].data.ptr;
            int fd = (int)d->fd;
            enum EPOLL_EVENT_TYPE type = d->type;

            switch (type)
            {
            case EPOLL_EVENT_TYPE_KUMSG:
            {
                unsigned char msg[MAX_KUMSG_SIZE] = {0};
                len = read(fd, (void *)msg, MAX_KUMSG_SIZE);
                if (len > 0)
                {
                    do_kumsg((KuMsgDH *)msg);
                }
                break;
            }
            case EPOLL_EVENT_TYPE_HOTPLUG_CONNECT:
            {
                printf("%s(), line: %d. get hotplug connect...\n", __func__, __LINE__);
                struct epoll_event ev;
                int new_sock = accept(fd, (struct sockaddr *)&client, &sock_len);
                if (new_sock < 0)
                    break;

                hotplg_msg_data.fd = new_sock;
                hotplg_msg_data.type = EPOLL_EVENT_TYPE_HOTPLUG_MSG;
                ev.events = EPOLLIN;
                ev.data.ptr = (void *)&hotplg_msg_data;
                epoll_ctl(m_hotplug_fd.epoll_fd, EPOLL_CTL_ADD, new_sock, &ev);
                break;
            }
            case EPOLL_EVENT_TYPE_HOTPLUG_MSG:
            {
                printf("%s(), line: %d. get hotplug msg...\n", __func__, __LINE__);
                char msg[128] = {0};
                len = read(fd, (void *)msg, sizeof(msg) - 1);
                if (len > 0)
                {
                    printf("%s\n", msg);
                    epoll_ctl(m_hotplug_fd.epoll_fd, EPOLL_CTL_DEL, fd, NULL);
                    if (strstr(msg, "ACTION="))
                    {
                        process_hotplug_msg(msg);
                    }
                }
                else
                {
                    printf("read hotplug msg fail\n");
                }
                break;
            }
            default:
                break;
            }
        }

        usleep(10 * 1000);
    }

    return NULL;
}

static int hotplug_init()
{
    pthread_attr_t attr;
    pthread_t tid;
    struct sockaddr_un serv;
    struct epoll_event ev;
    struct kumsg_event event = {0};

    int ret;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 0x2000);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED); // release task resource itself
    if (pthread_create(&tid, &attr, hotplug_receive_event_func, (void *)NULL))
    {
        printf("pthread_create receive_event_func fail\n");
        goto out;
    }
    pthread_attr_destroy(&attr);

    m_hotplug_fd.hotplug_fd = socket(AF_LOCAL, SOCK_STREAM, 0);
    if (m_hotplug_fd.hotplug_fd < 0)
    {
        printf("socket error\n");
        goto out;
    }
    else
    {
        printf("socket success\n");
    }

    unlink("/tmp/hotplug.socket");
    bzero(&serv, sizeof(serv));
    serv.sun_family = AF_LOCAL;
    snprintf(serv.sun_path, sizeof(serv.sun_path), "/tmp/hotplug.socket"); // strcpy(serv.sun_path, "/tmp/hotplug.socket");
    ret = bind(m_hotplug_fd.hotplug_fd, (struct sockaddr *)&serv, sizeof(serv));
    if (ret < 0)
    {
        printf("bind error\n");
        goto out;
    }
    else
    {
        printf("bind success\n");
    }

    ret = listen(m_hotplug_fd.hotplug_fd, 1);
    if (ret < 0)
    {
        printf("listen error\n");
        goto out;
    }
    else
    {
        printf("listen success\n");
    }

    m_hotplug_fd.epoll_fd = epoll_create1(0);

    hotplg_data.fd = m_hotplug_fd.hotplug_fd;
    hotplg_data.type = EPOLL_EVENT_TYPE_HOTPLUG_CONNECT;
    ev.events = EPOLLIN;
    ev.data.ptr = (void *)&hotplg_data;
    if (epoll_ctl(m_hotplug_fd.epoll_fd, EPOLL_CTL_ADD, m_hotplug_fd.hotplug_fd, &ev) != 0)
    {
        printf("EPOLL_CTL_ADD hotplug fail\n");
        goto out;
    }
    else
    {
        printf("EPOLL_CTL_ADD hotplug success\n");
    }

    m_hotplug_fd.hdmi_tx_fd = open(DEV_HDMI, O_RDWR);
    if (m_hotplug_fd.hdmi_tx_fd < 0)
    {
        printf("%s(), line:%d. open device: %s error!\n",
               __func__, __LINE__, DEV_HDMI);
        goto out;
    }
    m_hotplug_fd.kumsg_fd = ioctl(m_hotplug_fd.hdmi_tx_fd, KUMSGQ_FD_ACCESS, O_CLOEXEC);
    kumsg_data.fd = m_hotplug_fd.kumsg_fd;
    kumsg_data.type = EPOLL_EVENT_TYPE_KUMSG;
    ev.events = EPOLLIN;
    ev.data.ptr = (void *)&kumsg_data;
    if (epoll_ctl(m_hotplug_fd.epoll_fd, EPOLL_CTL_ADD, m_hotplug_fd.kumsg_fd, &ev) != 0)
    {
        printf("EPOLL_CTL_ADD fail\n");
        goto out;
    }

    event.evtype = HDMI_TX_NOTIFY_CONNECT;
    event.arg = 0;
    ret = ioctl(m_hotplug_fd.hdmi_tx_fd, KUMSGQ_NOTIFIER_SETUP, &event);
    if (ret)
    {
        printf("KUMSGQ_NOTIFIER_SETUP 0x%08x fail\n", (int)event.evtype);
        goto out;
    }

    event.evtype = HDMI_TX_NOTIFY_DISCONNECT;
    event.arg = 0;
    ret = ioctl(m_hotplug_fd.hdmi_tx_fd, KUMSGQ_NOTIFIER_SETUP, &event);
    if (ret)
    {
        printf("KUMSGQ_NOTIFIER_SETUP 0x%08x fail\n", (int)event.evtype);
        goto out;
    }
    event.evtype = HDMI_TX_NOTIFY_EDIDREADY;
    event.arg = 0;
    ret = ioctl(m_hotplug_fd.hdmi_tx_fd, KUMSGQ_NOTIFIER_SETUP, &event);
    if (ret)
    {
        printf("KUMSGQ_NOTIFIER_SETUP 0x%08x fail\n", (int)event.evtype);
        goto out;
    }

out:
    return 0;
}

#endif



//定时器注册函数
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <kernel/delay.h>
#include <kernel/lib/console.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <kernel/lib/console.h>

#include <nuttx/wqueue.h>
#include <hcuapi/iocbase.h>
#include <hcuapi/watchdog.h>
#include "../include/factory_setting.h"
#define WATCHDOG_TIMEOUT 400000

static const char *device = "/dev/watchdog";
static int m_dog_fd = -1;

// static void notify_watchdog_call(void *arg, unsigned long param)
// {
//     printf("%s:%d:receive watchdog timer notify\n", __func__, __LINE__);
//     puts("1111111111111111");
//     persistentmem_restart.restart_type =1;
//     if(ppicture){
//         persistentmem_restart.ppnew=ppicture->next;
//     }
//     puts("22222222");
//     // persistentmem_write();
//     puts("33333333");
//     persistentmem_read();
//     printf("persistentmem_restart.restart_type =%d\n",persistentmem_restart.restart_type);
//     puts("44444444");
//     extern int reset(void);
//     puts("5555555");
//     reset();
//     puts("66666666666");
//     return ;
// }

struct work_notifier_s notify_watchdog;

// int watchdog_init(void)
// {
//     int ret = 0;
//     uint32_t watchdog_value = WATCHDOG_TIMEOUT*25;   //10s

//     notify_watchdog.evtype = WDIOC_NOTIFY_TIMEOUT;
//     notify_watchdog.qid = HPWORK;
//     notify_watchdog.remote = false;
//     notify_watchdog.oneshot = false;
//     notify_watchdog.qualifier = NULL;
//     notify_watchdog.arg = NULL;
//     notify_watchdog.worker2 = notify_watchdog_call;
//     work_notifier_setup(&notify_watchdog);

//     m_dog_fd = open(device, O_RDWR);
//     if (m_dog_fd < 0) {
// 	    printf("can't open %s\n",device);
// 	    return -1;
//     }

//     ret = ioctl(m_dog_fd, WDIOC_SETTIMEOUT, (uint32_t)watchdog_value);
//     if (!ret) {
// 	    printf("%d set watchdog timer timeout = %ld\n",__LINE__, watchdog_value);
//     }

//     ret = ioctl(m_dog_fd, WDIOC_GETTIMEOUT, (uint32_t)&watchdog_value);
//     if (!ret) {
// 	    printf("%d get watchdog timer timeout = %ld\n",__LINE__, watchdog_value);
//     } 

//     ret = ioctl(m_dog_fd, WDIOC_SETMODE, WDT_MODE_TIMER );//定时器模式
//     // ret = ioctl(m_dog_fd, WDIOC_SETMODE, WDT_MODE_WATCHDOG );//看门狗模式
//     if (!ret) {
// 	    printf("%d set watchdog timer mode to timer\n",__LINE__);
//     }

//     ret = ioctl(m_dog_fd, WDIOC_START, 0);
//     if (!ret) {
// 	    printf("%d start watchdog timer\n",__LINE__);
//     }
// }

void watchdog_feed(void)
{
    if (m_dog_fd < 0)
    return;

    ioctl(m_dog_fd, WDIOC_KEEPALIVE, 0);
}





//设置系统时钟和获取系统时钟
static long m_clock_first = 0;
static long m_clock_last = 0;

 /**
 * @description: Get system clock time for calculating time-consuming, start to record time
 * @return: microsecond(us)
 */
void api_sys_clock_time_check_start(void)
{
    struct timeval time_t;
    struct timeval tv = {0, 0};
	settimeofday(&tv, NULL);

    int i=gettimeofday(&time_t, NULL);
    m_clock_first = m_clock_last = (long)(time_t.tv_sec * 1000000) + (long)time_t.tv_usec;
    
    printf("m_clock_first =%ld\n",(long)m_clock_first);
    printf("m_clock_last =%ld\n",(long)m_clock_last);
}

 /**
 * @description: Get system clock time for calculating time-consuming, get the time-consuming
 *     from start.
 * @param out:  last_clock, output time-consuming after last calling.    
 * @return: microsecond(us), output time-consuming from start.  
 */
long api_sys_clock_time_check_get(int64_t *last_clock)
{
    long clock_time = 0;
    struct timeval time_t;
    gettimeofday(&time_t, NULL);
    clock_time = (long)time_t.tv_sec * 1000000 + (long)time_t.tv_usec;

    if (last_clock)
        *last_clock = (long)clock_time - (long)m_clock_last;
    m_clock_last = (long)clock_time;

    // printf("m_clock_first=%ld\n",(long)m_clock_first);
    // printf("m_clock_last=%ld\n",(long)m_clock_last);

    return (clock_time - m_clock_first);
}

// osd层缩放
void osd_zoom(int x, int y, int w, int h){
    int fd_fb = open( "/dev/fb0" , O_RDWR);
	if (fd_fb < 0) {
		printf("%s(), line:%d. open device: %s error!\n", 
			__func__, __LINE__,  "/dev/fb0");
		return;
	}
    hcfb_lefttop_pos_t start_pos={x,y}; //osd层的起始位置
    hcfb_scale_t scale_param={1280,720,w,h};    //前面两个数是固定的1280，720是lvgl中的。后面两个是要设置的屏幕的宽和高。备注：放大超过一定值会导致出现logo
    // hcfb_scale_t scale_param={1920,1080,w,h};    //前面两个数是固定的1280，720是lvgl中的。后面两个是要设置的屏幕的宽和高。备注：放大超过一定值会导致出现logo
    ioctl(fd_fb, HCFBIOSET_SET_LEFTTOP_POS, &start_pos);
    ioctl(fd_fb, HCFBIOSET_SCALE, &scale_param); 

	close(fd_fb);
}











int api_system_init(void)
{
    #ifdef __HCRTOS__
        sys_register_notify(&usb_switch_nb);//注册 USB 状态变化的通知回调函数
    #else
        hotplug_init();
    #endif
    return 0;
}


int api_dis_show_onoff(bool on_off)
{
    int fd = -1;
    struct dis_win_onoff winon = {0};

    fd = open("/dev/dis", O_WRONLY);
    if (fd < 0)
    {
        return -1;
    }

    winon.distype = DIS_TYPE_HD;
    winon.layer = DIS_LAYER_MAIN;
    winon.on = on_off ? 1 : 0;

    ioctl(fd, DIS_SET_WIN_ONOFF, &winon);
    close(fd);

    return 0;
}

int api_control_send_msg(control_msg_t *control_msg) //control_msg 消息结构体
{
    if (INVALID_ID == m_control_msg_id)
    {
        m_control_msg_id = api_message_create(CTL_MSG_COUNT, sizeof(control_msg_t));//创建消息
        if (INVALID_ID == m_control_msg_id)
        {
            return -1;
        }
    }
    return api_message_send(m_control_msg_id, control_msg, sizeof(control_msg_t));//发送消息
}

int api_control_receive_msg(control_msg_t *control_msg) //接收消息
{
    if (INVALID_ID == m_control_msg_id)
    {
        return -1;
    }
    return api_message_receive_tm(m_control_msg_id, control_msg, sizeof(control_msg_t), 5);
}

void api_control_clear_msg(msg_type_t msg_type)
{
    control_msg_t msg_buffer;
    while (1)
    {
        if (api_control_receive_msg(&msg_buffer))
        {
            break;
        }
    }
    return;
}

int api_control_send_key(uint32_t key)
{
    control_msg_t control_msg;
    control_msg.msg_type = MSG_TYPE_KEY;
    control_msg.msg_code = key;

    if (INVALID_ID == m_control_msg_id)
    {
        m_control_msg_id = api_message_create(CTL_MSG_COUNT, sizeof(control_msg_t));
        if (INVALID_ID == m_control_msg_id)
        {
            return -1;
        }
    }
    return api_message_send(m_control_msg_id, &control_msg, sizeof(control_msg_t));
}

void api_sleep_ms(uint32_t ms)
{
    usleep(ms * 1000);
}


void api_set_display_area(dis_tv_mode_e ratio) // void set_aspect_ratio(dis_tv_mode_e ratio)
{
    struct dis_aspect_mode aspect = {0};
    struct dis_zoom dz;
    int fd = open("/dev/dis", O_WRONLY);
    if (fd < 0)
    {
        return;
    }
#if 1
    aspect.distype = DIS_TYPE_HD;
    aspect.tv_mode = ratio;
    if(ratio == DIS_TV_4_3)
        aspect.dis_mode = DIS_LETTERBOX; //DIS_PANSCAN
    else if(ratio == DIS_TV_16_9)
        aspect.dis_mode = DIS_PILLBOX; //DIS_PILLBOX
    else if(ratio == DIS_TV_AUTO){
        aspect.dis_mode = DIS_PILLBOX;
    }
	ioctl(fd , DIS_SET_ASPECT_MODE , &aspect);
#else
    dz.layer = DIS_LAYER_MAIN;
    dz.distype = DIS_TYPE_HD;
    int h = get_display_h(); // projector_get_some_sys_param(P_SYS_SCALE_MAIN_LAYER_H);
    int v = get_display_v(); // projector_get_some_sys_param(P_SYS_SCALE_MAIN_LAYER_V);
    if (h * 3 == v * 4 && (ratio == DIS_TV_16_9 || ratio == DIS_TV_AUTO))
    {
        v = v * 3 / 4;
    }
    else if (h * 9 == v * 16 && ratio == DIS_TV_4_3)
    {
        h = h * 3 / 4;
    }
    dz.dst_area.x = get_display_x() + (get_display_h() - h) / 2;
    dz.dst_area.y = get_display_y() + (get_display_v() - v) / 2;
    dz.dst_area.w = h;
    dz.dst_area.h = v;
    dz.src_area.x = 0;
    dz.src_area.y = 0;
    dz.src_area.w = 1920;
    dz.src_area.h = 1080;
    aspect.distype = DIS_TYPE_HD;
    aspect.tv_mode = DIS_TV_16_9;
    aspect.dis_mode = DIS_NORMAL_SCALE;
    if (ratio == DIS_TV_AUTO)
    {
        aspect.dis_mode = DIS_PILLBOX;
    }
    ioctl(fd, DIS_SET_ZOOM, &dz);
    usleep(30 * 1000);
    ioctl(fd, DIS_SET_ASPECT_MODE, &aspect);
#endif
    close(fd);
}

void api_set_display_zoom(int s_x, int s_y, int s_w, int s_h, int d_x, int d_y, int d_w, int d_h)
{
    struct dis_zoom dz = {0};
    dz.layer = DIS_LAYER_MAIN;
    dz.distype = DIS_TYPE_HD;
    dz.src_area.x = s_x;
    dz.src_area.y = s_y;
    dz.src_area.w = s_w;
    dz.src_area.h = s_h;
    dz.dst_area.x = d_x;
    dz.dst_area.y = d_y;
    dz.dst_area.w = d_w;
    dz.dst_area.h = d_h;

    int fd = -1;

    fd = open("/dev/dis", O_WRONLY);
    if (fd < 0)
    {
        return;
    }
    ioctl(fd, DIS_SET_ZOOM, &dz);
    close(fd);
}

void api_set_display_aspect(dis_tv_mode_e ratio, dis_mode_e dis_mode)
{
    int ret = 0;
    dis_aspect_mode_t aspect = {0};

    printf("ratio: %d, dis_mode: %d\n", ratio, dis_mode);
    int fd = open("/dev/dis", O_WRONLY);
    if (fd < 0)
    {
        return;
    }

    aspect.distype = DIS_TYPE_HD;
    aspect.tv_mode = ratio;
    aspect.dis_mode = dis_mode;
    ret = ioctl(fd, DIS_SET_ASPECT_MODE, &aspect);
    if (ret != 0)
    {
        printf("%s:%d: err: DIS_SET_ASPECT_MODE failed\n", __func__, __LINE__);
        close(fd);
        return;
    }
    close(fd);
    return;
}

int api_set_display_zoom2(dis_zoom_t *diszoom_param)
{
    struct dis_zoom dz = {0};
    memcpy(&dz, diszoom_param, sizeof(dis_zoom_t));
    dz.layer = DIS_LAYER_MAIN;
    dz.distype = DIS_TYPE_HD;
    dz.active_mode = DIS_SCALE_ACTIVE_IMMEDIATELY;
    int fd = -1;

    fd = open("/dev/dis", O_WRONLY);
    if (fd < 0)
    {
        return -1;
    }
    ioctl(fd, DIS_SET_ZOOM, &dz);
    close(fd);
    return 0;
}

int api_pic_effect_enable(bool enable)
{
    int vidsink_fd;

    vidsink_fd = open("/dev/vidsink", O_WRONLY);
    if (vidsink_fd < 0)
    {
        return -1;
    }

    if (enable)
    {
        ioctl(vidsink_fd, VIDSINK_ENABLE_IMG_EFFECT, 0);
    }
    else
    {
        ioctl(vidsink_fd, VIDSINK_DISABLE_IMG_EFFECT, 0);
    }

    if (vidsink_fd >= 0)
        close(vidsink_fd);

    return 0;
}

int api_set_backlight_brightness(int val)
{
    int lvds_fd;
    int backlight_fd = 0;
    backlight_fd = open("/dev/backlight", O_RDWR);
    lvds_fd = open("/dev/lvds", O_RDWR);
    if (lvds_fd < 0 && backlight_fd < 0)
    {
        printf("open backlight failed\n");
        return -1;
    }

    if (backlight_fd > 0)
    {
        write(backlight_fd, &val, 4);
        close(backlight_fd);
    }
    if (lvds_fd)
    {
        ioctl(lvds_fd, LVDS_SET_PWM_BACKLIGHT, val);  // lvds set pwm default
        ioctl(lvds_fd, LVDS_SET_GPIO_BACKLIGHT, val); // lvds gpio backlight close
        close(lvds_fd);
    }
    return 0;
}




uint32_t api_sys_tick_get(void)
{
    extern uint32_t custom_tick_get(void);
    return custom_tick_get();
}
