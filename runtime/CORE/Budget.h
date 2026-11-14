#pragma once
#include <atomic>
#include <cstdint>
#include <utility>
namespace w2v2 {
class Budget {
 std::atomic<std::uint64_t> used_{0},peak_{0};const std::uint64_t limit_;bool measured_=false;
public:
 explicit Budget(std::uint64_t limit,bool measured=false):limit_(limit),measured_(measured){}
 void measure(bool value){measured_=value;}
 bool acquire(std::uint64_t bytes){auto n=used_.load();for(;;){if(bytes>limit_-n)return false;if(used_.compare_exchange_weak(n,n+bytes))break;}if(measured_){auto p=peak_.load();while(p<n+bytes&&!peak_.compare_exchange_weak(p,n+bytes)){}}return true;}
 void release(std::uint64_t bytes){auto prior=used_.fetch_sub(bytes);if(prior<bytes)std::terminate();}
 std::uint64_t used()const{return used_.load();}std::uint64_t peak()const{return peak_.load();}std::uint64_t limit()const{return limit_;}
};
struct Lease {
 Budget* owner=nullptr;std::uint64_t bytes=0;
 Lease()=default;Lease(Budget& b,std::uint64_t n){if(b.acquire(n)){owner=&b;bytes=n;}}
 explicit operator bool()const{return owner!=nullptr;}
 Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
 Lease(Lease&& x)noexcept:owner(std::exchange(x.owner,nullptr)),bytes(std::exchange(x.bytes,0)){}
 Lease& operator=(Lease&& x)noexcept{if(this!=&x){reset();owner=std::exchange(x.owner,nullptr);bytes=std::exchange(x.bytes,0);}return *this;}
 void reset(){if(owner)owner->release(bytes);owner=nullptr;bytes=0;}
 ~Lease(){reset();}
};
struct Epoch {std::uint64_t world=1,device=1;bool operator==(const Epoch&)const=default;};
}
