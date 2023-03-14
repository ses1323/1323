// libev需要的头文件
#include <ev.h>
#include <stdio.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

#include "scan_rf.h"
#include <queue>
#include <pthread.h>
#include<sys/wait.h>

using namespace std;

#define MSG_QUEUE_CNT               3
#define LOCAL_INDEX_QUEUE_IDX       0
#define SCANTABLE_QUEUE_IDX         1
#define FPGA_RW_IDX                 2

#define ST_SIZE_LIMIT               4096 * 1024
#define LOC_SIZE_LIMIT              1024 * 1024

//#define  DUMP_RFDATA              1
//#define  SAVE_CMD_EN              1
//#define  SEND_RF_FILE             1

struct pkt{
    unsigned int len;
    unsigned int pk_id;
    unsigned char buf[2048];
};

static unsigned char local_buf[1024*1024],st_buf[ST_SIZE_LIMIT];
static pkt msgbuf[4096];
unsigned char cbuf[ST_SIZE_LIMIT + LOC_SIZE_LIMIT];

static int msgn = -1;

#ifdef DEBUG_PC_FUNC
SOCKET_TYPE soc_array[TCP_SOCKET_COUNT + 1];
#else
SOCKET_TYPE soc_array[TCP_SOCKET_COUNT];
#endif
int fd = -1; //文件描述符
int tcp_fpga_read_fd=-1,io_action_cnt=0,tools_action_cnt = 0;
volatile bool dump_rf=false, soc_error = false;
volatile bool wait_cmd_ready= true;
extern unsigned int *virtual_rf_addr;
int read_cb_fd=0, fpga_rw_fd = 0;
volatile bool sendrf_thread_status = false, soc_release = true;
extern led_t green_led, yellow_led;
volatile unsigned int cbuf_idx = 0;

struct ev_loop *loop = EV_DEFAULT,*loop_tools= EV_DEFAULT;
ev_io io_watcher,io_watcher_tools;
ev_timer timer_watcher;
FILE *fp = NULL;
extern unsigned int g_ttcode;
extern unsigned int g_hwver;
extern unsigned char g_dev_charge[2];
extern float g_dev_temp;
extern float g_dev_core_val;
extern int dac_dev;
extern bq40 g_bq40_status;

pthread_mutex_t msg_lock;
pthread_mutex_t g_fpga_lock;
pthread_mutex_t soc_status_lock;

#ifdef DEBUG_CH0_PAR
bool find_freeze_cmd = false;
unsigned g_aim_len = 0;
#endif
pkt* get_msg()
{
    msgn++;
    if (msgn == 4096)
        msgn = 0;
    return &msgbuf[msgn];
}

queue<pkt> q;
queue<pkt*> msg[MSG_QUEUE_CNT];
par_pkt_t par_buf;

void queue_push(unsigned int idx,pkt* i)
{
    if(idx < MSG_QUEUE_CNT){
        pthread_mutex_lock(&msg_lock);
        msg[idx].push(i);
        pthread_mutex_unlock(&msg_lock);
    }
}

void queue_pop(unsigned int idx)
{
    if(idx < MSG_QUEUE_CNT){
        pthread_mutex_lock(&msg_lock);
        if(!msg[idx].empty())
            msg[idx].pop();
        pthread_mutex_unlock(&msg_lock);
    }
}

pkt* queue_front(unsigned int idx)
{
    pkt* p=NULL;
    if(idx < MSG_QUEUE_CNT){
        pthread_mutex_lock(&msg_lock);
        if(!msg[idx].empty())
            p = msg[idx].front();
        pthread_mutex_unlock(&msg_lock);
    }
    return p;
}

bool queue_empty(unsigned int idx)
{
    bool r =false;
    if(idx <MSG_QUEUE_CNT){
        pthread_mutex_lock(&msg_lock);
        r = msg[idx].empty();
        pthread_mutex_unlock(&msg_lock);
    }
    return r;
}

void clear_msg_queue(void)
{
    int idx =0;
    for(idx = 0; idx < MSG_QUEUE_CNT;idx++ ){
        while(!queue_empty(idx)){
            queue_pop(idx);
        }
    }
    cbuf_idx = 0;
	//DEBUG_CH0_PAR
    //find_freeze_cmd = false;
    //free_demo_buf();
    DEBUG_INFO("clear_msg_queue done");
}

unsigned int float_to_uint(float f)
{
    return ( *(unsigned int *)&f  );
}

float uint_to_float(unsigned int i)
{
    return ( *(float *)&i );
}

int check_fpga_state_cmd(unsigned int *val)
{
    stable_t *scan_item,item;
    struct timeval tv;
    long long start_time, stop_time, delta_time;
    int cnt= 12500;

    scan_item = &item;
    scan_item->cmd_tp.sp_dw = SPECIAL;
    scan_item->cmd_tp.cmd_id = FPGA_CMD_READ;
    scan_item->start_offset = *val;//start

    //scan_item->start_offset = 0x00010001;//stop
    write_fpga_cmd((unsigned int *)scan_item, 4*3);
    pthread_mutex_lock(&g_fpga_lock);
    usleep(140); //old is 100us or 20nop;
    read_fpga_cmd(val, 4);

    gettimeofday(&tv, NULL);
    start_time= tv.tv_usec;

    while( *val == 0 && ((cnt--) > 0) ){
        usleep(1);
        printf("###retry cnt=%d###\n",cnt);
        read_fpga_cmd(val, 4);
    }

    gettimeofday(&tv, NULL);
    stop_time = tv.tv_usec;
    delta_time = (stop_time - start_time + 1000000)%1000000;
    DEBUG_INFO("FPGA_CMD_READ delta_time =%d us\n",delta_time);
    pthread_mutex_unlock(&g_fpga_lock);

    printf("check_fpga_state_cmd *val=%x\n",*val);

    return 0;
}

#ifdef SAVE_CMD_EN

void *save_cmd_thread(void *args)
{
    pkt item;
    unsigned int *p;
    unsigned char wbuf[11];
    char ch = '\n';
    int i = 0;
    while(1){

        if ( !q.empty() ){
            //printf("save msg\n");
            memset(wbuf, '\0', 11);
            item = q.front();

            p=(unsigned int *)item.buf;
            #if 0
            if(item.len == 4){
                if(fp != NULL){
                    fwrite(item.buf, 1, item.len, fp);
            }
            }else{
                //printf("save_cmd_thread ,len %d \n",item.len);
                if(item.len%4==0 && item.buf!=NULL){
                    for(i=0;i<item.len/4;i++){
                        sprintf((char *)wbuf, "0x%08x", *(p+i));
                        if(fp!=NULL){
                            fwrite(wbuf, 1, 10, fp);
                            fwrite(&ch, 1, 1, fp);
                        }
                    }
                }else{
                    printf("@@@Error@item.len  is not 4 times , remainder is %d, \
                        item.len=%d\n",item.len%4,item.len);
                }
            }

            #else
            if(strcmp((const char*)item.buf, "LOCAL") == 0)
                p = (unsigned int *)local_buf;
            if(strcmp((const char*)item.buf, "SCAN") == 0)
                p = (unsigned int *)st_buf;

            if(item.len == 4){
                if(fp!=NULL){
                    fwrite(item.buf, 1, item.len, fp);
                }
            } else {
                if(item.len % 4 == 0 ){
                    for(i = 0; i < item.len/4; i++){
                        sprintf((char *)wbuf, "0x%08x", *(p + i));
                        if(fp != NULL){
                            fwrite(wbuf, 1, 11, fp);
                            fwrite(&ch, 1, 1, fp);
                        }
                    }
                } else {
                    printf("@@@Error@item.len  is not 4 times ,\
                        remainder is %d，item.len=%d\n ", item.len%4, item.len);
                }
            }

            #endif
            q.pop();

        }

        usleep(10);
    }
}
#endif

volatile work_status status = WAITING_MSG;
stable_t *scan_item,st_item;

static int cur_idx = 0 ,cnt = 0,timeout=200000,send_once = 0;

char scant_buf[4096*1024];
char lastpkg_cmd[8];
bool lastpkg_unhandle =false;
volatile bool flag_unfreezed=false;
unsigned int check_local_len = 0 ;
volatile unsigned int fpga_real_state = 0;

