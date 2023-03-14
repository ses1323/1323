#include <iostream>
#include <sys/socket.h>//1connect
#include <sys/types.h>//2connect
#include <netinet/in.h> //struct
#include <string.h>//memset
#include <stdio.h>//gets
#include <unistd.h>//close
#include <arpa/inet.h>//inet
#define PORT 8111
#define MESSAGE_LEN 1024
using namespace std;
/// 网络数据包包头
struct NetPacketHeader
{
    unsigned short      wDataSize;  ///< 数据包大小，包含封包头和封包数据大小
    unsigned short      wOpcode;    ///< 操作码
};

/// 网络数据包
struct NetPacket
{
    NetPacketHeader     Header;                         ///< 包头
    unsigned char       Data[4];     ///< 数据
};

int main(int argc, char* argv[])
{
	int socket_fd;
	int ret = -1;
	char sendbuf[MESSAGE_LEN] = {0,};
	char recvbuf[MESSAGE_LEN] = {0,};
	struct sockaddr_in serverAddr;
	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (socket_fd < 0)
	{
		cout << "failed to create socket" << endl;
		exit(-1);
	}
	
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = PORT;
	serverAddr.sin_addr.s_addr = inet_addr("192.168.3.10");
	ret = connect(socket_fd, 
				(struct sockaddr *)&serverAddr, 
				sizeof(struct sockaddr));
	if (ret < 0)
	{
		cout << "failed to connect!" << endl;
		exit(-1);
	}
	struct NetPacket a;
	a.Header.wDataSize = 5;
	a.Header.wOpcode =1;
	
	send(socket_fd,&a,sizeof(NetPacket),0);
	while(1)
	{
		memset(sendbuf, 0, MESSAGE_LEN);
		cin.getline(sendbuf,99);
		ret = send(socket_fd, sendbuf, strlen(sendbuf), 0);
		if (ret <= 0)
		{
			cout << "failed to send data!" << endl;
			break;		
		}
		//guanbi 1 kill -9 1111      2 duibi
		if (strcmp(sendbuf, "quit") == 0)
		{
			break;
		}
		ret = recv(socket_fd, recvbuf, MESSAGE_LEN, 0);
		recvbuf[ret] = '\0';
		cout << "recv:" << recvbuf << endl;
	}
	close(socket_fd);
	
	
	return 0;
}


