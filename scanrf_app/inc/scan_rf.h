#ifndef SCAN_RF_H_
#define SCAN_RF_H_

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>

#include "wifi_config.h"
#include "i2c_ctrl.h"

#include "auto_version.h"
#include "kvconf.h"


//#define NO_SYSLOG
#ifdef NO_SYSLOG
#define DEBUG_LINE() printf("[%s:%s] line=%d\r\n",__FILE__, __func__, __LINE__)
#define DEBUG_ERR(fmt, args...) printf("\033[46;31m[%s:%d]\033[0m "#fmt" errno=%d, %m\r\n", __func__, __LINE__, ##args, errno, errno)
#define DEBUG_INFO(fmt, args...) printf("\033[33m[%s:%d]\033[0m "#fmt"\r\n", __func__, __LINE__, ##args)
#else
#define DEBUG_LINE()    syslog(LOG_INFO,"[%s:%s] line=%d\n",__FILE__, __func__, __LINE__)
#define DEBUG_ERR(fmt, args...) syslog(LOG_ERR, "[%s:%d]"#fmt" errno=%d, %m", __func__, __LINE__, ##args, errno, errno)
#define DEBUG_INFO(fmt, args...) syslog(LOG_INFO,"[%s:%d]"#fmt, __func__, __LINE__, ##args)
//#define DEBUG_LINE()
//#define DEBUG_ERR(fmt, ...)
//#define DEBUG_INFO(fmt, ...)
#endif


#define DDR_MALLOC_SIZE             0x4000000                       //64M
#define SIZE_4M                     4*1024*1024                     //4M = 0x400000
#define DDR_MAP_RF_SIZE             (DDR_MALLOC_SIZE - SIZE_4M)     //60M

#define TCP_PKG_SIZE                1344//tcp max is 1440
#define START_TAG                   0x9A9B9C9D
#define END_TAG                     0x87654321
#define CMD_FIXED_ARM_FLAG          0xFFEEDDCC

#define PKG_PAR_HEAD_FLAG           0x0ABCDEF0
#define PKG_PAR_END_FLAG            0x0FEDCBA0

#define START_TAG_MASK              0x88776655
#define END_TAG_MASK                0x44332211

#define SPECIAL                     0xAABBCCDD
#define CMA_BASE_ADDR               0x30000000//0x1c000000
#define FPGA_CMD_BASE_ADDR          0xA0000000
#define RF_BASE_ADDR                CMA_BASE_ADDR + SIZE_4M

#define ARMCMD_WRITE_TYPE           0x80000000
#define ARMCMD_READ_TYPE            0x82000000

#define TTCODE_FIX_MASK             0xfe000000
#define TTCODE_DEFAULT              0xfef00002

#define DEBUG_PC_FUNC               1

#define LED_TRIGGER_NONE "none"
#define LED_TRIGGER_TIMER "timer"
#define SYSLOG_NAME "VinnoScanrf"
#define GPIO_KILL_POWER "414"  //gpio338+76

//#define DEBUG_CN0_PAR               1

enum ARM_SOC_CMD
{
    ARM_CONF_THV        = (ARMCMD_WRITE_TYPE + 1),
    ARM_READ_THV        = (ARMCMD_READ_TYPE + 1),

    ARM_WRITE_TTCODE    = (ARMCMD_WRITE_TYPE + 2),
    ARM_READ_TTCODE     = (ARMCMD_READ_TYPE + 2),

    ARM_READ_TEMP       = (ARMCMD_READ_TYPE + 3),
    ARM_READ_CORE_VOL   = (ARMCMD_READ_TYPE + 4),
    ARM_READ_CHARGE     = (ARMCMD_READ_TYPE + 5),

    ARM_WRITE_SN        = (ARMCMD_WRITE_TYPE + 6), //write serial number

    ARM_WRITE_DONGLE    = (ARMCMD_WRITE_TYPE + 7),
    ARM_READ_DONGLE     = (ARMCMD_READ_TYPE + 7),

    ARM_WRITE_MCODE     = (ARMCMD_WRITE_TYPE + 8), //write machine code

    ARM_WRITE_LMU       = (ARMCMD_WRITE_TYPE + 9), //write mfg wifi lmu config
    ARM_READ_LMU        = (ARMCMD_READ_TYPE + 9), //read mfg wifi lmu config

    ARM_WRITE_KILLPOWER = (ARMCMD_WRITE_TYPE + 0xa), //try to kill power

    ARM_WRITE_TIME      = (ARMCMD_WRITE_TYPE + 0xb), //sync world time

    ARM_WRITE_MARKCODE  = (ARMCMD_WRITE_TYPE + 0xc), //write market code

    ARM_WRITE_HWVER     = (ARMCMD_WRITE_TYPE + 0xd), //write hardware version
    ARM_READ_HWVER     = (ARMCMD_READ_TYPE + 0xd),