int close_socket(int idx,int soc_fd)
{
    //int cur_connect = io_action_cnt;
    printf("@@@aim[%d]close_socket\n", idx);
    unsigned int buf[5];

    struct linger linger;
    linger.l_onoff = 1;
    linger.l_linger = 0;

    pthread_mutex_lock(&soc_array[idx].soc_lock);
    pthread_mutex_lock(&soc_status_lock);
    if(soc_array[idx].m_socketfd != 0){
        #if 1
        shutdown(soc_fd, SHUT_RDWR);
        setsockopt(soc_fd, SOL_SOCKET, SO_LINGER, (char *) &linger, sizeof(linger));
        #endif
        close(soc_fd);
        DEBUG_INFO("close_socket[%d],ev_io_stop loop= %x,watcher=%x",
            soc_fd,soc_array[idx].soc_loop,soc_array[idx].soc_w_client);

        soc_array[idx].m_socketfd = 0;
        ev_io_stop(soc_array[idx].soc_loop, soc_array[idx].soc_w_client);
        free(soc_array[idx].soc_w_client);
        soc_array[idx].soc_w_client->fd = 0;
        soc_array[idx].soc_w_client = NULL;

        DEBUG_INFO("clear watcher=%x",soc_array[idx].soc_w_client);

        if(idx == SOCKET_CHN_PARAM){
            flag_unfreezed = false;
            send_start_cmd(buf, 0);
        }

#ifdef DEBUG_PC_FUNC
        if(idx != SOCKET_CHN_PC_TEST_CMD)
        {
            soc_error = true;
            if(io_action_cnt >0 )
                io_action_cnt--;
            else
                io_action_cnt = 0;
        }
        else {
            tools_action_cnt = 0;
        }
#else
        if(io_action_cnt >0 )
            io_action_cnt--;
        else
            io_action_cnt = 0;
        soc_error = true;
#endif

        if(soc_array[0].m_socketfd  == 0 &&
           soc_array[1].m_socketfd  == 0 &&
           soc_array[2].m_socketfd  == 0 ){
           soc_release = true;
           soc_error = false;
           clear_msg_queue();
        }
    }
    pthread_mutex_unlock(&soc_status_lock);
    pthread_mutex_unlock(&soc_array[idx].soc_lock);

}

int to_do_send(int soc_fd,const char *buffer, int length)
{
    int ret = -1,timeout=30000; //3s timeout return

    while (ret < 0 && timeout>0)
    {
        ret = write(soc_fd, buffer, length); //nodelay del; diff from send;
        if (ret < 0)
        {
            int err_no = errno;
            if (((err_no == EINTR) || (err_no == EAGAIN)))
            {
                DEBUG_ERR(" err_no == EINTR ||EAGAIN\n");
                if(soc_fd == soc_array[SOCKET_CHN_PARAM].m_socketfd && !flag_unfreezed ){
                    break;
                } else {
                    timeout--;
                    usleep(30);/**发送区为满时候循环等待，直到可写为止*/
                    continue;
                }
            }else{
                DEBUG_ERR(" to_do_send quit\n");
                usleep(30);
                return -1;
            }
        }
        else if(ret == 0)
        {
            DEBUG_ERR(" FATAL ERROR: to_do_send send cnd =0,maybe soc close \n");
            return -1;
        }else{
            break;
        }
    }
    if(timeout == 0){
        DEBUG_ERR(" FATAL ERROR: to_do_send timeout == 0 \n");
        return -1;
    } else {
        return 0;
    }
}
int send_socket_msg(int soc_fd,char *buffer, int length)
{
    int ret = 0;

    unsigned int cnt, remainder =0;
    int *p = (int*)buffer;
    if(length % 2 != 0 || soc_fd == 0){
        DEBUG_ERR("****  ERROR: wifi write cmd  is not 2 times or soc_fd=0 ****");
        return -1;
    }

    if(length <= TCP_PKG_SIZE){
        if (to_do_send(soc_fd, (const char *)buffer, length) < 0 )
        {
            DEBUG_ERR(" FATAL ERROR:send_socket_msg error ,buf=%x,%x\n",buffer[0],buffer[1]);
            return -1;
        }
    }
    else {
        for (cnt = 0; cnt < length / TCP_PKG_SIZE; cnt++) {
            if(soc_fd == soc_array[SOCKET_CHN_PARAM].m_socketfd && !flag_unfreezed){
                DEBUG_INFO( " ######Break send_socket_msg! soc_fd=%d,flag_unfreezed=%d \n",soc_fd,flag_unfreezed);
                return 0;
            }
            if (to_do_send(soc_fd, (const char *)buffer+ cnt* TCP_PKG_SIZE, TCP_PKG_SIZE) < 0)
            {
                DEBUG_ERR( " FATAL ERROR: send_socket_msg error  \n");
                return -1;
            }

        }
        remainder = length % TCP_PKG_SIZE;
        if (cnt* TCP_PKG_SIZE != (length - remainder)){
            printf(" FATAL ERROR: wifi write remainder error  \n");
        }
        if(remainder > 0){
            if(soc_fd== soc_array[SOCKET_CHN_PARAM].m_socketfd && !flag_unfreezed){
                DEBUG_INFO( " ######Break send_socket_msg!  \n");
                return 0;
            }
            if (to_do_send(soc_fd, (const char *)buffer+ length- remainder, remainder) < 0)
            {
                DEBUG_ERR(" FATAL ERROR: send_socket_msg error \n");
                return -1;
            }

        }
    }

    return 0;
}

int get_tcp_state(int idx,int soc_fd)
{
    struct tcp_info info;

    int len = sizeof(info);
    getsockopt(soc_fd, IPPROTO_TCP, TCP_INFO, &info, (socklen_t *)&len);

    if(info.tcpi_state != TCP_ESTABLISHED || soc_error)
    {
        DEBUG_ERR("[get_tcp_state]connect[%d] error: info.tcpi_state = %d\n",soc_fd,info.tcpi_state);
        close_socket(idx,soc_fd);
        return -1;
    }
    //if(idx == 2)
        //DEBUG_INFO("[%d]info.tcpi_state = %d\n",soc_fd,info.tcpi_state);
    return 0;
}

int kill_power()
{
    int ret;
    ret = gpio_ctl_out(GPIO_KILL_POWER, "0");
    return ret;
}

int update_vinno_img(int soc_fd,char *cmd_arg, unsigned char *ip)
{
   printf("update_vinno_img\n");
   char *argv[3];
   const char * up_name= "vi_update";
#if 0
   int pid;
   pid = fork();
   if(pid < 0){
       printf("error in fork");
       return -1;
   }else if(pid == 0){
    #if UPDATE_IS_EXE
        int test;
        printf("ecexl被调用\n");
        //cmd arg= "1" or "2" , ip = "192.168.3.x"
        if(execl("/run/media/mmcblk0p1/vinno/vi_update",
            "./vi_update",cmd_arg, ip,NULL) == -1){
            printf("error in execl\n");
            return -1;
        }
    #else
        argv[0] = "vi_update";
        argv[1] = cmd_arg;
        argv[2] = (char *)ip;
        printf("vi_update func is called:%s,%s,%s \n",argv[0],argv[1],argv[2]);
        vi_update(3, argv);

    #endif
    }else{
        //wait(NULL);
        printf(" is complete\n");
        return 0;
    }
#else
    argv[0] = (char *)up_name;
    argv[1] = cmd_arg;
    argv[2] = (char *)ip;
    printf("vi_update func(fd=%d) is called:%s,%s,%s \n",soc_fd,argv[0],argv[1],argv[2]);
    vi_update(soc_fd, 3, argv);
#endif
    return 0;
}

int set_tcp_keepalive(int soc_fd)
{
    int keep_alive = 1; // 开启keepalive属性
    int keep_idle = 1; // 如该连接在1秒内没有任何数据往来,则进行探测
    int keep_interval = 1; // 探测时发包的时间间隔为1 秒
    int keep_count = 10; // 探测尝试的次数.如果第1次探测包就收到响应了,则后3次的不再发.
    struct linger so_linger;

    setsockopt(soc_fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keep_alive, sizeof(keep_alive));
    setsockopt(soc_fd, SOL_TCP, TCP_KEEPIDLE, (void*)&keep_idle, sizeof(keep_idle));
    setsockopt(soc_fd, SOL_TCP, TCP_KEEPINTVL, (void *)&keep_interval, sizeof(keep_interval));
    setsockopt(soc_fd, SOL_TCP, TCP_KEEPCNT, (void *)&keep_count, sizeof(keep_count));

    so_linger.l_onoff = true;
    so_linger.l_linger = 0;
    setsockopt(soc_fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);

    //设置发送超时
    struct timeval timeout = {3,0};
    setsockopt(soc_fd, SOL_SOCKET,SO_SNDTIMEO, (char *)&timeout,sizeof(struct timeval));
    setsockopt(soc_fd, SOL_SOCKET,SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval));
}

