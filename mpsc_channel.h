#ifndef MPSC_CHANNEL_H
#define MPSC_CHANNEL_H

#include <atomic>
#include <condition_variable>
#include <memory>
#include <cassert>
#include <utility>

namespace mpsc {

template<typename T>
class __mpsc_queue_t {
public:

    __mpsc_queue_t() :
            _head(reinterpret_cast<buffer_node_t *>(new buffer_node_aligned_t)),
            _tail(_head.load(std::memory_order_relaxed)) {
        buffer_node_t *front = _head.load(std::memory_order_relaxed);
        front->next.store(nullptr, std::memory_order_relaxed);
    }

    ~__mpsc_queue_t() {
        T output;
        while (this->dequeue(output)) {}
        buffer_node_t *front = _head.load(std::memory_order_relaxed);
        delete front;
    }

    void
    enqueue(const T &input) {
        buffer_node_t *node = reinterpret_cast<buffer_node_t *>(new buffer_node_aligned_t);
        node->data = input;
        node->next.store(nullptr, std::memory_order_relaxed);

        buffer_node_t *prev_head = _head.exchange(node, std::memory_order_acq_rel);
        prev_head->next.store(node, std::memory_order_release);
    }

    bool
    dequeue(T &output) {
        buffer_node_t *tail = _tail.load(std::memory_order_relaxed);
        buffer_node_t *next = tail->next.load(std::memory_order_acquire);

        if (next == nullptr) {
            return false;
        }

        output = next->data;
        _tail.store(next, std::memory_order_release);
        delete tail;
        return true;
    }


private:

    struct buffer_node_t {
        T data;
        std::atomic<buffer_node_t *> next;
    };

    typedef typename std::aligned_storage<sizeof(buffer_node_t), std::alignment_of<buffer_node_t>::value>::type buffer_node_aligned_t;

    std::atomic<buffer_node_t *> _head;
    std::atomic<buffer_node_t *> _tail;

    __mpsc_queue_t(const __mpsc_queue_t &) {}

    void operator=(const __mpsc_queue_t &) {}
};

template<typename T>
class Sender;

template<typename T>
class Receiver;

template<typename T>
std::pair<Sender<T>, Receiver<T>> make_channel();

template<typename T>
class __channel_inner {
    friend Sender<T>;
    friend Receiver<T>;
private:
    using Queue = __mpsc_queue_t<T>;
    using Cond = std::condition_variable;
    using Mutex = std::mutex;

    Queue queue;
    Cond cond;
    Mutex mut;
};


template<typename T>
class Sender {
    template<typename F>
    friend std::pair<Sender<F>, Receiver<F>> make_channel();

public:
    Sender(const Sender &other) : chan_(other.chan_) {}

    Sender(Sender &&other) noexcept : chan_(std::move(other.chan_)) {}

    ~Sender() = default;

    Sender &operator=(const Sender &other) {
        this->chan_ = other.chan_;
        return *this;
    }

    Sender &operator=(Sender &&other) noexcept {
        this->chan_ = std::move(other.chan_);
        return *this;
    }

    void Send(const T &elem) {
        {
            std::lock_guard<std::mutex> lk(this->chan_->mut);
            this->chan_->queue.enqueue(elem);
        }
        this->chan_->cond.notify_one();
    }

private:
    using ChannelPtr = std::shared_ptr<__channel_inner<T>>;

    explicit Sender(ChannelPtr chan) : chan_(std::move(chan)) {}

private:
    ChannelPtr chan_;
};

template<typename T>
class Receiver {
    template<typename F>
    friend std::pair<Sender<F>, Receiver<F>> make_channel();

public:
    Receiver(const Receiver &other) = delete;

    Receiver(Receiver &&other) noexcept: chan_(std::move(other.chan_)) {}

    Receiver &operator=(const Receiver &other) = delete;

    Receiver &operator=(Receiver &&other) noexcept {
        this->chan_ = std::move(other.chan_);
        return *this;
    }

    bool TryRecv(T &elem) {
        return this->chan_->queue.dequeue(elem);
    }

    void Recv(T &elem) {
        if (this->chan_->queue.dequeue(elem)) {
            return;
        }
        std::unique_lock<std::mutex> lk(this->chan_->mut);
        this->chan_->cond.wait(lk);
        bool r = this->chan_->queue.dequeue(elem);
        assert(r);
    }

    template<class Rep, class Period>
    bool RecvTimeout(T &elem, const std::chrono::duration<Rep, Period> &timeout) {
        if (this->chan_->queue.dequeue(elem)) {
            return true;
        }
        std::unique_lock<std::mutex> lk(this->chan_->mut);
        std::cv_status stat = this->chan_->cond.wait_for(lk, timeout);
        if (stat == std::cv_status::timeout) {
            return false;
        }
        bool r = this->chan_->queue.dequeue(elem);
        assert(r);
        return true;
    }

    template<class Clock, class Duration>
    bool RecvDeadline(T &elem, const std::chrono::time_point<Clock, Duration> &deadline) {
        if (this->chan_->queue.dequeue(elem)) {
            return true;
        }
        std::unique_lock<std::mutex> lk(this->chan_->mut);
        std::cv_status stat = this->chan_->cond.wait_until(lk, deadline);
        if (stat == std::cv_status::timeout) {
            return false;
        }
        bool r = this->chan_->queue.dequeue(elem);
        assert(r);
        return true;
    }

private:
    using ChannelPtr = std::shared_ptr<__channel_inner<T>>;

    explicit Receiver(ChannelPtr chan) : chan_(chan) {}

private:
    ChannelPtr chan_;
};

template<typename T>
std::pair<Sender<T>, Receiver<T>> make_channel() {
    std::shared_ptr<__channel_inner<T>> chan_ptr = std::make_shared<__channel_inner<T>>();
    return {Sender<T>(chan_ptr), Receiver<T>(chan_ptr)};
}


} // namespace mpsc

#endif // MPSC_CHANNEL_H