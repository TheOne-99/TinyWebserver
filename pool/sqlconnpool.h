#ifndef SQLCONNPOOL_H
#define SQLCONNPOOL_H

#include<mysql/mysql.h>
#include<queue>
#include<semaphore.h>
#include<mutex>

#include "../log/log.h"

class SqlConnPool
{
public:
//对外接口
    static  SqlConnPool *Instance();  // 单例模式：和昨天讲的日志系统一样，全局只能有一个连接池管家
 
    MYSQL *GetConn();   // 线程来借数据库连接
    void FreeConn(MYSQL* conn);  // 线程用完了，把连接还给池子
    int GetFreeConnCount();    // 查看还有多少空闲连接

    void Init(const char* host , int port ,const char* user, const char* pwd, const char* dbName , int connSzie);
    void ClosePool();    // 销毁所有连接

private:
    SqlConnPool() = default;
    ~SqlConnPool()
    {
        ClosePool();
    }
    int MAX_CONN_;  // 最大连接数
    std::queue<MYSQL *> connQue_;  // 核心队列：存放空闲的 MySQL 连接指针
    std::mutex mtx_;   // 互斥锁：保证多线程从队列里拿连接时不会冲突
    sem_t semId_;     // 信号量：记录当前池子里还有多少个空闲连接
};

class SqlConnRAII
{
public:
    SqlConnRAII(MYSQL** sql , SqlConnPool* connpool)
    {
        assert(connpool);
        *sql = connpool->GetConn();  // 借出连接，并赋值给外部传进来的指针
        sql_ = *sql;   // 自己内部也存一份记录
        connpool_ = connpool;  // 记住是从哪个池子借的
    }

    // 析构函数：对象生命周期结束被销毁时，自动归还连接
    ~SqlConnRAII()
    {
        if(sql_)
        {
            connpool_->FreeConn(sql_);
        }
    }
private:
    MYSQL* sql_;
    SqlConnPool* connpool_;
};

#endif