    ARM_READ_BQ_TEMP    = (ARMCMD_READ_TYPE + 0x0e),
    ARM_READ_BQ_SOH     = (ARMCMD_READ_TYPE + 0x0f),
    ARM_READ_BQ_CYCLE   = (ARMCMD_READ_TYPE + 0x10),

    ARM_READ_ARM_VER    = (ARMCMD_READ_TYPE + 0xfff9),

    ARM_WRITE_DUMP_ST   = (ARMCMD_WRITE_TYPE + 0xfffa),

    ARM_WRITE_UPDATE    = (ARMCMD_WRITE_TYPE + 0xfffb),
    ARM_READ_UPDATE     = (ARMCMD_READ_TYPE + 0xfffb),

    ARM_READ_DUMP_RF    = (ARMCMD_READ_TYPE + 0xfffc),

    ARM_WRITE_CHECK_ALIVE = (ARMCMD_WRITE_TYPE + 0xfffd),

    ARM_WRITE_FPGA_LOAD = (ARMCMD_WRITE_TYPE + 0xfffe),

    ARM_HANDLE_LIST     = (ARMCMD_WRITE_TYPE + 0xffff),
};

//#define DEBUG_FUNC              1

/*
------------------------------------------------------------------------------
SOCKET_CHN_PARAM        : |channel get localbus & indextable & scantable |send rf data
------------------------------------------------------------------------------
SOCKET_CHN_RW           : |channel get Read/Write FPGA cmd      |send read fpga info
------------------------------------------------------------------------------
SOCKET_CHN_ARM_CMD      : |channel get Read/Write ARM cmd       |send read arm cmd data

SOCKET_CHN_PC_TEST_CMD    : |get fpga cmd to fpga directly        |
*/

enum SOCKLET_CHN
{
    SOCKET_CHN_PARAM        = 0x0,//SOCKET_CHN_SCANTABLE 	= 0x1,
    SOCKET_CHN_RW           = 0x1,
    SOCKET_CHN_ARM_CMD      = 0x2,

    TCP_SOCKET_COUNT
};

#ifdef DEBUG_PC_FUNC
#define  SOCKET_CHN_PC_TEST_CMD    0x3
#endif

enum FPGA_ADDR
{
    FPGA_RF_DDR_ADDR_HIGH   = 0x00000401,
    FPGA_RF_DDR_ADDR_LOW    = 0x00000400,
};

typedef struct file_args {
    char            file_name[32];  //SPECIAL DW ,AABBCCDD
    FILE            *fp;
    unsigned char   *pbuf;
    unsigned int    len;            //CMD ID,32’h00000001
} file_t;

typedef struct cmd_def {
    unsigned int    sp_dw;          //SPECIAL DW ,AABBCCDD
    unsigned int    cmd_id;         //CMD ID,32’h00000001
} cmd_t;

typedef struct scan_table_up {
    cmd_t           cmd_tp;
    unsigned int    start_offset;   //START ADDRESS OFFSET,Bits[21:0] are utilized(4MB space)
    unsigned int    length;         //Bits[21:0] are utilized(4MB space)
    unsigned int    *data;          //Data shall be 16B alignment
} stable_t;

typedef enum {
    //FPGA_START_CMD = 0x0,
    SCAN_TABLE_UPDATE   = 0x1,
    INDEX_TABLE_UPDATE  = 0x2,
    LOCAL_BUS           = 0x4,
    FPGA_CMD_READ       = 0x8,
    FPGA_CMD_WRITE      = 0x10,
    LOOP_TEST           = 0x20,

    ARM_CMD             = 0x3,
}cmd_type;

typedef struct socket_type
{
    unsigned int  	soc_id;//index
    volatile int    m_socketfd;
    unsigned char 	cmd_type; //to map ToFpgaCmdId
    struct ev_io   *soc_w_client;
    struct ev_loop *soc_loop;
    pthread_mutex_t soc_lock;

} SOCKET_TYPE, *pSOCKET_TYPE;

typedef struct rf_data_header_info {
    unsigned long long timeTag;  	//8 bytes
    unsigned long long samples:15;
    unsigned long long dropped:1;
    unsigned long long mode:3;
    unsigned long long MLA:2;
    unsigned long long stream:4;
    unsigned long long focus:4;
    unsigned long long SLN:9;
    unsigned long long packet:5;
    unsigned long long volumeLocation:16;
    unsigned long long sampleRate:2;
    unsigned long long signalPath:1;
    unsigned long long ngsValid:1;
    unsigned long long reserved:1;  //8 bytes

} RF_DATA_HEADER_INFO, *PRF_DATA_HEADER_INFO;

