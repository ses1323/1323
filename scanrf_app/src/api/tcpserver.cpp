#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include "scan_rf.h"

#define MAXLINE 	1344
#define PORT	 	6666
extern file_t scan_tb, rfdata_file;
int soc_server_fd = 0;
static int handle_msg(int connfd,void *buf)
{
    int cmd_id = 0;
    unsigned int cur_idx =0;
    static int cnt = 0;
    char *p= (char *)buf;
    cmd_id = *(p+0);
    switch(cmd_id)
    {
        case SCAN_TABLE_UPDATE: //0x1
            printf("SCAN_TABLE_UPDATE  \n");
            break;

        case INDEX_TABLE_UPDATE://0x2:
            printf("INDEX_TABLE_UPDATE\n");
            break;

        case FPGA_CMD_WRITE: //0x10
            printf("Start or stop fpga \n");
            break;

        case 0x21: // Restart
            printf("@@Restart ,cur_idx  =%d\n\n\n\n",cur_idx);
            cur_idx =0;

            for(cnt=0;1460*cnt <rfdata_file.len;cnt++ ){
                send (connfd, rfdata_file.pbuf+1460*cnt, 1460, 0);
                usleep(2000);
            }
            if(rfdata_file.len - 1460*cnt>0 )
                send(connfd, rfdata_file.pbuf+1460*cnt, rfdata_file.len - 1460*cnt, 0);
            break;

        default:
            printf("recv msg from client:%x ,%x,%x,%x,%x,%x\n",*(p+0),*(p+1),*(p+2),*(p+3),*(p+4),*(p+5));
            printf("No this cmd \n");
            return -EINVAL;
    }
    
    if(rfdata_file.pbuf != NULL){
        free(rfdata_file.pbuf);
        printf("Free  rfdata_file.pbuf)\n");
    }
    
    return 0;
}

int sum_len =0;
void *tcp_server_thread(void *fd)
{
    char buff[MAXLINE];
    int listenfd = *(int *)fd;
    int connfd,n;
    printf("====waiting for client's request=======\n");
    if((connfd = accept(listenfd, (struct sockaddr *)NULL, NULL))  == -1) {
        printf(" accpt socket error: %s (errno :%d)\n",strerror(errno),errno);
        return 0;
    }

    while(1)
    {
        n = recv(connfd,buff,MAXLINE,0);
        sum_len+=n;
        //buff[n] = '\0';
        if(n%4 != 0)
            printf(" sum_len =%d,n=%d\n",sum_len,n);
        //handle_msg(connfd,buff);

        usleep(1);
        if(n == 0)
            break;
    }
    #if 0
    if(rfdata_file.pbuf != NULL){
        free(rfdata_file.pbuf);
        printf("Free  rfdata_file.pbuf)\n");
    }
    #endif
    close(connfd);

}

int tcp_server_exit(int *fd)
{
    int listenfd = *fd;
    close(listenfd);
}

int tcp_server_init(void)
{
    int listenfd,connfd;
    struct sockaddr_in servaddr;
    char buff[MAXLINE];
    int n,sum= 0;
    printf("@@@tcp_server_init\n");
    //创建一个TCP的socket
    if( (listenfd = socket(AF_INET,SOCK_STREAM,0)) == -1) {
        printf(" create socket error: %s (errno :%d)\n",strerror(errno),errno);
        return 0;
    }
    //fd = &listenfd;
    
    //先把地址清空，检测任意IP
    memset(&servaddr,0,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    //地址绑定到listenfd
    if ( bind(listenfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) == -1) {
        printf(" bind socket error: %s (errno :%d)\n",strerror(errno),errno);
        return 0;
    }

    //监听listenfd
    if( listen(listenfd,10) == -1) {
        printf(" listen socket error: %s (errno :%d)\n",strerror(errno),errno);
        return 0;
    }

    int mw_optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (char *)&mw_optval, sizeof(mw_optval) );
    //soc_server_fd = listenfd;


    //48KB ,15fps
    #if 1
    printf("====waiting for client's request=======\n");
    //accept 和recv,注意接收字符串添加结束符'\0'
    if( (connfd = accept(listenfd, (struct sockaddr *)NULL, NULL))  == -1) {
        printf(" accpt socket error: %s (errno :%d)\n",strerror(errno),errno);
        return 0;
    }
    

    while(1)
    {
        
        n = recv(connfd,buff,MAXLINE,0);
        buff[n] = '\0';
        sum += n;
        //printf("recv msg from client cnt:%d\n",n);
        if(n <= 0)
            break;
        
    }
    printf("recv msg from client sum cnt:%d\n",sum);

    close(connfd);
    #endif

    return listenfd;
}
