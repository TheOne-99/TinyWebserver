Buffer类（动态缓存区）的设计。由于muduo库使用的是非阻塞I/O模型，即每次send()不一定会发送完，没发完的数据要用一个容器进行接收，所以必须要实现应用层缓冲区。

空间划分为三个区域：

![image-20260402204948019](C:\Users\13469\AppData\Roaming\Typora\typora-user-images\image-20260402204948019.png)

（1）ReadFd：这里是指将外来数据写到buffer里面来。被httpconn中的read调用是读到缓冲区

（2）WriteFd: 将buffer的readable中的数据写入fd

注意：写是往writePos_的指针写，读取则是从readPos_的指针读。当外部写入fd的时候，是将buffer中的readable写入fd；当需要读取fd的内容时，则是读到writable的位置，注意区分写和读



- buffer的存储实体
   缓冲区的最主要需要是读写数据的存储，也就是需要一个存储的实体。直接用vector来完成。也就是buffer缓冲区里面需要一个：
- std::vector<char>buffer_;



- buffer所需要的变量
   由于buffer缓冲区既要作为读缓冲区，也要作为写缓冲区，所以我们既需要指示当前读到哪里了，也需要指示当前写到哪里了。所以在buffer缓冲区里面设置变量：

```undefined
std::atomic<std::size_t>readPos_;
std::atomic<std::size_t>writePos_;
```

分别指示当前读写位置的下标。其中atomic是一种原子类型，可以保证在多线的情况下，安全高性能得执行程序，更新变量。

读写接口

缓冲区最重要的就是读写接口，主要可以分为与客户端直接IO交互所需要的读写接口，以及收到客户端HTTP请求后，我们在处理过程中需要对缓冲区的读写接口。

与客户端直接I/O得读写接口（httpconn中就是调用的该接口。）：

ssize_t ReadFd(); ssize_t WriteFd();