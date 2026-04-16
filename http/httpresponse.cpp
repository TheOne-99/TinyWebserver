#include "httpresponse.h"

// 告诉浏览器你发的是啥类型的数据。比如 .html 对应 text/html，.png 对应 image/png
const std::unordered_map<std::string , std::string> HttpResponse :: SUFFIX_TYPE = {
    { ".html",  "text/html" },
    { ".xml",   "text/xml" },
    { ".xhtml", "application/xhtml+xml" },
    { ".txt",   "text/plain" },
    { ".rtf",   "application/rtf" },
    { ".pdf",   "application/pdf" },
    { ".word",  "application/nsword" },
    { ".png",   "image/png" },
    { ".gif",   "image/gif" },
    { ".jpg",   "image/jpeg" },
    { ".jpeg",  "image/jpeg" },
    { ".au",    "audio/basic" },
    { ".mpeg",  "video/mpeg" },
    { ".mpg",   "video/mpeg" },
    { ".avi",   "video/x-msvideo" },
    { ".gz",    "application/x-gzip" },
    { ".tar",   "application/x-tar" },
    { ".css",   "text/css "},
    { ".js",    "text/javascript "},
};

// 状态码翻译字典。200 对应 "OK"，404 对应 "Not Found"
const std::unordered_map<int , std::string> HttpResponse :: CODE_STATUS = {
    { 200, "OK" },
    { 400, "Bad Request" },
    { 403, "Forbidden" },
    { 404, "Not Found" },
};

// 错误路由。如果发生 404，就去加载 /404.html 给用户看
const std::unordered_map<int, std::string> HttpResponse::CODE_PATH = {
    { 400, "/400.html" },
    { 403, "/403.html" },
    { 404, "/404.html" },
};

HttpResponse :: HttpResponse()
{
    code_ = -1;
    path_ = srcDir_ = "";
    isKeepAlive_ = false;
    mmFile_ = nullptr;
    mmFlieStat_ = { 0 };
}

HttpResponse :: ~HttpResponse()
{
    UnmapFile();
}

void HttpResponse :: Init(const std::string& srcDir , std::string& path , bool isKeepAlive , int code)
{
    assert(srcDir != "");
    if(mmFile_) 
    {
        UnmapFile();
    }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr;
    mmFlieStat_ = {0};
}

//生成响应 (MakeResponse)
void HttpResponse::MakeResponse(Buffer& buff)
{
    /* 1. 检查请求的文件到底存不存在，能不能读！ 
   (srcDir_ + path_).data() 拼接出绝对路径，比如 /home/web/index.html
    stat() 去磁盘查这个文件，把信息存在 mmFileStat_ 里。如果返回 < 0，说明文件不存在；
    S_ISDIR 判断是不是个文件夹（不能直接把文件夹当文件发出去）
    */
   if(stat((srcDir_ + path_).data() , &mmFlieStat_) < 0  || S_ISDIR(mmFlieStat_.st_mode))
   {
    //问题1：if判断语句中是什么意思？stat的具体用法是什么？
    code_  = 404;
   }
   // 判断权限：S_IROTH 表示 "其他用户是否有读权限"。如果没有，报 403 权限拒绝
   else if(!(mmFlieStat_.st_mode & S_IROTH))
   {
        code_ = 403;
   }
   else if(code_ == -1)
   {
    code_ = 200;   // 一切正常，给 200
   }

   ErrorHtml_();   // 如果 code_ 是 404/403，偷偷把 path_ 换成对应的错误提示页面
   AddStateLine_(buff);      // 拼装第一行：HTTP/1.1 200 OK\r\n
   AddHeader_(buff);    // 拼装头部：Connection, Content-type 等\r\n
   AddContent_(buff);   // 拼装身体：准备发送文件！
}

