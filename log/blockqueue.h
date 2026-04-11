# ifndef BLOCKQUEUE_H
#define BLOCKQUEUE_H

#include<deque>
#include<mutex>
#include<condition_variable>
#include<sys/time.h>

template<typename T>
class BlockQueue
{
public:
    explicit BlockQueue(size_t maxsize = 1000);
    ~BlockQueue();
    bool empty();
    bool full();
    void push_back(const T& item);
    void push_front(const T& item);
    bool pop(T& item);   //弹出的任务放入item
    bool pop(T& item , int timeout);  //等待时间
    void clear();
    T front();
    T back();
    size_t capacity();
    size_t size();

    void flush();
    void Close();

private:
    std::deque<T> deq_;   //底层数据结构
    std::mutex mtx_;    //锁
    bool isClose_;        //关闭标志
    size_t capacity_;   //容量
    std::condition_variable condConsumer_;  //消费者条件变量
    std::condition_variable condProducer_;  //生产者条件变量
};

template<typename T>
BlockQueue<T>::BlockQueue(size_t maxsize) : capacity_(maxsize)  // 初始化列表：直接把传入的 maxsize 赋给 capacity_
{
    assert(maxsize > 0);  // 断言：强制要求最大容量必须大于0，如果为0或负数，程序直接在此处崩溃报错。
    isClose_ = false;     // 刚创建，队列默认是开启状态。
}

template<typename T>
BlockQueue<T>::~BlockQueue()
{
    Close();
}

template<typename T>
void BlockQueue<T> :: Close()
{
    clear();  // 先把队列里剩下的数据全删了
    isClose_ = true;  // 把关闭标志设为 true
    condConsumer_.notify_all();  // 极其关键！可能有消费者线程因为队列为空正在沉睡，全部叫醒它们，让它们看到 isClose_ == true 后安全退出。
    condProducer_.notify_all();  // 同理，把可能因为队列满而沉睡的生产者全叫醒。
}

template<typename T>
void BlockQueue<T> :: clear()
{
    std::lock_guard<std::mutex> locker(mtx_);  // 加锁！lock_guard 诞生即加锁，离开这个大括号自动解锁
    deq_.clear();  // 调用原生 deque 的 clear 清空数据
}


template<typename T>
bool BlockQueue<T> :: empty()
{
    std::lock_guard<std::mutex> locker(mtx_);  // 加锁！lock_guard 诞生即加锁，离开这个大括号自动解锁
    return deq_.empty();  // 调用原生 deque 的 empty 判断
}

template<typename T>
bool BlockQueue<T> :: full()
{
    std::lock_guard<std::mutex> locker(mtx_);  // 加锁！lock_guard 诞生即加锁，离开这个大括号自动解锁
    return deq_.size() >= capacity_;  // 调用原生 deque 的 size来获取当前队列大小
}

template<typename T>
void BlockQueue<T> :: push_back(const T& item)
{
    std::unique_lock<std::mutex> locker(mtx_);  // 注意，条件变量condition_variable::wait 必须配合unique_lock
    while(deq_.size() >= capacity_)   // 队列满了，需要等待
    { 
        condProducer_.wait(locker);   // 暂停生产，等待消费者唤醒生产条件变量
    }
    deq_.push_back(item);
    condConsumer_.notify_all();    // 唤醒消费者 
}

template<typename T>
void BlockQueue<T> :: push_front(const T& item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.size() >= capacity_)
    {
        condProducer_.wait(locker);   
    }

    deq_.push_front(item);
    condConsumer_.notify_one();
}

template<typename T>
bool BlockQueue<T> :: pop(T& item)
{
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empty())  // 只要队列是空的，就一直循环
    {
        condConsumer_.wait(locker);   //队列空了，需要等待。消费者释放锁，原地睡觉。
    }
    item = deq_.front();  // 把队列头部的第一个元素拷贝给传入的引用变量 item。
    deq_.pop_front();
    condProducer_.notify_one();  //拿走了一个，腾出空位了，唤醒一个生产者
    return true;
}

template<typename T>
bool BlockQueue<T> :: pop(T& item , int timeout)
{
    std::unique_lock<std::mutex> locker(mtx_);
    while(deq_.empy())   // 只要队列是空的，就一直循环
    {
        if(condConsumer_.wait_for(locker,std::chrono::seconds(timeout))
            == std::cv_status::timeout)  // wait_for：只等指定的时间（这里转换为秒）
            {  // 如果到了时间还没人叫醒它，返回值就是 std::cv_status::timeout
                return false;   // 超时了还没等到数据，返回 false 放弃
            }
        if(isClose_)
        {
            return false;   // 如果被叫醒是因为队列关了，也返回 false
        }
    }

    item = deq_.front();
    deq_.pop_front();
    condProducer_.notify_one();
    return true;
}

template<typename T>
T BlockQueue<T> ::front()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.front();
}

template<typename T>
T BlockQueue<T> :: back()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.back();
}

template<typename T>
size_t BlockQueue<T>::capacity()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return capacity_;
}

template<typename T>
size_t BlockQueue<T>::size()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return deq_.size();
}

//唤醒消费者
template<typename T>
void BlockQueue<T> :: flush()
{
    condConsumer_.notify_one();
}

# endif