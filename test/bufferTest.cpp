#include "../buffer/buffer.h"
#include <iostream>
#include <string>

# if 0
int main() {
    std::cout << "========== 测试开始 ==========" << std::endl;

    // 测试1：基础内存写入与读取
    std::cout << "\n[1] 测试基础 Append 和 Retrieve:" << std::endl;
    Buffer buff;
    buff.Append("Hello ", 6);
    buff.Append(std::string("World!"));
    
    std::cout << "当前可读字节数: " << buff.ReadableBytes() << std::endl;
    std::cout << "读取出的内容: " << buff.RetrieveAllToStr() << std::endl;
    std::cout << "清空后可读字节数: " << buff.ReadableBytes() << std::endl;

    // 测试2：模拟网络/文件 I/O 的读写 (使用 Linux Pipe)
    std::cout << "\n[2] 测试高阶功能 ReadFd:" << std::endl;
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        std::cerr << "创建管道失败!" << std::endl;
        return 1;
    }

    // 往管道的一端(写端 pipefd[1])写入数据，模拟网络发来的数据
    std::string msg = "这是一段来自网络底层的文件描述符数据流...";
    write(pipefd[1], msg.c_str(), msg.size());
 
    // 使用我们自己写的 Buffer 从管道另一端(读端 pipefd[0])读取
    int err = 0;
    Buffer networkBuff(10); // 故意把初始容量设得很小(10)，测试它的自动扩容能力
    networkBuff.ReadFd(pipefd[0], &err);

    std::cout << "成功利用 readv 读入数据，引发了自动扩容!" << std::endl;
    std::cout << "最终读到的数据: " << networkBuff.RetrieveAllToStr() << std::endl;

    // 关闭管道
    close(pipefd[0]);
    close(pipefd[1]);

    std::cout << "\n========== 测试完成 ==========" << std::endl;
    return 0;
}
#endif