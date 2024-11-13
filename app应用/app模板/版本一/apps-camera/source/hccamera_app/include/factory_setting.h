#ifndef __FACTORY_SETTING_H__
#define __FACTORY_SETTING_H__


struct persistentmem_node_restart{
    int restart_type;  //0 是正常重启   1 是死机重启
    struct file_node * ppnew;
};
extern struct persistentmem_node_restart persistentmem_restart;


#endif