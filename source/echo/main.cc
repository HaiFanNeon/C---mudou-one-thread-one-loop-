#include "echo.hpp"

int main() {
    EchoServer server(9999);
    server.Start();
    return 0;
}