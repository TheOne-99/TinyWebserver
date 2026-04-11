// 文件路径: test/test_log.cpp
#include "../log/log.h"
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

// 多线程打日志的函数：每个线程疯狂写入指定条数的日志
void threadLogTask(int threadId, int logCount) {
    for (int i = 0; i < logCount; ++i) {
        // 交替测试不同等级的宏定义
        if (i % 4 == 0) {
            LOG_DEBUG("Thread %d : 这是 Debug 日志, 序号: %d", threadId, i);
        } else if (i % 4 == 1) {
            LOG_INFO("Thread %d : 这是 Info 日志, 序号: %d", threadId, i);
        } else if (i % 4 == 2) {
            LOG_WARN("Thread %d : 这是 Warn 日志, 序号: %d", threadId, i);
        } else {
            LOG_ERROR("Thread %d : 这是 Error 日志, 序号: %d", threadId, i);
        }
    }
}

void TestAsyncLog() {
    std::cout << "========== 开启异步日志压力测试 ==========" << std::endl;
    
    // 初始化日志：等级为0(全量输出)，路径设为当前目录下的 ./log，最大队列容量 1024 (开启异步)
    Log::Instance()->init(0, "./log", ".log", 1024);

    int threadNum = 10;          // 开启 10 个线程
    int logsPerThread = 10000;   // 每个线程写 1 万条日志 (总共 10 万条)

    std::cout << "即将开启 " << threadNum << " 个线程，共计写入 " 
              << threadNum * logsPerThread << " 条日志..." << std::endl;
    std::cout << "由于我们设定了单文件最大 50000 行，预期将生成至少 2 个日志文件分卷。" << std::endl;

    auto start = std::chrono::high_resolution_clock::now();

    // 创建并启动多线程
    std::vector<std::thread> threads;
    for (int i = 0; i < threadNum; ++i) {
        threads.emplace_back(threadLogTask, i, logsPerThread);
    }

    // 等待所有生产者线程执行完毕
    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = end - start;

    std::cout << "日志写入请求已全部发送完毕！" << std::endl;
    std::cout << "耗时: " << diff.count() << " ms" << std::endl;
    std::cout << "========== 测试结束，请检查 ./log 文件夹 ==========\n" << std::endl;
}

#if 0
int main() {
    // 执行异步并发测试
    TestAsyncLog();
    return 0;
}
#endif