int get_client_addr(int soc_fd,unsigned char *addr)
{
    struct sockaddr_in sa;

    int len = sizeof(sa);
    if(!getpeername(soc_fd, (struct sockaddr *)&sa, (socklen_t *)&len))
    {
        memcpy(addr,inet_ntoa(sa.sin_addr),strlen(inet_ntoa(sa.sin_addr)));
        printf( "对方addr =%s,IP：%s\n ", addr,inet_ntoa(sa.sin_addr));
        printf( "对方PORT：%d \n", ntohs(sa.sin_port));
    }
    return 0;
}

void *check_tcp_alive(void *args)
{
    int soc_fd,idx;
    struct tcp_info info;

    while(idx < TCP_SOCKET_COUNT )
    {
        soc_fd = soc_array[idx].m_socketfd;

        if(soc_fd > 0)
        {
            //printf("alive idx=%d,  soc_fd = %d \n",idx, soc_fd);

            get_tcp_state(idx,soc_fd);

        }
        //usleep(30000);
        if(soc_error != true)
            usleep(50000);
        idx++;
        if(idx == TCP_SOCKET_COUNT)
            idx = 0;
    }

}

int get_app_version(char *buf,unsigned int get_len)
{
    int ver_len,git_len;
    ver_len =strlen(VERSION);
    git_len = strlen(GITVER);
    if(ver_len + git_len <= get_len){

        strcat(buf, VERSION);
        strcat(buf, GITVER);
        printf("version len = %d,git_len =%d, ver=%s",ver_len,git_len,buf);
    }else{
        DEBUG_INFO("get_app_version length too short");
    }
    return 0;
}

void handle_cmd_list()
{
    pkt* item, q_itm;
    unsigned char save_cmd[4]={'[','5',']','\n'};
    unsigned int cmd_off= 0,scant_off=0;
    unsigned int *p, scant_len;

    while(!queue_empty(LOCAL_INDEX_QUEUE_IDX)){
        item = queue_front(LOCAL_INDEX_QUEUE_IDX);
        if(cmd_off + item->len < 1024*2048){
            memcpy(local_buf + cmd_off, item->buf, item->len);
            cmd_off += item->len;
        }

        queue_pop(LOCAL_INDEX_QUEUE_IDX);
    }

    if(cmd_off%4 == 0){
        #ifdef SAVE_CMD_EN
            save_cmd[1]='4';
            memcpy(q_itm.buf,save_cmd,4);
            q_itm.len = 4;
            q.push(q_itm);
            memset(q_itm.buf,'\0',1024);
            memcpy(q_itm.buf,"LOCAL",5);
            q_itm.len = cmd_off;
            q.push(q_itm);
        #endif
        write_fpga_cmd((unsigned int *)local_buf,cmd_off);
        check_local_len = cmd_off;
    } else {
        DEBUG_ERR("######handle_cmd_list item.cmd_off %4 !=0,  cmd_off=%d\n",cmd_off);
    }

    while(!queue_empty(SCANTABLE_QUEUE_IDX)){
        item = queue_front(SCANTABLE_QUEUE_IDX);
        if(item->buf != NULL && item->len != 0){
            p=(unsigned int *)item->buf;

            if(*p == 0xAABBCCDD && *(p+1) == SCAN_TABLE_UPDATE){
                scant_len = *(p+3);
                DEBUG_INFO("####handle_cmd_list item scant_len =%d\n", scant_len);
                memcpy(st_buf,p + 4, item->len - 16);
                scant_off =  item->len - 16;
            }else{
                memcpy(st_buf + scant_off, item->buf, item->len);
                scant_off += item->len;
            }

        }
        queue_pop(SCANTABLE_QUEUE_IDX);
    }

    //printf("######### LOC/ST BUF CLEAR ###########\n\n\n");
    if(scant_off % 4 == 0){
        #ifdef SAVE_CMD_EN
            save_cmd[1]='1';
            memcpy(q_itm.buf,save_cmd,4);
            q_itm.len = 4;
            q.push(q_itm);

            memset(q_itm.buf,'\0',1024);
            memcpy(q_itm.buf,"SCAN",4);
            q_itm.len = scant_off;
            q.push(q_itm);
        #endif
        memcpy(virtual_scant_addr, st_buf,scant_off);
    }
    else{
        DEBUG_ERR("######handle_cmd_list item.cmd_off %4 !=0,  scant_off=%d#####\n",scant_off);
    }
}


void *handle_scanrf_thread(void *args)
{
    pkt* item, q_itm;
    unsigned int *p, scant_len, index, j, ret;

    while(1){
        if(!queue_empty(FPGA_RW_IDX)){
            item = queue_front(FPGA_RW_IDX);
            //printf("@@@@@handle_scanrf_thread item->len =%d\n", item->len);
            q_itm.len = item->len;
            memcpy(q_itm.buf,item->buf,item->len);
            queue_pop(FPGA_RW_IDX);

            p = (unsigned int *)q_itm.buf;
            if(item->len % 12 == 0){
                for(j = 0; j < q_itm.len/12; j++){
                    p = (unsigned int*)(q_itm.buf + j*12);
                    ret = parse_fpga_cmd(p, 12, SOCKET_CHN_RW);
                    if(ret == -1)
                        close_socket(SOCKET_CHN_RW,soc_array[SOCKET_CHN_RW].m_socketfd);
                }
            }else {
                DEBUG_INFO("handle_scanrf_thread,can not handle cmd... q_itm.len=%d\n",q_itm.len);
            }
        }
        usleep(20);
    }
    DEBUG_ERR("######Error  handle_scanrf_thread QUIT#####\n");

}

