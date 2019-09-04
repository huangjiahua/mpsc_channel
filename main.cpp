#include <iostream>
#include <thread>
#include <iostream>
#include "mpsc_channel.h"

using mpsc::Sender;
using mpsc::Receiver;

int main() {
    using namespace std::chrono_literals;
    auto chan = mpsc::make_channel<int>();
    Sender<int> sender = std::move(chan.first);
    Receiver<int> receiver = std::move(chan.second);
    int k = 0;
    bool ret = false;

    {
        std::thread t1([](Sender<int> sender) {
            std::this_thread::sleep_for(1s);
            sender.Send(1);
        }, sender);

        ret = receiver.TryRecv(k);
        assert(!ret);
        receiver.Recv(k);
        assert(k == 1);
        t1.join();
    }

    {
        std::thread t2([](Sender<int> sender) {
            std::this_thread::sleep_for(800ms);
            sender.Send(2);
        }, sender);

        ret = receiver.RecvTimeout(k, 400ms);
        assert(!ret);
        t2.join();
    }

    receiver.Recv(k);
    assert(k == 2);

    {
        std::thread t3([](Sender<int> sender) {
            std::this_thread::sleep_for(400ms);
            sender.Send(3);
        }, sender);

        receiver.RecvTimeout(k, 800ms);
        assert(k == 3);
        t3.join();
    }
}