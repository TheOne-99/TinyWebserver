#include "webserver.h"

WebServer::WebServer(
            int port, int trigMode, int timeoutMS, bool OptLinger,
            int sqlPort, const char* sqlUser, const  char* sqlPwd,
            const char* dbName, int connPoolNum, int threadNum,
            bool openLog, int logLevel, int logQueSize) :
            port_(port), openLinger_(OptLinger), timeoutMS_(timeoutMS), isClose_(false),
            timer_(new HeapTimer()), threadpool_(new ThreadPool(threadNum)), epoller_(new Epoller())
    {
        // 获取当前程序运行的绝对路径，拼上 /resources/，作为存放网页的根目录
        //问题1：getcwd函数的作用是什么？
        srcDir_ = getcwd(nullptr , 256);
        assert(srcDir_);
        strcat(srcDir_ , "/resources/");

        HttpConn::userCount = 0;
        HttpConn::srcDir = srcDir_;

        //唤醒数据库连接池
        SqlConnPool :: Instance()->Init("localhost" , sqlPort , sqlUser , sqlPwd , dbName , connPoolNum);
        
        // 初始化触发模式（ LT / ET）
        InitEventMode_(trigMode);

        // 🌟 创建监听套接字（网关），如果在这一步失败了，服务器直接宣布死亡 (isClose_ = true)
        if(!InitSocket_()) { isClose_ = true; }

        // 初始化日志系统
        if(openLog)
        {
            
            Log::Instance()->init(logLevel , "./log" , ".log" , logQueSize);
            if(isClose_) { LOG_ERROR("========== Server init error!==========\n"); }
            else{
            LOG_INFO("========== Server init ==========\n");
            LOG_INFO("Port:%d, OpenLinger: %s\n", port_, OptLinger? "true":"false");
            LOG_INFO("Listen Mode: %s, OpenConn Mode: %s\n",
                            (listenEvent_ & EPOLLET ? "ET": "LT"),
                            (connEvent_ & EPOLLET ? "ET": "LT"));
            LOG_INFO("LogSys level: %d\n", logLevel);
            LOG_INFO("srcDir: %s\n", HttpConn::srcDir);
            LOG_INFO("SqlConnPool num: %d, ThreadPool num: %d\n", connPoolNum, threadNum);
            }
        }
    }

WebServer::~WebServer()
{
    close(listenFd_);
    isClose_ = true;
    free(srcDir_);
    SqlConnPool::Instance()->ClosePool();
}

