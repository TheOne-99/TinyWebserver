#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include<sys/types.h>
#include<sys/uio.h>    // readv/writev
#include<arpa/inet.h>     // sockaddr_in
#include<stdlib.h>   // atoi()
#include<errno.h>

#include "../log/log.h"
#include "../buffer/buffer.h"
#include "httprequest.h"
#include "httpresponse.h"

/*
进行读写数据并调用httprequest 来解析数据以及httpresponse来生成响应
*/

class HttpConn
{
public:
    HttpConn();
    ~HttpConn();

    void Init(int sockFd , const sockaddr_in& addr);   // 新客户连进来时初始化
    ssize_t read(int* saveErrno);   // 从网卡读数据
    ssize_t write(int* saveErrno);  // 往网卡写数据

    void Close();
    int GetFd() const;
    int GetPort() const;
    const char* GetIP() const;
    sockaddr_in GetAddr() const;

    bool process();    // 处理业务逻辑（解析请求 -> 生成响应）

    int ToWriteBytes() {
        // iov_[0].iov_len 是第一块盘子（HTTP 响应头）剩下的待发送长度
    // iov_[1].iov_len 是第二块盘子（真实的网页文件内容）剩下的待发送长度
    // 它们相加，就是总共还有多少字节没发完。
    // 我们在 write 函数中用 ToWriteBytes() > 10240 来判断是否要暂停发送，就是调用的这个！
        return iov_[0].iov_len + iov_[1].iov_len; 
    }

    bool IsKeepAlive() const {
        return request_.IsKeepAlive();
    }


    static bool isET;     // 是否开启 ET (边缘触发) 模式
    static const char* srcDir;  // 静态资源根目录
    static std::atomic<int> userCount;   // C++11 原子变量：记录当前有多少个连接，自带锁，极度线程安全
    
  


private:
    int fd_;
    struct sockaddr_in addr_;

    bool isClose_;
    
    int iovCnt_;
    struct iovec iov_[2];
    
    Buffer readBuff_; // 读缓冲区
    Buffer writeBuff_; // 写缓冲区

    HttpRequest request_;
    HttpResponse response_;

};


#endif