typedef struct rf_data {
    unsigned int st_tag_low;   	//0x9a9b9c9d
    unsigned int st_tag_high;	//0x12345678
    unsigned int total_off;
    unsigned int end_tag_off;	//up 16bytes
#if 0
    unsigned int time_tag_low;
    unsigned int time_tag_high;
    unsigned int beaminfo_low;
    unsigned int beaminfo_high; //up 32Bytes
#else
    RF_DATA_HEADER_INFO rf_head;   //16bytes ,timetag + beaminfo
#endif
    unsigned int zero[4];
    unsigned int *rfdata; 		//N*4 字节，N>0
    unsigned int zero1[2];
    unsigned int end_tag_low;	//f0e0d0c
    unsigned int end_tag_high; 	//87654321
} rf_data_t;

typedef struct parameter_data {
    unsigned int head_tag;           //0x0abcdef0
    unsigned int total_len;
    unsigned int local_off;
    unsigned int local_len;
    unsigned int indext_off;
    unsigned int indext_len;
    unsigned int scant_off;          //scantable offset
    unsigned int scant_len;
    unsigned int *local_data;        //1MBytes
    unsigned int *indext_data;       //64Bytes
    unsigned int *scant_data;        //2MBytes


    //unsigned int local_data[1024*1024/4]; //1MBytes
    //unsigned int indext_data[16];       //64Bytes
    //unsigned int scant_data[2048*1024/4];//2MBytes
    unsigned int end_tag;               //0x0fedcba0
} par_pkt_t;




typedef enum {
    WAITING_MSG 		= 0x0,
    UPDATING_SCAN_T 	= 0x1,
    UPDATING_INDEX_T 	= 0x2,
    UPDATING_LOCAL_BUS 	= 0x3,
    SENDING_RFDATA 		= 0x4
}work_status;

typedef struct gpio_led {
    unsigned int    led_id;
    unsigned int    bright_fd;
    unsigned int    trigger_fd;
    //char          g_node_name[32];
    FILE *bright_fp;
    FILE *trigger_fp;
    //unsigned char 	*pbuf;
} led_t;

extern int sockfd,server_len;
extern struct sockaddr_in server;
extern file_t scan_tb, rfdata_file;

int send_start_cmd(unsigned     int *buf, bool val);
int init_demo_buf();
int free_demo_buf(void);
int clearddr_virtual_rfdata(void);
unsigned int parse_fpga_cmd(unsigned int *buf, unsigned int len,unsigned int idx);

void write_fpga_cmd(unsigned int *buf, unsigned int len);
void read_fpga_cmd(unsigned int *buf, unsigned int len);
unsigned int get_ddr_offset(void);

void read_rf_offset(unsigned int *buf, unsigned int len);
int send_rf_data(unsigned int *rx_buf, unsigned int len );
int check_data(int soc_fd,unsigned int *rx_buf, unsigned int len,
                unsigned int offset, unsigned int *next_off);
void *send_rfdata_thread(void *args);
void *send_rfdata_file_thread(void *fd);
void display_result(void *rx_buf, int len, int off_set);
void write_display_pattern(unsigned int *buf, unsigned int cnt);
int send_socket_msg(int soc_fd,char *buffer, int length);

extern unsigned int *virtual_scant_addr;
extern volatile work_status status;
int ev_demo(void);

//int udp_client_init(void);
int udp_client_init(int argc, char *argv[]);
int udp_client_test(int argc, char *argv[]);
int tcp_clinet_test(int argc, char **argv);
int tftp_test(int argc, char **argv);
int thread_init(int argc, char **argv);
void *tcp_server_thread(void *fd);
void *check_net_thread(void *args);

int tcp_server_init(void);
int tcp_server_exit(int *fd);
int fpga_load(const char *file_name );
int close_socket(int idx,int soc_fd);

int hwver_read(unsigned char *str);

int  dac_init(void);
int  dac_config(unsigned short *voltage);
int repalce_ap_conf(const char *new_str ,const char *prefix_str);


void dump_rfdata(void *buf, int len);
void dump_st_data(void *buf, int len);
unsigned int get_ddr_offset(void);

int test_bmp(int argc, char *argv[] );
int power_key_interrupt();
int update_vinno_img(int soc_fd,char *cmd_arg, unsigned char *ip);
int vi_update(int soc_fd,int argc, char **argv);
int read_md5(const   char *cmd,char *str);

//led ctl
void gpio_led_init(void);
void gpio_led_exit(void);
int  led_bright_ctl(int led_fd,unsigned int on);
int led_trigger_ctl(int led_trigger_fd,const char *trigger_type);
int  led_trigger_delay(int led_id,char *on_val, char *off_val);
int gpio_ctl_out(const char *gpio_num, const char *out_val);

long get_current_time();
void *cal_fpga_write_speed(void *args);


//log manager
void start_vinnolog();
#endif /* SCAN_RF_H_ */

