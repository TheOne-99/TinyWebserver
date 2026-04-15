#include "sqlconnpool.h"

SqlConnPool* SqlConnPool::Instance()   //获取连接池的全局唯一实例。
{
    static SqlConnPool pool;
    return &pool;
}

void SqlConnPool::Init(const char* host , int port , const char* user,
                        const char* pwd , const char* dbName ,int connSize = 10)
    {
        assert(connSize > 0);

        // 循环创建指定数量 (connSize) 的数据库连接
        for(int i=0 ; i<connSize ; i++)
        {
            MYSQL* conn = nullptr;
            conn = mysql_init(conn);  // 【MySQL C API】初始化一个 MYSQL 连接句柄
            if(!conn)
            {
                LOG_ERROR("MYSQL init error!");
                assert(conn);  //如果连句柄都创建失败，直接让程序崩溃，没必要往下走了
            }
            // 【MySQL C API】拿着刚才的句柄，携带IP、账号、密码、库名、端口号，向 MySQL 服务器发起真正的网络连接！
            conn = mysql_real_connect(conn,host,user,pwd,dbName,port ,nullptr ,0);
            if(!conn)
            {
                LOG_ERROR("Mysql Connect error!");
            }

            connQue_.emplace(conn);
        }

        MAX_CONN_ = connSize;

        /*
        初始化信号量 semId_
        参数2：0 表示这个信号量只在当前进程内的线程间共享
        参数3：信号量的初始值，也就是现在有多少个空闲连接（刚初始化完，当然是全满的 MAX_CONN_）
        */
        sem_init(&semId_,0,MAX_CONN_);
    }

MYSQL* SqlConnPool::GetConn()
{
    MYSQL* conn = nullptr;
    if(connQue_.empty())
    {
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }

    // 【关键特性：信号量等待 (P操作)】
    /*
    如果 semId_ 大于 0（有空闲连接），sem_wait 会立刻把信号量减 1，并往下执行。
    如果 semId_ 等于 0（全借出去了），sem_wait 会让当前线程休眠阻塞
    直到有人归还连接（信号量 +1）把它唤醒！
    */
    sem_wait(&semId_);  //信号量-1

    /*
    必须先 sem_wait，再加锁！
    试想如果先加锁，再 sem_wait 并且阻塞了，那么这把锁就被死死抱住了。
    其他想来归还连接的线程拿不到锁，就永远无法归还，这就是经典的“死锁”。
    */
    std::lock_guard<std::mutex> locker(mtx_);
    conn = connQue_.front();  // 拿出队列最前面的连接
    connQue_.pop();   // 把它从队列里删掉（代表借出去了）
    return conn;
}

void SqlConnPool::FreeConn(MYSQL* conn)
{
    assert(conn);  // 断言：归还的不能是个空指针
    std::lock_guard<std::mutex> locker(mtx_);  // 加锁保护队列，防止多个线程同时往里面 push 导致内存混乱
    connQue_.push(conn);  // 把连接重新放回池子尾部

    // 【关键特性：信号量发布 (V操作)】
    sem_post(&semId_);  // 信号量 +1
    // 这代表池子里又多了一个空闲连接。
    // 同时，它会立刻“敲锣”唤醒一个可能正卡在前面 sem_wait 那里苦苦等待连接的线程。
}

void SqlConnPool::ClosePool()
{
    std::lock_guard<std::mutex> locker(mtx_);  //拿锁
    while (!connQue_.empty())
    {
        auto conn = connQue_.front();
        connQue_.pop();

        // 【MySQL C API】告诉 MySQL 数据库服务器：“我要断开这个 TCP 连接了，再见”
        mysql_close(conn);
    }
    mysql_library_end();  //清理 MySQL 客户端库分配的所有内部内存
}

int SqlConnPool::GetFreeConnCount()
{
    std::lock_guard<std::mutex> locker(mtx_);  //为了安全，读取队列大小之前也要加锁，返回当前还有多少个可以用的连接
    return connQue_.size();
}