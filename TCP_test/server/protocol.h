#ifndef _PROTOCOL_H
#define _PROTOCOL_H

/// 网络数据包包头
struct NetPacketHeader
{
    unsigned short      hDataSize;  ///< 数据包大小，包含封包头和封包数据大小
    unsigned short      hCheck;    ///< 校验码

};

/// 网络数据包
struct NetPacket
{
    NetPacketHeader     Header;                         ///< 包头
    unsigned char       Data[1024-sizeof(NetPacketHeader)];     ///< 数据
};

enum 
{
   
};

#endif
