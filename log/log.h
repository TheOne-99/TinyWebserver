#ifndef LOG_H
#define LOG_H

#include<mutex>
#include<string>
#include<thread>
#include<sys/time.h>
#include<stdarg.h>
#include<string.h>
#include<assert.h>
#include<sys/stat.h>

#include "blockqueue.h"
#include "../buffer/buffer.h"

class Log
{
public:
    void init(int level , const char* path = "./log" ,
                const char* suffix = ".log",
                int maxQueueCapacity = 1024);
    static Log* Instance();  // 获取全局单例对象的静态方法
    /*
    给后台异步线程绑定的函数指针，
    必须是静态的，因为普通成员函数带有隐式的 this 指针，
    线程库不认。异步写日志公有方法，调用私有方法asyncWrite
    */
    static void FlushLogThread();   

    void write(int level , const char *format, ...);  //将输出内容按照标准格式整理
    void flush();  // 手动刷盘

    int GetLevel();
    void SetLvel(int level);
    bool IsOpen()
    {
        return isOpen_;
    }

private:
    Log();  // 构造函数私有化：单例模式的基石，防止别人在外部 new Log()。
    void AppendLogLevelTitle_(int level);
    virtual ~Log();
    void AsyncWrite_();

private:
    static const int LOG_PATH_LEN = 256;  //日志文件最长文件名
    static const int LOG_NAME_LEN = 256;  //日志最长名字
    static const int MAX_LINES = 5000;    //日志文件内的最长日志条数

    const char* path_;  //路径名
    const char* suffix_;   //后缀名

    int MAX_LINES_;  //最大日志行数

    int lineCount_;   //日志行数记录
    int toDay_;   //按当天日期区分文件

    bool isOpen_;

    Buffer buff_;   //输出的内容，缓冲区
    int level_;    //日志等级
    bool isAsync_;  //是否开启异步日志

    FILE* fp_;   //打开LOG的文件指针
    std::unique_ptr<BlockQueue<std::string>> deque_;   //阻塞队列
    std::unique_ptr<std::thread> writeThread_;    //写线程的指针
    std::mutex mtx_;   //同步日志必须的互斥量

};


/*
基础宏：把你写的日志请求，翻译成对 Log 实例的调用
如果日志系统开着，且你要打的日志等级 >= 系统要求的最低等级
调用 write。##__VA_ARGS__ 把你传的变量原封不动传给 write
log->flush();\ // 强制刷新，确保不会丢失
这个 do { } while(0) 没有任何循环意义，纯粹是为了让这几行代码在编译后变成一个不可分割的单一语句，
防止破坏外部的 if/else 结构。
*/ 
#define LOG_BASE(level,format,...) \
    do{ \
        Log* log = Log::Instance(); \
        if(log->IsOpen() && log->GetLevel() <= level) \
        {\
            log->write(level , format ,##__VA_ARGS__); \
            log->flush(); \
        }\
    }while(0);

// 对外接口宏：
// 当你写 LOG_DEBUG("Count: %d", 5); 
// 编译器会把它替换成 LOG_BASE(0, "Count: %d", 5);
/*
四个宏定义，主要用于不同类型的日志输出，也是外部使用日志的接口
... 表示可变参数，__VA_ARGS__就是将...的值复制到这里
前面加上##的作用是，当可变参数的个数为0，这里的##可以把前面多余的“，”去掉，否则会编译出错
*/
#define LOG_DEBUG(format, ...) do {LOG_BASE(0,format, ##__VA_ARGS__)} while(0);
#define LOG_INFO(format, ...) do {LOG_BASE(1,format, ##__VA_ARGS__)} while(0);
#define LOG_WARN(format, ...) do {LOG_BASE(2,format, ##__VA_ARGS__)} while(0);
#define LOG_ERROR(format, ...) do {LOG_BASE(3,format, ##__VA_ARGS__)} while(0);
#endif