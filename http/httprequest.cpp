#include "httprequest.h"

const std::unordered_set<std::string> HttpRequest::DEFAULT_HTML
{
    "/index" , "/register" , "/login",
    "/welcome" , "/video" , "/picture",
    "/search",
};

const std::unordered_map<std::string , int> HttpRequest :: DEFAULT_HTML_TAG
{
    {"/register.html" , 0},
    {"/login.html" , 1},
};

void HttpRequest::Init()
{
    method_ = path_ = version_ = body_ = "";
    state_ = REQUEST_LINE;
    header_.clear();
    post_.clear();
}

bool HttpRequest::IsKeepAlive() const
{
    // 如果头部包含 Connection 字段
    if(header_.count("Connection") == 1)
    {
        // 且它的值是 keep-alive，并且 HTTP 版本是 1.1（1.1默认长连接）
        return header_.find("Connection")->second == "keep-alive" && version_ == "1.1";
    }
    return false;
}

//解析处理
bool HttpRequest::parse(Buffer& buff)
{
    const char CRLF[] = "\r\n";   // 定义 HTTP 协议的换行标志
    if(buff.ReadableBytes() <= 0)  // 如果缓冲区连数据都没有，直接返回失败
    {
        return false;
    }

    // 只要有数据可读，且状态机还没有停下 (FINISH)，就一直循环处理！
    while (buff.ReadableBytes() && state_ != FINSH)
    {
        // 【关键搜索】：在 Buffer 从 Peek() 到 BeginWriteConst() 这个数据区间内，寻找 CRLF ("\r\n")。
        // search 是 C++ 标准库算法，返回找到的子串的首地址。
        const char* lineEnd = std::search(buff.Peek() , buff.BeginWriteConst() , CRLF ,CRLF+2);
        //问题3：search的作用是什么？用法是什么？

        // 把找到的这一行数据（不包括 \r\n）构造成一个 string 对象
        std::string line(buff.Peek() , lineEnd);

        switch (state_)
        {
        /*
        有限状态机，从请求行开始，每处理完后会自动转入到下一个状态    
        */
        case REQUEST_LINE:
            if(!ParseRequestLine_(line))  // 第一步：解析第一行
            {
                return false;   // 解析失败直接断开
            }
            ParsePath_();  // 第一行解析成功后，顺便处理一下路径
            break;     // 注意：状态的切换是在 ParseRequestLine_ 内部完成的！

        case HEADERS:
            ParseHeader_(line);       // 第二步：解析头部

            // 如果解析完头部后，发现后面没数据了（或者是空行 \r\n 占的 2 字节），说明没有 Body，提前结束！
            if(buff.ReadableBytes() <= 2)
            {
                state_ = FINSH;
            }
            break;
        case BODY:
            ParseBody_(line);  // 第三步：解析身体
            break;    
        default:
            break;
        }

        if(lineEnd == buff.BeginWrite())
        {
            break;
        }
        //这一行解析完了，手动把读指针往后挪，跳过 \r\n 这两个字符，准备读下一行
        buff.RetrieveUntil(lineEnd + 2);

    }

    // 成功解析出了一整个报文，打个日志庆祝一下
    LOG_DEBUG("[%s],[%s],[%s]" ,method_.c_str() , path_.c_str() , version_.c_str());
    return true; 
}


//解析各部分与正则表达 (Regex) 的魔法(解析路径)
void HttpRequest::ParsePath_()
{
    if(path_ == "/")
    {
        path_ = "/index.html";  // 用户只敲了域名没加路径，默认跳转到主页
    }
    else{
        // 如果用户敲了 /login，我们帮他自动补全为 /login.html
        for(auto &item:DEFAULT_HTML)
        {
            if(item == path_)
            {
                path_ += ".html";
                break;
            }
        }
    }
}

bool HttpRequest::ParseRequestLine_(const std::string& line)
{
    // 【正则表达式详解】：
    // ^ 表示匹配字符串的开头
    // ([^ ]*) 这是一个“捕获组”，匹配任何不是空格的连续字符。这里是用来抓取 "GET" 
    // 中间的空格就是严格匹配空格
    // $ 表示匹配字符串的结尾
    std::regex patten("^([^ ]*) ([^ ]*) HTTP/([^ ]*)$");
    std::smatch subMatch;   //存放匹配结果的数组。问题4：这是什么数据结构？

    //问题5：这个if语句是在干什么？
    if(std::regex_match(line ,subMatch ,patten))
    {
        method_ = subMatch[1];  // 第1个括号（对应上面patten定义的括号中的内容）抓到的: "GET" 或 "POST"
        path_ = subMatch[2];  // 第2个括号抓到的: "/index.html"
        version_ = subMatch[3];  // 第3个括号抓到的: "1.1"

        state_ = HEADERS;  // 【齿轮转动】：成功搞定第一行，将状态强制扭转为解析头部！
        return true;
    }
    LOG_ERROR("RequestLine Error");
    return false;
}

