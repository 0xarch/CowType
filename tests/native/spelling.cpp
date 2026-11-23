// SPDX-License-Identifier: GPL-3.0-or-later
#include "spelling.h"
#include <cassert>
#include <iostream>
static auto find(const std::vector<cowtype::SpellingMatch> &matches,std::string_view text) {
    return std::find_if(matches.begin(),matches.end(),[&](const auto &m){return m.text==text;});
}
int main() {
    libime::PinyinDictionary dictionary;
    dictionary.addEmptyDict();
    for(auto [reading,text]: {std::pair{"ni'hao","你好"},{"zhong'guo","中国"},{"shu'ru'fa","输入法"},
         {"ji'suan'ji","计算机"},{"ren'gong'zhi'neng","人工智能"},{"bei'jing","北京"},
         {"xue'xi","学习"},{"ce'shi","测试"},{"fa'lv","法律"}})
        dictionary.addWord(2,reading,text,-1);
    auto has=[&](const char *raw,const char *text,bool completion=false) {
        auto matches=cowtype::spellingMatches(dictionary,raw,completion);
        auto result=find(matches,text);
        assert(result!=matches.end());
        assert(result->source==2);
        return *result;
    };
    // Intra/cross-syllable swaps, duplicated and arbitrary extra keys.
    for(auto raw:{"nihoa","nhiao","nihhao","nihaoo","nixhao"}) {
        auto match=has(raw,"你好");
        assert(match.edits==1 && !match.completion && match.reading=="ni hao");
    }
    // Missing keys include syllable boundaries and a missing vowel/consonant.
    for(auto raw:{"niho","nhao","niha"}) assert(has(raw,"你好").edits==1);
    // Qwerty neighbours only; unrelated substitutions do not use this path.
    assert(has("nihso","你好").edits==1);
    auto distant=cowtype::spellingMatches(dictionary,"niqao");
    assert(find(distant,"你好")==distant.end());
    // Prefix completion is not an edit and does not cross explicit boundaries.
    for(auto [raw,text]:{std::pair{"zhonggu","中国"},{"jisuanj","计算机"},{"rengongzhin","人工智能"}}) {
        auto match=has(raw,text,true);
        assert(match.edits==0 && match.completion && match.penalty>0);
    }
    auto plain=cowtype::spellingMatches(dictionary,"zhonggu",false);
    assert(find(plain,"中国")!=plain.end()); // one missing final letter can also be an explicit typo
    assert(find(plain,"中国")->edits==1 && !find(plain,"中国")->completion);
    for(auto raw:{"ni'hoa","NIHAO","12345","n","abcdefghijklmnopqrstuvwxyzabcdefghi"})
        assert(cowtype::spellingMatches(dictionary,raw,true).empty());
    auto exact=cowtype::spellingMatches(dictionary,"nihao",false);
    // The unchanged target is not a repair; other valid one-edit words may be.
    assert(find(exact,"你好")==exact.end());
    std::cout<<"spelling: bounded one-edit words, keyboard neighbours and explicit completion metadata passed\n";
}
