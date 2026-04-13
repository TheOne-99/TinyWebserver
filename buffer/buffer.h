#pragma once
#include<iostream>
#include <cstring>   //perror
#include<sys/uio.h>
#include<unistd.h>
#include<assert.h>
#include<atomic>
#include<vector>

class Buffer
{
public:
    Buffer(int initBuffSize = 1024);
    ~Buffer() = default;

    size_t WritableBytes() const;  //还能写入多少字节
    size_t ReadableBytes() const;   //计算当前有多少有效数据可以读取
    size_t PrependableBytes() const;  //计算头部预留空间

    const char* Peek() const;  //偷瞄一眼数据。返回当前有效数据的起始物理内存地址（即 readPos_ 所在位置），但不移动指针。
    void EnsureWriteable(size_t len);  //确保写缓冲区有足够的空间容纳 len 字节。如果不够，就调用 MakeSpace_ 扩容或整理内存。
    void HasWritten(size_t len); //数据已经被写入到底层内存后，手动调用此函数将写指针向后移动 len。

    void Retrieve(size_t len);   //表示读取了 len 长度的数据，将读指针向后移动 len。
    void RetrieveUntil(const char* end);  //读取数据直到给定的 end 指针位置。通过计算 end 指针和当前读指针的距离来决定移动多少步。

    void RetrieveAll();  //清空整个缓冲区。通过 bzero 清零底层内存（非必须，但安全），然后把读写指针全部复位为 0。
    std::string RetrieveAllToStr();  //把当前所有可读的有效数据转化为 std::string 返回，并清空缓冲区。

    const char* BeginWriteConst() const;  //返回当前可以开始写入数据的物理内存地址（即 writePos_ 所在位置）。
    char* BeginWrite();  

    /*方便各种类型的数据追加。核心逻辑都是：检查空间 -> 拷贝数据 -> 移动写指针。
    函数重载，传入的变量必须不一样.
    这些函数用于向缓冲区添加数据，核心动作是向后移动写指针 writePos_。
    */
    void Append(const std::string& str);
    void Append(const char* str , size_t len);
    void Append(const void* data , size_t len);
    void Append(const Buffer* buff);

    ssize_t ReadFd(int Fd , int* Errno);  //从文件描述符（通常是 Socket）中读取数据到 Buffer 中。
    ssize_t WriteFd(int Fd , int* Errno);  //将 Buffer 中可读的数据写入到文件描述符（Socket）中。写成功后，移动读指针。

private:
    char* BeginPtr_();    //Buffer开头
    const char* BeginPtr_() const;
    void MakeSpace_(size_t len);

    std::vector<char> buffer_;   //缓冲区
    std::atomic<std::size_t> readPos_;   //读的下标
    std::atomic<std::size_t> writePos_;   //写的下标

};