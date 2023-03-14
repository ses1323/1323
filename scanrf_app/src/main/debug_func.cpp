#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
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

#ifdef DEBUG_FUNC
using namespace std;
#define ARCH_ARM64					1
//#define MODIFY_DATA	 			1
#define MM2S_CONTROL_REGISTER 		0x00
#define MM2S_STATUS_REGISTER 		0x04

#define SPECIAL    					0xAABBCCDD
#define CMA_BASE_ADDR 				0x1c000000//0x1c000000
#define FPGA_CMD_BASE_ADDR 			0xA0000000

#define RF_BASE_ADDR 				CMA_BASE_ADDR+SIZE_4M
#define START_TAG 					0x9a9b9c9d
#define END_TAG 					0x87654321

#define START_TAG_MASK 				0x88776655
#define END_TAG_MASK 				0x44332211

#define CHANLE_MAX_LENGTH	 		6400

#define BUFFER_SIZE 				4*1024
#define MAX_LINE 					100
#define DEMO_BUF_SIZE 				0x8000
#define TIME_OUT_CNT 				20000

static unsigned int* virtual_dma_cmd_addr = NULL;
unsigned int* virtual_scant_addr = NULL;
unsigned int *virtual_rf_addr = NULL;

unsigned int *demo_buf = NULL;
unsigned int data_buf[SIZE_4M/4];
file_t scan_tb, rfdata_file;

extern volatile bool flag_unfreezed;
unsigned long long last_tag = 0;
volatile unsigned int next_offset = 0, last_aim_byte = 0x2000;
unsigned int index_talb[4]={
	0x00000000,
	0x00000320,
	0x00000780,
	0x00176ce0
};

void write_display_pattern(unsigned int *buf, unsigned int cnt)
{
	if(cnt >= 10)
	{
		printf("write_display_pattern error, cnt too large\n");
		return ;
	}
	for(int i=0; i<cnt; i++) {
		printf("%8x  ", buf[i]);
	}
	write_fpga_cmd(buf, cnt*4);
}

// The pattern that we fill into the buffers
#define TEST_PATTERN(x) ((int)(0x1234ACDE + (x)))
static void init_data(unsigned int *tx_buf, int tx_buf_size)
{
    int  i;
    int *transmit_buffer;

    transmit_buffer = (int *)tx_buf;
    // Fill the buffer with integer patterns
    for (i = 0; i < tx_buf_size/4; i++)
    {
        transmit_buffer[i] = TEST_PATTERN(i);
    }

    return;
}


void show_rf_head(rf_data_t *data)
{
	//RF_DATA_HEADER_INFO r_head;
	if(data != NULL){
		printf("show_rf_head : \n");
		printf("	time tag :%llx\n",		data->rf_head.timeTag);

		printf("	samples :%llx\n",		data->rf_head.samples);
		printf("	dropped :%llx\n",		data->rf_head.dropped);
		printf("	mode :%llx\n",			data->rf_head.mode);
		printf("	MLA :%llx\n",			data->rf_head.MLA);
		printf("	Stream :%llx\n",		data->rf_head.stream);
		printf("	Focus :%llx\n",			data->rf_head.focus);
		printf("	SLN :%llx\n",			data->rf_head.SLN);
		printf("	Packet :%llx\n",		data->rf_head.packet);
		printf("	volumePos :%llx\n",		data->rf_head.volumeLocation);
		printf("	Samplerate :%llx\n",	data->rf_head.sampleRate);
		printf("	signalPath :%llx\n",	data->rf_head.signalPath);
		printf("\n");
	}

	//printf("rf_head :%x,%x,%x,%x\n",	(unsigned int *)data->rf_head,
	//(unsigned int *)(data->rf_head+1),(unsigned int *)(data->rf_head+2),(unsigned int *)(data->rf_head+3));
}

void show_rfdata(rf_data_t *data)
{
	//unsigned int *p = (unsigned int *)rx_buf;
	//for(i=off_set; i<off_set+len; i=i+4) {
	if(data != NULL){
		unsigned int *p=(unsigned int *)data;

		printf("start tag :%8x,%8x\n",data->st_tag_low,data->st_tag_high);
		printf("total_off :%8x\n",data->total_off);
		printf("end_off tag :%8x\n",data->end_tag_off);
		printf("time tag :%llx\n",data->rf_head.timeTag);
		printf("end: low:%x,high:%x\n",data->end_tag_low,data->end_tag_high);
		printf("data->rfdata  :\n");

		int i=0;
		for(i= 0; i < (data->total_off - 16 -48)/4;i++){
			printf("%x ",*(data->rfdata +i));
			if( i% 16 == 15){
				printf("\n");
			}
		}
		printf("data->rfhead  len %d\n",sizeof(RF_DATA_HEADER_INFO));

		show_rf_head(data);
	}
	printf("\n");
}