int handle_parameter(void)
{
    pkt* item, q_itm;
    unsigned int *p,cbuf_off=0, scant_len, index=0, j,total_len=0xffffffff,time_out= 60000;

#if 0
	unsigned char cbuf[4096*1024];
    while(index != total_len){
        if(soc_error)
            break;
        if(!queue_empty(LOCAL_INDEX_QUEUE_IDX)){
            item = queue_front(LOCAL_INDEX_QUEUE_IDX);
            //printf("@@@@@handle_scanrf_thread item->len =%d\n", item->len);
            p = (unsigned int *)item->buf;

            if(index + item->len < ST_SIZE_LIMIT + LOC_SIZE_LIMIT){
                if(*p == PKG_PAR_HEAD_FLAG){
                    if(item->len >= 32 ) {
                        DEBUG_INFO("####Header index=%d, item->len=%d#####\n",index,item->len);
                        memcpy(cbuf + index, item->buf, item->len);
                        total_len = *(p + 1);
                        index += item->len;
                        //printf("####Header  handle_parameter total_len=%d#####,l_off(%x)-len(%d),indt_off(%x)-len(%d),st_off(%x)-len(%d)\n",
                         //   total_len, *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
                    } else {
                        queue_pop(LOCAL_INDEX_QUEUE_IDX);
                        DEBUG_ERR("####  handle_parameter head error ,return -1 #####\n");
                        return -1;
                    }
                } else {
                   memcpy(cbuf + index, item->buf, item->len);
                   index += item->len;
                    //printf("####index=%d,item->len=%d,*p =%x#####\n", index,item->len, *p);
                }
            }else {
                queue_pop(LOCAL_INDEX_QUEUE_IDX);
                DEBUG_ERR("Error ,cbuf(4M) is not enough index =%d,item->len=%d\n",index,item->len);
                return -1;
            }
            queue_pop(LOCAL_INDEX_QUEUE_IDX);
            time_out= 60000;
        }
        else {
            usleep(20);
            time_out--;
            if(time_out == 0 ||index > total_len){
                DEBUG_ERR("ERROR :handle_parameter TIME OUT ,soc_error=%d,get length=%d\n",soc_error,index);
                break;
            }
        }
    }
#else
    #ifdef DEBUG_CH0_PAR

    if(find_freeze_cmd)
        cbuf_off = 12;
    else
        cbuf_off= 0;
        p = (unsigned int *)(cbuf + cbuf_off);
    printf("find_freeze_cmd = %d ,g_aim_len=%d,(head)*p=%x,*p1=%x\n",find_freeze_cmd,g_aim_len,*p,*(p+1));
    total_len = g_aim_len;
    index= g_aim_len;
    #else
    p = (unsigned int *)cbuf;
    while(index != total_len){
        if(soc_error)
            break;
        index = cbuf_idx;
        //DEBUG_INFO("@@@index =%d,total_len =%d",index,total_len);
        if(total_len==0xffffffff && index != 0){
            if(*p == PKG_PAR_HEAD_FLAG){
                DEBUG_INFO("####Header index=%d#####",index);
                total_len = *(p + 1);
                //printf("####Header  handle_parameter total_len=%d#####,l_off(%x)-len(%d),indt_off(%x)-len(%d),st_off(%x)-len(%d)\n",
                 //   total_len, *(p+2), *(p+3), *(p+4), *(p+5), *(p+6), *(p+7));
            } else {
                DEBUG_ERR("####  handle_parameter head error ,return -1 #####\n");
                //total_len = 100*1024*1024; //100M ,error val
                cbuf_idx = 0;
                return -1;
            }
        }
        usleep(20);
        time_out--;
        if(time_out == 0 ||index > total_len){
            DEBUG_ERR("ERROR :handle_parameter TIME OUT ,time_out=%d,get length=%d,t_len=%d\n",time_out,index,total_len);
            break;
        }

    }

    cbuf_idx = 0;
    #endif
#endif
    if(index != total_len){
        DEBUG_ERR("####Error  handle_parameter getlen=%d, total_len=%d#####\n",
            index, total_len);
        index = 0;
        return -1;
    }
    if(total_len <= (32 + 4 + ST_SIZE_LIMIT + LOC_SIZE_LIMIT)){
        memcpy((void *)&par_buf, (cbuf + cbuf_off), 32);
        par_buf.local_data = (unsigned int *)((cbuf + cbuf_off) +par_buf.local_off);
        par_buf.indext_data =(unsigned int *)((cbuf + cbuf_off)+par_buf.indext_off);
        par_buf.scant_data = (unsigned int *)((cbuf + cbuf_off)+par_buf.scant_off);
        par_buf.end_tag = *(unsigned int *)((cbuf + cbuf_off)+par_buf.scant_off+par_buf.scant_len);
        if(par_buf.end_tag == PKG_PAR_END_FLAG)
            DEBUG_INFO("handle_parameter  parse OK\n");
        else{
            DEBUG_ERR("handle_parameter  parse ERROR \n");
            return -1;
        }
        DEBUG_INFO("####handle_parameter ,par_buf----- \
            l_off(%x)-len(%d),indt_off(%x)-len(%d),st_off(%x)-len(%d)\n",
                par_buf.local_off,par_buf.local_len,
                par_buf.indext_off,par_buf.indext_len,
                par_buf.scant_off,par_buf.scant_len);
    } else {
        DEBUG_ERR(" handle_parameter error :total_len =%d", total_len);
    }

    DEBUG_INFO("localdata=%x,p+offset=%x\n", *par_buf.local_data,
                *(unsigned int *)((cbuf + cbuf_off) + par_buf.local_off));
    DEBUG_INFO("index=%x,p+offset=%x\n", *par_buf.indext_data,
                *(unsigned int *)((cbuf + cbuf_off) + par_buf.indext_off));
    DEBUG_INFO("scant=%x,p+offset=%x\n", *par_buf.scant_data,
        *(unsigned int *)((cbuf + cbuf_off) + par_buf.scant_off));

    if(*par_buf.local_data != 0xAABBCCDD &&     //||
        *par_buf.indext_data != 0xAABBCCDD &&   //||
        *par_buf.scant_data != 0xAABBCCDD){

        if (*par_buf.local_data == PKG_PAR_END_FLAG){
            DEBUG_ERR(" parse parameter maybe empty ");
            return 0;
        } else {
            DEBUG_ERR(" parse parameter error ");
            return -1;
        }

    }

    write_fpga_cmd(par_buf.local_data, par_buf.local_len);
    write_fpga_cmd(par_buf.indext_data, par_buf.indext_len);

    p = par_buf.scant_data;// (unsigned int *)(cbuf+par_buf.scant_off);
    if(*p == 0xAABBCCDD && *(p+1) == SCAN_TABLE_UPDATE && par_buf.scant_len < ST_SIZE_LIMIT){
        scant_len = *(p+3);
        DEBUG_INFO("handle_parameter item scant_len =%d\n", scant_len);
        memcpy(virtual_scant_addr, p + 4, par_buf.scant_len - 16);
    }else {
        DEBUG_ERR(" par error :par_buf.scant_len =%d", par_buf.scant_len);
    }

    //memcpy(virtual_scant_addr, par_buf.scant_data,par_buf.scant_len);
    //printf("handle_parameter  end\n");
    return 0;
}

static int pk_id=0, sum_len=0,check_g_local_len = 0;

unsigned int find_idx(int fd)
{
    unsigned char ch;
    if(fd == 0){
        DEBUG_ERR("find_idx :fd = 0");
        return -1;
    }
    for(int  i= 0;i < TCP_SOCKET_COUNT;i++){

        pthread_mutex_lock(&soc_array[i].soc_lock);
        if(soc_array[i].m_socketfd == fd){
           pthread_mutex_unlock(&soc_array[i].soc_lock);
           return soc_array[i].soc_id;
        }
        pthread_mutex_unlock(&soc_array[i].soc_lock);
    }

    if(soc_array[3].m_socketfd == fd){
        printf("find_idx  tools fd ,( fd =%d) to index(%d) \n",fd,soc_array[3].soc_id);
        return soc_array[3].soc_id;
    }
    printf("###find_idx  error ,can not find( fd =%d) to index \n",fd);
    return -1;
}

unsigned int add_cmd_list(struct ev_loop *loop,struct ev_io *watcher,unsigned int idx)
{
    int read_count=0;
    unsigned int index,*p, total_len = 0 ;
    pkt* item;

    if(watcher->fd == 0){
        DEBUG_ERR("%s :watcher->fd == 0:error: %s (errno :%d) \n",
            __func__,strerror(errno), errno);
        return -1;
    }
    #if 0
    item = get_msg();
    read_count = recv (watcher->fd, item->buf, TCP_PKG_SIZE, 0);
    pk_id++;

    if( read_count == -1 || read_count == 0 ){
        DEBUG_ERR("%s :(fd=%d)r_count %d,to quit:error: %s (errno :%d) \n",
            __func__, watcher->fd, read_count,strerror(errno), errno);
        return read_count;
    }

    //printf("###FD=%d, (pid=%d) ,get read_count  %d bytes \n",watcher->fd,pk_id,read_count);
    if( read_count > TCP_PKG_SIZE ){
        DEBUG_ERR("Error buf is not enough,buf_len = 1344, get read_count %x bytes \n",read_count);
    }
    else
    {
        item->len = read_count;
        item->pk_id = pk_id;

        //if(idx == 0 && item->len != 1344)
           // DEBUG_INFO("######read_cb fd=%d, item.pk_id =%d ,item.len=%d#####\n",watcher->fd,item->pk_id,item->len);
        queue_push(idx,item);

    }
    #else
    if(idx == 0){
        #ifndef DEBUG_CN0_PAR
        read_count = recv (watcher->fd, cbuf + cbuf_idx, TCP_PKG_SIZE, 0);
        if( read_count == -1 || read_count == 0 ){
            DEBUG_ERR("%s :(fd=%d)r_count %d,to quit:error: %s (errno :%d) \n",
                __func__, watcher->fd, read_count,strerror(errno), errno);
            return read_count;
        }
        cbuf_idx += read_count;
        if(flag_unfreezed){
            DEBUG_ERR("flag_unfreezed=1,add_cmd_list :cbuf_idx=%d\n",cbuf_idx);
        }

        #else

        if(cbuf_idx + TCP_PKG_SIZE >= ST_SIZE_LIMIT+LOC_SIZE_LIMIT)
            cbuf_idx = 0;

        read_count = recv (watcher->fd, cbuf + cbuf_idx, TCP_PKG_SIZE, 0);
        if( read_count == -1 || read_count == 0 ){
            DEBUG_ERR("%s :(fd=%d)r_count %d,to quit:error: %s (errno :%d) \n",
                __func__, watcher->fd, read_count,strerror(errno), errno);
            return read_count;
        }

        if(cbuf_idx == 0){
            p = (unsigned int *)cbuf;
            if(*p == 0xAABBCCDD){
                if(read_count >=12 ){
                    parse_fpga_cmd(p, 12, 0);

                    if(read_count >=16)
                        g_aim_len=*(p+4);
                    else
                        printf("read_count =%d (freeze),g_aim_len unknown\n",read_count);
                }else
                    printf("read_count <12?? error???\n");

            }else if(*p == PKG_PAR_HEAD_FLAG){

                if(read_count >=8)
                    g_aim_len=*(p+1);
            }else {
                printf("cbuf_idx =0 ,but can not find head ????\n");
            }
            DEBUG_INFO("g_aim_len = %d ,find_freeze_cmd = %d\n",g_aim_len,find_freeze_cmd);
        }else if (cbuf_idx == 12)
        {
            p = (unsigned int *)cbuf;
            if(*(p+3) == PKG_PAR_HEAD_FLAG)
                g_aim_len = *(p+4);
            else{
                printf("cbuf_idx =12 ,but can not find head ????\n");
            }
            DEBUG_INFO("cbuf=12, read_count =%d,g_aim_len=%d\n",read_count,g_aim_len);
        }

        cbuf_idx += read_count;
        if(g_aim_len != 0){
            if(find_freeze_cmd)
                total_len = g_aim_len + 12 + 12;
            else
                total_len = g_aim_len + 12;
            if(cbuf_idx == total_len ||cbuf_idx + 12 ==total_len)
            {
                DEBUG_INFO("Maybe try to melt:cbuf_idx=%d,read_count=%d,total_len=%d",cbuf_idx,read_count,total_len);
            }
            if(cbuf_idx == total_len && cbuf_idx>12 ){
                p = (unsigned int *)(cbuf + total_len-12);
                if(*p == 0xAABBCCDD){
                    parse_fpga_cmd(p, 12, 0);
                    //find_freeze_cmd = false;
                    cbuf_idx = 0;
                    g_aim_len = 0;
                }else{
                    DEBUG_ERR("Maybe cannot find melt cmd\n");
                }
            }
            if(cbuf_idx > total_len){
                DEBUG_ERR("Maybe fail :cbuf_idx=%d,read_count=%d,total_len=%d",cbuf_idx,read_count,total_len);
                p = (unsigned int *)(cbuf + total_len-12);
                if(*p == 0xAABBCCDD){
                    parse_fpga_cmd(p, 12, 0);
                }
                if((cbuf_idx - total_len)==12){
                    p = (unsigned int *)(cbuf + total_len);
                    parse_fpga_cmd(p, 12, 0);
                    if(find_freeze_cmd){
                        cbuf_idx = 0;
                        g_aim_len = 0;
                    }
                }else{
                    DEBUG_INFO("Maybe fail also :cbuf_idx=%d,total_len=%d",cbuf_idx,total_len);
                }
            }
        }
        #endif
    }else{

        item = get_msg();
        read_count = recv (watcher->fd, item->buf, TCP_PKG_SIZE, 0);
        pk_id++;

        if( read_count == -1 || read_count == 0 ){
            DEBUG_ERR("%s :(fd=%d)r_count %d,to quit:error: %s (errno :%d) \n",
                __func__, watcher->fd, read_count,strerror(errno), errno);
            return read_count;
        }

        //printf("###FD=%d, (pid=%d) ,get read_count  %d bytes \n",watcher->fd,pk_id,read_count);
        if( read_count > TCP_PKG_SIZE ){
            DEBUG_ERR("Error buf is not enough,buf_len = 1344, get read_count %x bytes \n",read_count);
        } else {
            item->len = read_count;
            item->pk_id = pk_id;

            //if(idx == 0 && item->len != 1344)
               // DEBUG_INFO("######read_cb fd=%d, item.pk_id =%d ,item.len=%d#####\n",watcher->fd,item->pk_id,item->len);
            queue_push(idx,item);

        }

    }
    #endif
    return read_count;
}

