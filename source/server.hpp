#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <memory>
#include <cstring>

#define BUFFER_DEFAULT_SIZE 1024

class Buffer
{
private:
    std::vector<char> _buffer; // 使用vector进行内存控件管理
    uint64_t _reader_idx;      // 读偏移
    uint64_t _writer_idx;      // 写偏移
public:
    Buffer() : _reader_idx(0), _writer_idx(0), _buffer(BUFFER_DEFAULT_SIZE) {}
    char *Begin() { return &*_buffer.begin(); }
    //  获取当前写位置的地址 ---  _buffer的空间起始地址，加上写偏移量
    char *WritePositon() { return Begin() + _writer_idx; }
    //  获取当前读位置地址 --- _写偏移之后的空闲空间，总体空间大小减去写偏移
    char *ReadPosition() { return Begin() + _reader_idx; }
    //  获取缓冲区末尾空闲空间大小 --- 写偏移之后的空闲空间，总体空间大小减去写偏移
    uint64_t TailIdleSize() { return _buffer.size() - _writer_idx; }
    //  获取缓冲区起始空闲空间大小 --- 读偏移之前的空闲空间
    uint64_t HeadIdleSize() { return _reader_idx; }
    //  将读位置向后移动指定长度
    void MoveReadOffset(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        _reader_idx += len;
    }
    //  将写位置向后移动指定长度
    void MoveWriteOffset(uint64_t len)
    {
        assert(len <= TailIdleSize() + HeadIdleSize());
        _writer_idx += len;
    }
    //  确保可写空间足够 (整体空闲空间够了就移动数据，否则就扩容)
    void EnsureWriteSpace(uint64_t len)
    {
        // 如果末尾空闲空间足够，直接返回
        if (TailIdleSize() >= len)
            return;
        // 末尾空闲不够，则判断加上起始位置的空闲空间大小是否足够，够了就将数据移动到起始位置
        if (len <= TailIdleSize() + HeadIdleSize())
        {
            // 将数据移动到起始位置
            uint64_t rsz = ReadAbleSize();
            std::copy(ReadPosition(), ReadPosition() + rsz, Begin()); // 把可读数据拷贝到起始位置
            _reader_idx = 0;
            _writer_idx = rsz;
        }
        else
        {
            // 总体空间不够，则需要扩容，不移动数据，直接给写偏移之后扩容足够的空间即可
            _buffer.resize(_writer_idx + len);
        }
    }
    // 写入数据
    void Write(const void *data, uint64_t len)
    {

        // 保证有足够空间，拷贝数据进去
        EnsureWriteSpace(len);
        const char *d = (const char*)(data);
        // std::cout << "const char* d: " << d << std::endl;
        std::copy(d, d + len, WritePositon());
    }
    void WriteAndPush(const void *data, uint64_t len)
    {
        Write(data, len);
        MoveWriteOffset(len);
    }
    void WriteString(const std::string &data)
    {
        return Write(data.c_str(), data.size());
    }
    void WriteStringAndPush(const std::string &data)
    {
        WriteString(data);
        // std::cout << << data << std::endl;
        MoveWriteOffset(data.size());
        // return Write(data.c_str(), data.size());
    }
    void WriteBuffer(Buffer &data)
    {
        return Write(data.ReadPosition(), data.ReadAbleSize());
    }
    void WriteBufferAndPush(Buffer &data)
    {
        WriteBuffer(data);
        // std::cout << data.ReadPosition() << std::endl;
        // Write(data.ReadPosition(), data.ReadAbleSize());
        MoveWriteOffset(data.ReadAbleSize());
    }
    // 读取数据
    void Read(void *buf, uint64_t len)
    {
        // 要求要获取的数据大小必须小于可读数据大小
        assert(len <= ReadAbleSize());
        std::copy(ReadPosition(), ReadPosition() + len, (char*)(buf));
    }
    void ReadAndPop(void *buf, uint64_t len)
    {
        Read(buf, len);
        MoveReadOffset(len);
    }
    std::string ReadAsString(uint64_t len)
    {
        // 要求要获取的数据大小必须小于可读数据大小
        assert(len <= ReadAbleSize());
        std::string str;
        str.resize(len);
        Read(&str[0], len);
        // std::cout << str << std::endl;
        return str;
    }
    std::string ReadAsStringAndPop(uint64_t len)
    {
        assert(len <= ReadAbleSize());
        // std::cout << len << std::endl;
        std::string str = ReadAsString(len);
        MoveReadOffset(len);
        return str;
    }
    //  获取可读数据大小 --- 写偏移 - 读偏移
    uint64_t ReadAbleSize() { return _writer_idx - _reader_idx; }

    char *FindCRLF() {
        char* res = (char*)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }

    // 同城获取一行数据，这种情况针对是
    std::string GetLine() {
        char* pos = FindCRLF();
        if (pos == nullptr) {
            return "";
        }
        // + 1是为了把换行字符也给取出来
        return ReadAsString(pos - ReadPosition() + 1);
    }
    std::string GetLineAndPop() {
        std::string str = GetLine();
        MoveReadOffset(str.size());                 
        return str;
    }
    //  清理功能
    void Clear()
    {
        // 只需要将偏移量归0即可
        _reader_idx = 0;
        _writer_idx = 0;
    }
};