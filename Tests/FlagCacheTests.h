#pragma once
#include <juce_audio_processors_headless/utilities/juce_FlagCache.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <ostream>
#include <stdexcept>
#include <thread>
#include <vector>

// Runs in the existing Windows CI comparison executable, before audio tests.
// This exercises the actual patched JUCE template, not a second implementation.
inline void runFlagCacheTests(std::ostream& report)
{
    const auto require=[](bool ok,const char* message) {
        if(!ok)throw std::runtime_error(message);
    };
    juce::FlagCache<1> empty;
    empty.ifSet([&](size_t,uint32_t){require(false,"Empty FlagCache callback");});
    constexpr size_t items=10001;
    juce::FlagCache<1> cache(items);
    std::vector<unsigned> seen(items,0);
    const auto collect=[&](size_t index,uint32_t bits) {
        require(index<items && bits==1,"FlagCache index/bits corrupted");
        ++seen[index];
    };
    for(size_t i : {size_t(0),size_t(31),size_t(32),size_t(63),items-1}) {
        cache.set(i,1);cache.set(i,1); // Repeated flags coalesce.
    }
    cache.ifSet(collect);
    cache.ifSet([&](size_t,uint32_t){require(false,"Empty drain repeated a notification");});
    for(size_t i : {size_t(0),size_t(31),size_t(32),size_t(63),items-1})
        require(seen[i]==1,"FlagCache boundary notification missing");
    cache.set(7,1);cache.clear();
    cache.ifSet([&](size_t,uint32_t){require(false,"FlagCache clear failed");});

    // A write after an exchange (including within its callback) stays pending.
    cache.set(2,1);
    cache.ifSet([&](size_t index,uint32_t) {
        require(index==2,"Unexpected reentrant notification");cache.set(2,1);
    });
    unsigned reentrant=0;
    cache.ifSet([&](size_t index,uint32_t){require(index==2,"Reentrant index");++reentrant;});
    require(reentrant==1,"Concurrent/reentrant flag was lost");

    juce::FlagCache<3> multi(33);
    multi.set(7,1);multi.set(7,4);multi.set(8,2);multi.set(32,7);
    unsigned multiCount=0;
    multi.ifSet([&](size_t i,uint32_t bits) {
        require((i==7 && bits==5)||(i==8 && bits==2)||(i==32 && bits==7),"Multi-bit flag mismatch");
        ++multiCount;
    });
    require(multiCount==3,"Multi-bit flag count");

    // Two producers publish distinct items while the consumer drains groups.
    // Each item must be delivered exactly once, including writes to a word
    // which the consumer has just observed empty.
    std::fill(seen.begin(),seen.end(),0);
    std::atomic<bool> start{false};
    std::atomic<unsigned> done{0};
    const auto producer=[&](size_t first) {
        while(!start.load(std::memory_order_acquire))std::this_thread::yield();
        for(size_t i=first;i<items;i+=2) {
            cache.set(i,1);
            if(i%31==0)std::this_thread::yield();
        }
        done.fetch_add(1,std::memory_order_release);
    };
    std::thread a(producer,0),b(producer,1);
    start.store(true,std::memory_order_release);
    // Do not throw until both threads are joined.
    bool invalid=false;
    const auto concurrentCollect=[&](size_t i,uint32_t bits) {
        if(i>=items || bits!=1)invalid=true;else ++seen[i];
    };
    while(done.load(std::memory_order_acquire)!=2)cache.ifSet(concurrentCollect);
    a.join();b.join();cache.ifSet(concurrentCollect);
    require(!invalid,"Concurrent flags corrupted");
    for(auto count:seen)require(count==1,"Concurrent flag lost or duplicated");

    juce::FlaggedFloatCache<1> values(65);
    values.setValueAndBits(32,.25f,1);values.setValueAndBits(32,.75f,1);
    unsigned valueCount=0;
    values.ifSet([&](size_t i,float value,uint32_t bits) {
        require(i==32 && value==.75f && bits==1,"Latest parameter value lost");++valueCount;
    });
    require(valueCount==1,"Float cache notification count");
    report<<"TEST143 JUCE FlagCache PASS: empty, boundaries, coalescing, clear, reentrant, multi-bit, concurrent producers, latest value.\n";
}

// Isolated notification-drain benchmark. This is not a REAPER/whole-plugin
// CPU measurement. The baseline reproduces the pre-TEST143 single-bit drain.
inline void benchmarkFlagCache(std::ostream& report)
{
    struct PreviousDrain {
        explicit PreviousDrain(size_t count):flags((count+31)/32) {
            for(auto& flag:flags)flag.store(0);
        }
        void set(size_t i) { flags[i/32].fetch_or(uint32_t(1)<<(i%32),std::memory_order_acq_rel); }
        void drain(uint64_t& checksum) {
            for(size_t word=0;word<flags.size();++word) {
                const auto bits=flags[word].exchange(0,std::memory_order_acq_rel);
                for(size_t bit=0;bit<32;++bit)
                    if(((bits>>bit)&1)!=0)checksum+=word*32+bit+1;
            }
        }
        std::vector<std::atomic<uint32_t>> flags;
    };
    constexpr size_t parameterItems=9000;
    constexpr int blocks=4096;
    PreviousDrain previous(parameterItems);
    juce::FlagCache<1> candidate(parameterItems);
    for(bool sparse : {false,true}) {
        std::array<double,5> oldMs{},newMs{};
        uint64_t oldSum=0,newSum=0;
        const auto runOld=[&] {
            const auto begin=std::chrono::steady_clock::now();
            for(int block=0;block<blocks;++block) {
                if(sparse && block%64==0)previous.set(size_t(block)%parameterItems);
                previous.drain(oldSum);
            }
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        };
        const auto runNew=[&] {
            const auto begin=std::chrono::steady_clock::now();
            for(int block=0;block<blocks;++block) {
                if(sparse && block%64==0)candidate.set(size_t(block)%parameterItems,1);
                candidate.ifSet([&](size_t i,uint32_t){newSum+=i+1;});
            }
            return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        };
        // Warm-up and alternating order, same observable checksum on both paths.
        runOld();runNew();
        for(size_t i=0;i<oldMs.size();++i) {
            if(i%2==0){oldMs[i]=runOld();newMs[i]=runNew();}
            else {newMs[i]=runNew();oldMs[i]=runOld();}
        }
        if(oldSum!=newSum)throw std::runtime_error("FlagCache benchmark lost notifications");
        std::sort(oldMs.begin(),oldMs.end());std::sort(newMs.begin(),newMs.end());
        report<<"TEST143 isolated FlagCache "<<(sparse?"sparse":"idle")
              <<" (9000 synthetic parameters, 4096 drains, median ms): previous="
              <<oldMs[2]<<", patched="<<newMs[2]<<". Not whole-plugin CPU.\n";
    }
}
