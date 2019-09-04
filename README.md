# mpsc channel

*Huang Jiahua*

C++ implementation of Rust's mpsc channel used to sync between threads the Rust's version is at https://doc.rust-lang.org/std/sync/mpsc/

mpsc means **Multi Provider Single Collector**

## How to use?

### 1. Create a channel

```c++
auto chan = mspc::make_channel<int>();
mspc::Sender<int> sender = std::move(chan.first);
mspc::Receiver<int> receiver = std::move(chan.second);
```

### 2. Send And Receive

```c++
// --create a channel--
using namespace std::chrono_literals;
int k = 0;
bool ret = false;

{
	std::thread t1([](Sender<int> sender) {
		std::this_thread::sleep_for(1s);
  	sender.Send(1);
	}, sender);
  
  ret = receiver.TryRecv(k); // won't block
  assert(!ret);
  receiver.Recv(k); // block as long as possible
  assert(k == 1);
  t1.join();
}

{
  std::thread t2([](Sender<int> sender) {
      std::this_thread::sleep_for(800ms);
      sender.Send(2);
  }, sender);

  ret = receiver.RecvTimeout(k, 400ms); // block at most 400ms
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
```

## Warnings

1. `Receiver<T>` cannot be copied(copy constructor and copy assign are deleted). Use `std::move` the transfer the ownership.
2. Copy `Sender<T>` to create multi senders.