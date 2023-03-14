#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <semaphore.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>
#include<stdlib.h>

#include "scan_rf.h"
#include "wifi_config.h"
#include "iniparser.h"


FILE *tt_fp = NULL;
FILE *dongle_fp = NULL;
extern unsigned int g_ttcode;

/*
10 = FCC
20 = IC
30 = ETSI  ，Radio Type ST60_440
31 = KCC
32 = AU
40 = JP
50 = CN
5242=99
FF is WW
*/
#if 1
struct region_code{
    unsigned char code_num[8];
    unsigned char code_char[8];

};
#endif
#define MAX_REGION_CNT 20
struct region_code region_list[MAX_REGION_CNT] ={
    {"10", "FCC"},
    {"20"
, "IC"},
    {"30"
, "EN440"},
    {"31"
, "KCC"},
    {"32"
, "AU"},
    {"40"
, "JP"},
    {"50"
, "CN"},
    {"FF"
, "WW"},
    {"5242","99"},
};

unsigned int ttcode_read(void)
{
    unsigned int val = 0;
    char rbuf[9];
    memset(rbuf,'\0', 9);
    tt_fp = fopen(TTCODE_FILE, "r");

    if(tt_fp != NULL){
        fseek(tt_fp, 0, SEEK_SET);
        fread(rbuf, 8, 1, tt_fp);
    }

    val = (unsigned int)strtoul(rbuf,0,16);
    //printf("@@@ttcode_read  : fread val tt_fp=%x,rbuf=%s, val=%x\n",tt_fp,rbuf,val);

    ttcode_file_close();
    if(val == 0){
        return TTCODE_DEFAULT;
    } else {
        return val;
    }
}

int ttcode_write(int len, unsigned int val)
{
    ttcode_init();
    char wbuf[10];
    sprintf((char *)wbuf, "%08x", val);

    if(tt_fp != NULL){

        fseek(tt_fp, 0, SEEK_SET);
        fwrite(wbuf, 8, 1, tt_fp);
        //fwrite(&val, len, 1, tt_fp);
        //fsync(fileno(tt_fp));
        g_ttcode = val;
    }

    ttcode_file_close();

    return 0;
}

int ttcode_init(void)
{
    if( (tt_fp = fopen(TTCODE_FILE, "w+") ) == NULL )
    {
        printf("Error File.\n");
        return -1;
    }
    return 0;
}

int ttcode_file_close(void)
{
    if(tt_fp != NULL){
        //printf("ttcode_file_close\n");
        fclose(tt_fp);
    }
    sync();
    system("sync");

    return 0;
}

unsigned int config_file_read(const char *fname,char *buf, unsigned int *ret_len)
{
    unsigned int val = 0;
    char rbuf[9];
    memset(rbuf,'\0', 9);
    dongle_fp = fopen(fname, "r");
    int idx = 0;
    if(dongle_fp != NULL){
        fseek(dongle_fp, 0, SEEK_END);
        *ret_len = ftell(dongle_fp);
        fseek(dongle_fp, 0, SEEK_SET);

        fread(buf, *ret_len, 1, dongle_fp);
        printf("region char = %s,len = %d\n",buf,strlen(buf));
        if(strcmp(REGION_CUR_FILE,fname) == 0){
            printf("modify mfg current\n");
            #if 1
            for(idx = 0;idx < MAX_REGION_CNT;idx++){
                 printf("@@@@code_num = %s\n",region_list[idx].code_num);
                 printf("@@@@code_num len = %d\n",strlen((const char*)region_list[idx].code_num));
                if(strncmp(buf, (const char*)region_list[idx].code_num, 2) == 0){
                    printf("code_char len = %d\n",strlen((const char*)region_list[idx].code_char));
                    memset(buf,'\0',strlen(buf));
                    memcpy(buf ,region_list[idx].code_char, strlen((const char*)region_list[idx].code_char));
                    printf("region char = %s\n",buf);
                    *ret_len = 8;
                    break;
                }
            }
            if(idx == MAX_REGION_CNT)
                DEBUG_ERR("maybe cannot find region name");
            #endif

        }

    }else{
        //dongle file is empty
        if(strcmp(DONGLE_FILE,fname) == 0){
            printf("dongle.bin not exist\n");
            *ret_len = 256;
            memset(buf,0, *ret_len);
        }
    }
    config_file_close(dongle_fp);
}

