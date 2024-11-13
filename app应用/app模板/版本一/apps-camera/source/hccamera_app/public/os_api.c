/**
 * @file os_api.c
 * @author your name (you@domain.com)
 * @brief the common apis used for OS
 * @version 0.1
 * @date 2022-01-21
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifdef __linux__
#include <sys/msg.h>
#include <termios.h>
#include <poll.h>
#include <signal.h>

#else
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <kernel/lib/console.h>
/*#include <showlogo.h>*/
#endif

#include <pthread.h>

#include "../include/com_api.h"
#include "../include/os_api.h"

static pthread_mutex_t m_msg_mutex = PTHREAD_MUTEX_INITIALIZER;

#ifdef __linux__
#define MAX_MSG_SIZE    128
typedef struct{
    long type;
    //char *buff;
    char buff[MAX_MSG_SIZE];
}msg_queue_t;    


uint32_t api_message_create(int msg_count, int msg_length)
{
    puts("\n\n I am api_message_create");
    printf("msg_count =%d\n",msg_count);
    printf("msg_length =%d\n",msg_length);

    (void)msg_count;
    (void)msg_length;
    int msgid = -1;

    pthread_mutex_lock(&m_msg_mutex);
    // static int m_msg_idx = 75412;
    // msgid = msgget((key_t)(m_msg_idx++), 0666 | IPC_CREAT);

    msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
    printf("msgid =%d\n",msgid);

    if (msgid <= 0){
        //make sure the msgid > 0, otherwise msgsnd would error!!!
        perror("msgget failed");
        msgid = msgget(IPC_PRIVATE, 0666 | IPC_CREAT);
        printf("msgid =%d\n",msgid);
        if (msgid <= 0){
            perror("msgget failed again");
            pthread_mutex_unlock(&m_msg_mutex);
            return INVALID_ID;
        }
    } 
    pthread_mutex_unlock(&m_msg_mutex);

    printf("create msg id: %d\n", msgid);
    return (uint32_t)msgid;
}

int api_message_delete(uint32_t msg_id)
{
    int msgid = (int)msg_id;
    if (msgctl(msgid,IPC_RMID,NULL) == -1){
        //fprintf(stderr, "msg delete failed\n");
        printf("msg delete failed\n");
        return -1;
    }
    return 0;
}

int api_message_send(uint32_t msg_id, void *msg, int length)
{
    puts("\n\n I am api_message_send\n\n");
    printf("msg_id =%d\n",msg_id);
    printf("length =%d\n",length);
    int msgid = (int)msg_id;

    if (length >= (MAX_MSG_SIZE-1)){
        printf("Message size too large!\n");
        return -1;
    }

    msg_queue_t msg_queue;
    msg_queue.type = 1;
    memcpy(msg_queue.buff, msg, length);


    if (msgsnd(msgid, (void *)&msg_queue, length, 0) == -1){
        //fprintf(stderr, "msgsnd failed\n");
        perror("msgsnd failed");
        return -1;
    }
    return 0;
}

int api_message_receive(uint32_t msg_id, void *msg, int length)
{
    int msgid = (int)msg_id;
    msg_queue_t msg_queue;

    if (msgrcv(msgid, (void *)&msg_queue, length, 0, IPC_NOWAIT) == -1){
        //fprintf(stderr, "msgrcv failed width erro: %d", errno);
        return -1;
    }    
    memcpy(msg, msg_queue.buff, length);

    //printf("receive msg id: %d,len:%d\n", msgid,length);
    return 0;
}

int api_message_receive_tm(uint32_t msg_id, void *msg, int length, int ms)
{
    int i;
    for (i = 0; i < ms; i++)
    {
        if (0 == api_message_receive(msg_id, msg, length))
            return 0;
        usleep(1000);//sleep 1ms
    }
    return -1;
}


int api_message_get_count(uint32_t msg_id)
{
    int msgid = (int)msg_id; 
    struct msqid_ds info;

    if (msgctl(msgid, IPC_STAT, &info) == -1) {
        printf("%s() error!\n", __func__);
        return -1;
    }
    return info.msg_qnum;
}

#else
uint32_t api_message_create(int msg_count, int msg_length)
{
    QueueHandle_t msgid = NULL;
    msgid = xQueueCreate(( UBaseType_t )msg_count, msg_length);
    if (!msgid) {
        printf ("create msg queue failed\n");
        return 0;
    }
    return (uint32_t)msgid;
}

int api_message_delete(uint32_t msg_id)
{
    QueueHandle_t msgid = ((QueueHandle_t)msg_id);
    vQueueDelete(msgid);
    return 0;
}

int api_message_send(uint32_t msg_id, void *msg, int length)
{
    (void)length;
    QueueHandle_t msgid = ((QueueHandle_t)msg_id);
    if (xQueueSend(msgid, msg, 0) != pdTRUE){
        fprintf(stderr, "xQueueSend failed\n");
        return -1;
    }
    return 0;
}

int api_message_receive(uint32_t msg_id, void *msg, int length)
{
    (void)length;
    QueueHandle_t msgid = ((QueueHandle_t)msg_id);
    if (xQueueReceive((QueueHandle_t)msgid, (void *)msg, 0) != pdPASS){
        //fprintf(stderr, "xQueueReceive failed width erro: %d", errno);
        return -1;
    }
    return 0;
}

int api_message_receive_tm(uint32_t msg_id, void *msg, int length, int ms)
{
    (void)length;
    QueueHandle_t msgid = ((QueueHandle_t)msg_id);
    if (xQueueReceive((QueueHandle_t)msgid, (void *)msg, ms) != pdPASS){
        //fprintf(stderr, "xQueueReceive failed width erro: %d", errno);
        return -1;
    }
    return 0;
}

int api_message_get_count(uint32_t msg_id)
{
    QueueHandle_t msgid = ((QueueHandle_t)msg_id);
    return uxQueueMessagesWaiting(msgid);
}

#endif