int send_index_table(unsigned     int *buf)
{
	stable_t *scan_item,item;
	scan_item = &item;

	scan_item->cmd_tp.sp_dw = SPECIAL;
	scan_item->cmd_tp.cmd_id = INDEX_TABLE_UPDATE;
	scan_item->start_offset = 0;
	scan_item->length = sizeof(index_talb); //16bytes
	scan_item->data = (unsigned int *)malloc(scan_item->length);
	if(scan_item->data == NULL)
	{
		printf("%s :%d , error\n ",__func__,__LINE__);
		return -1;
	}
	memcpy(scan_item->data,index_talb,scan_item->length);

	memcpy(buf,scan_item,4*4);
	memcpy(buf + 4, scan_item->data,scan_item->length);
	//printf("%x , %x\n",scan_item->cmd_tp.sp_dw,scan_item->cmd_tp.cmd_id)

	//total 32 Bytes
	write_fpga_cmd(buf,8*4);
	//printf("sizeof(buf), %x\n",sizeof(buf));
	//display_result(buf,8*4,0);
	free(scan_item->data);
	return 0;
}

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

unsigned int parse_strtoul(char *p)
{
	unsigned int ret = 0;

	//char *str =NULL;
	//str = strchr(p,'x');
	char tmp[9];
	memset(tmp,'\0',9);
	memcpy(tmp,p+2,8);
	//strncpy(tmp,str+1,8);
	ret = strtoul(tmp,NULL,16);

	if(ret == 0x7fffffff){
		printf("@parse_str ERROR\n");
	}

	if(0)
		printf("%x,%s\n",ret ,tmp);
	return ret;
}

int read_stable_file(char *path)
{
	FILE *fp;
	//char *p_data =NULL;
	char strLine[MAX_LINE];
	//char input_path[30];
	//input_path = path;
	unsigned int data=0,idx=0;
	fp = fopen(path, "r");
	if (fp == NULL) {
		perror("Error opening input file");
		return -1;
	}

	memset(data_buf,'\0',SIZE_4M);

	while (!feof(fp))									//loop, untile file end
	{
		fgets(strLine,MAX_LINE,fp);					    //将fp所指向的文件一行内容读到strLine缓冲区
		data = parse_strtoul(strLine);
		if(data != 0x7fffffff){
			if(idx < SIZE_4M/4){
				data_buf[idx]=data;						//save data to buf
				//printf("%x ", data_buf[idx]);
				idx++;
			}else{
				perror("Error buffer not enough\n");
			}
		}else{
			printf("parse_strtoul  error\n");
		}
	}
	//p_data = data_buf;
	//idx = strlen(p_data);
	printf("scan table len:%d\n",(idx-1)*4);

    fclose(fp);
	return 0;
}


unsigned char *rfdemo_buf = NULL;
unsigned long long last_time = 0;

int change_timetag(unsigned char *buf,unsigned int buf_len)
{
	unsigned long long time_tag,first_time;
	int line_length = 0x17c0,i=0,cnt =0;
	unsigned long long *p =NULL;
	unsigned char *p1 = NULL;
	p = (unsigned long long *)(buf+0x10);
	first_time=*p;
	for(cnt = 1;cnt * line_length < buf_len;cnt++){
		//printf("diff BUF time:");
		#if 0
		//for(i=0;i<8;i++)
			//printf("%x ",*(buf+i+0x10 + cnt * line_length));
		//printf("\n");
		memcpy(&time_tag,(void*)*(buf + 0x10 + cnt * line_length),8);
		#else
		p= (unsigned long long *)(buf+0x10 + cnt * line_length);

		//time_tag = (*(p+0)<<0)|(*(p+1)<<8)|(*(p+2)<<16)|(*(p+3)<<24)|(*(p+4)<<32)|(*(p+5)<<40)|(*(p+6)<<48)|(*(p+7)<<56);
		//p1= (buf+0x10 + cnt * line_length);
		//time_tag = (*p)<<0|(*p)<<8 |(*p)<<16|(*p)<<24|(*p)<<32|(*p)<<40|(*p)<<48|(*p)<<56;
		//
		*p = first_time + 0x9b8 *cnt;
		time_tag = (*p);
		if( last_time != time_tag ){
			//printf("bytes :%d ~~~~~tag =%x ,%x \n",cnt*line_length, time_tag,time_tag - last_time);
			last_time = time_tag;
		}
		#endif
		//printf("%x \n",time_tag);
	}

}


