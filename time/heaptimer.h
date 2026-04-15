#include<queue>
#include<unordered_map>
#include<algorithm>
#include<assert.h>
#include <arpa/inet.h>
#include<chrono>    // C++11 的超高精度时间库
#include<functional>

#include "../log/log.h"

// 将复杂的类型起个简短的别名，方便后面写代码
//问题1：什么是回调函数？应该怎样使用？
typedef std::function<void()> TimeoutCallBack;   // 定义回调函数类型，没有参数，没有返回值
typedef std::chrono::high_resolution_clock Clock;   // 定义一个超高精度的时钟
typedef std::chrono::milliseconds MS;     // 定义毫秒单位
typedef Clock::time_point TimeStamp;    // 定义一个绝对的时间点类型

struct TimerNode
{
    int id;
    TimeStamp expires;
    TimeoutCallBack cb;

    // 【重载小于号 <】
    bool operator<(const TimerNode& t)
    {
        return expires < t.expires;    // 告诉 C++，比较两个节点大小，其实就是比较它们的时间早晚。时间越早的，认为越小！
    }
    // 【重载小于号 >】
    bool operator>(const TimerNode& t)
    {
        return expires > t.expires;
    }
};

class HeapTimer
{
public:
// 构造函数：初始化时，直接让底层的 vector 预留 64 个位置。防止频繁扩容带来性能损耗。
    HeapTimer()
    {
        heap_.reserve(64);
    }
    ~HeapTimer() { clear(); }

    void adjust(int id , int newExpires);
    void add(int id , int timeOut , const TimeoutCallBack& cd);
    void doWork(int id);
    void clear();
    void tick();    //清除超时节点
    void pop();
    
    int GetNextTick();

private:
    void del_(size_t i);   // 删除数组下标为 i 的节点
    void siftup_(size_t i);   // 向上调整（把小的节点往树的顶端冒泡）
    bool sifdown_(size_t i , size_t n);     // 向下调整（把大的节点往树的底下沉）
    void SwapNode_(size_t i , size_t j);   // 交换两个节点

    std::vector<TimerNode> heap_; // 核心：用连续数组模拟的那棵“完全二叉树”！

    // 【极大提升性能的核心】：哈希映射表
    // key: 连接的 id (fd) ， value: 这个连接在 heap_ 数组中的下标
    // 为什么要有它？普通的堆不支持通过 id 快速查找节点。有了它，我们要修改某个客户的定时器，O(1) 瞬间就能找到它在数组里的位置！
    //问题2：详细解释为什么要有这个哈希映射表
    std::unordered_map<int , size_t> ref_;

};