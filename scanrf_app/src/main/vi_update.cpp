#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include<sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/tcp.h>
#include "scan_rf.h"
#include "iniparser.h"

#define IMGAE_NAME "image"
#define APP_NAME "vinno"

int run_tftp_update(char *ip,const char *filename,int fd)
{
    pid_t pid;
    char cmd[100];
    pid=fork();
    int len;
    char buf[10] ={0};

    //>0 返回父进程
    if(pid>0)  //父进程
    {
        wait(NULL);
        if(strstr(filename,IMGAE_NAME) != NULL){
            printf("Move image.ub to /run... \n");
            sprintf(cmd,"mv %s /run/media/mmcblk0p1/image.ub",filename);
            printf("cmd = %s\n",cmd);
            system(cmd);
            system("/run/media/mmcblk0p1/vinno/md5_up.sh");
            memcpy(buf,"KER_DONE",9);
            if(fd != 0){
                if (send(fd, (const char *)buf, sizeof(buf), 0) == 0 )
                {
                    DEBUG_ERR(" FATAL ERROR:send msg error \n");
                    return -1;
                }
            }

        } else if (strstr(filename,APP_NAME) != NULL){

            printf("tar vinno.tar.gz to /run/media/mmcblk0p1/ \n");
            system("mkdir /run/media/mmcblk0p1/conf");
            system("cp /run/media/mmcblk0p1/vinno/ttcode.ini /run/media/mmcblk0p1/conf");
            system("cp /run/media/mmcblk0p1/vinno/wifi_config.ini /run/media/mmcblk0p1/conf");
            system("mkdir /run/media/mmcblk0p1/vinno-cp");
            sprintf(cmd,"tar xzvf %s -C /run/media/mmcblk0p1/vinno-cp",filename);
            system(cmd);
            system("rm /run/media/mmcblk0p1/vinno/* -rf");
            //sprintf(cmd,"rm %s",filename);
            system("mv /run/media/mmcblk0p1/vinno-cp/vinno/* /run/media/mmcblk0p1/vinno");
            system("mv /run/media/mmcblk0p1/conf/* /run/media/mmcblk0p1/vinno");
            system("rm /run/media/mmcblk0p1/vinno-cp -rf");
            system("rm /run/media/mmcblk0p1/conf -rf");
            system("/run/media/mmcblk0p1/vinno/md5_up.sh");
            memcpy(buf,"APP_DONE",9);
            if(fd != 0){
                if (send(fd, (const char *)buf, sizeof(buf), 0) == 0 )
                {
                    DEBUG_ERR(" FATAL ERROR:send msg error \n");
                    return -1;
                }
            }
        }
        system("sync");

        printf("父进程: pid= %d , ppid=%d,子进程: %d \n", getpid(),getppid(),pid);
        sleep(1); //这里延迟父进程程序，等子进程先执行完。
    }
    else if(pid==0)  //子进程
    {
        printf("子进程: pid= %d , ppid=%d \n", getpid(),getppid());
        if(execl("/usr/bin/tftp","tftp","-gl",filename,ip,NULL) == -1){
            printf("error in execl\n");
            return -1;
        }

    } else if (pid==-1){
        perror("fork失败!");
        return -1;
    }

    return 0;

}

