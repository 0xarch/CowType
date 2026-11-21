// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "messages.h"
#include <charconv>
#include <deque>
#include <string>
#include <vector>
class VoiceLevel {
    std::deque<unsigned> history_;
public:
    void clear() { history_.clear(); }
    std::string push(const std::string &value) {
        unsigned level=0;
        auto parsed=std::from_chars(value.data(),value.data()+value.size(),level);
        if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || level>8) return {};
        history_.push_back(level);
        if(history_.size()>12) history_.pop_front();
        std::string symbols=ct_tr("voice.level.symbols");
        std::vector<std::string> bars;
        for(size_t i=0;i<symbols.size();) {
            const size_t start=i++;
            while(i<symbols.size() && (static_cast<unsigned char>(symbols[i])&0xc0)==0x80) ++i;
            bars.push_back(symbols.substr(start,i-start));
        }
        if(bars.size()!=9) return {};
        std::string result;
        for(size_t i=history_.size();i<12;++i) result+=bars[0];
        for(auto v:history_) result+=bars[v];
        return result;
    }
};