int read_rfdemo(char *path)
{
	//FILE *fp;
	//char *p_data =NULL;
	unsigned char strLine;
	//char input_path[30];
	//input_path = path;
	unsigned int data=0,idx=0,len,cnt =1024,sum =0;
	#if 0
	FILE *fp_save  = NULL;
	fp_save = fopen("save_data.dat", "wb");
	#endif
	if ((rfdata_file.fp = fopen(path, "r")) == NULL) {
		perror("Error opening input file");
		return -1;
	}

	rfdemo_buf = (unsigned char *)malloc(150*1024*1024);
	if(rfdemo_buf == NULL){
		perror("Error malloc rfdemo_buf\n");
		return  -1;
	}
	unsigned char *tmp_buf = (unsigned char *)malloc(150*1024);

	memset(rfdemo_buf,'\0',150*1024*1024);

	while (!feof(rfdata_file.fp))
	{
		if(idx < 150*1024*1024/cnt){
			len = fread(rfdemo_buf+cnt*idx,1,cnt,rfdata_file.fp);
			if(len!=cnt)
				printf("~len = %d ~  ",len);
			if(len == 0)
				break;
		}
		else{
			perror("Error buffer not enough\n");
			break;
		}
		if(len < cnt){
			printf("Maybe end file\n");
		}
		idx++;
		sum = sum + len;
	}
	rfdata_file.pbuf = rfdemo_buf;
	rfdata_file.len = sum;

	//change_timetag(rfdata_file.pbuf,rfdata_file.len);
	printf("rf_demo len:%ld  \n",sum);
	#if 0
	if(rfdemo_buf!=NULL || rfdata_file.pbuf!=NULL){
		free(rfdemo_buf);
		rfdata_file.pbuf = rfdemo_buf =NULL;
	}

	fwrite(rfdemo_buf,1 ,sum ,fp_save);
	#if 0
	fseek(fp_save,0,SEEK_SET);
	idx =0;
	while (!feof(fp_save))
	{
		len = fread(tmp_buf,1,1024,fp_save);
		if(len < 1024){
			perror("Maybe end file\n");
		}
		if(memcmp(rfdemo_buf+len*idx,tmp_buf,len)!= 0)
			printf("memcmp  error idx =%d\n\n",idx);

		idx++;
	}
	#endif

	//fwrite(rfdata_file.pbuf,1 ,sum ,fp_save);
	fclose(fp_save);
	fp_save =NULL;
	#endif
    free(tmp_buf);
	free(rfdemo_buf);
	fclose(rfdata_file.fp);
	rfdata_file.fp =NULL;
	return 0;
}


unsigned int parse_rfdata(unsigned int *p)
{
	unsigned int ret = 0,total_len =0 ;

	total_len = *(p+2);
	printf("parse_rfdata  len=%x\n",total_len);
	rf_data_t *r_data = (rf_data_t *)malloc(sizeof(rf_data_t));
	r_data->rfdata = (unsigned int*) malloc(total_len -48 - 16);

	if(r_data != NULL){

		memcpy(r_data , p, 32 + 4*4); //head+zero[4] 48bytes
		memcpy(r_data->rfdata, p + 48/4, total_len - 48 -16);

		r_data->zero1[0] = *(p+ r_data->end_tag_off/4-2);
		r_data->zero1[1] = *(p+ r_data->end_tag_off/4-1);
		r_data->end_tag_low = *(p+ r_data->end_tag_off/4);
		r_data->end_tag_high = *(p+ r_data->end_tag_off/4 +1);

		printf("rf_data_t len:%d \n",sizeof(rf_data_t));
		show_rfdata(r_data);

		free(r_data->rfdata);
		free(r_data);
	}
	return ret;
}

unsigned int parse_frame(unsigned int *p)
{
	unsigned int ret = 0;
	//rf_data_t *r_data = (rf_data_t *)p;
	//find start tag
	parse_rfdata(p);
	//unsigned int *next = p + *(p+2)/4;  //next frame;

	return ret;
}

void test_memory(void)
{
	unsigned int *test_buf= (unsigned int *)malloc(SIZE_4M);
	int cnt=4000;
	while(cnt){
		printf("test %d times\n",4000 -cnt);
		memset(test_buf,0,SIZE_4M);
		init_data(test_buf,SIZE_4M);

		cnt--;
	}
	free(test_buf);
}

