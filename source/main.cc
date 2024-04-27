#include "server.hpp"





int main()
{
    Buffer buf;
    for (int i = 0; i < 300; i++)
    {
        std::string str = "hello!" + std::to_string(i) + '\n';
        buf.WriteStringAndPush(str);
    }

    // buf.WriteStringAndPush(str);

    // Buffer buf1;
    // buf1.WriteBufferAndPush(buf);

    // std::string t;
    // t = buf1.ReadAsStringAndPop(buf1.ReadAbleSize());

    // std::cout << "string t: " << t << std::endl;
    // std::cout << buf.ReadAbleSize() << std::endl;
    // std::cout << buf1.ReadAbleSize() << std::endl;
    return 0;
}