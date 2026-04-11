#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include<string>
#include<regex>
#include<unordered_map>
#include<unordered_set>
#include<mysql/mysql.h>

#include "../buffer/buffer.h"      // 联动：读取缓冲区的数据
#include "../log/log.h"             // 联动：打日志
#include "../pool/sqlconnpool.h"        // 联动：借数据库连接用来验证账号密码

class HttpRequest
{
public:
    // 1. 定义有限状态机的所有状态
    enum PARSE_STATE
    {
        REQUEST_LINE,  // 正在解析请求行
        HEADERS,         // 正在解析请求头
        BODY,           // 正在解析请求体
        FINSH,          // 解析完成
    };

    HttpRequest()  // 构造函数，一创建就初始化
    {
        Init();
    }
    ~HttpRequest() = default;

    void Init();
    bool parse(Buffer& buff);

    std::string path() const;
    std::string& path();
    std::string method() const;
    std::string version() const;
    std::string GetPost(const std::string& key) const;    //获取表达数据
    std::string GetPost(const char* key) const; //问题1：为什么要重载？ 

    bool IsKeepAlive() const;    //判断是不是长连接
    


private:
    // 状态机处理流水线（按顺序调用）
    bool ParseRequestLine_(const std::string& line);   //处理请求行
    void ParseHeader_(const std::string& line);   //处理请求头
    void ParseBody_(const std::string& line);   //处理请求体

    // 辅助解析函数
    void ParsePath_();   // 补全路径（比如把 "/" 变成 "/index.html"）
    void ParsePost_();    // 专门处理 POST 请求带来的数据
    void ParseFromUrlencoded_();      // 解码浏览器传来的 URL 编码（比如 %20 转为空格）

    // 核心业务：去 MySQL 数据库里查账号密码
    static bool UserVerify(const std::string& name , const std::string& pwd , bool isLogin);  //用户验证

    PARSE_STATE state_;   // 当前状态机的状态指针

    // 存储解析出来的数据
    std::string method_ , path_ , version_ , body_;
    std::unordered_map<std::string , std::string> header_;    // 存请求头 (键值对)
    std::unordered_map<std::string , std::string> post_;  // 存 POST 数据 (键值对)   问题2：为什么请求头和POST数据都是键值对

    // 静态常量，存放默认允许访问的 HTML 页面，提高安全性和效率
    static const std::unordered_set<std::string> DEFAULT_HTML;
    static const std::unordered_map<std::string ,int> DEFAULT_HTML_TAG;

    static int ConverHex(char ch);     // 辅助函数：把字母 A-F 转成数字 10-15

};

#endif