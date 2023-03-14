#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <termios.h>
#include <sys/mman.h>

#include <assert.h>
#include <termios.h>
#include <sys/mman.h>
#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <unistd.h>             // Close() system call
#include <string.h>             // Memory setting and copying
#include <getopt.h>             // Option parsing
#include <errno.h>              // Error codes
#include <queue>
#include <iostream>
#include <ev.h>

#include "scan_rf.h"
#include "i2c_ctrl.h"

using namespace std;
#define ARCH_ARM64                  1
//#define MODIFY_DATA               1
#define MM2S_CONTROL_REGISTER       0x00
#define MM2S_STATUS_REGISTER        0x04
#define MM2S_OFFSET_REGISTER        0x08


#define CHANLE_MAX_LENGTH           6400

#define BUFFER_SIZE                 4*1024
#define MAX_LINE                    100
#define DEMO_BUF_SIZE               0x8000
#define TIME_OUT_CNT                20000

static unsigned int *virtual_dma_cmd_addr = NULL;
unsigned int *virtual_scant_addr = NULL;
unsigned int *virtual_rf_addr = NULL;

unsigned int *demo_buf = NULL;
//file_t scan_tb, rfdata_file;
extern volatile bool flag_unfreezed,soc_error;
unsigned long long last_tag = 0;
volatile unsigned int next_offset = 0, last_aim_byte = 0x6000;
const static long long valid_starttag = 0x123456789A9B9C9D;
const static long long valid_endtag = 0x876543210F0E0D0C;

extern led_t green_led, yellow_led;
#ifdef DEBUG_PC_FUNC
extern SOCKET_TYPE soc_array[TCP_SOCKET_COUNT + 1];
#else
extern SOCKET_TYPE soc_array[TCP_SOCKET_COUNT];
#endif
extern pthread_mutex_t g_fpga_lock;

unsigned int g_ttcode = TTCODE_DEFAULT;
unsigned int g_hwver = 0x0001;

unsigned char g_dev_charge[2]={0};
float g_dev_temp = 0;
float g_dev_core_val = 0;

//#define AFE_PATTERN 1
#ifdef AFE_PATTERN
unsigned int pat_cmdbuf0[10]=
            {0xaabbccdd, 0x00000004, 0x00000010, 0x0111ada5,
             0x00016000, 0x00000021, 0x00000000};

unsigned int pat_cmdbuf1[10]=
            {0xaabbccdd, 0x00000004, 0x00000010, 0xfff0ada5,
             0x00042000, 0x00020380, 0xfffc0018};

unsigned int pat_cmdbuf2[10]=
            {0xaabbccdd, 0x00000004, 0x0000000c, 0x3184eda5,
             0x00012000, 0x0000000f, 0xfffc0018};

unsigned int cmd_bypass[10]=
            {0xaabbccdd, 0x00000004, 0x00000010, 0x0112ada5,
             0x00016000, 0x00020002, 0x00000000};
#endif

unsigned int dma_set(unsigned int* dma_virtual_address,
                        int offset, unsigned int value)
{
    *(volatile unsigned int *)(dma_virtual_address)= value;
    return 0;
}

unsigned int dma_get(unsigned int* dma_virtual_address, int offset)
{
    unsigned int val;
    val = *(volatile unsigned int *)(dma_virtual_address + offset/4);
    return val;
    //return dma_virtual_address[offset >> 2];
}

void read_fpga_cmd(unsigned int *buf, unsigned int len)
{
    if(len%4 != 0)
        DEBUG_ERR(" read_fpga_cmd len%4!=0,len =%d", len);

    for(int i=0; i < len/4; i++){
        buf[i] = dma_get(virtual_dma_cmd_addr, MM2S_STATUS_REGISTER);
    }
}

void write_fpga_cmd(unsigned int *buf, unsigned int len)
{
    if(len%4 != 0)
        DEBUG_ERR(" write_fpga_cmd len%4!=0,len =%d", len);
    for(int i=0; i < len/4; i++){
        dma_set(virtual_dma_cmd_addr, 0, *(buf + i));
    }
}

