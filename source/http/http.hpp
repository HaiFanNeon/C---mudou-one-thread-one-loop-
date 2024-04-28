#pragma once

#include "../server.hpp"

class Util
{
public:
    // 拆分字符串，将字符串按照sep字符进行分割，得到各个字符串放到arry中，最终返回子串的数量
    static size_t Split(const std::string &src, const std::string &sep, std::vector<std::string> *arry)
    {
        size_t offset = 0;
        while (offset < src.size())
        {
            // 在src字符串偏移量offset处，开始向后查找sep字符/字串，返回查找到的位置
            size_t pos = src.find(sep, offset);
            if (pos == std::string::npos) {
                if (pos == src.size()) break;
                // 将剩余的部分当作一个子串，放入arry中
                arry->push_back(src.substr(offset));
                return arry->size();
            }
            if (pos == offset) {
                offset = pos + sep.size();
                continue; // 当前子串是一个空串，没有内容
            }
            arry->push_back(src.substr(offset, pos - offset));
            offset = pos + sep.size();
        }
        return arry->size();
    }
    // 读取文件内容
    static void ReadFile();
    // 向文件写入内容
    static void WriteFile();
    // URL编码
    static void UrlEncode();
    // URL解码
    static void UrlDecode();
    // 响应状态码的描述信息获取
    static std::string StatuDesc();
    // 根据文件后缀名获取文件mime
    static std::string ExtMime();
    // 判断一个文件是否是一个目录
    static bool IsDirectory();
    // 判断一个文件是否是一个普通文件
    static bool IsRegular();
    // http请求的资源路径有效性判断
    static bool ValidPath();
}