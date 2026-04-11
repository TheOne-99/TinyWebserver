#ifndef THREADPOOH_H
#define THREADPOOH_H

#include<queue>
#include<mutex>
#include<condition_variable>
#include<thread>
#include<functional>
#include<assert.h>

class ThreadPool
{
public:
    ThreadPool() = default;  //为了要回被编译器收走的默认构造函数
    ThreadPool(ThreadPool&&) = default; //移动构造函数，这里的 && 不是逻辑与，而是 C++11 的灵魂特性：右值引用。

    // explicit: 防止发生隐式类型转换。只能老老实实写 ThreadPool pool(8);
    explicit ThreadPool(int threadCount = 8) : pool_(std::make_shared<Pool>())
    {
        assert(threadCount > 0);
        //循环创建threadCount个线程
        for(int i = 0; i < threadCount ; i ++)
        {
            std::thread([this](){  //函数对象参数为this，函数体内可以使用Lambda所在类中的成员变量。
                std::unique_lock<std::mutex> locker(pool_->mtx_);
                while(true)
                {
                    if(!pool_->tasks.empty()){
                        // 【细节优化】：使用 move 资产转移
                        auto task = std::move(pool_->tasks.front());
                        /*
                        tasks.front() 拿到的是一个函数对象。普通的赋值是“深拷贝”，非常耗时。
                        std::move 是 C++11 的大杀器，直接把原对象的内存控制权“偷”过来（左值变右值）
                        性能极高。
                        */
                        pool_->tasks.pop();

                        // 【神仙操作】：提前解锁！
                        locker.unlock();
                        task();   // 执行拿到的任务
                        locker.lock();   // 任务执行完了，重新上锁去拿下一个任务
                    }else if(pool_->isClosed){
                        break;  // 池子关了，跳出死循环，线程结束
                    }else{
                        pool_->cond_.wait(locker);  // 队列没任务，挂起等待
                    }
                }
            }).detach();     
        }
    }

    ~ThreadPool()
    {
        if(pool_)
        {
            std::unique_lock<std::mutex> locker(pool_->mtx_);
            pool_->isClosed = true;
        }
        pool_->cond_.notify_all();  // 唤醒所有的线程
    }

    template<typename T>
    void AddTask(T&& task)
    {
        std::unique_lock<std::mutex> locker(pool_->mtx_);
        pool_->tasks.emplace(std::forward<T>(task));
        pool_->cond_.notify_one();
    }


private:
// 将底层资源用一个结构体封装起来，方便调用
    struct Pool
    {
        std::mutex mtx_;  // 互斥锁
        std::condition_variable cond_;   // 条件变量
        bool isClosed;    // 线程池是否关闭的标志

        /*
        这是 C++11 的通用多态函数封装器。不管你塞进来的是普通函数、Lambda 表达式、还是类的成员函数，
        只要它没有参数且没有返回值（void()），都可以被它统一装进 tasks 队列里。
        */
        std::queue<std::function<void()>> tasks;  // 任务队列！存放所有需要被执行的函数！
    };
    std::shared_ptr<Pool> pool_;   // 智能指针，管理上面那个结构体.共享智能指针。只要还有人（比如某个正在执行的线程）在使用这个 Pool，它的内存就不会被释放。
};

#endif