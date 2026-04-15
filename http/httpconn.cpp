#include"httpconn.h"

const char* HttpConn :: srcDir;
std::atomic<int> HttpConn::userCount;
bool HttpConn::isET;

HttpConn::HttpConn()
{
    fd_ = -1;  // 文件描述符初始化为 -1（在 Linux 中，-1 代表无效的句柄）
    addr_ = { 0 };   // 将存放客户端 IP 和端口的结构体清零
    isClose_ = true;   // 刚创建出来时，默认连接是关闭状态的
}

HttpConn::~HttpConn()
{
    Close();
}

void HttpConn::Init(int fd , const sockaddr_in& addr)
{
    assert(fd > 0);  // 防御性编程：确保传进来的 socket 文件描述符是有效的（大于0）

    userCount++;   // 原子操作：全局在线用户数 + 1
    addr_ = addr;   // 保存这个客户的 IP 地址结构体
    fd_ = fd;   // 保存这个客户的专属 Socket 句柄
 
    // 【关键细节】：清空读写缓冲区！
    // 因为 HttpConn 对象在服务器里是被“复用”的（放在连接池或者数组里）。
    // 上一个客户用完断开后，缓冲区可能还有残余垃圾数据，必须彻底清空，迎接新客户！

    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();

    isClose_ = false;  // 标记当前连接状态为：活跃/开启！
    iovCnt_ = 0;
    iov_[0].iov_base = nullptr;
    iov_[0].iov_len = 0;
    iov_[1].iov_base = nullptr;
    iov_[1].iov_len = 0;
    // 打印一条日志。这里调用了我们下面要讲的 GetIP() 和 GetPort()
    LOG_INFO("Client[%d](%s:%d) in , userCount:%d" , fd_ , GetIP() , GetPort() ,(int)userCount);
}

void HttpConn::Close()
{
    // 1. 释放 HttpResponse 中的内存映射（mmap）！
    // 这是极其重要的一步。如果客户断开了，但我们映射到内存里的文件没释放，会导致严重的内存泄漏！
    response_.UnmapFile(); 
    
    // 2. 检查一下是不是已经被关过了（防止重复关闭导致程序崩溃）
    if(isClose_ == false)
    {
        isClose_ = true;  // 马上标记为已关闭
        userCount--;   // 原子操作：全局在线用户数 - 1

        close(fd_);   // 【Linux 系统调用】：真正地关闭底层的 TCP Socket 句柄，切断网络通道！
        // 打印客户离开的日志
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", fd_, GetIP(), GetPort(), (int)userCount);
    }
}

int HttpConn::GetFd() const
{
    return fd_;
}

struct sockaddr_in HttpConn :: GetAddr() const{
    return addr_;
}

// 【重点】：获取易读的 IP 地址字符串
const char* HttpConn::GetIP() const{
    // inet_ntoa 是 Linux 网络库函数 (在 <arpa/inet.h> 中)。
    // addr_.sin_addr 里面存的 IP 是一串机器才懂的 32 位二进制数字（网络字节序）。
    // inet_ntoa 的作用是把这串二进制，翻译成我们人类能看懂的点分十进制字符串！
    // 比如：把 0x0100007F 翻译成 "127.0.0.1"
    return inet_ntoa(addr_.sin_addr);
}

int HttpConn::GetPort() const
{
    return addr_.sin_port;
}

