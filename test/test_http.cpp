// 文件路径: test/test_http.cpp
#include "../http/httprequest.h"
#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include <iostream>
#include <string>

void TestGetRequest() {
    std::cout << "========== 1. 测试标准 GET 请求 ==========" << std::endl;
    Buffer buff;
    
    // 伪造一段真实的浏览器 GET 请求报文 (注意 \r\n 换行符)
    std::string rawRequest = 
        "GET / HTTP/1.1\r\n"
        "Host: 127.0.0.1:1316\r\n"
        "Connection: keep-alive\r\n"
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64)\r\n"
        "\r\n"; // 必须有空行代表头部结束
        
    buff.Append(rawRequest); // 1. 先把报文存进缓冲区

    HttpRequest req;
    bool success = req.parse(buff); // 2. 丢给状态机去解析！

    if (success) {
        std::cout << "状态机解析 [成功]!" << std::endl;
        std::cout << "请求方法: " << req.method() << std::endl;
        // 验证 ParsePath_ 是否成功把 "/" 补全为了 "/index.html"
        std::cout << "请求路径: " << req.path() << std::endl; 
        std::cout << "HTTP版本: " << req.version() << std::endl;
        std::cout << "是否长连接: " << (req.IsKeepAlive() ? "是" : "否") << std::endl;
    } else {
        std::cout << "状态机解析 [失败]!" << std::endl;
    }
    std::cout << std::endl;
}

void TestPostRequest() {
    std::cout << "========== 2. 测试 POST 请求与 URL 编码解析 ==========" << std::endl;
    Buffer buff;
    
    // 伪造一段 POST 请求报文。
    // 注意：这里的路径我们故意写 /search，而不是 /login
    // 因为 /login 会触发你代码里的 UserVerify 去连数据库，
    // 如果你还没建好 MySQL 数据库，程序会在这里断言崩溃。
    std::string rawRequest = 
        "POST /search HTTP/1.1\r\n"
        "Host: 127.0.0.1:1316\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 35\r\n"
        "\r\n"
        "username=admin&password=123%20456"; 
        // %20 是空格的 URL 编码，用来测试你的 ConverHex 函数！

    buff.Append(rawRequest);

    HttpRequest req;
    bool success = req.parse(buff);

    if (success) {
        std::cout << "状态机解析 [成功]!" << std::endl;
        std::cout << "请求方法: " << req.method() << std::endl;
        std::cout << "请求路径: " << req.path() << std::endl; 
        std::cout << "提取 POST 账号: " << req.GetPost("username") << std::endl;
        // 如果解码成功，这里应该输出 123 456 (注意中间的空格)
        std::cout << "提取 POST 密码: " << req.GetPost("password") << std::endl; 
    } else {
        std::cout << "状态机解析 [失败]!" << std::endl;
    }
    std::cout << std::endl;
}

#if 0
int main() {
    // 1. 初始化日志系统 (必须有，否则 HTTPRequest 里的 LOG_DEBUG 会报错)
    Log::Instance()->init(0, "./log", ".log", 1024);

    // 2. 运行测试用例
    TestGetRequest();
    TestPostRequest();

    return 0;
}
#endif