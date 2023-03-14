//#include <typeinfo>  typeid().name()
#include <iostream>
#include <fstream> //文件
#include <sys/socket.h>
#include <netinet/in.h>//struct
#include <netinet/tcp.h>//TCP_KEEP
#include <unistd.h>//close head sleep
#include <string.h>//bzero
#include <arpa/inet.h>//inet_ntoa
#include <thread>  //thread
#include <pthread.h>
#include "protocol.h"

using namespace std; 

//#define MESSAGE_SIZE 1024

unsigned short Check(const unsigned char *buf, int len);
bool server_recv(int conn_fd); //接收数据函数
int thread_state(int socket_state_fd);
string trim(string& str);

int main(int argc, char* argv[])
{
	int socket_fd;
	int backlog = 3; //可排队的最大监听数
	int ret = -1;
	int flag = 1;
	struct sockaddr_in local_addr, remote_addr;
	
	if(argc != 2){
		cout << "please enter port like: .\\server 8111 \n" << endl;
		return -1;
	}
	
	//create socket
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd == -1){
		cout << "file to create socket!" << endl;
		exit(-1);
	}
	//set socket options  设置套接字描述符的属性
	ret = setsockopt(socket_fd, 
					SOL_SOCKET, //
					SO_REUSEADDR, // close后会出现 端口会TIME_WAIT 
					&flag,   		
					sizeof(flag));
	if (ret == -1){
		cout << "failed to set socket SO_REUSEADDR options!" << endl;
	}

	//set localaddr
	local_addr.sin_family = AF_INET;
	local_addr.sin_port = htons(stoi(argv[1]));
	local_addr.sin_addr.s_addr = INADDR_ANY;//0 RENHE IP DOU LISTEN
	//printf("%#x\n",htons(0x1234));   //输出为0x3412 为小端
	bzero(&(local_addr.sin_zero),8); //清零
	//bind socket
	ret = bind(socket_fd, (struct sockaddr *)&local_addr, sizeof(struct sockaddr));
	if (ret == -1){
		cout << "failed to bind addr!" << endl;
		exit(-1);
	}
	//listen
	ret = listen(socket_fd, backlog);
	if (ret == -1){
		cout << "failed to listen socket!" << endl;
		exit(-1);
	}
	
	//提取出所监听套接字的等待连接队列中第一个连接请求，
	//创建一个新的套接字，并返回指向该套接字的文件描述符
	int accept_fd;
	socklen_t addr_len = sizeof(struct sockaddr);
	accept_fd  = accept(socket_fd, 
						(struct sockaddr *) &remote_addr, 
						&addr_len);
	cout << "connect success\n" 
	<<"addr:" <<inet_ntoa(remote_addr.sin_addr) 
	<< "\n"<< endl;

	close(socket_fd); //不在监听

	// 开启探活
	int keepalive = 1; // 开启keepalive属性
	int keepidle = 2; // 如该连接在10秒内没有任何数据往来,则进行探测
	int keepinterval = 1; // 探测时发包的时间间隔为3秒
	int keepcount = 3; // 探测尝试的次数.如果第1次探测包就收到响应了,则后2次的不再发.
	
	setsockopt(accept_fd, SOL_SOCKET,
			   SO_KEEPALIVE, (void *)&keepalive,
			   sizeof(keepalive));
	setsockopt(accept_fd, SOL_TCP, 
			   TCP_KEEPIDLE, (void *)&keepidle,
	 		   sizeof(keepidle));
	setsockopt(accept_fd, SOL_TCP, 
			   TCP_KEEPINTVL, (void *)&keepinterval,
	 		   sizeof(keepinterval));
	setsockopt(accept_fd, SOL_TCP, 
			   TCP_KEEPCNT, (void *)&keepcount, 
			   sizeof(keepcount));
	
	/*struct timeval timeout = {4,0}; 
	//设置发送超时
	setsockopt(accept_fd,SOL_SOCKET,SO_SNDTIMEO,
			   (char *)&timeout,sizeof(struct timeval));
	//设置接收超时
	setsockopt(accept_fd,SOL_SOCKET,SO_RCVTIMEO,
			   (char *)&timeout,sizeof(struct timeval));*/
	//thread threadObj(thread_state, accept_fd);  //创建一个线程
	unsigned char recv_buffer[1024];  //接收数据的buffer
	while (true){

		 ret=server_recv(accept_fd);
		 if (ret == false){
		  	break;
		}
/*		recv(accept_fd,recv_buffer,
			 1024,0);
		cout<< "recv="<<recv_buffer<<endl; */
		//	cout<< "ret="<<ret<<endl; 
	}

	cout<<"close server"<<endl;
	close(accept_fd); 
	//threadObj.join(); //等待线程结束
	return 0;
}