// FTP update
int run_ftp_update(char *ip, const char *filename, int fd)
{
    char cmd[100];
    char buf[10] = {0};

    if (strstr(filename, IMGAE_NAME) != NULL)
    {
        printf("Move image.ub to /run... \n");
        // sprintf(cmd, "mv %s /run/media/mmcblk0p1/image.ub", filename);
        // printf("cmd = %s\n", cmd);
        // system(cmd);
        system("mv /run/media/mmcblk0p1/image-st60.ub /run/media/mmcblk0p1/image.ub");
        system("/run/media/mmcblk0p1/vinno/md5_up.sh");
        memcpy(buf, "KER_DONE", 9);
        system("sync");
        if (fd != 0)
        {
            if (send(fd, (const char *)buf, strlen(buf), 0) == 0)
            {
                DEBUG_ERR(" FATAL ERROR:send msg error \n");
                return -1;
            }
        }
    }
    else if (strstr(filename, APP_NAME) != NULL)
    {
        printf("tar vinno-st60.tar.gz to /run/media/mmcblk0p1/ \n");
        system("mkdir /run/media/mmcblk0p1/conf");
        system("cp /run/media/mmcblk0p1/vinno/ttcode.ini /run/media/mmcblk0p1/conf");
        system("cp /run/media/mmcblk0p1/vinno/wifi_config.ini /run/media/mmcblk0p1/conf");
        system("mkdir /run/media/mmcblk0p1/vinno-cp");
        // sprintf(cmd, "tar xzvf %s -C /run/media/mmcblk0p1/vinno-cp", filename);
        // system(cmd);
        system("tar -xzvf /run/media/mmcblk0p1/vinno-st60.tar.gz -C /run/media/mmcblk0p1/vinno-cp");
        system("rm /run/media/mmcblk0p1/vinno/* -rf");
        // sprintf(cmd,"rm %s",filename);
        system("mv /run/media/mmcblk0p1/vinno-cp/vinno/* /run/media/mmcblk0p1/vinno");
        system("mv /run/media/mmcblk0p1/conf/* /run/media/mmcblk0p1/vinno");
        system("mv /run/media/mmcblk0p1/vinno-st60.tar.gz /run/media/mmcblk0p1/vinno.tar.gz");
        system("rm /run/media/mmcblk0p1/vinno-cp -rf");
        system("rm /run/media/mmcblk0p1/conf -rf");
        system("/run/media/mmcblk0p1/vinno/md5_up.sh");
        memcpy(buf, "APP_DONE", 9);
        system("sync");
        if (fd != 0)
        {
            if (send(fd, (const char *)buf, strlen(buf), 0) == 0)
            {
                DEBUG_ERR(" FATAL ERROR:send msg error \n");
                return -1;
            }
        }
    }
    system("sync");
    printf("update done \n");
    sleep(1); //这里延迟父进程程序，等子进程先执行完。
    return 0;
}
int vi_update(int soc_fd,int argc, char **argv)
{
    printf("argc = %d,argv[1]=%s\n",argc,argv[1]);
    if(argc < 3){

        printf("Args is too short\n");
        return -1;
    }
#ifdef TFTP
    if(strcmp(argv[1],"1") == 0){
        printf("update kernel\n");
        run_tftp_update(argv[2], "image-st60.ub",soc_fd);
    }else if(strcmp(argv[1],"2") == 0) {
        printf("update app vinno.tar.gz\n");
        run_tftp_update(argv[2], "vinno.tar.gz",soc_fd);
    }else{
        printf("update cmd  failed\n");
    }
#else
    if(strcmp(argv[1],"1") == 0){
        printf("update kernel\n");
        run_ftp_update(argv[2], "image-st60.ub",soc_fd);
    }else if(strcmp(argv[1],"2") == 0) {
        printf("update app vinno.tar.gz\n");
        run_ftp_update(argv[2], "vinno-st60.tar.gz",soc_fd);
    }else{
        printf("update cmd  failed\n");
    }
#endif
    printf("update end\n");

    return 0;
}

int read_md5(const char *cmd,char *str)
{
    dictionary *ini;
    int n = 0;
    const char *ret_str;
    //char *test1 = "md5:image_md5";
    ini = iniparser_load("/run/media/mmcblk0p1/vinno/config_md5.ini");//parser the file
    if(ini == NULL)
    {
        DEBUG_ERR("can not open config_md5.ini");
        return -1;
    }

    printf("dictionary obj:\n");
    iniparser_dump(ini,stderr);//save ini to stderr
    printf("\n%s:\n",iniparser_getsecname(ini,0));//get section name

    if(strcmp(cmd,"1") == 0) {
        ret_str = iniparser_getstring(ini,"md5:image_md5","null");
        DEBUG_INFO("image_md5 : %s\n",ret_str);
    } else if(strcmp(cmd,"2") == 0) {
        ret_str = iniparser_getstring(ini,"md5:app_md5","null");
        DEBUG_INFO("app_md5: %s\n",ret_str);
    } else {
        DEBUG_ERR("read_md5 can not support this cmd=%s\n",cmd);
        iniparser_freedict(ini);
        return -1;
    }

    memcpy(str,ret_str,strlen(ret_str));
    iniparser_freedict(ini);//free dirctionary obj

    return 0;

}