int config_file_write(const char *fname,char* buf,int len)
{
    dongle_fp = config_file_init(fname);
    //char wbuf[10]={0};
    //sprintf((char *)wbuf, "%08x", val);

    if(dongle_fp != NULL){
        fseek(dongle_fp, 0, SEEK_SET);
        //if(len <= sizeof(wbuf))
        fwrite(buf, len, 1, dongle_fp);
        //fwrite(&val, len, 1, tt_fp);
        //fsync(fileno(tt_fp));
    }
    config_file_close(dongle_fp);

    return 0;
}

FILE *config_file_init(const char *fname)
{
    FILE *fp =NULL;
    if( (fp = fopen(fname, "w+") ) == NULL )
    {
        printf("Error File.\n");
    }
    return fp;
}

int config_file_close(FILE *fp)
{
    if(fp != NULL){
        //printf("ttcode_file_close\n");
        fclose(fp);
    }
    sync();
    system("sync");

    return 0;
}

// 该函数用来寻找子字符串在被查找的目标字符串中出现的次数；
int substr_count(char *str, const char *sub_str)
{
    if (str == NULL || sub_str == NULL)
    {
        printf("ERROR：[substr_count] parameter(s) should not be NULL!\n");
        return -1;
    }
    int n = 0;                   // 用来表示子串在被查找的目标字符串中出现的次数
    char *begin_ptr = str;
    char *end_ptr = NULL;        // 将NULL初始化给end_ptr，防止指向不明

    // strstr函数包含在头文件string.h中，其函数原型如下：
    // char *strstr(const char *str, const char *SubStr)，其参数解释如下：
    //  str     --->指向被查找的目标字符串“父串”
    //  SubStr  --->指向要查找的字符串对象“子串”
    // 该函数用来搜索“子串”在“指定字符串”中第一次出现的位置（char *即字符串指针）
    // 若成功找到，返回在“父串”中第一次出现的位置的char *指针
    // 若未找到，也即不存在这样的子串，返回：“NULL”
    while ((end_ptr = strstr(begin_ptr, sub_str)) != NULL)
    {
        end_ptr += strlen(sub_str);
        begin_ptr = end_ptr;            // 更新起始地址，重新调用strstr函数进行匹配。
        ++n;
    }
    return n;
}

