// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Original bounded whole-word repair using public libime syllable/trie APIs.
// No second lexicon, model, per-variant sentence decode or retained input history.
#include <libime/pinyin/pinyindictionary.h>
#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
namespace cowtype {
struct SpellingMatch {
    std::string text, reading;
    float weight;
    unsigned source;
};
inline const auto &spellingSyllables() {
    static const auto syllables=[] {
        std::map<std::string,std::array<char,2>,std::less<>> result;
        using Encoder=libime::PinyinEncoder;
        for(char initial=Encoder::firstInitial;initial<=Encoder::lastInitial;++initial)
            for(char final=Encoder::firstFinal;final<=Encoder::lastFinal;++final) {
                if(!Encoder::isValidInitialFinal(static_cast<libime::PinyinInitial>(initial),static_cast<libime::PinyinFinal>(final))) continue;
                const std::array<char,2> encoded{initial,final};
                const auto reading=Encoder::decodeFullPinyin(encoded.data(),2);
                if(!reading.empty()) result.emplace(reading,encoded);
            }
        return result;
    }();
    return syllables;
}
inline std::vector<SpellingMatch> spellingMatches(const libime::PinyinDictionary &dictionary,const std::string &raw) {
    std::vector<SpellingMatch> matches;
    if(raw.size()<4 || raw.size()>24 || raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos) return matches;
    std::set<std::string> unique;
    std::vector<std::string> variants;
    auto add=[&](std::string value) {if(variants.size()<32 && unique.insert(value).second) variants.push_back(std::move(value));};
    // Duplicate-key repair first, then cross/intra-syllable adjacent swaps.
    for(size_t i=1;i<raw.size();++i) if(raw[i]==raw[i-1]) {auto value=raw;value.erase(i,1);add(std::move(value));}
    for(size_t i=1;i<raw.size();++i) if(raw[i]!=raw[i-1]) {auto value=raw;std::swap(value[i],value[i-1]);add(std::move(value));}
    using Trie=libime::PinyinTrie;
    using Positions=std::array<Trie::position_type,3>;
    const auto &syllables=spellingSyllables();
    size_t work=0,paths=0,words=0;
    std::set<std::pair<std::string,std::string>> seen;
    for(const auto &variant:variants) {
        std::string encoded;
        std::function<void(size_t,Positions,unsigned)> visit=[&](size_t offset,Positions positions,unsigned depth) {
            if(++work>1024 || paths>=32 || words>=128 || depth>6) return;
            if(offset==variant.size()) {
                if(depth<2) return;
                ++paths;
                dictionary.matchWords(encoded.data(),encoded.size(),[&](std::string_view key,std::string_view text,float weight) {
                    if(++words>128) return false;
                    auto reading=libime::PinyinEncoder::decodeFullPinyin(key);
                    unsigned source=2;
                    for(unsigned i=0;i<3;++i) if(dictionary.lookupWord(i,reading,text).has_value()) {source=i;break;}
                    if(seen.emplace(std::string(text),reading).second) {
                        std::replace(reading.begin(),reading.end(),'\'',' ');
                        matches.push_back({std::string(text),std::move(reading),weight,source});
                    }
                    return true;
                });
                return;
            }
            // Longer syllables first; all alternatives remain bounded by work.
            for(size_t length=std::min(size_t(6),variant.size()-offset);length>0;--length) {
                const auto found=syllables.find(std::string_view(variant).substr(offset,length));
                if(found==syllables.end()) continue;
                auto next=positions;
                bool viable=false;
                for(unsigned i=0;i<3;++i) {
                    if(next[i]==~Trie::position_type(0)) continue;
                    const auto *trie=dictionary.trie(i);
                    if(!trie || Trie::isNoPath(trie->traverse(found->second.data(),2,next[i]))) next[i]=~Trie::position_type(0);
                    else viable=true;
                }
                if(!viable) continue;
                encoded.append(found->second.data(),2);
                visit(offset+length,next,depth+1);
                encoded.resize(encoded.size()-2);
            }
        };
        visit(0,Positions{},0);
        if(work>=1024 || paths>=32 || words>=128) break;
    }
    std::stable_sort(matches.begin(),matches.end(),[](const auto &a,const auto &b) {
        if(a.source!=b.source) return a.source<b.source;
        return a.weight>b.weight;
    });
    if(matches.size()>16) matches.resize(16);
    return matches;
}
} // namespace cowtype
