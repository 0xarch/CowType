// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// Original bounded syllable/edit search over public libime dictionary APIs.
// No second lexicon, model, per-variant sentence decode or retained input history.
#include <libime/pinyin/pinyindictionary.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <map>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <string_view>
#include <vector>
namespace cowtype {
struct SpellingMatch {
    std::string text, reading;
    float weight;
    unsigned source;
    float penalty = 1.0f;
    unsigned edits = 1;
    bool completion = false;
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
namespace spelling_detail {
// Keyboard geometry is explicit and independent of corpus or expected answers.
inline bool neighbours(char a,char b) {
    static constexpr std::array<std::string_view,3> rows{"qwertyuiop","asdfghjkl","zxcvbnm"};
    int ax=-100,ay=0,bx=100,by=0;
    for(int y=0;y<3;++y) {
        auto ai=rows[y].find(a),bi=rows[y].find(b);
        if(ai!=std::string_view::npos) {ax=int(ai)*4+y;ay=y;}
        if(bi!=std::string_view::npos) {bx=int(bi)*4+y;by=y;}
    }
    return a!=b && std::abs(ay-by)<=1 && std::abs(ax-bx)<=5;
}
// Return a cost for exactly one edit, or infinity. Intended is a full syllable.
inline float editCost(std::string_view typed,std::string_view intended) {
    size_t common=0;
    while(common<typed.size() && common<intended.size() && typed[common]==intended[common]) ++common;
    if(typed==intended) return 0;
    if(typed.size()==intended.size()) {
        if(common+1<typed.size() && typed[common]==intended[common+1] && typed[common+1]==intended[common] &&
           typed.substr(common+2)==intended.substr(common+2)) return 0.70f;
        if(neighbours(typed[common],intended[common]) && typed.substr(common+1)==intended.substr(common+1)) return 1.10f;
    } else if(typed.size()+1==intended.size()) {
        if(typed.substr(common)==intended.substr(common+1)) return 1.15f;
    } else if(typed.size()==intended.size()+1) {
        if(typed.substr(common+1)==intended.substr(common))
            return common>0 && typed[common]==typed[common-1]?0.60f:1.05f;
    }
    return std::numeric_limits<float>::infinity();
}
struct Transition {
    std::array<char,2> syllable;
    size_t end;
    float penalty;
    bool completion;
};
}
inline std::vector<SpellingMatch> spellingMatches(const libime::PinyinDictionary &dictionary,const std::string &raw,bool allowCompletion=false) {
    std::vector<SpellingMatch> matches;
    if(raw.size()<3 || raw.size()>32 || raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos) return matches;
    using Trie=libime::PinyinTrie;
    using Positions=std::array<Trie::position_type,3>;
    constexpr auto dead=~Trie::position_type(0);
    const auto &syllables=spellingSyllables();
    // One cache per raw offset, not per path; repeated word prefixes share work.
    std::vector<std::vector<spelling_detail::Transition>> transitions(raw.size());
    for(size_t offset=0;offset<raw.size();++offset) {
        const auto tail=std::string_view(raw).substr(offset);
        auto &options=transitions[offset];
        for(const auto &[reading,encoded]:syllables) {
            if(tail.starts_with(reading)) options.push_back({encoded,offset+reading.size(),0,false});
            if(allowCompletion && tail.size()<reading.size() && std::string_view(reading).starts_with(tail))
                options.push_back({encoded,raw.size(),0.35f+0.08f*float(reading.size()-tail.size()),true});
            const size_t first=reading.size()>1?reading.size()-1:1;
            for(size_t length=first;length<=reading.size()+1 && length<=tail.size();++length) {
                const float cost=spelling_detail::editCost(tail.substr(0,length),reading);
                if(cost>0 && std::isfinite(cost)) options.push_back({encoded,offset+length,cost,false});
            }
        }
        std::stable_sort(options.begin(),options.end(),[](const auto &a,const auto &b){return a.penalty<b.penalty;});
    }
    struct State {
        size_t offset;
        Positions positions;
        std::string encoded;
        float penalty;
        unsigned edits;
        bool completion;
        unsigned order;
    };
    auto worse=[](const State &a,const State &b){return a.penalty!=b.penalty?a.penalty>b.penalty:a.order>b.order;};
    std::priority_queue<State,std::vector<State>,decltype(worse)> pending(worse);
    unsigned order=0;
    pending.push({0,Positions{},"",0,0,false,order++});
    size_t work=0,paths=0,words=0;
    std::map<std::pair<std::string,std::string>,size_t> seen;
    auto collect=[&](const std::string &encoded,float penalty,unsigned edits,bool completion) {
        if(encoded.size()<4 || encoded.size()>16 || ++paths>96 || words>=768) return;
        dictionary.matchWords(encoded.data(),encoded.size(),[&](std::string_view key,std::string_view text,float weight) {
            if(++words>768) return false;
            auto reading=libime::PinyinEncoder::decodeFullPinyin(key);
            unsigned source=2;
            for(unsigned i=0;i<3;++i) if(dictionary.lookupWord(i,reading,text).has_value()) {source=i;break;}
            const auto identity=std::make_pair(std::string(text),reading);
            if(auto found=seen.find(identity);found!=seen.end()) {
                auto &previous=matches[found->second];
                if(penalty<previous.penalty) {previous.penalty=penalty;previous.edits=edits;previous.completion=completion;}
            } else {
                std::replace(reading.begin(),reading.end(),'\'',' ');
                seen.emplace(identity,matches.size());
                matches.push_back({std::string(text),std::move(reading),weight,source,penalty,edits,completion});
            }
            return true;
        });
    };
    while(!pending.empty() && work<8192 && paths<96 && words<768) {
        auto state=pending.top();pending.pop();++work;
        if(state.offset==raw.size()) {
            if(state.edits || state.completion) collect(state.encoded,state.penalty,state.edits,state.completion);
            continue;
        }
        if(state.encoded.size()>=16) continue;
        for(const auto &next:transitions[state.offset]) {
            const unsigned edits=state.edits+unsigned(next.penalty>0 && !next.completion);
            if(edits>1 || (next.completion && (state.edits || state.completion))) continue;
            auto positions=state.positions;
            bool viable=false;
            for(unsigned i=0;i<3;++i) {
                if(positions[i]==dead) continue;
                const auto *trie=dictionary.trie(i);
                if(!trie || Trie::isNoPath(trie->traverse(next.syllable.data(),2,positions[i]))) positions[i]=dead;
                else viable=true;
            }
            if(!viable) continue;
            auto encoded=state.encoded;encoded.append(next.syllable.data(),2);
            if(pending.size()<8192) pending.push({next.end,positions,std::move(encoded),state.penalty+next.penalty,edits,state.completion||next.completion,order++});
        }
    }
    // A swap spanning syllable boundaries cannot be represented by a single
    // syllable edit. Use exact syllables only for these bounded extra strings.
    size_t extraWork=0;
    for(size_t i=1;i<raw.size() && extraWork<1024 && paths<96 && words<768;++i) {
        if(raw[i]==raw[i-1]) continue;
        auto variant=raw;std::swap(variant[i],variant[i-1]);
        std::string encoded;
        std::function<void(size_t,Positions)> visit=[&](size_t offset,Positions positions) {
            if(++extraWork>1024 || paths>=96 || words>=768 || encoded.size()>16) return;
            if(offset==variant.size()) {collect(encoded,0.70f,1,false);return;}
            for(size_t length=std::min(size_t(6),variant.size()-offset);length>0;--length) {
                const auto found=syllables.find(std::string_view(variant).substr(offset,length));
                if(found==syllables.end()) continue;
                auto next=positions;bool viable=false;
                for(unsigned source=0;source<3;++source) {
                    if(next[source]==dead) continue;
                    const auto *trie=dictionary.trie(source);
                    if(!trie || Trie::isNoPath(trie->traverse(found->second.data(),2,next[source]))) next[source]=dead;
                    else viable=true;
                }
                if(!viable) continue;
                encoded.append(found->second.data(),2);visit(offset+length,next);encoded.resize(encoded.size()-2);
            }
        };
        visit(0,Positions{});
    }
    std::stable_sort(matches.begin(),matches.end(),[](const auto &a,const auto &b) {
        if(a.source!=b.source) return a.source<b.source;
        return a.weight-a.penalty>b.weight-b.penalty;
    });
    if(matches.size()>48) matches.resize(48);
    return matches;
}
} // namespace cowtype