void read_rf_offset(unsigned int *buf, unsigned int len)
{
    for(int i=0; i < len/4; i++){
        buf[i] = dma_get(virtual_dma_cmd_addr, MM2S_OFFSET_REGISTER);
    }
}

void display_result(void *rx_buf, int len, int off_set)
{
    int i;
    if(rx_buf==NULL){
        printf("display buffer is NULL \n");
        return ;
    }
    unsigned int *p = (unsigned int *)rx_buf;
    printf("display_result len=%x\n", len);
    for(i = off_set; i < (off_set + len); i = i + 4) {

        printf("%p:%8x  ",p + i/4, *(p + i/4));

        if( i% 32 == 28){
            printf("\n");
        }
        if(*(p + i/4) == 0x87654321)
            printf("\n\n");
    }
}

void dump_rfdata(void *buf, int len)
{
    FILE*fp = NULL;
    unsigned int *p, i=0;
    char wbuf[10];
    char ch=' ';
    if( (fp = fopen("dump_rfdata.bin", "wb+") ) == NULL )
    {
        printf("Error File.\n");
        return ;
    }

    if(buf != NULL){
        #ifdef USE_TXT
        for(i = 0; i < len/4; i++){
            p = (unsigned int *)buf;
            sprintf(wbuf, "%08x", *(p + i));
            if(fp != NULL){
                fwrite(wbuf, 1, 8, fp);
                fwrite(&ch, 1, 1, fp);
            }
        }
        #else
        p = (unsigned int *)buf;
        for(i = 0; i < len/4; i++){
            if(fp != NULL){
                fwrite(p + i, 1, 4, fp);
            }
        }
        #endif
    }

    fclose(fp);
}

void dump_st_data(void *buf, int len)
{
    FILE*fp = NULL;
    unsigned int *p, i=0;
    char wbuf[10];
    char ch='\n';
    if( (fp = fopen("dump_st_data.txt", "wb+") ) == NULL )
    {
        printf("Error File.\n");
        return ;
    }

    if(buf != NULL){
        #if 1
        for(i = 0; i < len/4; i++){
            p = (unsigned int *)buf;
            sprintf(wbuf, "0x%08x", *(p + i));
            if(fp != NULL){
                fwrite(wbuf, 1, 10, fp);
                fwrite(&ch, 1, 1, fp);
            }
        }
        #else
        p = (unsigned int *)buf;
        for(i = 0; i < len/4; i++){
            if(fp != NULL){
                fwrite(p + i, 1, 4, fp);
            }
        }
        #endif
    }

    fclose(fp);
}

int clear_headtail_data(unsigned int *rbuf, unsigned int jumpb, int flag)
{
    switch(flag)
    {
        case 0:
            *rbuf = START_TAG_MASK;
            *(rbuf + (jumpb - 4)/4) = END_TAG_MASK;
            break;
        case 1://clear head
            *rbuf = START_TAG_MASK;
            break;
        case 2://clear tail
            *(rbuf + (jumpb - 4)/4) = END_TAG_MASK;
            break;
        default:
            printf("No this flag \n");
            return -EINVAL;
    }

    return 0;
}