void HttpRequest::ParseHeader_(const std::string& line)
{
    // 正则：抓取 "Key: Value" 格式
    /*
    1：?代表前面的一个字符可以出现0次或1次
    2：*代表前面的一个字符可以出现0次或多次
    3：. 表示匹配除换行符 \n 之外的任何单字符，*表示零次或多次。
    所以.*在一起就表示任意字符出现零次或多次。
    4：^限定开头；取反：[^a]表示“匹配除了a的任意字符”，只要不是在[]里面都是限定开头
    */
    std::regex patten("^([^:]*): ?(.*)$");
    std::smatch subMatch;

    if(std::regex_match(line , subMatch , patten))
    {   //问题6：这是什么意思？怎样判断匹配成功
        header_[subMatch[1]] = subMatch[2]; // 匹配成功，存入哈希表 (例如 Host -> 127.0.0.1)
    }else{
        // 【非常巧妙】：HTTP 头部和身体之间有一个空行 \r\n。
        // 当读到空行时，没有任何冒号，正则表达式必然失败！
        // 一旦失败，说明头部全读完了，下面该读身体了！
        state_ = BODY;
    }
}

void HttpRequest::ParseBody_(const std::string& line)
{
    body_ = line;  // 把剩下的一长串都当做身体
    ParsePost_();  // 如果是 POST 请求，里面可能有表单数据，去处理它
    state_ = FINSH;  // 【齿轮转动】：报文全部解析完毕，机器停转！
    LOG_DEBUG("Body:%s, len:%d",line.c_str() , line.size());
}

