#include "log.h"

Log::Log()
{
    fp_ = nullptr;
    deque_ = nullptr;
    writeThread_ = nullptr;
    lineCount_ = 0;
    toDay_ = 0;
    isAsync_ = false;
}

Log::~Log()
{
    while (!deque_->empty())
    {
        deque_->flush();  // 唤醒消费者，处理掉剩下的任务
    }
    deque_->Close();  //关闭队列
    writeThread_->join(); //等待当前线程完成手中的任务
    if(fp_)  // 冲洗文件缓冲区，关闭文件描述符
    {
        std::lock_guard<std::mutex> locker(mtx_);
        flush();   // 清空缓冲区中的数据
        fclose(fp_);   // 关闭日志文件
    }
}

void Log::flush()
{
    if(isAsync_)  //有异步日志才会用到deque
    {
        deque_->flush();
    }
    fflush(fp_);   // 清空输入缓冲区
}

// 懒汉模式 局部静态变量法（这种方法不需要加锁和解锁操作）
Log* Log::Instance()
{
    static Log log;
    return &log;
}

// 异步日志的写线程函数
void Log::FlushLogThread()
{
    Log::Instance()->AsyncWrite_();
}

void Log::AsyncWrite_()
{
    std::string str = " ";
    while(deque_->pop(str))
    {
        std::lock_guard<std::mutex> locker(mtx_);
        fputs(str.c_str(),fp_);
    }
}

void Log::init(int level , const char* path, const char* suffix ,int maxQueueCapacity)
{
    isOpen_ = true;
    level_ = level;
    path_ = path;
    suffix_ = suffix;

    if(maxQueueCapacity)  // 如果传进来的容量 > 0
    {
        isAsync_ = true;  // 开启异步模式！
        if(!deque_)  // 如果队列指针是空的
        {
            // 创建一个新的 BlockQueue 放在堆区
            std::unique_ptr<BlockQueue<std::string>> newQue(new BlockQueue<std::string>);
            // 因为unique_ptr不支持普通的拷贝或赋值操作,所以采用move
            // 将动态申请的内存权给deque，newDeque被释放

            // std::move 把 newQue 的所有权抢走，交给类成员 deque_。此时 newQue 变为空。
            deque_ = std::move(newQue);  // 左值变右值,掏空newDeque

            // 创建一个新线程，一出生就开始跑 FlushLogThread 函数。
            std::unique_ptr<std::thread> newThread(new std::thread(FlushLogThread));
            writeThread_ = std::move(newThread);
             
        }
    }else{
        isAsync_ = false;
    }

    // 生成日志文件名：./path/2026_04_07.log
    lineCount_ = 0;  
    time_t timer = time(nullptr);  // 获取时间戳
    struct tm* systime = localtime(&timer);  // 转成本地时间结构体
    char fileName[LOG_NAME_LEN] = {0}; // 准备一个字符数组

    // snprintf 是安全的格式化函数，把拼好的路径字符串写进 fileName
    snprintf(fileName , LOG_NAME_LEN - 1 , "%s/%04d_%02d_%02d%s",
            path_ , systime->tm_year + 1900 , systime->tm_mon+1 , systime->tm_mday ,
            suffix_);
    toDay_ = systime->tm_mday; //记录一下今天的是几号，用来判断是否跨天

    {
        std::lock_guard<std::mutex> locker(mtx_);  // 加锁准备动文件了
        buff_.RetrieveAll();  //// 清空一下 Buffer
        if(fp_)
        {
            flush();
            fclose(fp_);  // 如果之前开了文件,关掉
        }

        fp_ = fopen(fileName , "a"); // "a" 表示 append，追加模式打开
        if(fp_ == nullptr)  // 打不开？通常是因为目录不存在。
        {
            mkdir(fileName,0777);  // 暴力建个目录
            fp_ = fopen(fileName,"a");  // 重新打开
        }
        assert(fp_ != nullptr);   // 如果还是空，直接崩溃。
    }
}