//问题2：这整个函数都需要重新解释，一点都不明白.在这里用socket的成员方法都是什么意思
/* Create listenFd */
bool WebServer::InitSocket_()
{
    int ret;
    struct sockaddr_in addr;  // 存放 IP 和端口
    // ... 端口合法性校验 ...
    if(port_ > 65535 || port_ < 1024)
    {
        LOG_ERROR("Port:%d error!",port_);
        return false;
    }

    addr.sin_family = AF_INET;   // 使用 IPv4
    addr.sin_addr.s_addr = htonl(INADDR_ANY);    // 监听本机所有的网卡 IP
    addr.sin_port = htons(port_);   // 监听我们设定的端口 (比如 1316)

    // 优雅关闭 (Linger 选项) 问题3：这一段是什么意思？linger又是什么类？作用是什么？
    {
        struct linger optLinger = { 0 };
        if(openLinger_)
        {
            // 如果开启优雅关闭：当服务器 close 掉 socket 时，如果还有残余数据没发完，
            // 操作系统会在后台强行阻塞等待一会，直到数据发完或超时，而不是直接咔嚓掉。
            optLinger.l_onoff = 1;
            optLinger.l_linger = 1;
        }
        // 1. 创建套接字 (TCP模式)
        listenFd_ = socket(AF_INET , SOCK_STREAM , 0);
        if(listenFd_ < 0)
        {
            LOG_ERROR("Create socket error!" , port_);
            return false;
        }

        // 将优雅关闭选项设置到底层 Socket 上
        ret = setsockopt(listenFd_ , SOL_SOCKET , SO_LINGER , &optLinger , sizeof(optLinger));

        if(ret < 0)
        {
            close(listenFd_);
            LOG_ERROR("Init linger error!" , port_);
            return false;
        }
    }

    int optval = 1;
    // 端口复用 (SO_REUSEADDR) 极其重要！
    // 如果服务器突然崩溃重启，旧的端口其实还没完全释放（处于 TIME_WAIT 状态）。
    // 开启这个选项，允许我们立刻强行霸占、复用这个端口，防止服务器重启失败！
    ret = setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));
    if(ret == -1)
    {
        LOG_ERROR("set socket setsockopt error !");
        close(listenFd_);
        return false;
    }

    // 2. 绑定 (把刚才的 IP 和端口，死死绑定在这个 listenFd_ 上)
    ret = bind(listenFd_, (struct sockaddr*)&addr, sizeof(addr));
    if(ret < 0) {
        LOG_ERROR("Bind Port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 3. 监听 (开门接客！6 代表操作系统底层全连接队列的长度，这个数字随便设，系统会自己调整)
    ret = listen(listenFd_, 6);
    if(ret < 0) {
        LOG_ERROR("Listen port:%d error!", port_);
        close(listenFd_);
        return false;
    }

    // 4. 将这个用来接客的“总网关 (listenFd_)” 加入到 epoll 雷达中！
    // 只要有新客人敲门，epoll 就会报 EPOLLIN 读事件！
    ret = epoller_->AddFd(listenFd_, listenEvent_ | EPOLLIN);
    if(ret == 0) {
        LOG_ERROR("Add listen error!");
        close(listenFd_);
        return false;
    }  

    // 5. 设置为非阻塞模式（ET 模式必备！）
    SetFdNonblock(listenFd_);
    LOG_INFO("Server port:%d", port_);
    return true;
}

// 设置文件描述符（Socket）为非阻塞模式 (O_NONBLOCK)
int WebServer::SetFdNonblock(int fd) {
    assert(fd > 0);
    // fcntl 是 Linux 底层专门用来修改文件句柄属性的神器
    return fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}



//问题4：这个函数的作用是什么？参数trigMode是什么意思？如何设置connEvent_和listenEvent_
void WebServer::InitEventMode_(int trigMode) {
    listenEvent_ = EPOLLRDHUP;    // 检测socket关闭
    connEvent_ = EPOLLONESHOT | EPOLLRDHUP;     // EPOLLONESHOT由一个线程处理
    switch (trigMode)
    {
    case 0:
        break;
    case 1:
        connEvent_ |= EPOLLET;
        break;
    case 2:
        listenEvent_ |= EPOLLET;
        break;
    case 3:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    default:
        listenEvent_ |= EPOLLET;
        connEvent_ |= EPOLLET;
        break;
    }
    HttpConn::isET = (connEvent_ & EPOLLET);
}

void WebServer::Start()
{
    int timeMS = -1;   // epoll_wait 的超时时间
    while(!isClose_)
    {
        if(timeoutMS_ > 0)
        {
            // 向小根堆请教：距离下一个客人超时还有多久？
            // 比如还有 5000ms，那就让 epoll 最多等 5000ms 必须醒过来去清理垃圾。
            timeMS = timer_->GetNextTick();
        }
        // 核心阻塞点：死盯大屏幕，捞取发生的事件！
        int evenCount = epoller_->Wait(timeMS);

        // 循环处理捞到的每一个事件
        for(int i = 0; i < evenCount; i ++)
        { 
            int fd = epoller_->GetEventFd(i);   // 哪个客人的桌子响了？
            uint32_t events = epoller_->GetEvents(i);   // 响的是什么铃？

            // 场景 A：如果响的是总网关 (listenFd_)，说明来新客人了！
            if(fd == listenFd_){
                DealListen_();
            }

            // 场景 B：如果事件包含 RDHUP, HUP, ERR，说明客人掉线了，或者出错了
            else if(events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR))
            {
                CloseConn_(&users_[fd]);  // 强行关闭他的连接
            }
            // 场景 C：客人发来消息了，准备好读数据 (EPOLLIN)
            else if(events & EPOLLIN)
            {
                DealRead_(&users_[fd]);   // 处理读
            }
            // 场景 D：我们可以给客人发消息了 (EPOLLOUT)
            else if(events & EPOLLOUT)
            {
                DealWrite_(&users_[fd]);// 处理写
            }
        }
    }
}

void::WebServer::SendError_(int fd , const char* info)
{
    assert(fd > 0);
    int ret = send(fd , info ,strlen(info) , 0);
    if(ret < 0)
    {
        LOG_WARN("send error to client[%d] error!" , fd);
    }
    close(fd);
}

void WebServer::CloseConn_(HttpConn* client)
{
    assert(client);
    LOG_INFO("Client[%d] quit!",client->GetFd());
    client->Close();
}

