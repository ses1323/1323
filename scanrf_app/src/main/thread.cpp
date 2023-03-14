/*
* =====================================================================================
*
* Filename: thread.cpp

* Version: 1.0
* Created: 06/13/2019
* Revision: none
* Compiler: gcc
*
* =====================================================================================
*/
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

#define WLAN0_NET_PATH "/sys/class/net/wlan0/operstate"
extern unsigned int *virtual_rf_addr;
extern volatile bool flag_unfreezed;
extern volatile bool sendrf_thread_status;
#include <signal.h>
void handle_pipe(int sig)
{
    printf("handle_pipe\n");
}

int init_signal()
{
    struct sigaction sa;
    sa.sa_handler = handle_pipe;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGPIPE,&sa,NULL);
    //do something
}

#ifdef DEBUG_FUNC
void *send_rfdata_file_thread(void *fd)
{
	int cnt = 0, ret=0, write_cnt =100;
	unsigned int offset=0, next_offset = 0;
	int unfreezed = true;
	int soc_fd = *(int *)fd;
	printf("send_rfdata_file_thread IN\n");

	while(write_cnt--){
		for(cnt=0; TCP_PKG_SIZE * cnt < rfdata_file.len; cnt++ ){
			send (soc_fd, rfdata_file.pbuf+TCP_PKG_SIZE*cnt, TCP_PKG_SIZE, 0);
		}
		if(rfdata_file.len - TCP_PKG_SIZE * cnt > 0 )
			send (soc_fd, rfdata_file.pbuf+TCP_PKG_SIZE * cnt, rfdata_file.len - TCP_PKG_SIZE * cnt, 0);

		usleep(50);
	}
}
#endif

// 检测网络连接
// net_path: 网络路径，注意检测的是wlan0、eth0等
// 返回值: 网络正常返回0，异常返回-1
int check_net_link(const char *net_path)
{
    int net_fd;
    char state[20];

    net_fd=open(net_path,O_RDONLY);//以只读的方式打开/sys/class/net/eth0/operstate
    if(net_fd<0)
    {
        printf("open err\n");
        return -1;
    }

    memset(state,0,sizeof(state));
    int ret=read(net_fd,state,10);
    printf("state is %s",state);
    if(NULL!=strstr(state,"up"))
    {
        printf("check net on line\n");
        return 0;
    }
    else if(NULL!=strstr(state,"down"))
    {
       printf("check net is down, off line\n");
       return -1;
    }
    else
    {
        printf("unknown err\n");
        return -1;
    }
}

void *send_rfdata_thread(void *args)
{
	int cnt = 0, ret = 0;
	unsigned int offset = 0, next_offset = 0;
    init_signal();
    signal(SIGPIPE, SIG_IGN);

    sendrf_thread_status = true;
    send_rf_data(virtual_rf_addr, DDR_MAP_RF_SIZE);
    sendrf_thread_status = false;
    flag_unfreezed = false;

	printf("send_rfdata_thread END\n");
}

void remove_wifiap_file(void)
{
    DEBUG_ERR("mv wifi_ap.sh wifi_ap-failed.sh");
    system("mv wifi_ap.sh wifi_ap-failed.sh");
    system("sync");
}

void modify_st60_domain(void)
{
    DEBUG_ERR("reset domain to CN");
    system("echo CN >mfg_update.ini");
    system("sync");
}

//上电后检测网络状态，如果长时间未up, 切换QSPI系统启动，防止设备黑盒
void *check_net_thread(void *args)
{
    int ret=0 ,count = 7;//启动后一分钟内进行检测，没有up，即进行切换系统操作

    while(count--)
    {
        ret = check_net_link(WLAN0_NET_PATH);
        if(ret == 0)
            break;
        else {
            if(count == 1){
                remove_wifiap_file();
                modify_st60_domain();
            }
            else
                sleep(10);
        }
    }
    DEBUG_INFO("check_net_thread EXIT");
}

int thread_init(int argc,char ** argv)
{
	pthread_t tcp_server_tid;
	int ret = 0, fd = 0;

	fd = tcp_server_init();
	if(fd != 0 ){
		ret = pthread_create(&tcp_server_tid, NULL, tcp_server_thread, &fd);
		if (ret!=0)
	    {
	        printf ("Create pthread error!/n");
	        return -1;
	    }
	}

	pthread_join(tcp_server_tid, NULL);
	tcp_server_exit(&fd);
	return 0;
}

