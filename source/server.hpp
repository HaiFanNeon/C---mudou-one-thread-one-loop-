#pragma once
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>
#include <thread>
#include <mutex>

#include <cassert>
#include <cstring>

#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include "LOG.hpp"

#define MAX_LISTEN 10
#define BUFFER_DEFAULT_SIZE 1024
#define MAX_EPOLLEVENTS 1024

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
        const char *d = (const char *)(data);
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
        std::copy(ReadPosition(), ReadPosition() + len, (char *)(buf));
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

    char *FindCRLF()
    {
        char *res = (char *)memchr(ReadPosition(), '\n', ReadAbleSize());
        return res;
    }

    // 同城获取一行数据，这种情况针对是
    std::string GetLine()
    {
        char *pos = FindCRLF();
        if (pos == nullptr)
        {
            return "";
        }
        // + 1是为了把换行字符也给取出来
        return ReadAsString(pos - ReadPosition() + 1);
    }
    std::string GetLineAndPop()
    {
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

class Socket
{
private:
    int _sockfd;

public:
    Socket() : _sockfd(-1) {}
    Socket(int fd) : _sockfd(fd) {}
    ~Socket() { Close(); }
    int Fd() { return _sockfd; }
    // 创建套接字
    bool Create()
    {
        // socket(int domain, int type, int protocol)
        _sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_sockfd == -1)
        {
            ERR_LOG("CREATE SOCKET FAILED!!");
            return false;
        }
        return true;
    }
    // 绑定地址信息
    bool Bind(const std::string &ip, const uint64_t port)
    {
        // bind(int fd, struct sockaddr* addr, socklen_t len);
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr.s_addr);
        socklen_t len = sizeof(struct sockaddr_in);

        int ret = bind(_sockfd, (struct sockaddr *)&addr, len);
        if (ret == -1)
        {
            ERR_LOG("BIND ADDRESS FAILED!!");
            return false;
        }
        return true;
    }
    // 监听
    bool Listen(int backlog = MAX_LISTEN)
    {
        // listen(int fd, int backlog);
        int ret = listen(_sockfd, backlog);
        if (ret == -1)
        {
            ERR_LOG("SOCKET LISTEN FAILED!!");
            return false;
        }
        return true;
    }
    // 向服务器发起连接
    bool Connect(const std::string &ip, uint16_t &port)
    {
        // int connect(int sockfd, const struct sockaddr *addr,socklen_t addrlen);
        struct sockaddr_in addr;
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, ip.c_str(), &addr.sin_addr.s_addr);
        socklen_t len = sizeof(struct sockaddr_in);
        int ret = connect(_sockfd, (const struct sockaddr *)&addr, len);
        if (ret == -1)
        {
            // std::cout << ret << std::endl;
            ERR_LOG("CONNECT SERVER FAILED!!");
            return false;
        }
        return true;
    }
    // 获取新连接
    int Accept()
    {
        // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
        int newfd = accept(_sockfd, nullptr, nullptr);
        if (newfd == -1)
        {
            ERR_LOG("SOCKET ACCEPT FAILED!!");
            return -1;
        }
        return newfd;
    }
    // 接收数据
    ssize_t Recv(void *buf, size_t len, int flag = 0)
    {
        // ssize_t recv(int sockfd, void *buf, size_t len, int flags);
        ssize_t ret = recv(_sockfd, buf, len, flag);
        if (ret <= 0)
        {
            // EAGAIN 当前socket的接收缓冲区中没有数据了，在非阻塞的情况下才会有这个错误
            // EINTR 表示当前socket的阻塞等待，被信号打断了
            if (errno == EAGAIN || errno == EINTR)
            {
                return 0; // 表示这次接收没有接收到数
            }
            ERR_LOG("SOCKET RECV FAILED!!");
            return -1;
        }
        return ret;
    }
    ssize_t NonBlockRecv(void *buf, size_t len)
    {
        return recv(_sockfd, buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前接收为非阻塞
    }
    ssize_t NonBlockSend(void *buf, size_t len)
    {
        return send(_sockfd, buf, len, MSG_DONTWAIT); // MSG_DONTWAIT 表示当前接收为非阻塞
    }
    // 发送数据
    ssize_t Send(const void *buf, size_t len, int flag = 0)
    {
        // ssize_t send(int sockfd, const void *buf, size_t len, int flags);
        ssize_t ret = send(_sockfd, buf, len, flag);
        if (ret < 0)
        {
            ERR_LOG("SOCKET SEND FAILED!!");
            return -1;
        }
        return ret;
    }
    // 关闭套接字
    void Close()
    {
        if (_sockfd != -1)
        {
            close(_sockfd);
            _sockfd = -1;
        }
        return;
    }
    // 创建服务端连接
    bool CreateServer(uint16_t port, const std::string &ip = "0,0,0,0", bool block_flag = false)
    {
        if (!Create())
            return false;
        if (block_flag)
            NonBlock();
        if (!Bind(ip, port))
            return false;
        if (!Listen())
            return false;
        ReuseAddress();
        return true;
    }
    // 创建一个客户端连接
    bool CreateClient(uint16_t port, const std::string &ip)
    {
        // 创建套接字 连接服务器
        if (!Create())
            return false;
        if (!Connect(ip, port))
            return false;
        return true;
    }
    // 设置套接字选项 --- 开启地址端口重用
    void ReuseAddress()
    {
        int val = 1;
        setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, (void *)&val, sizeof(int));
    }
    // 设置套接字阻塞属性 --- 设置为非阻塞
    void NonBlock()
    {
        int flag = fcntl(_sockfd, F_GETFL, 0);
        fcntl(_sockfd, F_SETFL, flag | O_NONBLOCK);
    }
};