int test_case(int argc,char **argv)
{
	//unsigned int *frame_buf= (unsigned int *)malloc(DDR_MALLOC_SIZE);
	int dh = open("/dev/mem", O_RDWR |O_SYNC );//O_SYNC ,O_NONBLOCK		// Open /dev/mem which represents the whole physical memory
	if(dh==0)
	{
		printf("Open /dev/mem error ! Return \n");
		return -1;
	}
#if defined(ARCH_ARM64)
	virtual_dma_cmd_addr = (unsigned int *)mmap64(NULL, 0x10000, PROT_READ | PROT_WRITE,
		MAP_SHARED, dh, FPGA_CMD_BASE_ADDR);		  	// Memory map AXI Lite register block
	virtual_scant_addr = (unsigned int *)mmap64(NULL, DDR_MALLOC_SIZE, PROT_READ | PROT_WRITE,
		MAP_SHARED, dh, CMA_BASE_ADDR); 					// Memory map destination address
#else
	virtual_dma_cmd_addr = (unsigned int *)mmap(NULL, 0x10000, PROT_READ | PROT_WRITE,
		MAP_SHARED, dh, FPGA_CMD_BASE_ADDR);		  // Memory map AXI Lite register block
	virtual_scant_addr = (unsigned int *)mmap(NULL, DDR_MALLOC_SIZE, PROT_READ | PROT_WRITE,
		MAP_SHARED, dh, CMA_BASE_ADDR); // Memory map destination address
#endif

	if(virtual_dma_cmd_addr == (void *)-1)
	{
		printf("@@@@mmap Error \n");
	}

	memset(virtual_scant_addr,0,DDR_MALLOC_SIZE);
	virtual_rf_addr = virtual_scant_addr + SIZE_4M/4;
	//init_data(virtual_scant_addr,ST_SIZE);
	printf("virtual_scant_addr =%p  , =%p \n",virtual_scant_addr,virtual_rf_addr);

	printf("@virtual_scant_addr =%x , rf_addr=%x\n",*virtual_scant_addr,*virtual_rf_addr);

	struct timeval last_t,start_t;
	int i = 0,ret = 0,input_cnt=0 ;
	unsigned int send_buf[8]={0};
	unsigned int cmd_buf[8];


	char rfdata_path[]="rfdata_u3.dat";
	char stable_path[]="Scantable.txt";
	while(1){
		char ch = getchar();
		getchar();
		switch(ch)
		{
			case '1':
				printf("test rf data file \n");
				//thread_init(argc,argv);
				memset(virtual_scant_addr,0,DDR_MALLOC_SIZE);
				memset(rfdata_file.file_name,'\0',32);
				memcpy(rfdata_file.file_name,rfdata_path,strlen(rfdata_path)+1);

				if(argv[1]!= NULL)
					read_rfdemo(argv[1]);
				else
					read_rfdemo(rfdata_path);

				#if 1//def  EV_DEMO
				ev_demo(argc,argv);
				//thread_init(argc, argv);
				#endif
				//set fpga dma start
				//dma_set(virtual_dma_cmd_addr,0,0x10000001);				break;

			case '2':

				#if 1
				memset(virtual_scant_addr,0,DDR_MALLOC_SIZE);
				read_stable_file(stable_path); //update scan table
				memcpy(virtual_scant_addr,data_buf,4*1024*1024);
				usleep(500);
				send_index_table(send_buf);
				#endif

				printf("set fpga start \n");

				//dma_set(virtual_dma_cmd_addr,0,0x10000000);				send_start_cmd(send_buf,1);
				usleep(5000);				//printf("set fpga stop \n");
				//send_start_cmd(send_buf,0);
				break;

			case '3':
				//display_result(virtual_scant_addr,32,0);
				memset(virtual_scant_addr,0,DDR_MALLOC_SIZE);

				printf("read_stable_file\n");
				read_stable_file(stable_path); //update scan table
				memcpy(virtual_scant_addr,data_buf,4*1024*1024);
				send_index_table(send_buf);
				if(argc > 2 && argc<10)
				{
					for(i= 1 ;i<argc;i++){
						sscanf(argv[i], "%x",&cmd_buf[i-1] );
						//cmd_buf[i-1] =(unsigned int )atoi(argv[i]);
						printf("%s,%8x  \n",argv[i],cmd_buf[i-1] );
					}
					printf("Write pattern \n");
					write_display_pattern(cmd_buf, argc-1);
				}else{
					printf("cmd buf is not enough  or cmd error\n");
				}

				//display_result(virtual_scant_addr,0x03fff000);				break;

			case '4':
				//printf("set fpga stop \n");
				//send_start_cmd(send_buf,0);
				//test_memory();
				//test_queue();
				//memcpy(frame_buf,virtual_scant_addr,0x4000000);
				thread_init(argc, argv);

				break;

			case '5':
				dma_set(virtual_dma_cmd_addr,0,0x10000001);
				usleep(5000);				gettimeofday(&start_t, NULL);
				memcpy(frame_buf,virtual_scant_addr,512*1024);
				gettimeofday(&last_t, NULL);
				printf("use (%ld)\n",last_t.tv_usec-start_t.tv_usec);
				//check_data(frame_buf,1024*512,0);
				break;

			case '6':
				//udp_client_init(argc,argv);
				fpga_load("hh_wifi_top.bit");
				memset(virtual_scant_addr, 0, DDR_MALLOC_SIZE);
				clearddr_virtual_rfdata();

				//here wait for data
				ev_demo(argc,argv);

				break;

			case '7':
				//tftp_test(argc,argv);
				fpga_load("hh_wifi_top.bit");
				memset(virtual_scant_addr, 0, DDR_MALLOC_SIZE);
				input_cnt = 7;
				while(1){
					#if 1
					printf("please input your cmd args count:\n" );
					ch = getchar();
					input_cnt = ch-'0';
					getchar();
					#endif
					if(input_cnt>=1 && input_cnt<=8){
						printf("waiting for input data(ARGS count =7):\n" );
						for(i= 0; i<input_cnt; i++){
							scanf( "%x", &cmd_buf[i]);
							//sscanf(argv[i], "%x",&cmd_buf[i] );
							printf("%8x	\n",cmd_buf[i]);
						}
						write_fpga_cmd(cmd_buf, input_cnt*4);
						getchar();

					}else{
						printf("cmd buf is not enough  or cmd error,arg count = %d\n",input_cnt);
						break;
					}
					usleep(200);

				}

				break;
			case '8':
				dac_ctl();
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
				if(demo_buf != NULL)
					free(demo_buf);
				close(dh);

				exit(0);
				break;

			default:
				printf("No this cmd \n");
                return -EINVAL;
		}
		usleep(400);
	}

	return 0;

}
#endif

