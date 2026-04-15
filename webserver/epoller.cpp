#include"epoller.h"
#include <stdio.h>

Epoller::Epoller(int maxEvent)
    :epollFd_(epoll_create(512)) , events_(maxEvent)
    {
        if (epollFd_ < 0) {
        // 如果失败了，让系统打印出具体的底层错误原因！
        perror("epoll_create failed"); 
        }
    // epoll_create(512)：向 Linux 内核申请创建一个 epoll 句柄（买大屏幕）。
    // 里面的 512 只是个历史遗留的提示值，在现代 Linux 中无实际意义，只要大于 0 就行。
    assert(epollFd_ >= 0 && events_.size() > 0);
    }

Epoller :: ~Epoller()
{
    close(epollFd_);
}

bool Epoller::AddFd(int fd , uint32_t events)
{
    if(fd < 0) return false;
    epoll_event ev = {0};  // 准备一个事件结构体
    ev.data.fd = fd;       // 记录这是哪个客人的桌号 (Socket 句柄)
    ev.events = events;       // 记录你要监听他干啥？(EPOLLIN 读事件，还是 EPOLLOUT 写事件？)

    return 0 == epoll_ctl(epollFd_ , EPOLL_CTL_ADD , fd , &ev);
} 

bool Epoller :: ModFd(int fd , uint32_t events)
{
    if(fd < 0) return false;
    epoll_event ev = {0};
    ev.data.fd = fd;
    ev.events = events;

    // EPOLL_CTL_MOD 表示“修改”。比如刚才在监听他发消息(读)，现在改成监听我能不能给他发消息(写)
    return 0 == epoll_ctl(epollFd_ , EPOLL_CTL_MOD , fd , &ev);
}

bool Epoller :: DelFd(int fd)
{
    if(fd < 0) return false;
    // EPOLL_CTL_DEL 表示“删除”。客人走了，把他的监听器拆掉。
    return 0 == epoll_ctl(epollFd_ , EPOLL_CTL_DEL , fd , 0);
}

//等待事件发生 (epoll_wait)
int Epoller :: Wait(int timeoutMs)
{
    // epoll_wait：死盯着大屏幕！
    // 参数1：epoll 句柄。
    // 参数2：&events_[0]，把底层发生的事件，统统捞出来放进我们自己的 vector 数组里。
    // 参数3：最多能捞多少个？(vector的大小)。
    // 参数4：最多等多久？(timeoutMs)。如果传 -1，就死等，直到有事件发生。
    // 返回值：告诉你这次一共捞到了几个事件。
    return epoll_wait(epollFd_ , &events_[0] , static_cast<int>(events_.size()) , timeoutMs);
}

int Epoller:: GetEventFd(size_t i) const
{
    assert( i < events_.size() && i >= 0);
    return events_[i].data.fd;   // 获取第 i 个发生的事件是哪个 fd 触发的
}

uint32_t Epoller :: GetEvents(size_t i) const
{
    assert( i < events_.size() && i >= 0);
    return events_[i].events;  // 获取第 i 个发生的事件具体是啥（读、写、还是断开了？）
}