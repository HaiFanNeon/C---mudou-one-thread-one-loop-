#include "timewheel.hpp"

class Test {
    public:
        Test() {
            std::cout << "构造" << std::endl;
        }
        ~Test() {
            std::cout << "析构" << std::endl;
        }
};

void Del(Test* t) {
    delete t;
}


int main() {
    
    TimerWheel tw;

    Test* t = new Test();
    
    tw.TimerAdd(888, 5, std::bind(Del, t));
    for (int i = 0; i < 5; i++){
        sleep(1);
        tw.TimerRefresh(888);
        tw.RunTimerTask();
        std::cout << "刷新了一下定时任务，重新需要五秒钟后才会销毁" << std::endl;
    }
    tw.TimerCancel(888);
    while (1) {
        sleep(1);
        std::cout << "---------" << std::endl;
        tw.RunTimerTask();
    }

    return 0;
}
