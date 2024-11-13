#include "../include/factory_setting.h"
#include <fcntl.h>  //open、O_RDWR的头文件
#include <unistd.h> //close的头文件
#include <sys/ioctl.h> //ioctl 的头文件
#include <hcuapi/persistentmem.h>
#include <stdio.h>

struct persistentmem_node_restart persistentmem_restart;
// int projector_sys_param_load(void)
// {
// 	struct persistentmem_node_create new_node={0};
// 	struct persistentmem_node node={0};
//     int fd;

// 	fd = open("/dev/persistentmem", O_RDWR);
// 	if (fd < 0) {
// 		printf("Open /dev/persistentmem failed (%d)\n", fd);
// 		return -1;
// 	}

//     node.id = PERSISTENTMEM_NODE_ID_CASTAPP;
// 	node.offset = 0;
// 	node.size = sizeof(struct persistentmem_node_restart);
// 	node.buf =&persistentmem_restart;

// 	if (ioctl(fd, PERSISTENTMEM_IOCTL_NODE_GET, &node) < 0) {
//         new_node.id = PERSISTENTMEM_NODE_ID_CASTAPP;
//         new_node.size = sizeof(struct persistentmem_node_restart);
//         if (ioctl(fd, PERSISTENTMEM_IOCTL_NODE_CREATE, &new_node) < 0) {
//             printf("get sys_data failed\n");
//             close(fd);
//             return -1;
//         }
//         close(fd);
//         return 0;
//     }
//     else{
//         int i=ioctl(fd, PERSISTENTMEM_IOCTL_NODE_GET, &node);
//         printf("i=%d\n",i);
//         printf("\n\n node=%d\n\n",node.size);  //1
//         printf("\n\n node=%d\n\n",node.offset);  //1
//         printf("\n\n node=%d\n\n",(*(struct persistentmem_node_restart *)(node.buf)).restart_type);
//         printf("\n\n node=%d\n\n",(*(struct persistentmem_node_restart *)(node.buf)).path);

//         if((*(struct persistentmem_node_restart *)(node.buf)).path == 444){
//             puts("\n\n fffffffffff \n\n");
//             extern int reset(void);
//             reset();
//         }
//         close(fd);
//         int fd2 = open("/dev/persistentmem", O_RDWR);
//         if (fd2 < 0) {
//             printf("Open /dev/persistentmem failed (%d)\n", fd);
//             return -1;
//         }

//         persistentmem_restart.restart_type=111;
//         persistentmem_restart.path+=persistentmem_restart.restart_type;
//         node.id = PERSISTENTMEM_NODE_ID_CASTAPP;
//         node.offset = 0;
//         node.size = sizeof(struct persistentmem_node_restart);
//         node.buf =&persistentmem_restart;
//         i=ioctl(fd2, PERSISTENTMEM_IOCTL_NODE_PUT, &node);
//         printf("i=%d\n",i);
//         close(fd2);
//     }

    
    

	
// 	return 0;
// }

void persistentmem_read(void)
{
    struct persistentmem_node_create new_node={0};
	struct persistentmem_node node={0};
    int fd;
	fd = open("/dev/persistentmem", O_RDWR);
	if (fd < 0) {
		printf("Open /dev/persistentmem failed (%d)\n", fd);
		return ;
	}

    node.id = PERSISTENTMEM_NODE_ID_CASTAPP;
	node.offset = 0;
	node.size = sizeof(struct persistentmem_node_restart);
	node.buf =&persistentmem_restart;

    if (ioctl(fd, PERSISTENTMEM_IOCTL_NODE_GET, &node) < 0) {
        new_node.id = PERSISTENTMEM_NODE_ID_CASTAPP;
        new_node.size = sizeof(struct persistentmem_node_restart);
        if (ioctl(fd, PERSISTENTMEM_IOCTL_NODE_CREATE, &new_node) < 0) {
            printf("get sys_data failed\n");
            close(fd);
            return ;
        }
    }
    printf("aaa persistentmem_restart.restart_type =%d\n",persistentmem_restart.restart_type);
    close(fd);
    return;
}

// void persistentmem_write(void)
// {
//     printf("bbb persistentmem_restart.restart_type =%d\n",persistentmem_restart.restart_type);

//     struct persistentmem_node_create new_node={0};
// 	struct persistentmem_node node={0};
//     int fd;
// 	fd = open("/dev/persistentmem", O_RDWR);
// 	if (fd < 0) {
// 		printf("Open /dev/persistentmem failed (%d)\n", fd);
// 		return ;
// 	}

//     node.id = PERSISTENTMEM_NODE_ID_CASTAPP;
// 	node.offset = 0;
// 	node.size = sizeof(struct persistentmem_node_restart);
// 	node.buf =&persistentmem_restart;

//     if (ioctl(fd, PERSISTENTMEM_IOCTL_NODE_PUT, &node) < 0) {
//         new_node.id = PERSISTENTMEM_NODE_ID_CASTAPP;
//         new_node.size = sizeof(struct persistentmem_node_restart);
//         if (ioctl(fd, PERSISTENTMEM_IOCTL_NODE_CREATE, &new_node) < 0) {
//             printf("get sys_data failed\n");
//             close(fd);
//             return ;
//         }
//     }

//     close(fd);
//     return;
// }