//used by ev_demo.cpp
#if 0
extern unsigned int pat_cmdbuf0[10];
extern unsigned int pat_cmdbuf1[10];
extern unsigned int pat_cmdbuf2[10];

static int handle_msg_process(int fd ,void *buf,unsigned int len)
{
	unsigned int cmd_id,sp_dw,size,to_write,ret = 0,ret_rev= 0,val;
	unsigned int *p ;
	unsigned int pk_len=len, write_cnt = 0, m_count=0;
	static unsigned int sum_pk_len;
	unsigned char save_cmd[4]={'[','5',']','\n'};
	unsigned int s_buf[2];

	char *cbuf;
	char rehandle_tmp[4096];
	pkt pkt_item;
	unsigned int send_buf[8]={0};
	pthread_t send_tid;
	bool get_new_msg =true;

	scan_item = &st_item;
#if 0//network data
	scan_item->cmd_tp.sp_dw = ntohl (*p) ;//pkt length;
	scan_item->cmd_tp.cmd_id = ntohl(*(p+1)) ;
	cmd_id =ntohl ( *(p+1));
	scan_item->start_offset =ntohl (*(p+2));
	scan_item->length =ntohl ( *(p+3)); //16bytes
#else
	//DEBUG HERE

#endif
	int k,rehandle_byte=len;
	cbuf = rehandle_tmp;
	memset(cbuf,'\0',4096);
	if(lastpkg_unhandle == true){
		memcpy(cbuf,lastpkg_cmd,8);
		memcpy(cbuf+8,(char*)buf,rehandle_byte);
		rehandle_byte = rehandle_byte+8;
		lastpkg_unhandle =false;
	}else{
		memcpy(cbuf,(char*)buf,rehandle_byte);
	}

	//rehandle_tmp = (char *)buf;

	while(rehandle_byte >0){
		//printf("handle_msg  len =%d. status =%d \n",rehandle_byte,status);
		p =(unsigned int *)cbuf;
		sp_dw = (*(p+0));

		if((status == WAITING_MSG || status == SENDING_RFDATA)&&sp_dw == 0xaabbccdd){
			cmd_id = *(p+1);
			if(rehandle_byte == 8)//pkg _last
			{
				rehandle_byte = 0;
				lastpkg_unhandle=true;
				memcpy(lastpkg_cmd,cbuf,8);
				break;
			}

			switch(cmd_id)
			{
				case SCAN_TABLE_UPDATE: //0x1
					//pthread_mutex_lock(&g_mutex_lock);
					//pthread_mutex_unlock(&g_mutex_lock);
					scan_item->cmd_tp.sp_dw = (*p) ;//pkt length;
					scan_item->cmd_tp.cmd_id = (*(p+1)) ;
					scan_item->start_offset = (*(p+2));
					scan_item->length = ( *(p+3)); //16bytes
					printf("SCAN_TABLE_UPDATE ,scan_item->length = %d,pk_len = %d \n",scan_item->length,rehandle_byte);

					#ifdef SAVE_CMD_EN
					save_cmd[1]='1';
					memcpy(pkt_item.buf,save_cmd,4);
					pkt_item.len = 4;
					q.push(pkt_item);

					#endif
					//write_fpga_cmd((unsigned int*)cbuf,16);


					memset(scant_buf,'\0',4*1024*1024);

					//first pkg need handle;
					if( rehandle_byte >= 16 ){
						//go to continue handle
						status = UPDATING_SCAN_T;
						rehandle_byte = rehandle_byte -16;
						memcpy(cbuf, cbuf+ 16,rehandle_byte);
						sum_pk_len =0;
					}else{
						rehandle_byte= 0;
					}
					get_new_msg =false;
					break;

				case INDEX_TABLE_UPDATE://0x2
					//HANDLE all  buf ,not have body!

					scan_item->cmd_tp.sp_dw = (*p) ;//pkt length;
					scan_item->cmd_tp.cmd_id = (*(p+1)) ;
					scan_item->start_offset = (*(p+2));
					scan_item->length = ( *(p+3)); //16bytes
					printf("INDEX_TABLE_UPDATE ,scan_item->length = %d,pk_len = %d \n",scan_item->length,pk_len);
					

					#ifdef SAVE_CMD_EN
					save_cmd[1]='2';
					memcpy(pkt_item.buf,save_cmd,4);
					pkt_item.len = 4;
					q.push(pkt_item);
					/*
					write_cnt = rehandle_byte;
					if(write_cnt < 4096 && write_cnt!=0){
						memcpy(pkt_item.buf,cbuf,write_cnt);
						pkt_item.len = write_cnt;
						q.push(pkt_item);
					}else{
						printf("save cmd error ,buf cmd is not enough\n");
					}*/

					#endif

					#ifndef JUMP_LOCALBUS_CMD
					write_fpga_cmd((unsigned int*)cbuf,16);
					#endif
					if(rehandle_byte >= 16 ){
						//go to continue handle
						status = UPDATING_INDEX_T;
						rehandle_byte = rehandle_byte -16;
						memcpy(cbuf, cbuf+ 16,rehandle_byte);
						sum_pk_len= 0;
					}else{
						rehandle_byte= 0;
					}
					get_new_msg =false;
					break;

				case FPGA_CMD_READ: //0x8
					val =  *(p+2);
					s_buf[0] = 0;
					//printf("FPGA_CMD_READ,1   rehandle_byte = %d,cmd val=0x%x ,reg=0x%x :",rehandle_byte,val,*(p+1));

					for(m_count=0;m_count<rehandle_byte/4;m_count++)
						printf("0x%x   ",*(p+m_count));
					printf("\n ");

					//get buf data ,then TCP send  //data come from fd=8 , return use fd=9...
					write_fpga_cmd((unsigned int*)cbuf,12);
					read_fpga_cmd(s_buf,4);

					//read_fpga_cmd(&s_buf[1],4);
					//if(val == 0x10001|| val == 0x0001)
					//printf("FPGA_CMD_READ, 2 read back  ,cmd val=0x%x ,s_buf=0x%x \n",val,s_buf[0]);
					//s_buf[0]=val;//*(p+1);
					if(send(tcp_fpga_read_fd, s_buf,  4, 0)<0)
					{
						printf(" send socket error: %s (errno :%d)\n",strerror(errno),errno);
						usleep(1);
					}

					if( rehandle_byte >= 12 ){
						//go to continue handle
						rehandle_byte = rehandle_byte -12;
						memcpy(cbuf, cbuf+ 12,rehandle_byte);
						sum_pk_len =0;
					}else{
						rehandle_byte= 0;
					}

					break;

				case FPGA_CMD_WRITE: //0x10
					val =  *(p+2);
					#if 0
					write_display_pattern(pat_cmdbuf0,7);
					write_display_pattern(pat_cmdbuf1,7);
					write_display_pattern(pat_cmdbuf2,7);
					#endif

					printf("@@@@@@Start or stop fpga,len = %d,cmd val=0x%x  : ",rehandle_byte,val);
					write_fpga_cmd((unsigned int*)cbuf,12);
					for(m_count=0;m_count<rehandle_byte/4;m_count++)
						printf("0x%x   ",*(p+m_count));
					printf("\n ");

					if(val== 0x00000001){
						status = SENDING_RFDATA;
						flag_unfreezed =true;

						//display_result(virtual_scant_addr,800,0x10fe0);
						#ifndef SEND_RF_FILE
							ret = init_demo_buf();
							if(ret != 0)
								printf("Send data ret error \n");
							pthread_create(&send_tid,NULL,send_rfdata_thread,&fd);
						#else
							pthread_create(&send_tid,NULL,send_rfdata_file_thread,&fd);
						#endif
					}
					if(val== 0x00010001){//stop  // 0x00010001 = 16bit val+16 bit addr;
						status = WAITING_MSG;

						if(flag_unfreezed == true){
							printf("Stop flag_unfreezed=false  \n");
							flag_unfreezed=false;

							pthread_join(send_tid,NULL);
							printf("~~~Stop rfdata_thread ~~~\n");
						}

						clearddr_virtual_rfdata();
					}

					if( rehandle_byte >= 12 ){
						//go to continue handle
						rehandle_byte = rehandle_byte -12;
						memcpy(cbuf, cbuf+ 12,rehandle_byte);
						sum_pk_len =0;
					}else{
						rehandle_byte= 0;
					}

					break;
				case LOCAL_BUS:

					scan_item->cmd_tp.sp_dw = (*p) ;//pkt length;
					scan_item->cmd_tp.cmd_id = (*(p+1)) ;
					scan_item->length = (*(p+2));//12bytes

					#ifdef SAVE_CMD_EN
					save_cmd[1]='4';
					memcpy(pkt_item.buf,save_cmd,4);
					pkt_item.len = 4;
					q.push(pkt_item);

					#endif

					#ifndef JUMP_LOCALBUS_CMD
					write_fpga_cmd((unsigned int*)cbuf,12);
					//printf("@@LOCAL_BUS ,scan_item->length = %d, len=%d\n",scan_item->length,rehandle_byte);
					#endif

					if(rehandle_byte >= 12){
						//go to continue handle
						status = UPDATING_LOCAL_BUS;
						rehandle_byte =  rehandle_byte - 12;
						memcpy(cbuf, cbuf+ 12,rehandle_byte);
						//sum_pk_len = rehandle_byte;
						sum_pk_len =0;
					}else{
						rehandle_byte= 0;
					}
					get_new_msg =false;

					break;
				case LOOP_TEST: //0x20
					printf("@@Cmd  LOOP_TEST: \n \n \n \n \n");
					#if 0
					printf("@@Cmd  LOOP_TEST: cnt =%d\n",cnt++);
					pk_len = *(cbuf+1);
					data_len = pk_len- 2;
					//printf("LOOP_TEST ,pk_len=%d data_len =%d \n",pk_len,data_len);
					if(data_len == 4){
						size = (*(cbuf+2))|(*(cbuf+3)<<8)|(*(cbuf+4)<<16)|(*(cbuf+5)<<24);
						//size = 1<<18;
						//
						//printf("@@Need send size %d,  = (%ld)(%ld)(%ld)(%ld)\n",size,*(cbuf+2),*(cbuf+3)<<8,*(cbuf+4)<<16,*(cbuf+5)<<24);
					}

					if(cur_idx + size <= rfdata_file.len){
						ret = send (watcher->fd, rfdata_file.pbuf + cur_idx, size, 0);
						ret_rev = 0;
						cur_idx += size;
					}
					else{
						printf("file has ended ,reset file,cur_idx = %d\n", cur_idx);
						ret = send (watcher->fd, rfdata_file.pbuf + cur_idx,rfdata_file.len - cur_idx, 0);
						ret_rev= send (watcher->fd, rfdata_file.pbuf ,cur_idx + size - rfdata_file.len, 0);
						cur_idx  = cur_idx + size - rfdata_file.len;
					}

					if((ret + ret_rev) != size ){
						printf("tcp send  msg failed \n");
						ret_rev =0;
					}
					#endif
					break;
				case 0x21: //test : send data from file
					printf("@@Restart ,cur_idx  =%d\n\n\n\n",cur_idx);
						cur_idx =0;
					#if 1
					to_write = 100;
					while(to_write--){
						if(send_once==0){
							for(cnt=0;TCP_PKG_SIZE*cnt <rfdata_file.len;cnt++ ){
								send (fd, rfdata_file.pbuf+TCP_PKG_SIZE*cnt, TCP_PKG_SIZE, 0);
							}
							if(rfdata_file.len - TCP_PKG_SIZE*cnt>0 )
								send (fd, rfdata_file.pbuf+TCP_PKG_SIZE*cnt, rfdata_file.len - TCP_PKG_SIZE*cnt, 0);
						}
						usleep(100);
					}

					#endif
					break;

				default:
					printf("@@@@@@@@@@No this cmd ,last cmdid= %lld,cur cmdid= %lld@@@@@@@@@@\n",scan_item->cmd_tp.cmd_id,cmd_id);

		            return -EINVAL;
			}

			/*
			if(scan_item->data !=NULL) {
				free(scan_item->data);
				scan_item->data =NULL;
			}*/

		}
		else if(status == UPDATING_SCAN_T||status == UPDATING_INDEX_T||status == UPDATING_LOCAL_BUS){

			if(sum_pk_len+rehandle_byte >= scan_item->length){
				write_cnt = scan_item->length-sum_pk_len;
			}
			else{
				write_cnt  = rehandle_byte;
			}

			#ifdef SAVE_CMD_EN
			if(write_cnt <= TCP_PKG_SIZE){
				//printf("UPDATING TO write(%d),sum_pk_len(%d) ,%x,,%x,,%x,,%x,,%x,,%x\n",
				//	write_cnt,sum_pk_len,*p,*(p+1),*(p+2),*(p+3),*(p+4),*(p+5));
				memcpy(pkt_item.buf,cbuf,write_cnt);
				pkt_item.len = write_cnt;
				if(pkt_item.len %4 !=0 ){
					printf("@@@@@save cmd error ,pkt_item.len =%d ,scan_item->length=%d,sum_pk_len=%d,rehandle_byte=%d\n",
						pkt_item.len,scan_item->length,sum_pk_len,rehandle_byte );
				}
				q.push(pkt_item);
			}else{
				printf("save cmd error ,buf cmd is not enough\n");
			}
			#endif

			#ifndef JUMP_LOCALBUS_CMD
			if(status == UPDATING_LOCAL_BUS||status == UPDATING_INDEX_T){

				if(write_cnt %4 !=0 ){
					printf("@@@@@write fpga error write_cnt =%d ,scan_item->length=%d ,rehandle_byte=%d \n",
						write_cnt,scan_item->length,rehandle_byte );
				}
				write_fpga_cmd((unsigned int*)cbuf,write_cnt);

			}else {
				#if 0
				if(sum_pk_len == 0 && write_cnt >=12)
					rf_aim_size= ((*(p+2) >> 16))*4+0x40;
				#endif
				if(sum_pk_len+write_cnt<4096*1024){
					//printf("CP msg to scant_buf,%d\n",write_cnt);
					memcpy(&scant_buf[sum_pk_len],cbuf,write_cnt);
				}
			}
			#endif

			sum_pk_len +=rehandle_byte;
			if(sum_pk_len < scan_item->length){
				rehandle_byte = 0;
			}else if(sum_pk_len == scan_item->length) {
				#ifndef JUMP_LOCALBUS_CMD
				//printf("Scan table update to DDR\n");
				if(status == UPDATING_SCAN_T)
					memcpy(virtual_scant_addr,scant_buf,4096*1024);
				#endif

				printf("@@@@@END (status = %d)@table  len =%d.   sum pkg len %d\n",
					status,scan_item->length,sum_pk_len);
				status = WAITING_MSG;
				sum_pk_len = 0;
				rehandle_byte = 0;
			}else{
				rehandle_byte = sum_pk_len - scan_item->length;
				if(pk_len>rehandle_byte){
					memcpy(cbuf,(char *)buf+(pk_len-rehandle_byte),rehandle_byte);
				//for(k=0;k<6;k++){
				//		printf(" %8x  ",*(p+k));
					//}
				}

				#ifndef JUMP_LOCALBUS_CMD
				if(status == UPDATING_SCAN_T)
					memcpy(virtual_scant_addr,scant_buf,4096*1024);
				#endif

				status = WAITING_MSG;
				sum_pk_len = 0;

			}
			write_cnt=0;
		}else{
			printf("########ERROR Rehandle:::No this cmd ,last cmdid= %lld,cur cmdid= %lld########\n",
				scan_item->cmd_tp.cmd_id,cmd_id);
			return -EINVAL;

		}
	}

	if(len == 0){
		printf("handle_msg_process:Close file\n\n");
		if(fp!= NULL){
			fclose(fp);
			fp = NULL;
		}

		free_demo_buf();

		#if 1
		if(rfdata_file.pbuf != NULL){
			free(rfdata_file.pbuf);
			rfdata_file.pbuf= NULL;
			printf("Free  rfdata_file.pbuf)\n");
		}
		#endif
		new_file = 0;
	}

	return 0;
}

void *handle_msg_thread(void *fd)
{
	pkt* item;
	unsigned int *p;
	printf("Running handle_msg_thread\n");
	int i = 0;
	while(1){
		if (!queue_empty(0)){
			item = queue_front(0);

			if(item->buf!= NULL)
				handle_msg_process(read_cb_fd ,item->buf,item->len);
			else
				printf("$$$$$$handle_msg_thread  item.pk_id =%d ,item.len=%d$$$$$$$\n",item->pk_id,item->len);
			queue_pop(0);
		}

		usleep(1);
	}
}
#endif