unsigned int get_ddr_offset(void)
{
    //0xAABBCCDD,0x08, 0x00000401
    //0xAABBCCDD,0x08, 0x00000400
    unsigned int tmp_buf[3],readbuf[3];
    unsigned int cnt=3;

    while(cnt--)
        read_fpga_cmd(&tmp_buf[2], 4);

    tmp_buf[0] = SPECIAL;
    tmp_buf[1] = FPGA_CMD_READ;
    //low first
    tmp_buf[2] = FPGA_RF_DDR_ADDR_LOW;

    pthread_mutex_lock(&g_fpga_lock);
    write_fpga_cmd(tmp_buf, 4*3);
    tmp_buf[2] = FPGA_RF_DDR_ADDR_HIGH;//0x00000401
    write_fpga_cmd(tmp_buf, 4*3);
    for(cnt = 20;cnt > 0; cnt--)
        asm("nop");
    read_fpga_cmd(&readbuf[1], 4);
    read_fpga_cmd(&readbuf[0], 4);
    pthread_mutex_unlock(&g_fpga_lock);

    cnt = 20;
    if((readbuf[0] & 0xffff) == FPGA_RF_DDR_ADDR_HIGH) {
        readbuf[2] = (readbuf[0] & 0xffff0000);
    } else {
        DEBUG_ERR("####read fpga offset high  readbuf[0]=%x,addr=%x",readbuf[0],tmp_buf[2]);
        while(cnt--){
            read_fpga_cmd(&tmp_buf[2], 4);
            printf("@@@err offset=%x\n",tmp_buf[2]);
        }
        readbuf[2] = 0;
    }

    cnt =  20;
    if((readbuf[1] & 0xffff) == FPGA_RF_DDR_ADDR_LOW) {
        readbuf[2] |=  ( (readbuf[1] >> 16) & 0x0000ffff);
    }else {
        DEBUG_ERR("####read fpga offset low  readbuf[1]=%x,addr=%x",readbuf[1],tmp_buf[2]);
        while(cnt--){
            read_fpga_cmd(&tmp_buf[2], 4);
            printf("@@@err offset=%x\n",tmp_buf[2]);
        }
        readbuf[2] = 0;
    }
    //printf("@@@@@offset = %x\n", readbuf[2]);

    return readbuf[2];
}