bool HttpConn :: process()
{
    request_.Init();
    // 如果读缓冲区没数据，没法处理
    if(readBuff_.ReadableBytes() <= 0)
    {
        return false;
    }

    // 1. 让 request_ 去解析读缓冲区里的数据
    if(request_.parse(readBuff_))
    {
        LOG_DEBUG("%s" , request_.path().c_str());
       // 2A. 解析成功！拿着路径去让 response_ 生成 200 OK 的响应报文
        response_.Init(srcDir , request_.path() , request_.IsKeepAlive() , 200);
    }else{
        // 2B. 瞎传了乱七八糟的报文，解析失败，生成 400 错误响应
        response_.Init(srcDir , request_.path() , false , 400);
    }
    // 3. 让 response_ 把拼装好的响应头（如 HTTP/1.1 200 OK...）写进 writeBuff_
    response_.MakeResponse(writeBuff_);

    // 🌟 4. 准备分散写 (Scatter-Gather I/O) 🌟
    // 第一块盘子 iov_[0]：装响应头！指向 writeBuff_ 里的数据。
    iov_[0].iov_base = const_cast<char*>(writeBuff_.Peek());
    iov_[0].iov_len = writeBuff_.ReadableBytes();
    iovCnt_ = 1;

    // 第二块盘子 iov_[1]：装真实的网页/图片数据！指向刚才 mmap 映射出来的内存 mmFile_！
    if (response_.FileLen() > 0 && response_.File()) {
        iov_[1].iov_base = response_.File();
        iov_[1].iov_len = response_.FileLen();
        iovCnt_ = 2; // 现在有两块盘子了
    }
    return true; // 准备就绪，可以触发系统的 write 动作了
}

ssize_t HttpConn::read(int* saveErrno) {
    ssize_t len = -1;
    do {
        // 利用第一天写的 ReadFd (底层是 readv) 把网卡的数据抽到 readBuff_ 中
        len = readBuff_.ReadFd(fd_, saveErrno);
        if (len <= 0) {
            break;
        }
    // isET (Edge Trigger, 边缘触发模式)：如果开了 ET，底层网卡缓冲区有数据时只会通知你一次！
    // 就算没读完，下次也不会再通知了。所以必须用 do-while 循环，把网卡里的数据一次性榨干！
    } while (isET); 
    return len;
}

ssize_t HttpConn::write(int* saveErrno) {
    ssize_t len = -1;
    do {
        // 【大杀器 writev】：把 iov_ 数组里的两块盘子（头和身体）连续写到 Socket 发送缓冲区里。
        //问题3：writev函数的作用，用法是什么？？
        len = writev(fd_, iov_, iovCnt_);   
        
        if (len <= 0) {
            *saveErrno = errno; // 写报错了，可能缓冲区满了 (EAGAIN)
            break;
        }
        
        // 场景 A：两块盘子全发空了，传输彻底结束！
        if (iov_[0].iov_len + iov_[1].iov_len == 0) { break; } 
        
        // 场景 B：发出去的数据量 len，大于第一块盘子(响应头)的长度
        // 说明响应头已经全发出去了，并且第二块盘子(响应体文件)也发出去了一部分！
        //问题4：static_cast的作用是什么？
        else if (static_cast<size_t>(len) > iov_[0].iov_len) {
            // (len - iov_[0].iov_len) 就是文件发出去的字节数
            // 把第二块盘子的指针往后推这么多字节！
            // 为什么强转 (uint8_t*)？因为 void* 不能进行加减运算，必须转成单字节指针才能精确移动。
            iov_[1].iov_base = (uint8_t*)iov_[1].iov_base + (len - iov_[0].iov_len);
            // 第二块盘子剩余待发的长度相应减少
            iov_[1].iov_len -= (len - iov_[0].iov_len);
            
            // 第一块盘子已经全发完了，重置它。清空 writeBuff_。
            if (iov_[0].iov_len) {
                writeBuff_.RetrieveAll();
                iov_[0].iov_len = 0;
            }
        }
        // 场景 C：发出去的数据量 len，连第一块盘子(响应头)都没发完！网络太拥堵了！
        else {
            // 把第一块盘子的指针往后推 len 个字节，下次接着发头部剩下的
            iov_[0].iov_base = (uint8_t*)iov_[0].iov_base + len;
            iov_[0].iov_len -= len;
            writeBuff_.Retrieve(len); // Buffer 读游标后移
        }      
    // 循环条件：如果开启了 ET 模式，或者待发送的数据超过了 10KB (为了防饿死其他连接，单次最大写点限制，然后让出循环)，就继续尝试写
    } while (isET || ToWriteBytes() > 10240);
    return len;
}