//用new_str替换prefix_str之后的字符串，直到本行结束
int str_replace(const char *file_path, const char *new_str, const char *prefix_str)
{
    char str[100] ={0}, aim[32]={0};
    if (file_path == NULL || new_str == NULL || prefix_str == NULL)
    {
        printf("ERROR: [str_replace] parameter(s) should not be NULL！\n");
        return -1;
    }

    FILE *fp = NULL;
    if ((fp = fopen(file_path, "a+")) == NULL)
    {
        printf("ERROR: file open error!\n");
        return -1;
    }
    // 用fopen函数打开一个“读写”的文件（w+表示可读可写），如果打开文件成功，函数的返回值
    // 是该文件所建立的信息区的起始地址，把它赋给指针变量fp（fp已定义为指向文件的指针变）。
    // 如果不能成功的打开文件，则返回NULL。

    long file_len;
    fseek(fp, 0, SEEK_END);     // 将文件指针移动到文件结尾，成功返回0，不成功返回-1
    file_len = ftell(fp);     // 求出当前文件指针距离文件开始的字节数
    fseek(fp, 0, SEEK_SET);    // 再定位指针到文件头
    // 在C语言中测试文件的大小，主要使用二个标准函数
    // （1）fseek: 函数原型为 int fseek(FILE *_Stream,long _Offset,int _Origin)
    // 参数说明: _Stream,文件流指针；_Offset，偏移量； _Origin，原始位置。其中 _Origin
    // 的可选值有SEEK_SET（文件开始）、SEEK_CUR（文件指针当前位置）、SEEK_END（文件结尾）。
    //  函数说明：对于二进制模式打开的流，新的流位置是 _Origin+_Offset。
    // （2）ftell:  函数原型为  long int ftell(FILE * _Stream)
    //  函数说明：返回流的位置。对于二进制流返回值为距离文件开始位置的字节数。


    while(fgets(str, 100, fp)!=NULL) {
        if(strstr(str,prefix_str) != 0) {
            //fprintf(fp1, "%d\n", i-1);
            if( strlen(str) - strlen(prefix_str) < sizeof(aim))
                strncpy(aim, str + strlen(prefix_str), strlen(str) - strlen(prefix_str)-1 ); //del'\0'
            printf("@@@@@@@aim len:%d, st=%d,ol=%d,aim %s\n", strlen(aim) ,strlen(str),strlen(prefix_str),aim);
        }
    }
    fseek(fp, 0, SEEK_SET);

    char *ori_str = (char *)malloc(file_len*sizeof(char) + 1);
    if (ori_str == NULL)
    {
        printf("ERROR: malloc ori_str failed!\n");
        fclose(fp);
        return -1;
    }
    memset(ori_str, 0, file_len*sizeof(char) + 1);
    // 开辟空间给ori_str
    // malloc函数申请内存空间，file_len*sizeof(char)是为了更严谨，16位机器上char占一个字符，其他机器上可能变化
    // 用malloc函数申请的内存是没有初始值的，如果不赋值会导致写入到时候找不到结束标志符而出现内存比实际申请值大
    // 写入数据后面跟随乱码的情况
    // memset函数将内存空间都赋值为“\0"

    int count = 1;
    int ret = fread(ori_str, file_len*sizeof(char), count, fp);
    // printf("%d\n",ret);
    // C语言允许用fread函数从文件中读一个数据块，用fwrite函数向文件写一个数据块。在读写时
    // 是以二进制形式进行的。在向磁盘写数据时，直接将内存中一组数据原封不动、不加转换复制到
    // 磁盘文件上，在读入时也是将磁盘文件中若干字节的内容一批读入内存。
    // 它们的一般调用形式为：
    // fread(buffer,size,count,fp);
    // fwrite(buffer,size,count,fp);
    // 其中，buffer是一个地址。对fread来说，它是用来存放从文件读入的数据的存储区的地址。
    // 对fwrite来说，它是要把此地址开始的存储区中的数据向文件输出（以上指的是起始地址）
    // size: 要读写的字节数；
    // count: 要读写多少个数据项（每个数据项长度为size);
    // fp: FILE类型指针。
    // fread或fwrite函数的类型为int型，如果fread或fwrite函数执行成功，则函数返回值为形参count
    // 的值，即输入或输出数据项的个数。

    if (ret != count)
    {
        printf("ERROR: read file error!\n");
        fclose(fp);
        free(ori_str);     //释放malloc开辟的内存区域
        return -1;
    }

    int n = substr_count(ori_str, prefix_str);
    if (n == -1)
    {
        printf("ERROR: substring count error!\n");
        fclose(fp);
        free(ori_str);     //释放malloc开辟的内存区域
        return -1;
    }
    //  计算子字符串在父串中出现的次数
    //  如果出现错误，需要关闭文件，释放内存空间
    //  不能忘记

    int rst_str_len = file_len + n * abs(int(strlen(new_str) - strlen(aim))) + 1;
    char *rst_str = (char *)malloc(rst_str_len*sizeof(char));
    if (rst_str == NULL)
    {
        printf("ERROR: malloc rst_str failed!\n");
        fclose(fp);
        return -1;
    }
    memset(rst_str, 0, rst_str_len*sizeof(char));

    char *cpy_str = rst_str;
    char *begin_ptr = ori_str;
    char *end_ptr = NULL;

    // 替换过程
    while ((end_ptr = strstr(begin_ptr, prefix_str)) != NULL)  //子字符串只要匹配上，执行循环体
    {
        end_ptr += strlen(prefix_str);
        memcpy(cpy_str, begin_ptr, end_ptr- begin_ptr);
        cpy_str += (end_ptr - begin_ptr);
        memcpy(cpy_str, new_str, strlen(new_str));     //在字符串后面拷贝new_str
        cpy_str += strlen(new_str);
        end_ptr += strlen(aim);
        begin_ptr = end_ptr;
    }
    fclose(fp);

    strcpy(cpy_str,begin_ptr);

    printf("ori：%s\n", ori_str);
    printf("rst：%s\n", rst_str);

    FILE *fp_1 = NULL;
    if ((fp_1 = fopen(file_path, "w")) == NULL)
    {
        printf("ERROR: file open error!\n");
        return -1;
    }

    ret = fwrite(rst_str,strlen(rst_str),count, fp_1);
    if (ret != count)
    {
        printf("ERROR: write file error!\n");

        fclose(fp_1);
        free(ori_str);     //释放malloc开辟的内存区域
        free(rst_str);
        return -1;
    }

    //free and close
    free(ori_str);
    free(rst_str);
    fclose(fp_1);

    return 0;
}