bool server_recv(int conn_fd)  //接收数据函数
{
    int nrecvsize=0; //一次接收到的数据大小
    int sum_recvsize=0; //总共收到的数据大小
    int packersize;   //数据包总大小
    int datasize;     //数据总大小
    unsigned char recv_buffer[1024];  //接收数据的buffer

    memset(recv_buffer,0,sizeof(recv_buffer));  //初始化接收buffer
	unsigned int error_count = 0; //计数
    while(sum_recvsize!=sizeof(NetPacketHeader))
    {
        nrecvsize=recv(conn_fd,recv_buffer+sum_recvsize,
					   sizeof(NetPacketHeader)-sum_recvsize,MSG_WAITALL);
        if(nrecvsize==0){
            cout<<"error--recv Header"<<endl;
			sleep(5);
			error_count++;
			if (error_count>=3){
				cout<<"error--recv HeaderX3 close server"<<endl;
				return false;
			}           	
        }else if(nrecvsize==-1){
			cout<<"send timeout\n"<<endl;
			//return false;
		}
        sum_recvsize+=nrecvsize;
    }
	
   // NetPacketHeader *phead=(NetPacketHeader*)recv_buffer;
	NetPacket *rec = (NetPacket*)recv_buffer;
    packersize=rec->Header.hDataSize;  //数据包大小
    datasize=packersize-sizeof(NetPacketHeader);     //数据总大小
	error_count = 0;
    while(sum_recvsize!=packersize)
    {	
		//cout << "recv Date" <<endl;
        nrecvsize=recv(conn_fd,recv_buffer+sum_recvsize,
					   packersize-sum_recvsize,MSG_WAITALL);
        if(nrecvsize==0)
        {
            cout << "error--recv Data" << endl;
			sleep(5);
			error_count++;
			if (error_count>=3){
				cout<<"error--recv DataX3"<<endl;
				return false;
			}  
            //return false;
        }
        sum_recvsize+=nrecvsize;
    }

	unsigned short checkSum = Check(rec->Data,datasize);  //获取校验和
	//校验数据是否正确
	if(checkSum==rec->Header.hCheck){
		cout<<"\nget :"<< rec->Data<<endl;
	} else {
		cout<<"\nCheck(rec->Data,datasize):" <<Check(rec->Data,datasize)
		<<"\nrec->Header.hCheck):" <<rec->Header.hCheck
		<<endl;
		cout<<"\nerror--Check"<<endl;
		return false;
	}
    

	string Data;       //转换为字符串
	for(int i=0;i<strlen((char *)rec->Data);i++)
	{
		Data+=rec->Data[i];	 
	}
	//if(!strcmp((char *)rec->Data,"ooo"))  //字符串比对

	char send_buf[25]; //发送数据
	memset(send_buf,0,sizeof(send_buf));
	fstream file; //实例化文件的类
	if (Data == "A"){
		send(conn_fd, "do A", 4,0);
	} else if (Data == "check"){
		memcpy(send_buf,"check:",6);
		sprintf(send_buf+6, "%d",rec->Header.hCheck);
		//itoa(rec->Header.hCheck, string, 10); 
		send(conn_fd, send_buf, strlen(send_buf),0);
	} else if (Data == "socket"){
		memcpy(send_buf,"socket:",7);
		sprintf(send_buf+7, "%d",conn_fd); //转换
		send(conn_fd,send_buf, strlen(send_buf),0);
	} else if (Data == "close"){
		memcpy(send_buf,"ok close",8);
		send(conn_fd,send_buf, strlen(send_buf),0);
		cout << "ok close" << endl;
		return false;
	} else if (Data == "ssid"){
		file.open("wpa_supplicant_ap.conf",ios::in);
		if (!file.is_open()){  
			memcpy(send_buf,"Error opening file",18);
			cout << "Error opening file" << endl;
			send(conn_fd,send_buf, strlen(send_buf),0);
		}
		string file_get;
		while(1 ){
			getline(file,file_get);
			bool find = file_get.find("ssid=") == string::npos; // 字符串对比 查找到返回0
			if(!find){
				break;
			}

				
		}
		file.close();
		//memcpy(send_buf,"ssid:",5);
		send(conn_fd,trim(file_get).c_str(), file_get.length(),0);
	} else{
		send(conn_fd, rec->Data, strlen((char *)&rec->Data),0);
	}
	return true;
}

//校验和  返回2个字节
unsigned short Check(const unsigned char *buf, int len)  
{  
    int iSum = 0; 
    for (int i = 0;i < len;i++){  
        iSum += buf[i];  
    }  
    iSum %= 0x10000;   //也可以&0xffff
    return (unsigned short)iSum;  
} 

//去除空格
string trim(string& str)
{
    str.erase(0, str.find_first_not_of(" \t")); // 去掉头部空格
	str.erase(str.find_last_not_of(" \t") + 1); // 去掉尾部空格
    return str;
}

//state 线程
int thread_state(int socket_state_fd)
{
	struct tcp_info info;
    int len = sizeof(info);
	while(true){
		getsockopt(socket_state_fd, IPPROTO_TCP, 
			   TCP_INFO, &info, (socklen_t *)&len);
		if(info.tcpi_state == TCP_ESTABLISHED){
			//printf("state: %d \n ",info.tcpi_state);
		//	cout<< info.tcpi_state+0 << "\n" <<endl;
		} else{
			printf("disconnect state: %d\n",info.tcpi_state);
			return 1;
			//close(socket_state_fd);
			//exit(-1);  //直接结束进程
		}
		usleep(500000); //500000us 500ms 
	}
	
}