void Log::write(int level , const char *format , ...)
{
    // 1. 获取微秒级时间
    struct timeval now = {0 ,0};
    gettimeofday(&now , nullptr);
    time_t tSec = now.tv_sec;
    struct tm* sysTime = localtime(&tSec);
    struct tm t = *sysTime;
    va_list vaList;  // 声明一个用于处理变长参数的变量


    // 2. 日志滚动策略：遇到跨天，或者行数达到 50000 行
    if(toDay_ != t.tm_mday || (lineCount_ && (lineCount_ % MAX_LINES == 0)))
    {
        std::unique_lock<std::mutex> locker(mtx_);
        locker.unlock();  // 立刻解锁！因为下面只是拼接字符串，不涉及共享资源

        char newFile[LOG_NAME_LEN];  // 准备新名字
        char tail[36] = {0};  // 准备日期后缀部分
        snprintf(tail , 36 , "%04d_%02d_%02d" ,t.tm_year+1900 , t.tm_mon+1 , t.tm_mday);  // 拼出 2026_04_07

        if(toDay_ != t.tm_mday)  // 如果是跨天了
        {
            snprintf(newFile , LOG_NAME_LEN - 72 , "%s/%s%s" , path_ ,tail ,suffix_);  // 名字就是正常的日期
            toDay_ = t.tm_mday;  // 更新日期
            lineCount_ = 0;  // 行数清零
        }else{  // 如果没跨天，说明是超了 5 万行了
            snprintf(newFile , LOG_NAME_LEN - 72 , "%s/%s-%d%s",path_ ,tail,(lineCount_ / MAX_LINES) ,suffix_);  // 加上分段索引 -1, -2           
        }
        
        locker.lock();  // 字符串拼完了，马上要动 fp_ 了，重新加锁保护！
        flush();
        fclose(fp_);
        fp_ = fopen(newFile,"a");  //开新文件
    }

        // 3. 把日志内容格式化进 Buffer
    {
        std::unique_lock<std::mutex> locker(mtx_);  // 保护 buff_，防止多个线程把字符串混在一起
        lineCount_++; // 行数+1

        // 按照固定格式打印时间头，写入到 Buffer 当前可以写入的内存首地址 BeginWrite()
        int n = snprintf(buff_.BeginWrite() , 128 , "%d-%02d-%02d %02d:%02d:%02d.%06ld",
                        t.tm_year+1900 , t.tm_mon+1 , t.tm_mday , t.tm_hour , t.tm_min , t.tm_sec , now.tv_usec);

        buff_.HasWritten(n);  // 手动把 Buffer 的写指针往后推 n 位

        AppendLogLevelTitle_(level);  // 塞入 [info] 这种前缀，也是调用的 buff_.Append

        // 终极绝杀：把用户传过来的 format（如"IP:%s"）和后面的参数，合并生成真正的字符串！
        va_start(vaList , format);  // 初始化 vaList
        // vsnprintf 把不定参数融合，直接写在 Buffer 剩余的空区 WritableBytes() 里！
        int m = vsnprintf(buff_.BeginWrite() , buff_.WritableBytes() , format , vaList);

        va_end(vaList);  // 清理 vaList

        buff_.HasWritten(m);   buff_.HasWritten(m); // 写指针往后推 m 位
        buff_.Append("\n\0", 2); // 补上回车和字符串结束符

        // 4. 发送数据
        if(isAsync_ && deque_->full())
        {
            deque_->push_back(buff_.RetrieveAllToStr()); // 如果开着异步，提取整个 Buffer 为 string，塞进阻塞队列

        }else{
            fputs(buff_.Peek() , fp_);  // 如果没开异步，或者队列爆满了，主线程只能自己憋屈地直接往磁盘文件里写了   
        }
        buff_.RetrieveAll();  // 把 Buffer 清零，供下一条日志使用
    }
}

// 添加日志等级
void Log::AppendLogLevelTitle_(int level) {
    switch(level) {
    case 0:
        buff_.Append("[debug]: ", 9);
        break;
    case 1:
        buff_.Append("[info] : ", 9);
        break;
    case 2:
        buff_.Append("[warn] : ", 9);
        break;
    case 3:
        buff_.Append("[error]: ", 9);
        break;
    default:
        buff_.Append("[info] : ", 9);
        break;
    }
}

int Log::GetLevel()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return level_;
}

void Log::SetLvel(int level)
{
    std::lock_guard<std::mutex> locker(mtx_);\
    level_ = level;
}