#include "stdio.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>

#include "scan_rf.h"

int power_key_interrupt()
{
    int keys_fd;

    struct input_event t;
    printf("power_key_interrupt test\n");
    keys_fd = open("/dev/input/event0", O_RDONLY);

    if(keys_fd<=0)
    {
        printf("open /dev/input/event0 device error!\n");
        return 0;
    }

    while(1)
    {
        if(read(keys_fd,&t,sizeof(t))==sizeof(t))
        {
            printf("int type:%d,coe:%d,value:%d,time: %d\n",t.type,t.code,t.value,t.time);
            if(t.type==EV_KEY) //get key message
            {

            if(t.value==1)
            {
                printf("key %d Pressed time: %d\n",t.code,t.time);
            }
            if(t.value==0)    //return 0 or 1
                printf("key %d %s\n",t.code,(t.value)?"Pressed":"Released");     //1表按下，0表弹起
            }
        }
    }
    close(keys_fd);

    return 0;

}