void WebServer::AddClient_(int fd , sockaddr_in addr)
{
    assert(fd > 0);
    users_[fd].Init(fd,addr);   // 让 HttpConn 自己初始化
    if(timeoutMS_ > 0)
    {
        // 给这个新客户挂上倒计时炸弹。
        // std::bind 把类的成员函数 CloseConn_ 和 this 指针绑定在一起当成回调函数！
        timer_->add(fd , timeoutMS_ ,std::bind(&WebServer::CloseConn_, this, &users_[fd]));
    }

    // 把这个新客人的 fd 加进 epoll 监听池！
    epoller_->AddFd(fd , EPOLLIN | connEvent_);
    SetFdNonblock(fd);  // 设为非阻塞
    LOG_INFO("Client[%d] in!" , users_[fd].GetFd());
}

void WebServer::DealListen_()
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    do{
        // accept：从大门口领进来一个新客人，给他分配一个专属的 fd
        int fd = accept(listenFd_ , (struct sockaddr*)&addr , &len);
        if(fd <= 0) return;

        // 如果客人太多（超过 65536），超载了
        else if(HttpConn::userCount >= MAX_FD){
            SendError_(fd , "Server busy!");  // 发一句“太忙了”，立刻关门
            return;
        }
        // 把新客人安顿好
        AddClient_(fd , addr);
        // 因为听门用的 ET 模式，必须把大门口排队的新客人一次性全领进来
    }while(listenEvent_ & EPOLLET);
}

// 处理读事件，主要逻辑是将OnRead加入线程池的任务队列中
void WebServer::DealRead_(HttpConn* client)
{
    assert(client);
    ExtenTime_(client);

    // 把 OnRead_ 函数和这个 client 绑定，打包成一个任务，扔进线程池！
    // 此时主线程立刻抽身，回去继续盯 epoll。由后厨的某个子线程去执行 OnRead_！
    threadpool_->AddTask(std::bind(&WebServer::OnRead_ , this , client));
}

// 处理写事件，主要逻辑是将OnWrite加入线程池的任务队列中
void WebServer::DealWrite_(HttpConn* client)
{
    assert(client);
    ExtenTime_(client);

    threadpool_->AddTask(std::bind(&WebServer::OnWrite_ , this , client));
}

void WebServer::ExtenTime_(HttpConn* client)
{
    assert(client);
    if(timeoutMS_ > 0)
    {
        timer_->adjust(client->GetFd() , timeoutMS_);
    }
}

void WebServer::OnRead_(HttpConn* client)
{
    assert(client);
    int ret = -1;
    int readErrno = 0;

    ret = client->read(&readErrno);   // 读取客户端套接字的数据，读到httpconn的读缓存区

    // 如果读失败了，且不是 EAGAIN (EAGAIN代表没数据了，是正常的)，直接关闭连接
    if(ret <= 0 && readErrno != EAGAIN)
    {
        CloseConn_(client);
        return;
    }
    // 数据读进 Buffer 了，开始业务处理！业务逻辑的处理（先读后处理）
    OnProcess(client);
}

/* 处理读（请求）数据的函数 */
void WebServer::OnProcess(HttpConn* client)
{
    // 首先调用process()进行逻辑处理
    if(client->process())   // 根据返回的信息重新将fd置为EPOLLOUT（写）或EPOLLIN（读）
    {
        //读完事件就跟内核说可以写了
        epoller_->ModFd(client->GetFd() , connEvent_ | EPOLLOUT);  // 响应成功，修改监听事件为写,等待OnWrite_()发送
    }else{
        //写完事件就跟内核说可以读了
        epoller_->ModFd(client->GetFd() , connEvent_ | EPOLLIN); 
    }
}

void WebServer::OnWrite_(HttpConn* client)
{
    assert(client);
    int ret = -1;
    int writeErrno = 0;
    // 调用 writev 把内存里的文件和头信息发往网卡
    ret = client->write(&writeErrno);

    if(client->ToWriteBytes() == 0)
    {
        /* 传输完成 */
        if(client->IsKeepAlive())
        {
            // 如果是长连接，不断开。重置监听状态，继续等他发下一个 HTTP 请求 (EPOLLIN)
            epoller_->ModFd(client->GetFd() , connEvent_ | EPOLLIN);  // 回归换成监测读事件
            return;
        }
    }
    else if(ret < 0)
    {
        if(writeErrno == EAGAIN)  // TCP 发送缓冲区塞满了，发不动了
        {
            /* 告诉 epoll 别急，等发送缓冲区有空位了 (EPOLLOUT)，再叫我过来继续发！ */
            epoller_->ModFd(client->GetFd() , connEvent_ | EPOLLOUT);
            return;
        }
    }
    // 发完了且不是长连接，或者发生严重错误，挥手再见！
    CloseConn_(client);
}







