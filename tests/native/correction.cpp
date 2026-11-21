// SPDX-License-Identifier: GPL-3.0-or-later
#include "libime.h"
#include <cassert>
#include <iostream>
#include <vector>
struct Result { std::string text,reading; size_t consumed; unsigned corrected; };
void emit(void *p,const char *text,const char *reading,size_t consumed,uint32_t corrected) {
    static_cast<std::vector<Result> *>(p)->push_back({text,reading,consumed,corrected});
}
int main(int argc,char **argv) {
    assert(argc==2);
    LibimeDecoder decoder(argv[1],"");
    for(const auto &input:{"lu","wei","na","kn"}) {
        std::vector<Result> plain,corrected;
        decoder.queryImpl(input,13,emit,&plain);
        decoder.queryImpl(input,15,emit,&corrected);
        assert(!plain.empty() && corrected.size()>=plain.size());
        assert(corrected.front().reading==(std::string(input)=="kn"?"ke neng":input));
        for(size_t i=0;i<plain.size();++i) {
            assert(plain[i].text==corrected[i].text && plain[i].reading==corrected[i].reading);
            assert(!corrected[i].corrected);
        }
        for(size_t i=plain.size();i<corrected.size();++i) assert(corrected[i].corrected);
    }
    std::vector<Result> typo;
    decoder.queryImpl("lu",15,emit,&typo);
    assert(std::any_of(typo.begin(),typo.end(),[](const auto &c){return c.reading=="li" && c.corrected;}));
    std::cout<<"libime: exact readings and mixed initials precede adversarial high-frequency typo candidates\n";
}
