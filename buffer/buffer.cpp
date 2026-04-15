#include"buffer.h"

//读写下标初始化，vector<char>初始化
Buffer::Buffer(int initBuffSize)
    :buffer_(initBuffSize), readPos_(0) , writePos_(0)
    {}

//可写的数量：buffer大小 - 写下标
size_t Buffer::WritableBytes() const
{
    return buffer_.size() - writePos_;
}

//可读的数量：写小标 - 读下标
size_t Buffer::ReadableBytes() const
{
    return writePos_ - readPos_;
}

//可预留空间：已经读过的就没用了，等于读下标
size_t Buffer::PrependableBytes() const
{
    return readPos_;
}

//偷瞄一眼数据。返回当前有效数据的起始物理内存地址（即 readPos_ 所在位置），但不移动指针。
const char* Buffer:: Peek() const {
    return &buffer_[readPos_];
}

//确保可写的长度
void Buffer::EnsureWriteable(size_t len)
{
    if(len > WritableBytes())
    {
        MakeSpace_(len);
    }
    assert(len <= WritableBytes());
}

// 移动写下标，在Append中使用
void Buffer::HasWritten(size_t len)
{
    writePos_ += len;
}

//读取len长度，移动读下标
void Buffer::Retrieve(size_t len)
{
    readPos_ += len;
}

//读取到end位置
void Buffer::RetrieveUntil(const char* end)
{
    assert(Peek() <= end);  //断言目标终点指针 end 必须在当前读指针的后面（或重合），防止指针乱指导致越界。
    //指针减法。两个 char* 指针相减，得到的是它们之间相隔的字节数（即要读取的数据长度）。然后传给 Retrieve 将 readPos_ 往后推。
    Retrieve(end - Peek());
}

//取出所有数据，buffer归零，读写下标归零
void Buffer::RetrieveAll()
{
    bzero(&buffer_[0],buffer_.size());  //覆盖原本数据
    readPos_ = writePos_ = 0;
}

//取出剩余可读的str
std::string Buffer::RetrieveAllToStr()
{
    std::string str(Peek() , ReadableBytes());
    RetrieveAll();
    return str;
}

//写指针的位置
const char* Buffer::BeginWriteConst() const
{
    return &buffer_[writePos_];
}

char* Buffer::BeginWrite()
{
    return &buffer_[writePos_];
}

//添加str到缓存区
void Buffer::Append(const char* str , size_t len)
{
    assert(str);  //断言传入的字符串指针不是空指针（NULL / nullptr）。
    EnsureWriteable(len); //确保可写的长度
    std::copy(str , str + len , BeginWrite());  //将str放到写下标开始的地方
    HasWritten(len);  //移动写下标
}

void Buffer::Append(const std::string& str)
{
    Append(str.c_str() , str.size());
}

void Append(const void* data , size_t len)
{
    Append(static_cast<const char*>(data) , len);
}

// 将buffer中的读下标的地方放到该buffer中的写下标位置
void Append(const Buffer& buff) {
    Append(buff.Peek(), buff.ReadableBytes());
}

//将fd的内容读到缓冲区，即writeable的位置
ssize_t Buffer:: ReadFd(int fd , int* Errno)
{
    char buff[65535];   //在栈区开辟 64KB 的临时数组
    /*iovec 是 Linux 系统中专门用于**分散读/聚集写（Scatter/Gather I/O）**的结构体。
    它只包含两个字段：iov_base（内存首地址）和 iov_len（这段内存的长度）。*/
    struct iovec iov[2]; 
    size_t writeable = WritableBytes();  //先记录能写多少
    
    //分散读，保证数据全部读完
    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writeable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);

    /*readv 是 Linux 系统调用。它会从文件描述符 fd（通常是网络 Socket）读取数据。
    它会优先把数据往 iov[0] 里填；
    如果 iov[0] 填满了数据还没读完，它会自动继续往 iov[1] 里填。
    返回值 len：实际读到的总字节数。如果返回 -1，说明出错了。*/
    size_t len = readv(fd ,iov ,2);

    if(len < 0)
    {
        *Errno = errno;
    }else if(static_cast<size_t>(len) <= writeable)  //若len小于writeable，说明写区可以容纳len
    {
        writePos_ += len;  //直接移动写下标
    }else
    {
        writePos_ = buffer_.size(); // 写区满了，下标移到最后
        Append(buff , static_cast<size_t>(len-writeable)); //剩余的长度
        /*先把 Buffer 的写游标移到末尾（满了）。
        然后调用 Append，把栈区 buff 里溢出的那部分数据 len - writeable 追加进来。
        由于 Buffer 内部空间不够，这个 Append 操作会自动触发后面的 MakeSpace_ 进行扩容。*/
    }
    return len;
}

//将buffer中可读的区域写入fd
ssize_t Buffer::WriteFd(int fd , int* Errno)
{
    ssize_t len = write(fd , Peek() , ReadableBytes());
    if(len < 0)
    {
        *Errno = errno;
        return len;
    }
    Retrieve(len);
    return len;
}

char* Buffer::BeginPtr_()
{
    return &buffer_[0];
}

const char* Buffer::BeginPtr_() const
{
    return &buffer_[0];
}

//扩展空间
void Buffer::MakeSpace_(size_t len)
{
    if(WritableBytes() + PrependableBytes() < len)
    {
        buffer_.resize(writePos_ + len + 1);
    }else //进入 else 说明：空间是够的，只是碎片化了（尾部不够，但加上头部已经被读过作废的空间就够了）。
    {
        size_t readable = ReadableBytes();

        std::copy(BeginPtr_() + readPos_ , BeginPtr_() + writePos_ , BeginPtr_());
        readPos_ = 0;
        writePos_ = readable;
        assert(readable == ReadableBytes());
        /*这一行极其关键。它把当前 待读取的有效数据 [readPos_, writePos_) 这一段，整体平移拷贝到 vector 的最开头 BeginPtr_()。
        这样一来，头部那些废弃的空间就被覆盖利用了，而尾部则合并出了一大块连续的空闲空间。
        随后，更新读写游标。读游标回到 0，写游标回到 readable（也就是有效数据的长度处）。*/
    }
}