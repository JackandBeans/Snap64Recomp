#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace snap::vr {
// Bounded presentation history, not a queue of jobs to replay. The producer
// mailbox replaces obsolete unpublished work; this window keeps the pair that
// brackets the one-frame-delayed pose, even when a newer pair arrives early.
template<class T, size_t Capacity=4> class SnapshotWindow {
    struct Entry { std::shared_ptr<T> value; uint64_t epoch=0; double a=0,b=0; };
    std::array<Entry,Capacity> entries{};
    size_t count=0;
public:
    void clear(){for(auto& entry:entries)entry={};count=0;}
    size_t size()const{return count;}
    void publish(std::shared_ptr<T> value,uint64_t epoch,double a,double b) {
        if(count&&entries[count-1].epoch!=epoch)clear();
        if(count&&b<=entries[count-1].b)return;
        if(count==Capacity){for(size_t i=1;i<count;++i)entries[i-1]=std::move(entries[i]);--count;}
        entries[count++]={std::move(value),epoch,a,b};
    }
    std::shared_ptr<T> select(double time)const {
        if(!count)return {};
        for(size_t i=0;i<count;++i)if(time<=entries[i].b)return entries[i].value;
        return entries[count-1].value;
    }
};
}
