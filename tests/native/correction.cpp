// SPDX-License-Identifier: GPL-3.0-or-later
#include "libime.h"
#include <cassert>
#include <iostream>
#include <vector>
struct Result { std::string text,reading; size_t consumed; unsigned corrected; bool operator==(const Result &) const = default; };
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
    // A complete correction must be reachable before partial-prefix pages.
    std::vector<Result> corrected;
    decoder.queryImpl("nihoa",15,emit,&corrected);
    assert(!corrected.empty() && corrected.front().text=="你好");
    std::vector<Result> noCorrection;
    decoder.queryImpl("nihoa",13,emit,&noCorrection);
    assert(std::none_of(noCorrection.begin(),noCorrection.end(),[](const auto &c){return c.corrected;}));
    // Recover exact sentences when typo edges favor a different best path.
    std::vector<Result> sentence;
    decoder.queryImpl("qingbaochiyunsu",15,emit,&sentence);
    assert(!sentence.empty() && sentence.front().text=="请保持匀速" && !sentence.front().corrected);
    // Append, erase, middle replacement, unrelated field, separators, invalid
    // inputs and all feature toggles must agree with a fresh stateless decoder.
    const std::vector<std::string> edits={"n","ni","nih","niha","nihao","niha","nih","ni","n","",
        "lu","nihao","nihoa","nihao","NI HAO","ni'hao","ni''hao","'ni","ni'",
        "qingbao","qingbaochiyunsu","qingbao","kn","na","wei","nihao","!","nihao"};
    for(unsigned flags:{15u,13u,11u,7u,14u,9u,15u}) {
        for(const auto &raw:edits) {
            std::vector<Result> reused,fresh;
            decoder.queryImpl(raw,flags,emit,&reused);
            LibimeDecoder independent(argv[1],"");
            independent.queryImpl(raw,flags,emit,&fresh);
            assert(reused==fresh);
        }
    }
    std::cout<<"libime: exact readings and mixed initials precede adversarial high-frequency typo candidates\n";
}