unsigned int handle_arm_cmd(struct ev_loop *loop, struct ev_io *watcher, unsigned int cnt, char *r_buf,unsigned int index)
{
    char *buf;
    unsigned char *tmp, tmp_val[4],ip_addr[30]={'\0'};
    char nstr[32]={'\0'},pre_str[32]="serial=", dg_buf[256];
    unsigned int s_buf[2], *p, read_count = cnt, pk_len = 0, msg_len = 0,arm_cmd_addr = 0;
    unsigned short val = 0;
    unsigned int ttcode, ret, handled=0, remaining=0,data_val[8],i,back_len=0;

    float temp;

    buf = r_buf;
    memset(tmp_val, 0, 4);
    tmp = &tmp_val[0];

    if(read_count <= 0 || buf == NULL){
        DEBUG_ERR("Error :handle_arm_cmd error, read_count= %x or buf=NULL\n", read_count);
        return -1;
    }

    remaining = read_count;
    //SP          //CMD      //LENGTH           //ADDR      //val
    //0xFFEEDDCC, 0x00000003, 0x00000008,0x80000001, 0x00000001
    while(remaining > 0 && read_count >= 20){
        p=(unsigned int*)(buf + handled);
        //printf("handle_arm_cmd --*p=%x ,read_count =%d\n",*p,read_count);
        if(*p == CMD_FIXED_ARM_FLAG){
            pk_len = *(p + 2);
            msg_len = pk_len + 12;
            handled = handled + msg_len;
            remaining = read_count - handled;
            if(pk_len >= 8 && pk_len <= 8*4){
                memcpy(data_val, p + 3, pk_len);
                arm_cmd_addr = data_val[0];
            }else{
                arm_cmd_addr = *(p + 3);
            }
            //printf("arm_cmd_addr =%x\n",arm_cmd_addr);
            if((arm_cmd_addr == ARM_WRITE_SN ||arm_cmd_addr == ARM_WRITE_MCODE||
                arm_cmd_addr == ARM_WRITE_DONGLE ||arm_cmd_addr == ARM_WRITE_TTCODE
                ||arm_cmd_addr == ARM_WRITE_LMU||arm_cmd_addr == ARM_WRITE_FPGA_LOAD
                ||arm_cmd_addr == ARM_WRITE_MARKCODE||arm_cmd_addr == ARM_WRITE_HWVER)
                && index != SOCKET_CHN_PC_TEST_CMD){
                DEBUG_ERR("Can not support cmd and jump,arm_cmd_addr=%x",arm_cmd_addr);
                continue;
            }

            switch(arm_cmd_addr){
            case ARM_CONF_THV:
                val = (unsigned short)(data_val[1]);
                dac_config(&val);
                break;

            case ARM_READ_THV:
                s_buf[0] = dac_dev;
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                break;

            case ARM_WRITE_TTCODE:
                DEBUG_INFO("ARM_WRITE_TTCODE ttcode=%x\n",data_val[1]);
                ttcode_write(4, data_val[1]);
                break;

            case ARM_READ_TTCODE:
            case ARM_READ_HWVER:
                if (arm_cmd_addr == ARM_READ_TTCODE)
                    s_buf[0] = g_ttcode;
                else if(arm_cmd_addr == ARM_READ_HWVER)
                    s_buf[0] = g_hwver;
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                printf("ARM_READ ttcode=%x,g_hwver=%x,s_buf[0]=%x\n",g_ttcode,g_hwver,s_buf[0]);//0xfef00002

                break;

            case ARM_READ_TEMP:
                //temp = get_xadc_val(0);
                s_buf[0] = (unsigned int)round(g_dev_temp*100);//scal 100 times,then round
                //s_buf[0] = (s_buf[0] << 16 | 0x0301);
                DEBUG_INFO("ARM_READ_TEMP(soc_err=%d,unfreezed status = %d) temp = %d\n\n",soc_error, flag_unfreezed, s_buf[0]);
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                break;

            case ARM_READ_CORE_VOL:

                //temp = get_xadc_val(2);
                s_buf[0] = (unsigned int)round(g_dev_core_val*100);
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                DEBUG_INFO("ARM_READ_CORE_VOL to do,vol = %d V ,vol raw =%f V\n\n", s_buf[0], g_dev_core_val);
                break;

            case ARM_READ_CHARGE:

                //bq40_read_relative_charge(tmp);
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)g_dev_charge, 4);
                #if 0
                //if(*tmp < 0x14){ //15%
                 //   led_bright_ctl(green_led.bright_fd,0);
                 //   led_bright_ctl(yellow_led.bright_fd,1);
                    //led_trigger_ctl(yellow_led.trigger_fd, LED_TRIGGER_TIMER);
                    //led_trigger_delay(1, "200","500");
                //}
                #endif
                DEBUG_INFO("ARM_READ_CHARGE(pkgcnt=%d) :%x,%x,%x,%x\n\n",read_count,*tmp,*(tmp+1),*(tmp+2),*(tmp+3));
                break;

            case ARM_WRITE_SN:
                printf("ARM_WRITE_SN ser size =%d\n\n",sizeof("serial="));
                memcpy(pre_str,"serial=",sizeof("serial="));//sizeof()=strlen()+1
                memset(nstr,'\0',32);
                memcpy(nstr, p + 4, pk_len - 4);
                for(i = 0;i< pk_len - 4; i++){
                    printf("%c ",nstr[i]);
                }
                if(pk_len - 4 <= 20)
                    repalce_ap_conf(nstr,pre_str);
                else
                    DEBUG_ERR("repalce_ap_conf SN failed ");
                break;

            case ARM_WRITE_MCODE:
            case ARM_WRITE_MARKCODE:
            case ARM_WRITE_HWVER:
                DEBUG_INFO("wifi config replace :cmd = %x\n\n", arm_cmd_addr);
                if(arm_cmd_addr == ARM_WRITE_MCODE)
                    memcpy(pre_str,"machinecode=",sizeof("machinecode="));
                else if(arm_cmd_addr == ARM_WRITE_MARKCODE)
                    memcpy(pre_str,"marketcode=",sizeof("marketcode="));
                else if(arm_cmd_addr == ARM_WRITE_HWVER)
                    memcpy(pre_str,"ver=",sizeof("ver="));
                memset(nstr,'\0',32);
                memcpy(nstr, p + 4, pk_len - 4);
                for(i = 0;i< pk_len - 4; i++){
                    printf("%c ",nstr[i]);
                }
                i = *(p + 4);
                if((pk_len - 4 <= 4) &&  ((i & 0x0000ffff) != 0))
                    repalce_ap_conf(nstr,pre_str);
                else
                    DEBUG_ERR("repalce_ap_conf m_code failed ");
                break;

            case ARM_WRITE_DONGLE:
                DEBUG_INFO("ARM_WRITE_DONGLE");
                if(sizeof(dg_buf) >= pk_len - 4 ){
                    memcpy(dg_buf, p + 4, pk_len - 4);
                    config_file_write(DONGLE_FILE,dg_buf,pk_len - 4);
                }
                break;

            case ARM_READ_DONGLE:
            case ARM_READ_LMU:
                DEBUG_INFO("ARM_READ_DONGLE/REGION FILE ");
                memset(dg_buf,'\0',sizeof(dg_buf));
                if(arm_cmd_addr == ARM_READ_DONGLE)
                    config_file_read(DONGLE_FILE,dg_buf, &back_len);
                else if(arm_cmd_addr == ARM_READ_LMU)
                    config_file_read(REGION_CUR_FILE,dg_buf, &back_len);
                ret = send_socket_msg(soc_array[index].m_socketfd, dg_buf, back_len);
                break;

            case ARM_WRITE_LMU:
                DEBUG_INFO("ARM_WRITE_LMU");
                if(pk_len - 4 <= 8) {
                    memset(dg_buf,'\0',sizeof(dg_buf));
                    memcpy(dg_buf, p + 4, pk_len - 4);
                    config_file_write(MFG_UPDATE_FILE, dg_buf,pk_len - 4);
                }
                break;

            case ARM_HANDLE_LIST:
                printf("maybe :ARM_HANDLE_LIST  to do  \n\n");
                handle_cmd_list();
                break;

            case ARM_WRITE_KILLPOWER:
                DEBUG_INFO("Start to kill power\n\n");
                kill_power();
                break;

            case ARM_WRITE_TIME:
                DEBUG_INFO("SYNC world time \n\n");
                if(pk_len - 4 == 24) {
                    printf("*p=%x,%x,%x,%x,%x,%x,%x,%x\n",*(p + 0),*(p + 1),*(p + 2),
                        *(p + 3),*(p + 4),*(p + 5),*(p + 6),*(p + 7));
                    set_sync_time(p + 4,pk_len - 4);
                    start_vinnolog();
                    system("sync");
                }
                break;

            case ARM_WRITE_UPDATE:
                bq40_read_relative_charge(tmp);
                sprintf(nstr, "%d", data_val[1]);
                get_client_addr(soc_array[index].m_socketfd,ip_addr);
                if(*tmp > 0x14){ //15%
                    DEBUG_INFO("Start update app \n\n");
                    update_vinno_img(soc_array[index].m_socketfd,nstr,ip_addr);
                } else {
                    DEBUG_INFO("Charge is not enough, update failed\n");
                    update_vinno_img(soc_array[index].m_socketfd,nstr,ip_addr);
                }
                break;

            case ARM_READ_UPDATE:
                sprintf(nstr, "%d", data_val[1]);
                memset(dg_buf,'\0',sizeof(dg_buf));
                printf("nstr=%s\n",nstr);
                if(!read_md5(nstr,dg_buf)){
                    printf("md5code = %s,len=%d\n",dg_buf,strlen(dg_buf));
                    ret = send_socket_msg(soc_array[index].m_socketfd, dg_buf, strlen(dg_buf));
                }else{
                    ret = send_socket_msg(soc_array[index].m_socketfd, dg_buf, 32);
                }
                break;

            case ARM_WRITE_CHECK_ALIVE:
                printf("ARM_WRITE_CHECK_ALIVE,pkg readcnt = %d\n\n",read_count);
                ttcode =  ttcode_read();
                printf("ARM_READ_TTCODE ttcode=%x\n",ttcode);//0xfef00002
                s_buf[0] = ttcode;
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                break;

            case ARM_WRITE_FPGA_LOAD:
                DEBUG_INFO("ARM_WRITE_FPGA_LOAD cmd=%x",data_val[1]);
                if(data_val[1] == 0x1)
                    fpga_load("hh_wifi_top_mfg.bit");
                else if(data_val[1] == 0x2)
                    fpga_load("hh_wifi_top.bit");
                break;

            case ARM_READ_ARM_VER:
                DEBUG_INFO("ARM_READ_ARM_VER read_len=%x",data_val[1]);
                //设置读取长度data_val[1]
                char *verbuf;
                verbuf = (char *)malloc(data_val[1]);
                if(verbuf == NULL){
                    DEBUG_ERR("Malloc verbuf error ! Return");
                    return -1;
                }
                memset(verbuf,'\0',data_val[1]);
                if(!get_app_version(verbuf,data_val[1]))
                    ret = send_socket_msg(soc_array[index].m_socketfd, (char *)verbuf, data_val[1]);
                free(verbuf);
                break;

            case ARM_READ_BQ_SOH:
                s_buf[0] = g_bq40_status.state_of_health;
                DEBUG_INFO("ARM_READ_BQ_SOH  SOH = %d",s_buf[0]);
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                break;
            case ARM_READ_BQ_TEMP:
                s_buf[0] = g_bq40_status.temperature;
                DEBUG_INFO("ARM_READ_BQ_TEMP  temp = %f",(float)(s_buf[0])/100);
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                break;
            case ARM_READ_BQ_CYCLE:
                s_buf[0] = g_bq40_status.cycle_count;
                DEBUG_INFO("ARM_READ_BQ_CYCLE  CycleCount = %d",s_buf[0]);
                ret = send_socket_msg(soc_array[index].m_socketfd, (char *)s_buf, 4);
                break;      

#ifdef DEBUG_PC_FUNC
            case ARM_READ_DUMP_RF:
                DEBUG_INFO("ARM_READ_DUMP_RF  to do ,read len =%d\n\n",data_val[1]);
                if(data_val[1] <= DDR_MAP_RF_SIZE){
                    ret = send_socket_msg(soc_array[index].m_socketfd,
                        (char *)virtual_rf_addr,data_val[1]); //SOCKET_CHN_PC_TEST_CMD

                /*
                ret = send(soc_array[SOCKET_CHN_PC_TEST_CMD].m_socketfd, virtual_rf_addr, data_val, 0);

                if(ret<0)
                {
                    printf(" send socket error: %s (errno :%d)\n", strerror(errno), errno);
                    soc_error = true;
                    usleep(1);
                }else if(ret != data_val){
                    printf(" send socket error:sendcnt(%d)!=aim(%d)\n", ret, data_val);

                }*/

                } else {
                    DEBUG_ERR("ARM_READ_DUMP_RF Get len Error \n");
                }
                break;
             case ARM_WRITE_DUMP_ST:
                DEBUG_INFO("ARM_WRITE_DUMP_ST  to do , len =%d\n\n",data_val[1]);
                dump_st_data(virtual_scant_addr,SIZE_4M);
                break;
#endif
            default:
                DEBUG_ERR("No this func:cur arm_cmd_addr= 0x%x\n", arm_cmd_addr);
            }
        }else {
            DEBUG_ERR("No this func:head--*p = 0x%x\n", *p);
            break;
        }

        if(ret == -1){
            if(soc_array[index].m_socketfd != 0)
                soc_error =true;
            DEBUG_ERR("handle_arm_cmd, soc_error ,try to close socket");
            break;
        }
    }


    if(soc_error){
        DEBUG_INFO("Maybe need close socket,channel = %d\n",index);
        close_socket(index,soc_array[index].m_socketfd);
        return -1;
    }

    return 0;
}

