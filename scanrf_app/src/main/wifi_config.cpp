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
30 = ETSI  ��Radio Type ST60_440
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

// �ú�������Ѱ�����ַ����ڱ����ҵ�Ŀ���ַ����г��ֵĴ�����
int substr_count(char *str, const char *sub_str)
{
    if (str == NULL || sub_str == NULL)
    {
        printf("ERROR��[substr_count] parameter(s) should not be NULL!\n");
        return -1;
    }
    int n = 0;                   // ������ʾ�Ӵ��ڱ����ҵ�Ŀ���ַ����г��ֵĴ���
    char *begin_ptr = str;
    char *end_ptr = NULL;        // ��NULL��ʼ����end_ptr����ָֹ����

    // strstr����������ͷ�ļ�string.h�У��亯��ԭ�����£�
    // char *strstr(const char *str, const char *SubStr)��������������£�
    //  str     --->ָ�򱻲��ҵ�Ŀ���ַ�����������
    //  SubStr  --->ָ��Ҫ���ҵ��ַ��������Ӵ���
    // �ú��������������Ӵ����ڡ�ָ���ַ������е�һ�γ��ֵ�λ�ã�char *���ַ���ָ�룩
    // ���ɹ��ҵ��������ڡ��������е�һ�γ��ֵ�λ�õ�char *ָ��
    // ��δ�ҵ���Ҳ���������������Ӵ������أ���NULL��
    while ((end_ptr = strstr(begin_ptr, sub_str)) != NULL)
    {
        end_ptr += strlen(sub_str);
        begin_ptr = end_ptr;            // ������ʼ��ַ�����µ���strstr��������ƥ�䡣
        ++n;
    }
    return n;
}

//��new_str�滻prefix_str֮����ַ�����ֱ�����н���
int str_replace(const char *file_path, const char *new_str, const char *prefix_str)
{
    char str[100] ={0}, aim[32]={0};
    if (file_path == NULL || new_str == NULL || prefix_str == NULL)
    {
        printf("ERROR: [str_replace] parameter(s) should not be NULL��\n");
        return -1;
    }

    FILE *fp = NULL;
    if ((fp = fopen(file_path, "a+")) == NULL)
    {
        printf("ERROR: file open error!\n");
        return -1;
    }
    // ��fopen������һ������д�����ļ���w+��ʾ�ɶ���д����������ļ��ɹ��������ķ���ֵ
    // �Ǹ��ļ�����������Ϣ������ʼ��ַ����������ָ�����fp��fp�Ѷ���Ϊָ���ļ���ָ��䣩��
    // ������ܳɹ��Ĵ��ļ����򷵻�NULL��

    long file_len;
    fseek(fp, 0, SEEK_END);     // ���ļ�ָ���ƶ����ļ���β���ɹ�����0�����ɹ�����-1
    file_len = ftell(fp);     // �����ǰ�ļ�ָ������ļ���ʼ���ֽ���
    fseek(fp, 0, SEEK_SET);    // �ٶ�λָ�뵽�ļ�ͷ
    // ��C�����в����ļ��Ĵ�С����Ҫʹ�ö�����׼����
    // ��1��fseek: ����ԭ��Ϊ int fseek(FILE *_Stream,long _Offset,int _Origin)
    // ����˵��: _Stream,�ļ���ָ�룻_Offset��ƫ������ _Origin��ԭʼλ�á����� _Origin
    // �Ŀ�ѡֵ��SEEK_SET���ļ���ʼ����SEEK_CUR���ļ�ָ�뵱ǰλ�ã���SEEK_END���ļ���β����
    //  ����˵�������ڶ�����ģʽ�򿪵������µ���λ���� _Origin+_Offset��
    // ��2��ftell:  ����ԭ��Ϊ  long int ftell(FILE * _Stream)
    //  ����˵������������λ�á����ڶ�����������ֵΪ�����ļ���ʼλ�õ��ֽ�����


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
    // ���ٿռ��ori_str
    // malloc���������ڴ�ռ䣬file_len*sizeof(char)��Ϊ�˸��Ͻ���16λ������charռһ���ַ������������Ͽ��ܱ仯
    // ��malloc����������ڴ���û�г�ʼֵ�ģ��������ֵ�ᵼ��д�뵽ʱ���Ҳ���������־���������ڴ��ʵ������ֵ��
    // д�����ݺ��������������
    // memset�������ڴ�ռ䶼��ֵΪ��\0"

    int count = 1;
    int ret = fread(ori_str, file_len*sizeof(char), count, fp);
    // printf("%d\n",ret);
    // C����������fread�������ļ��ж�һ�����ݿ飬��fwrite�������ļ�дһ�����ݿ顣�ڶ�дʱ
    // ���Զ�������ʽ���еġ��������д����ʱ��ֱ�ӽ��ڴ���һ������ԭ�ⲻ��������ת�����Ƶ�
    // �����ļ��ϣ��ڶ���ʱҲ�ǽ������ļ��������ֽڵ�����һ�������ڴ档
    // ���ǵ�һ�������ʽΪ��
    // fread(buffer,size,count,fp);
    // fwrite(buffer,size,count,fp);
    // ���У�buffer��һ����ַ����fread��˵������������Ŵ��ļ���������ݵĴ洢���ĵ�ַ��
    // ��fwrite��˵������Ҫ�Ѵ˵�ַ��ʼ�Ĵ洢���е��������ļ����������ָ������ʼ��ַ��
    // size: Ҫ��д���ֽ�����
    // count: Ҫ��д���ٸ������ÿ���������Ϊsize);
    // fp: FILE����ָ�롣
    // fread��fwrite����������Ϊint�ͣ����fread��fwrite����ִ�гɹ�����������ֵΪ�β�count
    // ��ֵ������������������ĸ�����

    if (ret != count)
    {
        printf("ERROR: read file error!\n");
        fclose(fp);
        free(ori_str);     //�ͷ�malloc���ٵ��ڴ�����
        return -1;
    }

    int n = substr_count(ori_str, prefix_str);
    if (n == -1)
    {
        printf("ERROR: substring count error!\n");
        fclose(fp);
        free(ori_str);     //�ͷ�malloc���ٵ��ڴ�����
        return -1;
    }
    //  �������ַ����ڸ����г��ֵĴ���
    //  ������ִ�����Ҫ�ر��ļ����ͷ��ڴ�ռ�
    //  ��������

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

    // �滻����
    while ((end_ptr = strstr(begin_ptr, prefix_str)) != NULL)  //���ַ���ֻҪƥ���ϣ�ִ��ѭ����
    {
        end_ptr += strlen(prefix_str);
        memcpy(cpy_str, begin_ptr, end_ptr- begin_ptr);
        cpy_str += (end_ptr - begin_ptr);
        memcpy(cpy_str, new_str, strlen(new_str));     //���ַ������濽��new_str
        cpy_str += strlen(new_str);
        end_ptr += strlen(aim);
        begin_ptr = end_ptr;
    }
    fclose(fp);

    strcpy(cpy_str,begin_ptr);

    printf("ori��%s\n", ori_str);
    printf("rst��%s\n", rst_str);

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
        free(ori_str);     //�ͷ�malloc���ٵ��ڴ�����
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
        printf("INFO: string replace failed��\n");
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