long get_current_time()
{
   struct timeval tv;

   gettimeofday(&tv,NULL);
   return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

unsigned int last_off = 0;
int send_rf_data(unsigned int *rx_buf, unsigned int len )
{
    volatile unsigned int cur_off;
    unsigned int *p = rx_buf,cur_buf[2];
    const int time_out_cnt = 20000;//1000=100ms, 20000~2s,
    int send_cnt=0,ret = 0,fpga_alive_cnt = time_out_cnt,fpga_block = 0;
    struct timeval last_t, start_t;
    long start ,stop,tmp;
    int soc_fd = 0;
    rf_data_t *rfdata_p=NULL;
    while( 1 ) {//flag_unfreezed
        if(!flag_unfreezed){
            last_off = 0;
            fpga_alive_cnt = time_out_cnt;
            fpga_block = 0;
            usleep(30);
            continue;
        } else {
            soc_fd = soc_array[SOCKET_CHN_PARAM].m_socketfd;
        }
        if(last_off == 0 || fpga_block > 100000){
            fpga_alive_cnt--;
            if(fpga_alive_cnt > 0 && fpga_alive_cnt%5000 == 0 ) {
                read_rf_offset((unsigned int *)&cur_off,4);
                DEBUG_INFO("rf running...cur_off= %x,alive_cnt =%d,flag_unfreezed=%d,head?=(%x),timetag =(%x,%x)",
                cur_off,fpga_alive_cnt,flag_unfreezed,*(p + 0),*(p + 16/4), *(p + 20/4));
            }
            if(fpga_alive_cnt == 0) {
                DEBUG_ERR(" Maybe FPGA ERROR:fpga_alive_cnt = 0,try to close socket");
                close_socket(0, soc_fd);
            }
        }

        read_rf_offset((unsigned int *)&cur_off,4);
        if(cur_off == 0 && fpga_block ==0){
            usleep(30);
            continue;
        }

        if(cur_off <= len && flag_unfreezed && soc_fd != 0)
        {
            if(cur_off > last_off) {
                /**/
                if(last_off == 0 && *(p + 0) == START_TAG){
                    rfdata_p = (rf_data_t *)p;
                    DEBUG_INFO( "[%d]first , curoff =%x", soc_fd, cur_off);

                    DEBUG_INFO("timetag =(%llx),info=(%x,%x),mode=(%d),real data=(%x,%x),zero=(%x,%x,%x,%x)",
                        rfdata_p->rf_head.timeTag,*(p + 6),*(p + 7),rfdata_p->rf_head.mode,
                    *(p + 12),*(p + 13),*(p + 8),*(p + 9),*(p + 10),*(p + 11));
                    DEBUG_INFO("data =(%x,%x,%x,%x),tail=(%x)",*(p + 0), *(p + 1),*(p + 2),*(p + 3),*(p + *(p + 3)/4));
                }
                /**/
                ret = send_socket_msg(soc_fd, (char *)(p + last_off/4), cur_off - last_off);
                if(ret < 0) {
                    DEBUG_ERR(" Link ERROR(slen=%d),last_off=%d,maybe head=%x,send socket error: %s (errno :%d)",
                        cur_off- last_off,last_off,*(p + 0),strerror(errno),errno);
                    last_off = cur_off;
                    close_socket(0, soc_fd);
                }

                //if(cur_off - last_off >0x10000)
                  //  DEBUG_INFO( "last_off=%x , curoff =%x", last_off, cur_off);
                last_off = cur_off;
                fpga_alive_cnt = time_out_cnt;
                fpga_block = 0;
            } else if ( cur_off == last_off )  {
                fpga_block++;
                if(fpga_block>100000)
                    usleep(30);
                continue;
            } else {
                ret = send_socket_msg(soc_fd, (char *)(p + last_off/4), len - last_off);
                if(ret<0) {
                    DEBUG_ERR(" Link ERROR(slen=%d),send socket error: %s (errno :%d)",
                        cur_off- last_off, strerror(errno),errno);
                    last_off = 0;
                    flag_unfreezed = false;
                    close_socket(0, soc_fd);
                }
                DEBUG_INFO("send len(%x) OK ,cur_off = %x ,last_off = %x, return back",
                        len, cur_off, last_off);

                last_off = 0;
                continue;
            }

        }else {
            usleep(10);
            //printf("send_rf_data Error cur_off =%x，freezed=%x\n", cur_off,flag_unfreezed);
        }

    }

    DEBUG_ERR("ERROR send_rf_data  END\n");
    return -1;

}

int clearddr_virtual_rfdata(void)
{
    struct timeval last_t, start_t;
    gettimeofday(&start_t, NULL);
    memset(virtual_rf_addr, 0xff, DDR_MAP_RF_SIZE);
    gettimeofday(&last_t, NULL);
    printf("clear_virtual_rf_ddr  use (%ld)\n", last_t.tv_usec-start_t.tv_usec);
    return 0;
}

#if 0
int init_demo_buf()
{
    int ret = 0,cnt = 0;

    if(demo_buf == NULL)
        demo_buf = (unsigned int *)malloc(DEMO_BUF_SIZE);

    if(demo_buf == NULL){
        printf("Malloc demo_buf error ! Return \n");
        return -1;
    } else {
        printf("Malloc demo_buf =0x%x ! \n", DEMO_BUF_SIZE);
    }

    memset(demo_buf, 0, 128);
    usleep(3000);
    memcpy(demo_buf, virtual_rf_addr, 128);
    //display_result(demo_buf,128,0);
    return ret;
}

int free_demo_buf(void)
{
    if(demo_buf != NULL){
        free(demo_buf);
        demo_buf = NULL;
    }
}

#endif

int send_start_cmd(unsigned     int *buf,bool val)
{
    stable_t *scan_item,item;
    scan_item = &item;
    scan_item->cmd_tp.sp_dw = SPECIAL;
    scan_item->cmd_tp.cmd_id = FPGA_CMD_WRITE;
    if(val == true)
        scan_item->start_offset = 0x00000001;//start
    else
        scan_item->start_offset = 0x00010001;//stop

    memcpy(buf,scan_item, 4*3);

    write_fpga_cmd(buf, 4*3);
    printf("%x , %x\n", scan_item->cmd_tp.sp_dw, scan_item->cmd_tp.cmd_id);
    //display_result(buf,3*4,0);
    return 0;
}

int get_fpga_verison(unsigned     int *buf,bool val)
{
    stable_t *scan_item,item;
    unsigned int rbuf;
    scan_item = &item;
    scan_item->cmd_tp.sp_dw = SPECIAL;
    scan_item->cmd_tp.cmd_id = FPGA_CMD_READ;
    scan_item->start_offset = 0x00000007;//start

    memcpy(buf,scan_item, 4*3);

    write_fpga_cmd(buf, 4*3);
    usleep(20);
    read_fpga_cmd(&rbuf,4);
    printf("get_fpga_verison %x\n", rbuf);
    //display_result(buf,3*4,0);
    return 0;
}

int test_case(int argc, char **argv)
{
    unsigned int send_buf[8] = {0};
    unsigned char tmpbuf[24] = {0};
    unsigned short vol = 56;
    char md5[40];
    char *md_ret;
    float temp=0 , temp1=0;
    char ch ;
    while(1){
        ch = getchar();
        getchar();
        //char ch = '6';
        switch(ch)
        {
            case '0':
                //fpga_load("hh_wifi_top.bit");
                //dac_init();
                i2c_dev_init();
                bq40_ctl();
                break;

            case '1':
                //fpga_load("hh_wifi_top.bit");
                get_fpga_verison(send_buf,1);
                gpio_led_init();
                break;

            case '2':
                led_bright_ctl(green_led.bright_fd, 1);
                usleep(2000000);

                led_bright_ctl(green_led.bright_fd, 0);
                usleep(2000000);

                led_bright_ctl(yellow_led.bright_fd, 1);
                usleep(2000000);

                led_bright_ctl(yellow_led.bright_fd, 0);
                usleep(2000000);
                break;

            case '3':
                led_trigger_ctl(yellow_led.trigger_fd, LED_TRIGGER_TIMER);
                usleep(1000000);
                led_trigger_ctl(yellow_led.trigger_fd, LED_TRIGGER_NONE);
                usleep(1000000);
                led_trigger_ctl(green_led.trigger_fd, LED_TRIGGER_TIMER);
                break;

            case '4':
                //gpio_ctl_out(GPIO_KILL_POWER);
                power_key_interrupt();

                break;

            case '5':

                read_md5("1",md5);
                printf("md_ret=%s,md5=%s\n",md_ret,md5);

                read_md5("2",md5);
                printf("md_ret = %s,md5=%s\n",md_ret,md5);
                //tcp_server_init();
                //pthread_t tcp_s_tid;
                //pthread_create(&tcp_s_tid, NULL, tcp_server_thread, &listenfd);
                break;

            case '6':
                fpga_load("hh_wifi_top.bit");
                gpio_led_init();
                //ttcode_init();
                //ttcode_write(4,0xfef00002);
                memset(virtual_scant_addr, 0, DDR_MALLOC_SIZE);
                i2c_dev_init();
                dac_init();
                temperature_init();
                clearddr_virtual_rfdata();

                bq40_read_relative_charge(tmpbuf);
                printf("@@@@@tmpbuf[0] =%x\n",tmpbuf[0]);
                if(tmpbuf[0] < 0x64){ //15%
                    led_bright_ctl(green_led.bright_fd,0);
                    led_bright_ctl(yellow_led.bright_fd,1);
                }

                //here wait for data
                ev_demo();
                break;

            case '7':
                sscanf(argv[1], "%d",&vol);
                dac_init();
                dac_config(&vol);
                break;

            case '8':
                ttcode_init();
                ttcode_write(4, 0xfef00002);
                ttcode_read();
                temp = get_xadc_val(0);
                send_buf[0] = (unsigned int)round(temp);
                send_buf[0] = (send_buf[0] << 16 | 0x0301);
                printf("Temp test :float-f(%f), float-x(%x),  uint(%f),uint(%d)\n", temp, temp, send_buf[0], send_buf[0]);
                temp = get_xadc_val(2);
                send_buf[1]  = temp;
                printf("voltage test :float-f(%f), float-x(%x),  uint-f(%f),uint(%d)\n", temp, temp, send_buf[1], send_buf[1]);
                break;

            case 'q':
                if (munmap(virtual_dma_cmd_addr, 0x10000) < 0) {
                    perror("Failed to free  virtual_dma_cmd_addr mapped region");
                    //assert(false);
                }

                if (munmap(virtual_scant_addr, DDR_MALLOC_SIZE) < 0) {
                    perror("Failed to free  virtual_scant_addr mapped region");
                   // assert(false);
                }

                //free_demo_buf();
                exit(0);
                break;

            default:
                printf("No this cmd,ch=%x \n",ch);
                return -EINVAL;
        }
        usleep(10);
    }
    return 0;

}

int main(int argc, char **argv)
{
    unsigned char tmpbuf[4] = {0};
    char ssid[50];
    openlog("VinnoScanrf", LOG_CONS | LOG_PID, LOG_LOCAL2);
    get_profile_string("/etc/wpa_supplicant_ap.conf", "ssid", ssid);
    DEBUG_INFO("This is a syslog start message generated by program '%s' ,ssid=%s",argv[0],ssid);

#ifdef VER_AUTO
    DEBUG_INFO("============================");
    DEBUG_INFO("Soft version:%s",VERSION);
    DEBUG_INFO("compile date:%s",DATE);
    DEBUG_INFO("git version:%s",GITVER);
    DEBUG_INFO("============================");
#endif

    int dh = open("/dev/mem", O_RDWR | O_SYNC );//O_NONBLOCK
    if(dh == 0)
    {
        printf("Open /dev/mem error ! Return \n");
        return -1;
    }

#if defined(ARCH_ARM64)
    // Memory map AXI Lite register block
    virtual_dma_cmd_addr = (unsigned int *)mmap64(NULL, 0x10000, PROT_READ | PROT_WRITE,
        MAP_SHARED, dh, FPGA_CMD_BASE_ADDR);
    // Memory map destination address
    virtual_scant_addr = (unsigned int *)mmap64(NULL, DDR_MALLOC_SIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED, dh, CMA_BASE_ADDR);
#else
    // Memory map AXI Lite register block
    virtual_dma_cmd_addr = (unsigned int *)mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
        MAP_SHARED, dh, FPGA_CMD_BASE_ADDR);
    // Memory map destination address
    virtual_scant_addr = (unsigned int *)mmap(NULL, DDR_MALLOC_SIZE, PROT_READ | PROT_WRITE,
        MAP_SHARED, dh, CMA_BASE_ADDR);
#endif

    if(virtual_dma_cmd_addr == (void *)-1)
    {
        printf("@@@@mmap Error \n");
    }

    memset(virtual_scant_addr, 0, DDR_MALLOC_SIZE);
    virtual_rf_addr = virtual_scant_addr + SIZE_4M/4;

    DEBUG_INFO("virtual_scant_addr =%p, rf_addr=%p ", virtual_scant_addr, virtual_rf_addr);
    DEBUG_INFO("*virtual_scant_addr =%x, *rf_addr=%x", *virtual_scant_addr, *virtual_rf_addr);
#if 0
    test_case(argc, argv);
#endif

    //fpga_load("hh_wifi_top.bit");
    gpio_led_init();
    g_ttcode =  ttcode_read();

    g_hwver = hwver_read(tmpbuf);
    printf("hwver = %d\n",g_hwver);
    memset(virtual_scant_addr, 0, DDR_MALLOC_SIZE);

    i2c_dev_init();
    dac_init();
    temperature_init();
    clearddr_virtual_rfdata();

    bq40_read_relative_charge(g_dev_charge);
    if(g_dev_charge[0] < 0x14){ //15%
        led_bright_ctl(green_led.bright_fd,0);
        led_bright_ctl(yellow_led.bright_fd,1);
    } else {
        //waiting for connect
        led_trigger_ctl(green_led.trigger_fd, LED_TRIGGER_TIMER);
    }

    pthread_t battery_tid;
    pthread_create(&battery_tid, NULL, check_dev_thread, NULL);

    pthread_t check_net_alive;
    pthread_create(&check_net_alive, NULL, check_net_thread, NULL);
#if 1
    pthread_t check_fpga_speed;
    pthread_create(&check_fpga_speed, NULL, cal_fpga_write_speed, NULL);
#endif
    //here wait for data
    ev_demo();

    closelog();
    close(dh);

    return 0;

}