unsigned int parse_fpga_cmd(unsigned int *buf, unsigned int len,unsigned int idx)
{
    unsigned int cmd_rw_type = *(buf + 1), data_val =  *(buf + 2);
    unsigned int s_buf[2], ret = 0,*p;

    DEBUG_INFO("chn[%d],parse_fpga_cmd cmd:=%x, data_val=%x\n   ",idx, cmd_rw_type, data_val);
    if(cmd_rw_type == 0x8)
    {
        if(data_val != 0x1 && data_val != 0x00010001){
            check_fpga_state_cmd(&data_val);
            s_buf[0] = data_val;
        }else{
            s_buf[0] = fpga_real_state;
        }

        ret = send(soc_array[idx].m_socketfd, s_buf, 4, 0);
        if(ret < 0)
        {
            DEBUG_ERR(" send socket error: %s (errno :%d)\n", strerror(errno), errno);
            return -1;
        }
        DEBUG_INFO("FPGA_CMD_READ send ,m_socketfd=%d, s_buf=%x,send ret =%d\n",
            soc_array[idx].m_socketfd, s_buf[0], ret);

        if(0x00000001 == s_buf[0] )
        {
            flag_unfreezed = true;
            DEBUG_INFO("flag_unfreezed true 2-pc\n");
        }else{
            DEBUG_INFO("flag_unfreezed not true,s_buf[0]=%x ",s_buf[0]);
            if(s_buf[0] & 0x40000000)
                DEBUG_INFO("FPGA try ST error,s_buf[0]=%x ",s_buf[0]);
        }
    }

    if(cmd_rw_type == 0x10)
    {
        printf("FPGA_CMD_WRITE ,m_socketfd=%d, val = 0x%x\n",
            soc_array[idx].m_socketfd,  data_val);
        switch(data_val)
        {
            case 0x00000001://start
                //printf("######### START RF ###########\n\n\n");
                ret = handle_parameter();
                if(soc_array[0].m_socketfd  != 0){
                    pthread_mutex_lock(&g_fpga_lock);
                    if(ret == 0){
                        write_fpga_cmd(buf, len);
                        DEBUG_INFO("@@@@START begin :*buf = %x,%x,%x\n",*buf, *(buf+1), *(buf+2));
                    }
                    pthread_mutex_unlock(&g_fpga_lock);
                }

                if(ret != 0)
                    DEBUG_ERR("handle_parameter data error ,err=%d\n",ret);

                check_fpga_state_cmd(&data_val);

                if(data_val != 0x1) {
                    data_val = 0x1;
                    usleep(1000);
                    check_fpga_state_cmd(&data_val);
                }
                fpga_real_state = data_val;
                if(0x00000001 == fpga_real_state )
                {
                    flag_unfreezed = true;
                    DEBUG_INFO("flag_unfreezed true 1-myself\n");
                }else{
                    DEBUG_ERR("Maybe flag_unfreezed not true, melt failed?val =%x",data_val);
                }
                #ifdef DEBUG_CN0_PAR
                    find_freeze_cmd = false;
                #endif

                break;

            case 0x00010001://stop
                printf("@@@@@@@flag_unfreezed =%d\n", flag_unfreezed);
                if(flag_unfreezed == true) {
                    DEBUG_INFO(" Stop flag_unfreezed=false \n");
                    flag_unfreezed = false;
                    //led_bright_ctl(green_led.bright_fd,0);
                    //led_bright_ctl(yellow_led.bright_fd,1);

                }
                usleep(10);
                pthread_mutex_lock(&g_fpga_lock);
                write_fpga_cmd(buf, len);
                pthread_mutex_unlock(&g_fpga_lock);

                if(data_val != 0x80010001){
                    usleep(1000);
                    check_fpga_state_cmd(&data_val);
                }
                fpga_real_state = data_val;
                #ifdef DEBUG_CH0_PAR
                    find_freeze_cmd = true;
                #else
                p = (unsigned int *)cbuf;
                if(*p != PKG_PAR_HEAD_FLAG){
                    cbuf_idx = 0; //clear ch0 all data!
                }
                #endif
                #ifndef DUMP_RFDATA
                    //clearddr_virtual_rfdata();
                #endif

                break;

            case 0x00010220://deep freezed
            case 0x00000220://deep unfreezed
                write_fpga_cmd(buf, len);
                break;

            default:
                write_fpga_cmd(buf, len);
                printf("Not a normal cmd:data_val= 0x%x\n",data_val);
        }

    }

    return 0;
}

