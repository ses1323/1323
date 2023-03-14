#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#include "scan_rf.h"

extern volatile bool flag_unfreezed;

int fpga_load(const char *file_name)
{
    char cmd[50];
    sprintf(cmd,"fpgautil -b %s",file_name);
    printf("%s\n",cmd);
    system(cmd);
    return 0;
}

int cal_network_speed(long start,long stop,unsigned int len)
{
    long tmp;
    tmp=(stop-start)*1.0/1000.0;
    float speed,test;

    if(0 != (stop-start))
    {

        speed = len*(8.0*1000/(stop-start))/1024/1024;
        test = len*(8.0*1000/(stop-start));
        printf("test =%lf bps\n",test);
        printf("@@@(%d,end=%ld,bg=%ld)send speed = %lf Mbps!\n",len,stop,start, speed);
    }
    return 0;
}

void *cal_fpga_write_speed(void *args)
{
    volatile unsigned int cur_off,end_off;
    long start,stop;
    printf("@@@cal_fpga_write_speed start !!\n");
    int diff_val;
    while(1)
    {
        if(flag_unfreezed){
            read_rf_offset((unsigned int *)&cur_off,4);
            start = get_current_time();
            usleep(1000000);
            read_rf_offset((unsigned int *)&end_off,4);
            stop = get_current_time();
            diff_val = end_off - cur_off;

            if(diff_val > 0)
                cal_network_speed(start,stop,diff_val);
            else{
                diff_val = DDR_MAP_RF_SIZE + end_off - cur_off;
                cal_network_speed(start,stop,diff_val);
            }

        }
        else{
            usleep(1);
        }
    }
}