//POST 表单处理与 URL 解码
// 辅助函数：把 ASCII 字符（如 'A'）变成真正的整数 (10)
int HttpRequest::ConverHex(char ch)
{
    if(ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    if(ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if(ch >= '0' && ch <= '9') return ch - '0';
    return ch;
}

void HttpRequest::ParsePost_()
{
    // 只有在方法是 POST，且数据格式是标准的 HTML 表单时才处理
    //问题6：application/x-www-form-urlencoded这是什么意思？
    if(method_ == "POST" && header_["Content-Type"] == "application/x-www-form-urlencoded")
    {
        ParseFromUrlencoded_();  // 去解析那一长串密密麻麻的 URL 编码

        if(DEFAULT_HTML_TAG.count(path_))  // 如果是在请求登录或注册页面
        {
            int tag = DEFAULT_HTML_TAG.find(path_)->second; // 取出标识符 (0 或 1)
            LOG_DEBUG("Tag:%d", tag);

            if(tag == 0 || tag == 1)
            {
                bool isLogin = (tag == 1);  // 判断到底是想登录还是想注册
                
                // 去数据库验证！如果验证成功，把请求路径偷偷改为欢迎页面！
                if(UserVerify(post_["username"] , post_["password"] , isLogin))
                {
                    path_ = "/welcome.html";
                }
                else { 
                    // 验证失败，重定向到错误页面
                    path_ = "/error.html";
                }
            }
        }
    }
}

std::string HttpRequest::UrlDecode(const std::string& str)
{
    std::string res;
    for(size_t i = 0; i < str.size(); i++)
    {
        if(str[i] == '%')
        {
            if(i+2 < str.size())
            {
                // 直接计算出真实的字符，存进结果字符串中！
                char ch = ConverHex(str[i+1]) * 16 + ConverHex(str[i+2]);
                res += ch;
                i += 2;
            }
        }else if(str[i] == '+')
        {
            res += ' ';  //'+'替换为空格
        }else{
            res += str[i];
        }
    }
    return res;
}

// 极其底层的字符串切片：解析 "username=admin&password=123" 这种格式
void HttpRequest::ParseFromUrlencoded_()
{
    if(body_.size() == 0) { return ;}

    std::string key , value;
    int num = 0;
    int n = body_.size();
    int i = 0 , j = 0;  //i不断往后移动，当遇到特殊符号需要存相应的值时，j记录下一个值开始的地方

    for(; i < n ; i ++)
    {
        char ch = body_[i];
        switch (ch)
        {
        case '=': // 遇到了等号，说明前面的那部分全是 key
            key = body_.substr(j , i - j);
            j = i + 1;  // 记录 value 开始的位置
            break;
        
        case '&':  // 遇到了与号，说明这个键值对结束了！
            value = body_.substr(j , i - j);  // 截取 value
            j = i + 1;  // 更新下一个 key 的起点
            post_[key] = UrlDecode(value);  // 截取出来之后，再进行解码，然后存入哈希表！
            LOG_DEBUG("%s = %s" , key.c_str() , value.c_str());
            break;
        default:
            break;
        }
    }
    assert(j <= i);    // 防御性编程
    /*
    由于上面的 switch 是在遇到 & 符号时，才把键值对存进 post_ 哈希表。
    但是，表单里的最后一个键值对，后面是没有 & 符号的！
    （比如 username=a&password=b，b后面啥也没有）。
     所以循环结束后，必须用这个 IF 判断一下：如果还有截取出来的 key 没存进去
     赶紧把它和最后那个 value 存进去，防止漏掉最后一个参数！
    */     
    if(post_.count(key) == 0 && j < i)
    {
        value = body_.substr(j , i - j);
        post_[key] = UrlDecode(value);
    }                                                                                                                                                                       
}

bool HttpRequest::UserVerify(const std::string& name , const std::string& pwd , bool isLogin)
{
    if(name == "" || pwd == "")
    {
        return false;
    }
    LOG_INFO("Verify name: %s pwd: %s" , name.c_str() , pwd.c_str());

    MYSQL* sql;
    // 【联动 SqlConnPool】：看这里！我们极其优雅的 RAII 机制登场了！
    // 借用一个连接去查询数据库，离开这个函数作用域时，连接会自动归还！

    SqlConnRAII(&sql , SqlConnPool::Instance());
    assert(sql);

    bool flag = false;
    unsigned int j = 0;
    char order[256] = { 0 };  // 存放要执行的 SQL 语句
    MYSQL_FIELD* fields = nullptr;
    MYSQL_RES* res = nullptr;  // 存放查询结果

    if(!isLogin)  // 如果是注册，默认设为可以注册，直到发现重名
    {
        flag = true;
    }

// 拼装 SQL 查询语句 (这里是直接拼接字符串，在真实的工业项目中会有 SQL 注入风险，但作为项目练习非常经典)

    snprintf(order , 256 , "SELECT username , password FROM user WHERE username='%s' LIMIT 1" , name.c_str());
    LOG_DEBUG("%S" , order);

    // mysql_query 执行这条 SQL。返回非 0 表示执行失败。
    if(mysql_query(sql , order))
    {
        mysql_free_result(res);
        return false;
    }
    res = mysql_store_result(sql);   // 把 MySQL 返回的结果交给res指针管理
    j = mysql_num_fields(res);  // 获取有多少列数据
    fields = mysql_fetch_field(res);    // 获取列名等元数据

    // 循环读取每一行结果 (其实上面加了 LIMIT 1，最多只有一行)
    while (MYSQL_ROW row = mysql_fetch_row(res))
    {
        LOG_DEBUG("MYSQL ROW : %s %s" , row[0] , row[1]);
        std::string password(row[1]);    // 获取数据库里存的真实密码

        if(isLogin)
        {
            if(pwd == password)
            {
                flag = true;  // 密码对上了！
            }else{
                flag = false;
                LOG_INFO("pwd error!");   //密码错误
            }
        }else {
            flag = false;  // 能查到结果，说明用户名已经存在了，不许注册！
            LOG_INFO("user exit!");
        }
    }
    
    mysql_free_result(res);   // 用完结果集必须释放内存！

    // 如果是注册逻辑，且刚才没查到同名用户 (flag == true)
    if(!isLogin && flag == true)
    {
        LOG_DEBUG("regirster!");
        bzero(order,256);  // 清空 order 数组
        // 拼装插入语句
        snprintf(order , 256 , "INSERT INTO user(username,password) VALUES('%s','%s')" , name.c_str() , pwd.c_str());
        LOG_DEBUG("%s" , order);
 
        if(mysql_query(sql , order))  // 执行写入操作
        {
            LOG_DEBUG("Insert error!");
            flag  =  false;
        }
        flag = true;
    }
     LOG_DEBUG("UserVerify success!!");
     return flag;   
}

std::string HttpRequest::path() const
{
    return path_;
}

std::string& HttpRequest::path()
{
    return path_;
}

std::string HttpRequest::method() const
{
    return method_;
}

std::string HttpRequest::version() const
{
    return version_;
}

std::string HttpRequest::GetPost(const std::string& key) const
{
    assert(key != "");
    if(post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::GetPost(const char* key) const
{
    assert(key != nullptr);
    if(post_.count(key) == 1)
    {
        return post_.find(key)->second;
    }
    return "";
}
