#ifndef HTTP_RESPONSE_H
#define HTTP_RESPONSE_H

#include<unordered_map>
#include<fcntl.h>    //open
#include<unistd.h>    //close
#include<sys/stat.h>   //stat
#include<sys/mman.h>    //mmap , munmap

#include "../buffer/buffer.h"
#include "../log/log.h"

class HttpResponse
{
public:
    HttpResponse();
    ~HttpResponse();

    void Init(const std::string& srcDir , std::string& path , bool isKeepAlive = false , int code = -1);
    void MakeResponse(Buffer& buff);    //生成响应,从缓存区取数据

    void UnmapFile();     // 解除内存映射
    char* File();    // 获取映射后的文件内存指针
    size_t FileLen() const;    //获取文件大小
    
    void ErrorContent(Buffer& buff , std::string message);
    int Code() const { return code_; }


private:
    int code_;  // HTTP 状态码 (200成功, 404找不到, 403无权限)

    // HTTP 报文拼装流水线
    void AddStateLine_(Buffer& buff);
    void AddHeader_(Buffer& buff);
    void AddContent_(Buffer& buff);

    void ErrorHtml_();
    std::string GetFileType_();

    bool isKeepAlive_;
    std::string path_;    // 请求的文件路径 (如 /index.html)
    std::string srcDir_;    // 服务器的根目录 (如 /home/user/web/resources)

    char* mmFile_;   // 指向文件在内存中映射位置的指针！极其关键！
    struct stat mmFlieStat_;  // 存放文件属性（大小、权限等）的结构体

    static const std::unordered_map<std::string , std::string> SUFFIX_TYPE;    // 后缀类型集 -> MIME类型
    static const std::unordered_map<int , std::string> CODE_STATUS;  // 状态码 -> 状态描述,
    static const std::unordered_map<int , std::string> CODE_PATH;    // 状态码 -> 对应的错误页面路径

};


#endif