class Poller;


class EventLoop;
// 对于文件描述符进行监控事件管理
class Channel
{
private:
    int _fd;
    Poller *_poller;
    EventLoop *_loop;
    uint32_t _events;  // 当前需要监控的事件
    uint32_t _revents; // 当前连续触发的事件
    using EventCallback = std::function<void()>;
    EventCallback _read_callback;  // 可读事件回调被触发的回调
    EventCallback _write_callback; // 可写事件回调被触发的回调
    EventCallback _error_callback; // 错误事件回调被触发的回调
    EventCallback _close_callback; // 连接断开事件被触发的回调
    EventCallback _event_callback; // 任意时间被触发的回调
public:
    Channel(EventLoop *loop, int fd) : _fd(fd), _loop(loop), _events(0), _revents(0) {}
    int Fd() { return _fd; }
    void SetREvents(uint32_t events) { _revents = events; }; // 设置实际就绪的事件
    uint32_t Events() { return _events; }                    // 获取想要监控的事件
    void SetReadCallback(const EventCallback &cb) { _read_callback = cb; }
    void SetWriteCallback(const EventCallback &cb) { _write_callback = cb; }
    void SetErrorCallback(const EventCallback &cb) { _error_callback = cb; }
    void SetCloseCallback(const EventCallback &cb) { _close_callback = cb; }
    void SetEventCallback(const EventCallback &cb) { _event_callback = cb; }
    // 当前是否可读
    bool ReadAble()
    {
        return (_events & EPOLLIN);
    }
    // 当前是否可写
    bool WriteAble()
    {
        return (_events & EPOLLOUT);
    }
    // 启动读事件
    void EnableRead()
    {
        _events |= EPOLLIN;
        // 后面会添加到EventLoop的事件监控中
        Update();
    }
    // 启动写事件
    void EnableWrite()
    {
        _events |= EPOLLOUT;
        Update();
    }
    // 关闭写事件监控
    void DisableRead()
    {
        _events &= ~EPOLLIN;
        Remove();
    }
    // 关闭读事件监控
    void DisableWrite()
    {
        _events &= ~EPOLLOUT;
        Remove();
    }
    // 关闭所有事件监控
    void DisableAll()
    {
        _events = 0;
        Remove();
    }
    // 移除监控
    void Remove();
    void Update();
    // 事件处理， 一旦连接触发了事件，就调用这个函数，自己触发了什么事件如何处理自己决定
    void HandleEvent()
    {
        if ((_revents & EPOLLIN) || (_revents & EPOLLRDHUP) || (_revents & EPOLLPRI))
        {
            // 不管任何事件，都调用的回调函数
            if (_event_callback)
                _event_callback();
            if (_read_callback)
                _read_callback();
        }
        // 有可能会释放连接的操作事件， 一次只处理一个
        if (_revents & EPOLLOUT)
        {
            if (_write_callback)
                _write_callback();
            // 不管任何事件，都调用的回调函数
            if (_event_callback)
                _event_callback(); // 放到事件处理完毕后调用，刷新活跃度
        }
        else if (_revents & EPOLLERR)
        {
            if (_event_callback)
                _event_callback();
            // 一旦出错，就会释放连接，因此要放到前边调用任意回调，刷新活跃度
            if (_error_callback)
                _error_callback();
        }
        else if (_revents & EPOLLHUP)
        {
            if (_event_callback)
                _event_callback();

            if (_close_callback)
                _close_callback();
        }
        // if (_event_callback)
        //     _event_callback();
    }
};



