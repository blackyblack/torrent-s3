#pragma once
#include <deque>
#include <mutex>

template<class T>
class ThreadSafeDeque {
public:
    T pop_front_waiting() {
        // unique_lock can be unlocked, lock_guard can not
        std::unique_lock<std::mutex> lock{ mutex }; // locks
        while(deque.empty()) {
            condition.wait(lock); // unlocks, sleeps and relocks when woken up  
        }
        auto t = deque.front();
        deque.pop_front();
        return t;
    } // unlocks as goes out of scope

    void push_back(const T t) {
        std::unique_lock<std::mutex> lock{ mutex }; 
        deque.push_back(t);
        lock.unlock();
        condition.notify_one(); // wakes up pop_front_waiting  
    }
private:
    std::deque<T>               deque;
    std::mutex                  mutex;
    std::condition_variable condition;
};  