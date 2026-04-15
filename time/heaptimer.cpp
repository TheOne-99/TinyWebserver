#include"heaptimer.h"

void HeapTimer::SwapNode_(size_t i , size_t j)
{
    assert(i >=0 && i < heap_.size());   // 防御性编程：下标不能越界
    assert(j >=0 && j < heap_.size());

    std::swap(heap_[i] , heap_[j]);  // 1. 交换 vector 里实际存放的两个 TimerNode 对象

    // 2. 更新哈希表里的路标！因为节点被挪动了，必须告诉哈希表它们的新家在哪（新的索引位置）
    ref_[heap_[i].id] = i;
    ref_[heap_[j].id] = j;
}

/*
新加入一个节点（通常放在数组末尾），或者节点的时间被调小了，它可能会比它的父亲还要小。
这时候必须让它“向上冒泡”，直到它大于它的父亲为止，以维持小根堆的规矩。
*/
void HeapTimer :: siftup_(size_t i)
{
    assert(i>=0 && i<heap_.size());

    size_t parent = (i-1) / 2;  //算出当前节点 i 的父亲节点的下标！

    while(parent >= 0)   // 只要还没冒泡到最顶端的根节点
    {
        // 如果父亲的值 大于 孩子的值，坏了规矩！（因为小根堆要求父亲必须最小）
        if(heap_[parent] > heap_[i])
        {
            SwapNode_(i , parent);  // 把小的值（孩子）跟父亲交换，让它上位！

            i = parent;   // 孩子的当前下标变成了原来父亲的下标，继续准备往上看
            parent = (i - 1) / 2;     // 重新计算新的父亲下标
        }else{
            break;
        }
    }
}

 // false：不需要下滑  true：下滑成功
bool HeapTimer::sifdown_(size_t i , size_t n)
{
    assert(i >= 0 && i < heap_.size());
    assert(n >= 0 && n <= heap_.size());    // n: 表示要在前 n 个节点中进行调整

    auto index = i;
    auto child = 2 * index + 1;   //计算它的左孩子下标(索引从0开始)

    while(child < n)   // 只要它还有孩子（孩子下标没有越界）
    {
        /*
        步骤 1：找出左右孩子中，谁更小？
        child + 1 是右孩子。如果右孩子存在，且右孩子比左孩子还小，
        那我们就把目光锁定在右孩子身上（child++），因为下沉是要和最小的那个孩子交换才安全！
        */
       if(child + 1 < n && heap_[child+1] < heap_[child])
       {
        child ++;
       }

       // 步骤 2：拿着最小的孩子，跟父亲(index)比
       if(heap_[child] < heap_[index])
       {
        SwapNode_(index , child);  // 孩子比父亲小，坏了规矩，父子交换！父亲往下沉淀！

        index = child;   // 更新当前父亲下沉后的新位置
        child = 2*index + 1;     // 继续找他新的左孩子，准备进行下一轮比对！
       }else{
        break;
       }
    }
    // 如果最终的 index 大于最初传进来的 i，说明它挪动过位置，返回 true
    return index > i;
}


//从堆里删掉一个任意位置的节点。
void HeapTimer :: del_(size_t index)
{
    assert(index >= 0 && index < heap_.size());

    //先把你要删的那个节点，和数组最末尾的那个节点交换！
    size_t tmp = index;
    size_t n = heap_.size() - 1;

    assert(tmp <= n);

    if(index < heap_.size() - 1)
    {
        SwapNode_(tmp , heap_.size() - 1);  // 交换！现在你要删的节点被扔到数组最后面了。

        // 那个原来在队尾的节点，莫名其妙被换到了中间。
        // 它可能偏大也可能偏小。我们先尝试让它往下沉 (siftdown_)
        // 如果它沉不动（返回 false），说明它偏小，那就让它往上冒泡 (siftup_)！
        if(!sifdown_(tmp , n)){
            siftup_(tmp);
        }
    }
    
    //从哈希表里把这个可怜的节点记录彻底抹杀
    //问题4：以下的这两个函数调用删除的都是最后一个元素吗？原理是什么？
    ref_.erase(heap_.back().id);
    heap_.pop_back();
}

// 调整指定id的结点
void HeapTimer::adjust(int id , int newExpires)
{
    assert(!heap_.empty() && ref_.count(id)); // 确保堆不为空，且这个 id 存在

    // 1. 通过哈希表 O(1) 瞬间找到他在数组里的下标，更新绝对超时时间
    heap_[ref_[id]].expires = Clock::now() + MS(newExpires);

    // 2. 因为时间往后延了（值变大了），所以在小根堆里，他必须往下沉！
    sifdown_(ref_[id] , heap_.size());
} 

// 增加或者更新一个定时器
void HeapTimer :: add(int id , int timeOut , const TimeoutCallBack& cb)
{
    assert(id >= 0);
    
    // 场景 A：如果这个 id 已经在哈希表里了（老客户）,id是文件标识符fd的值
    if(ref_.count(id))
    {
        //哈希表ref_中存储了fd 和 它所在数组下标的对应关系
        int tmp = ref_[id];   //tmp表示他在数组中的下标是多少
        heap_[tmp].expires = Clock::now() + MS(timeOut);  // 续命
        heap_[tmp].cb = cb;

        if(!sifdown_(tmp , heap_.size()))
        {
            siftup_(tmp);
        }
    }

    // 场景 B：这是一个全新的客户连进来了！
    else{
        size_t n = heap_.size();

        ref_[id] = n;
        // { ... } 这是 C++11 的统一初始化列表，直接就地构造一个 TimerNode 对象塞进尾部！
        heap_.push_back({id , Clock::now() + MS(timeOut) ,cb});

        siftup_(n);
    }
}

// 删除指定id，并触发回调函数。
//问题5：为什么要触发回调函数
void HeapTimer :: doWork(int id)
{
    if(heap_.empty() || ref_.count(id) == 0)
    {
        return;
    }
    size_t i = ref_[id];
    auto node = heap_[i];

    node.cb();  // 触发回调函数
    del_(i);
}

//清除超时节点
void HeapTimer :: tick()
{
    if(heap_.empty())
    {
        return;
    }

    while(!heap_.empty())   
    {
        // 直接看向堆顶（数组第 0 个元素）！因为它是全场最快到期的！
        TimerNode node = heap_.front();

        // 计算堆顶节点的时间减去当前时间。
        // 如果大于 0，说明还没到期！
        if(std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0)
        {
        //连全场最快到期的都没到期，后面的节点绝对也没到期！直接 break 退出，极大地节省了 CPU！
            break;
        }
        // 如果运行到了这里，说明到期了！
        node.cb();   // 毫不留情地执行回调函数（切断客户连接）
        pop();   // 把堆顶节点删掉。
    }
}

void HeapTimer::pop()
{
    assert(!heap_.empty());
    del_(0);   // 删除堆顶元素（下标为 0）
}

void HeapTimer::clear()
{
    ref_.clear();   // 清空哈希表
    heap_.clear();   // 清空数组
}


//告诉主线程的 epoll_wait 函数：“你最多只能睡多久就要醒过来处理超时？”
int HeapTimer::GetNextTick()
{
    tick();  // 先干掉目前已经超时的
    size_t res = -1;  // 默认返回 -1（代表如果没有定时器，epoll 就可以一直死等阻塞下去）

    if(!heap_.empty())
    {   // 算出距离全场最快到期的那个定时器，还剩多少毫秒
        res = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();
        if(res < 0)
        {
            res = 0;
        }
    }
    return res;
}