int repalce_ap_conf(const char *new_str ,const char *prefix_str)
{
    const char *file_path = "./wifi_config.ini";
    if (str_replace(file_path, new_str, prefix_str) != 0)
    {
        printf("INFO: string replace failed！\n");
        return -1;
    }
    sync();
    system("sync");
    return 0;
}

void set_linux_systime(int year,int month,int day,int hour,int min,int sec)
{
    struct tm tptr;
    struct timeval tv;
    printf("set_sync_time\n");

    tptr.tm_year = year - 1900;
    tptr.tm_mon = month - 1;
    tptr.tm_mday = day;
    tptr.tm_hour = hour;
    tptr.tm_min = min;
    tptr.tm_sec = sec;

    tv.tv_sec = mktime(&tptr);
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
}


int set_sync_time(unsigned int *time_buf,int buf_len)
{
    if(time_buf == NULL)
    {
        DEBUG_ERR("set_sync_time error,time_buf is NULL\n");
        return -1;
    }

    //(int year,int month,int day,int hour,int min,int sec)
    if(buf_len == 24){
        printf("set_sync_time,%d,%d,%d,%d,%d,%d\n",*(time_buf + 0),*(time_buf + 1),
            *(time_buf + 2),*(time_buf + 3),*(time_buf + 4),*(time_buf + 5));
        set_linux_systime(*(time_buf + 0),*(time_buf + 1),*(time_buf + 2),
                        *(time_buf + 3),*(time_buf + 4),*(time_buf + 5));
    }else{
        DEBUG_ERR("set_sync_time buf_len!=24, config err");
    }

    return 0;
}

int hwver_read(unsigned char *str)
{
    dictionary *ini;
    int n = 0,val;
    const char *ret_str;

    ini = iniparser_load("/run/media/mmcblk0p1/vinno/wifi_config.ini");//parser the file
    if(ini == NULL)
    {
        DEBUG_ERR("can not open config_md5.ini");
        return -1;
    }

    printf("dictionary obj:\n");
    iniparser_dump(ini,stderr);//save ini to stderr
    printf("\n%s:\n",iniparser_getsecname(ini,0));//get section name

    ret_str = iniparser_getstring(ini,"CONFIG:ver","null");
    DEBUG_INFO("hwver : %s\n",ret_str);

    memcpy(str,ret_str,strlen(ret_str));
    iniparser_freedict(ini);//free dirctionary obj

    val =  (*(str + 1) <<8) | (*(str+0)) ;  //0x00 06

    return val;
}

int wifi_ap_start(void)
{
    system("hostapd /etc/hostapd.conf -B");
    return 0;
}

int wifi_sta_start(void)
{
    //system("wpa_supplicant -Dnl80211 -c /etc/wpa_supplicant_ap.conf -i wlan0 -B -ddd ");
    return 0;
}

int wifi_stop(void  )
{
    system("killall -9 wpa_supplicant");
    return 0;
}

int region_code_init(void)
{
    return 0;
}