class Poller
{
private:
    int _epfd;
    struct epoll_event _evs[MAX_EPOLLEVENTS];
    std::unordered_map<int, Channel *> _channel;

private:
    // 对epoll的直接操作
    void Update(Channel *channel, int op)
    {
        // int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
        int fd = channel->Fd();
        struct epoll_event ev;
        ev.data.fd = fd;
        ev.events = channel->Events();
        int ret = epoll_ctl(_epfd, op, fd, &ev);
        if (ret < 0)
        {
            ERR_LOG("EPOLLCTL FAILED!!");
            abort(); // 退出程序
        }
        return;
    }
    // 判断一个Channel是否已经添加了事件监控
    bool HasChannel(Channel *channel)
    {
        auto it = _channel.find(channel->Fd());
        if (it == _channel.end())
        {
            return false;
        }
        return true;
    }

public:
    Poller()
    {
        _epfd = epoll_create(MAX_EPOLLEVENTS);
        if (_epfd < 0)
        {
            ERR_LOG("EPOLL CREATE FAILED!!");
            abort(); // 这里有异常直接退出
        }
    }
    // 添加或修改监控事件
    void UpdateEvent(Channel *channel)
    {
        bool ret = HasChannel(channel);
        if (!ret)
        {
            // 不存在则添加
            _channel.insert(std::make_pair(channel->Fd(), channel));
            return Update(channel, EPOLL_CTL_ADD);
        }
        return Update(channel, EPOLL_CTL_MOD);
    }
    // 移除监控
    void RemoveEvent(Channel *channel)
    {
        auto it = _channel.find(channel->Fd());
        if (it != _channel.end())
        {
            _channel.erase(it);
        }
        Update(channel, EPOLL_CTL_DEL);
    }
    // 开始监控，返回活跃连接
    void Poll(std::vector<Channel *> *active)
    {
        // int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
        int nfds = epoll_wait(_epfd, _evs, MAX_EPOLLEVENTS, -1);
        if (nfds < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            ERR_LOG("EPOLL WAIT ERROR:%s\n", strerror(errno));
        }
        for (int i = 0; i < nfds; i++)
        {
            auto it = _channel.find(_evs[i].data.fd);
            assert(it != _channel.end());
            it->second->SetREvents(_evs[i].events); // 设置实际就绪的事件
            active->push_back(it->second);
        }
        return;
    }
};



class EventLoop
{
private:
    using Functor = std::function<void()>;
    std::thread::id _thread_id;  // 线程ID
    int _event_fd;               // eventfd 唤醒IO事件监控有可能导致的阻塞
    Poller _poller;              // 进行所有描述符的事件监控
    std::vector<Functor> _tasks; // 任务池
    std::mutex _mutex;           // 实现任务池操作的线程安全
    std::unique_ptr<Channel> _event_channel;

public:
    // 执行任务池中的所有任务
    void RunAllTask()
    {
        // 任务队列处理机制，确保多线程环境下对任务队列的安全访问和执行。
        std::vector<Functor> functor;
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.swap(functor);
        }
        for (auto &f : functor)
        {
            f();
        }
        return;
    }

    static int CreateEventFd()
    {
        int efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (efd < 0)
        {
            ERR_LOG("CREATE EVENTFD TAILED!!");
            abort();
        }
        return efd;
    }
    void ReadEventFd()
    {
        uint64_t res = 0;
        int ret = read(_event_fd, &res, sizeof(res));
        if (ret < 0)
        {
            // EINTR --- 被信号打断
            // EAGAIN --- 无数据可读
            if (errno == EINTR || errno == EAGAIN)
            {
                return;
            }
            ERR_LOG("READ EVENTFD FAILED!!");
            abort();
        }
        return;
    }
    void WeakUpEventFd()
    {
        uint64_t val = 1;
        int ret = write(_event_fd, &val, sizeof(val));
        if (ret < 0)
        {
            if (errno == EINTR)
            {
                return;
            }
            ERR_LOG("READ EVENTFD FAILED!!");
            abort();
        }
        return;
    }

public:
    EventLoop()
        : _thread_id(std::this_thread::get_id()), _event_fd(CreateEventFd()), _event_channel(new Channel(this, _event_fd))
    {
        // 给evenfd添加可读事件回调函数，读取eventfd事件通知次数
        _event_channel->SetReadCallback(std::bind(&EventLoop::ReadEventFd, this));
        _event_channel->EnableRead();
    }
    // 三步走 -> 事件监控 --- 就绪事件处理 --- 执行任务
    void Start()
    {
        // 事件监控
        std::vector<Channel *> actives;
        _poller.Poll(&actives);
        // 就绪事件处理
        for (auto &channel : actives)
        {
            channel->HandleEvent();
        }
        // 执行任务
        RunAllTask();
    }
    // 用于判断当前线程是否是EventLoop对应的线程
    bool IsInLoop()
    {
        return _thread_id == std::this_thread::get_id();
    }
    // 判断当前要执行的任务是否处于房前线程中，如果是则执行，不是则压入队列
    void RunInLoop(const Functor &cb)
    {
        if (IsInLoop())
        {
            return cb();
        }
        return QueueInLoop(cb);
    }
    // 将操作压入线程池
    void QueueInLoop(const Functor &cb)
    {
        {
            std::unique_lock<std::mutex> _lock(_mutex);
            _tasks.push_back(cb);
        }
        // 唤醒有可能因为没有事件
        // 其实就是给eventfd写入一个数据，eventfd就会触发可读事件
        WeakUpEventFd();
    }
    // 添加/修改描述符的监控事件
    void UpdateEvent(Channel *channel)
    {
        return _poller.UpdateEvent(channel);
    }
    // 移除描述符的监控
    void RemoveEvent(Channel *channel)
    {
        return _poller.RemoveEvent(channel);
    }
};

void Channel::Remove()
{
    return _loop->RemoveEvent(this);
}
void Channel::Update()
{
    return _loop->UpdateEvent(this);
}