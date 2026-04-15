#ifndef EPOLLER_H
#define EPOLLER_H

#include<sys/epoll.h>
#include<unistd.h>
#include<assert.h>
#include<vector>
#include<errno.h>


class Epoller
{
public:
    explicit Epoller(int maxEvent = 1024);
    ~Epoller();

    //添加、修改、删除监听事件 (epoll_ctl)
    bool AddFd(int fd , uint32_t events);
    bool ModFd(int fd , uint32_t events);
    bool DelFd(int fd);

    //等待事件发生 (epoll_wait)
    int Wait(int timeoutMs = -1);
    int GetEventFd(size_t i) const;
    uint32_t GetEvents(size_t i) const;

private:
    int epollFd_;
    std::vector<struct epoll_event> events_;
};

#endif