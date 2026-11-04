#include "../Source/IncrementalParameterSearch.h"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>
int main() {
    using lsampler::IncrementalParameterSearch;
    IncrementalParameterSearch s;
    std::vector<std::wstring> names{L"Attack",L"Decay",L"Filter Cutoff",L"Filter Resonance",L"Frequency",L"Gain",L"0 Zero Crossing", L"1 Mode", L"16 Detector Source"};
    auto label=[&](int i){return names.at(static_cast<size_t>(i));};
    int x=s.next(L'f',0,(int)names.size(),1,100,label); assert(x==2);
    x=s.next(L'i',x,(int)names.size(),1,150,label);assert(x==2 && s.text()==L"fi");
    x=s.next(L'l',x,(int)names.size(),1,180,label);assert(x==2);
    x=s.next(L'f',x,(int)names.size(),1,1500,label);assert(x==3);
    x=s.next(L'f',x,(int)names.size(),1,1550,label);assert(x==4);
    x=s.next(L'f',x,(int)names.size(),1,1600,label,true);assert(x==-1);
    s.reset(); x=s.next(L'c',0,(int)names.size(),1,2000,label);assert(x==2); // beginning of word in Filter Cutoff
    s.reset(); x=s.next(L'r',0,(int)names.size(),1,2200,label);assert(x==3); // Resonance
    s.reset(); x=s.next(L'z',0,(int)names.size(),1,2400,label);assert(x==6); // Zero Crossing from word start despite 0 alias
    s.reset(); x=s.next(L'0',0,(int)names.size(),1,2600,label);assert(x==6); // historical numeric alias
    s.reset(); x=s.next(L'1',7,(int)names.size(),-1,3000,label);assert(x==8);
    x=s.next(L'6',x,(int)names.size(),-1,3100,label);assert(x==8 && s.text()==L"16");
    s.reset(); x=s.next(L'1',0,(int)names.size(),1,4000,label);assert(x==7); // full-name prefix takes precedence
    s.reset(); x=s.next(L'f',0,(int)names.size(),1,5000,label);assert(x==2);
    x=s.next(L'x',x,(int)names.size(),1,5070,label);assert(x==-1 && s.text()==L"x");
    s.reset(); x=s.next(L'f',0,(int)names.size(),1,0xffffffffu-100,label); assert(x==2);
    x=s.next(L'i',x,(int)names.size(),1,40,label);assert(x==2 && s.text()==L"fi");
    std::cout<<"PASS TEST165: parity for prefix, word starts, 0 alias, reverse digits, repeated, timeout, rollover\n";
}