static void read_cb (struct ev_loop *loop, struct ev_io *watcher, int revents)
{
    int read_count = 0, j = 0;
    char buf[1460],rbuf[1460];
    unsigned int *u32_buf,index;
    if(watcher == NULL){
        DEBUG_ERR("%s :watcher is NULL:error: %s (errno :%d) \n",
            __func__,strerror(errno), errno);
        return ;
    }
    memset(buf,'\0',1460);
    read_cb_fd = watcher->fd;
    //printf("@@@1111read_cb ,FD=%d, (id=%d)   \n",watcher->fd,index);
    index = find_idx(read_cb_fd);
    if(index == -1)
        return ;

    //printf("@@@read_cb ,FD=%d, (id=%d)   \n",watcher->fd,index);
    switch(soc_array[index].soc_id)
    {
        //case SOCKET_CHN_SCANTABLE://SCAN_TABLE_UPDATE:         //0x1
        case SOCKET_CHN_PARAM:// INDEX_TABLE_UPDATE:            //0x2
                            //LOCAL_BUS:                    //0x4
            //printf("@@@read_cb_fd =%d\n",read_cb_fd);
            read_count = add_cmd_list(loop,watcher,0);
            break;

        case SOCKET_CHN_RW://FPGA_CMD_READ:                     //0x8
                           //FPGA_CMD_WRITE:                  //0x10
            fpga_rw_fd = read_cb_fd;
            read_count = add_cmd_list(loop,watcher,FPGA_RW_IDX);
            break;

        case SOCKET_CHN_ARM_CMD:
            //use channel = 3
            read_count = recv (watcher->fd, buf, TCP_PKG_SIZE, 0);
            if(read_count < 20){
                DEBUG_ERR("SOCKET_CHN_ARM_CMD: recv(%d) socket error: %s (errno :%d)\n",read_count, strerror(errno), errno);
            } else {
                handle_arm_cmd(loop, watcher, read_count, buf,index);
            }

            break;

#ifdef DEBUG_PC_FUNC
        case SOCKET_CHN_PC_TEST_CMD:
            read_count = recv (watcher->fd, buf, TCP_PKG_SIZE, 0);
            if(read_count <= 0)
                break;

            if(read_count % 4 != 0)
                DEBUG_ERR("Error read_count %4 != 0");
            u32_buf = (unsigned int *)buf;
            //memset(u32_buf,buf,read_count);
            if( *u32_buf == CMD_FIXED_ARM_FLAG )
                handle_arm_cmd(loop, watcher, read_count, buf,index);
            else if( *u32_buf == 0xAABBCCDD ) {

                #if 1
                // add test cmd debug !!!
                if(read_count % 12 == 0){
                    for(j = 0; j < read_count/12; j++){
                        u32_buf = (unsigned int*)(buf + j*12);
                        //parse_fpga_cmd(u32_buf, 12, index);
                        if(*(u32_buf+1)== 0x10 ){
                            write_fpga_cmd(u32_buf, 12);
                            if(*(u32_buf+2)== 0x1)
                                flag_unfreezed = true;
                            else if(*(u32_buf+2)== 0x00010001)
                                flag_unfreezed = false;
                        }
                    }
                }else {
                    DEBUG_ERR("#SOCKET_CHN_PC_TEST_CMD, trans to fpga ,read_count=%d\n",read_count);
                    write_fpga_cmd((unsigned int *)buf, read_count);
                }
                #endif
            }else{
                printf("ERROR,Can not find this cmd : %x\n",*u32_buf);
                //write_fpga_cmd((unsigned int *)buf, read_count);
            }

            printf("SOCKET_CHN_PC_TEST_CMD  get cnt =%d ,data =\n",read_count);

            for(j = 0 ;j<read_count/4;j++){
                printf("%x  ",*(u32_buf+j));
            }
            printf("\n");

            break;
#endif
        default:
            printf("No this cmd:cur cmdid= 0x%x\n",soc_array[index].cmd_type);

    }

    if((read_count <= 0) && soc_array[index].m_socketfd != 0){
        DEBUG_INFO("To close socket: soc fd = %d,loop_ad=%x,iowatcher=%x,error: %s (errno :%d)\n",
            soc_array[index].m_socketfd,loop,watcher,strerror(errno),errno);

        close_socket(index,soc_array[index].m_socketfd);

        if(dump_rf == false){
            dump_rf = true;
            #ifdef DUMP_RFDATA
            dump_rfdata(virtual_rf_addr, DDR_MALLOC_SIZE - SIZE_4M);
            #endif
        }
    }
}