void HttpResponse::ErrorHtml_()
{
    // CODE_PATH 是我们定义的静态哈希表，存着 400->"/400.html", 404->"/404.html"
    // count(code_) 用来检查当前的错误码在不在这个表里
    if(CODE_PATH.count(code_) == 1)
    {
        // 如果在，就把原本错误的路径 path_，强行替换成对应的错误页面路径
        path_ = CODE_PATH.find(code_)->second;

        // 极其细节的一步：既然要发新的错误页面给用户，必须重新调用 stat 函数！
        // 去磁盘里查一下这个 404.html 到底有多大，把它的属性重新存入 mmFileStat_，
        // 否则一会发文件的时候，大小就全乱了！
        stat((srcDir_ + path_).data() , &mmFlieStat_);
    }
}

void HttpResponse :: AddStateLine_(Buffer& buff)
{
    std::string status;   // 准备一个字符串装状态描述（"OK" 或 "Not Found"）
    // CODE_STATUS 也是个静态哈希表，检查当前 code_ 在不在里面
    if(CODE_STATUS.count(code_) == 1)
    {
        // 在的话，取出对应的描述。比如 200 就取出 "OK"
        status = CODE_STATUS.find(code_)->second;
    }
    else {
        // 万一遇到一个奇葩的状态码（不认识），统一当做 400 请求错误处理
        code_ = 400;
        status = CODE_STATUS.find(400)->second;  // 取出 "Bad Request"
    }
    // 【核心拼装】：把协议版本、状态码、描述词拼接在一起，末尾加上极其重要的回车换行 \r\n
    // buff.Append() 就是调用第一天写的 Buffer，把这串字塞进内存里！
    buff.Append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

std::string HttpResponse::GetFileType_()
{
    // find_last_of('.')：从路径字符串的最后往前找，找到最后一个点 '.' 的位置（下标）
    // 比如 "/image/cat.png"，就会找到 ".png" 中点的位置
    std::string::size_type idx = path_.find_last_of('.');

    // 如果返回的是 string::npos（这是C++标准库定义的一个极大值，代表没找到）
    // 说明这个文件连后缀名都没有！

    if(idx == std::string::npos)
    {
        return "text/plain";   // 没后缀就当做纯文本 (text/plain) 对待
    }

    // substr(idx)：从那个点开始，把后面的字符串全切下来。suffix 此时等于 ".png"
    std::string suffix = path_.substr(idx);


    // SUFFIX_TYPE 是存着各种后缀对应 HTTP 标准类型的哈希表
    if(SUFFIX_TYPE.count(suffix) == 1)
    {
        // 如果认识这个后缀，就返回对应的标准名称（比如 "image/png"）
        return SUFFIX_TYPE.find(suffix)->second;
    }
    
    // 默认 fallback：不认识的后缀，也统统当纯文本处理
    return "text/plain";
}

void HttpResponse :: AddHeader_(Buffer& buff)
{
    buff.Append("Connection: ");
    // 根据上一节 HttpRequest 解析出的结果，看看客户端支不支持长连接
    if(isKeepAlive_)
    {
        buff.Append("keep-alive\r\n");  // 告诉浏览器：我也支持长连接！
        // 这一行是长连接的参数：最多保持6次请求不断开，如果闲置了 120 秒就强制断开
        buff.Append("keep-Alive: max=6, timeout=120\r\n");
    }else{
        // 如果不支持，告诉浏览器：发完这条我就关了 (close)
        buff.Append("close\r\n");
    }
    // 写入 Content-type 字段，调用上面刚讲的 GetFileType_ 拿到文件格式
    buff.Append("Content-type: " + GetFileType_() + "\r\n");
}

void HttpResponse::AddContent_(Buffer& buff)
{
    // open 是 Linux 系统调用，O_RDONLY 以只读模式打开磁盘上的文件
    // (srcDir_ + path_).data()：拼接绝对路径并转为 C 风格字符串
    int srcFd = open((srcDir_ + path_).data() , O_RDONLY);

    if(srcFd < 0)  // 如果打开失败（比如文件损坏或被删了）
    {
        // 调用下面马上要讲的 ErrorContent 生成一段紧急的 HTML 报错代码
        ErrorContent(buff , "File NotFound");
        return;   // 文件打不开，直接退出，下面不搞了
    }

    // mmap 魔法：把刚才打开的磁盘文件，直接映射到操作系统的内存中！
    // PROT_READ 表示这块内存只读，MAP_PRIVATE 表示私有映射。
    // mmRet 会拿到这块内存的首地址指针
    //问题2：mmap的用法是什么？这里为什么要用mmap？
    /*
    int* mmRet = (int*)mmap(0 , mmFlieStat_.st_size , PROT_READ , MAP_PRIVATE ,srcFd , 0);

     2. 🌟 mmap (Memory Map)：将磁盘文件直接映射到内存！🌟
   参数1：0（让操作系统自动分配内存地址）
    参数2：文件大小
    参数3：PROT_READ（这块内存是只读的）
    参数4：MAP_PRIVATE（私有映射，读操作不会影响别处）
    参数5：srcFd（刚刚打开的文件句柄）
   参数6：0（偏移量，从文件头开始映射）
    
    if(*mmRet == -1)   // -1 在 mmap 里代表映射失败 (MAP_FAILED)
    {
        ErrorContent(buff, "File NotFound!");
        return;
    }
    */
    void* mmRet = mmap(0, mmFlieStat_.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if (mmRet == MAP_FAILED) {
    ErrorContent(buff, "File NotFound!");
    return;
        }

    // 将拿到的内存地址保存给类的成员变量 mmFile_
    mmFile_ = (char*)mmRet;
    close(srcFd);  // 文件内容已经在内存里了，立刻关闭文件句柄，节省操作系统的句柄资源

    // 【最关键的一句】：
    // 此时此刻，文件的数据并没有装进 buff 里！！
    // buff 里只追加了一行："Content-length: 1024\r\n\r\n"
    // 注意结尾的两个 \r\n\r\n！在 HTTP 协议里，连续两个换行代表“所有的头部信息到此结束，下面全是纯数据！”
    buff.Append("Content-length: " + std::to_string(mmFlieStat_.st_size) + "\r\n\r\n");
}

void HttpResponse::ErrorContent(Buffer& buff , std::string message)
{
    std::string body;   // 存放 HTML 网页的身体
    std::string status;   // 存放状态描述

    // 纯手工一行一行拼写 HTML 代码！
    body += "<html><title>Error</title>"; // 网页标题栏显示 Error
    body += "<body bgcolor=\"ffffff\">";  // 网页背景设为白色
    
    // 获取错误码描述
    if (CODE_STATUS.count(code_) == 1) {
        status = CODE_STATUS.find(code_)->second;
    } else {
        status = "Bad Request";
    }
    
    // 网页大字：打印错误码和描述，比如 "404 : Not Found"
    body += std::to_string(code_) + " : " + status + "\n"; 
    // 网页小字：打印传入的具体错误原因（比如传入的 "File NotFound!"）
    body += "<p>" + message + "</p>"; 
    // 画一条横线 (<hr>)，并签上我们服务器的大名 (TinyWebServer)
    body += "<hr><em>TinyWebServer</em></body></html>"; 

    // 因为是硬拼的字符串，我们自己可以算出它有多长 (body.size())
    // 告诉浏览器这串 HTML 代码有多长，外加 \r\n\r\n 宣告头部结束
    buff.Append("Content-length: " + std::to_string(body.size()) + "\r\n\r\n");
   
    // 最后，把拼好的 HTML 代码，原原本本地塞进 Buffer 里！
    // 这里的场景下，网页数据是真的进了 Buffer 的，而没用 mmap！
    buff.Append(body);

}

// 返回映射文件的内存指针
char* HttpResponse::File() {
    return mmFile_;
}

// 返回映射文件的大小
size_t HttpResponse::FileLen() const {
    return mmFlieStat_.st_size;
}

// 解除内存映射（极其重要，防止内存泄漏）
void HttpResponse::UnmapFile() {
    if (mmFile_) {
        munmap(mmFile_, mmFlieStat_.st_size);
        mmFile_ = nullptr;
    }
}