void soc_arry_init(unsigned int idx,int fd,struct ev_loop *main_loop, ev_io*io_w)
{
    if(idx > 7)
    {
        printf ("Error idx, array cmd_tpye :out of max idx\n");
        return ;
    }
    //soc_array[idx].cmd_type = 1<< (idx);
    soc_array[idx].soc_id = idx;
    soc_array[idx].m_socketfd = fd;
    soc_array[idx].soc_w_client = io_w;
    soc_array[idx].soc_loop = main_loop;
}

static void set_soc_nonblock(int fd)
{
    fcntl(fd,F_SETFL,fcntl(fd,F_GETFL) | O_NONBLOCK);
}

static int set_reuseaddr(int fd)
{
    int ok=1,ret;
    ret = setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&ok,sizeof(ok));
}

//ev_io callback
void io_action(struct ev_loop *main_loop, ev_io*io_w, int e)
{
    int ret = 0,client_sd = 0,n;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    printf("tcp connect \n");
    struct ev_io *w_client = NULL;

    if(EV_ERROR & e)
    {
        DEBUG_ERR("got invalid event");
        return;
    }

    // accept() func
    client_sd = accept (io_w->fd, (struct sockaddr *)(&client_addr), &client_len);
    if (client_sd < 0)
    {
        DEBUG_ERR(" accept error ");
        return;
    }

    // prepare memory for read watcher
    w_client  = (struct ev_io *)malloc(sizeof(struct ev_io));
    if(w_client == NULL){
        close(client_sd);
        ev_io_stop(main_loop,io_w);
        DEBUG_ERR(" w_client malloc error");
        return ;
    }
    DEBUG_INFO(" io_action  client_sd =%d io_action_cnt=%d,w_client=%x", client_sd,io_action_cnt,w_client);

    ev_io_init (w_client, read_cb, client_sd, EV_READ);
    ev_io_start (loop, w_client);
    if(io_action_cnt == TCP_SOCKET_COUNT || soc_release == false){
        close(client_sd);
        ev_io_stop(loop,w_client);
        DEBUG_INFO(" io_action_cnt(%d) is max or soc is not released ",io_action_cnt);
        return ;
    }

    pthread_mutex_lock(&soc_status_lock);
    if(io_action_cnt < TCP_SOCKET_COUNT){
        if(soc_release == true ){
            soc_arry_init(io_action_cnt, client_sd,loop,w_client);
            if(io_action_cnt == 2){
                soc_release = false;
                led_trigger_ctl(green_led.trigger_fd, LED_TRIGGER_NONE);
                bq40_read_relative_charge(g_dev_charge);
                if(g_dev_charge[0] < 0x14){ //15%
                    led_bright_ctl(green_led.bright_fd,0);
                    led_bright_ctl(yellow_led.bright_fd,1);
                }else{
                    led_bright_ctl(green_led.bright_fd,1);
                    led_bright_ctl(yellow_led.bright_fd,0);
                }
            }
            set_tcp_keepalive(client_sd);
            io_action_cnt++;
        }
    }
    pthread_mutex_unlock(&soc_status_lock);
#if 0
    if((io_action_cnt  == (SOCKET_CHN_PARAM + 1))  && sendrf_thread_status == false){
        pthread_t send_tid;
        pthread_create(&send_tid, NULL, send_rfdata_thread, &soc_array[SOCKET_CHN_PARAM].m_socketfd);
    }
#endif

}

#ifdef DEBUG_PC_FUNC
void io_action_tools(struct ev_loop *main_loop, ev_io*io_w, int e)
{
    int ret = 0,client_sd = 0,n;
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    printf("io_action_tools tcp connect \n");
    struct ev_io *w_client = NULL;
    if(EV_ERROR & e)
    {
        DEBUG_ERR("got invalid event");
        return;
    }

    // accept() func
    client_sd = accept (io_w->fd, (struct sockaddr *)(&client_addr), &client_len);
    DEBUG_INFO(" io_action  client_sd =%d tools_action_cnt=%d", client_sd,tools_action_cnt);

    if (client_sd < 0)
    {
        DEBUG_ERR(" accept error ");
        return;
    }

    // prepare memory for read watcher
    w_client  = (struct ev_io *)malloc(sizeof(struct ev_io));
    if(w_client == NULL){
        close(client_sd);
        ev_io_stop(main_loop,io_w);
        DEBUG_ERR(" w_client malloc error");
        return ;
    }

    ev_io_init (w_client, read_cb, client_sd, EV_READ);
    ev_io_start (loop, w_client);
    if(tools_action_cnt == 1 ){
        close(client_sd);
        ev_io_stop(loop,w_client);
        DEBUG_INFO(" io_action_cnt(%d) is max ",tools_action_cnt);
        return ;
    }

    soc_arry_init(SOCKET_CHN_PC_TEST_CMD, client_sd,loop,w_client);
    tools_action_cnt++;
}
#endif

int ev_demo(void)
{
    struct sockaddr_in addr;
    int ret= 0;
    int fd_tools = -1;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    DEBUG_INFO(" ev_demo start fd=%d ",fd);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(6666);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    ret = pthread_mutex_init(&g_fpga_lock, NULL);
    if (ret != 0) {
        DEBUG_ERR("mutex init failed\n");
        return -1;
    }

    struct timeval timeout = {5,0};

    setsockopt(fd, SOL_SOCKET,SO_SNDTIMEO, (char *)&timeout,sizeof(struct timeval));
    setsockopt(fd, SOL_SOCKET,SO_RCVTIMEO, (char *)&timeout,sizeof(struct timeval));

    //int enable = 1;
    //setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (void*)&enable, sizeof(enable));
    ret = set_reuseaddr(fd);
    //set_soc_nonblock(fd);

    int rcv_buf_cnt=8*1024*1024;//set 8M
    ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (const char*)&rcv_buf_cnt, sizeof(int));

    int snd_buf_cnd=8*1024*1024;//set 8M
    ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF, (const char*)&snd_buf_cnd, sizeof(int));

    set_tcp_keepalive(fd);
    pthread_mutex_init(&soc_array[0].soc_lock, NULL);
    pthread_mutex_init(&soc_array[1].soc_lock, NULL);
    pthread_mutex_init(&soc_array[2].soc_lock, NULL);
    pthread_mutex_init(&soc_status_lock, NULL);

    //addr bind to listenfd
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        DEBUG_ERR(" bind socket error: %s (errno :%d)",strerror(errno),errno);
        return 0;
    }

    //listen fd
    if( listen(fd,10) == -1) {
        DEBUG_ERR(" listen socket error: %s (errno :%d)",strerror(errno),errno);
        return 0;
    }

    #ifdef SAVE_CMD_EN
        if((fp = fopen("save_cmd.txt", "wb+")) == NULL )
        {
            DEBUG_ERR(" Error File.\n");
        }
        pthread_t save_tid;
        pthread_create(&save_tid, NULL, save_cmd_thread, NULL);
    #endif

    pthread_t tcp_alive_tid;
    pthread_create(&tcp_alive_tid, NULL, check_tcp_alive, NULL);

    pthread_t hand_scanrf_tid;
    pthread_create(&hand_scanrf_tid, NULL, handle_scanrf_thread, NULL);

    //maybe socket m_socketfd is wrong ,doesn't matter
    pthread_t send_tid;
    int fd_tmp = soc_array[SOCKET_CHN_PARAM].m_socketfd;
    pthread_create(&send_tid, NULL, send_rfdata_thread, &fd_tmp);

    ev_io_init(&io_watcher, io_action, fd, EV_READ);
    ev_io_start(loop, &io_watcher);

#ifdef DEBUG_PC_FUNC
    fd_tools = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_port = htons(8888);
    //addr bind to listenfd
    if (bind(fd_tools, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        DEBUG_ERR(" bind socket error: %s (errno :%d)",strerror(errno),errno);
        return 0;
    }
    if( listen(fd_tools,10) == -1) {
        DEBUG_ERR(" listen socket error: %s (errno :%d)",strerror(errno),errno);
        return 0;
    }
    ev_io_init(&io_watcher_tools, io_action_tools, fd_tools, EV_READ);
    ev_io_start(loop, &io_watcher_tools);
#endif
    ev_run(loop,0);

    return 0;
}
