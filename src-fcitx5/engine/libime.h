// SPDX-License-Identifier: GPL-3.0-or-later
// New adapter against libime 1.1 public APIs. No system dictionary or LM is loaded.
#pragma once
#include "cowtype2.h"
#include <libime/pinyin/pinyinime.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/core/lattice.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <stdexcept>

class LibimeDecoder {
    std::mutex mutex_;
    std::unique_ptr<libime::PinyinIME> ime_;
    struct Entry { std::string reading, text; double frequency; };
    static void load(const std::string &path, std::vector<Entry> &rows, bool required) {
        std::ifstream input(path);
        if(!input) { if(required) throw std::runtime_error("Pinyin dictionary cannot be read"); return; }
        std::string line;
        while(std::getline(input,line)) {
            if(line.empty() || line[0]=='#') continue;
            const auto a=line.find('\t'), b=line.find('\t',a==std::string::npos?0:a+1);
            if(a==std::string::npos || b==std::string::npos) throw std::runtime_error("Invalid dictionary row");
            auto reading=line.substr(0,a), text=line.substr(a+1,b-a-1);
            double frequency=std::stod(line.substr(b+1));
            if(!std::isfinite(frequency) || frequency<=0) throw std::runtime_error("Invalid dictionary frequency");
            std::replace(reading.begin(),reading.end(),' ','\'');
            rows.push_back({std::move(reading),std::move(text),frequency});
        }
    }
public:
    LibimeDecoder(const std::string &path, const std::string &optional) {
        std::vector<Entry> rows; load(path,rows,true);
        // The Rust loader validates the optional file independently. Never partially add it.
        if(!optional.empty()) { std::vector<Entry> extra; try { load(optional,extra,false); rows.insert(rows.end(),extra.begin(),extra.end()); } catch(const std::exception &) {} }
        double total=0;for(const auto &e:rows) total+=e.frequency;
        auto dict=std::make_unique<libime::PinyinDictionary>();size_t accepted=0;
        for(const auto &e:rows) {
            try { dict->addWord(0,e.reading,e.text,static_cast<float>(std::log10(e.frequency/total)));++accepted; }
            catch(const std::invalid_argument &) { /* Public dictionary may include readings unsupported by libime. */ }
        }
        if(!accepted) throw std::runtime_error("No supported pinyin dictionary entries");
        ime_=std::make_unique<libime::PinyinIME>(std::move(dict),std::make_unique<libime::UserLanguageModel>());
        ime_->setCorrectionProfile(std::make_shared<libime::PinyinCorrectionProfile>(libime::BuiltinPinyinCorrectionProfile::Qwerty));
    }
    static void destroy(void *p) { delete static_cast<LibimeDecoder *>(p); }
    static void query(void *p,const char *raw,uint32_t flags,Cowtype2Emit emit,void *output) noexcept {
        try { static_cast<LibimeDecoder *>(p)->queryImpl(raw,flags,emit,output); }
        catch(const std::exception &) { /* Never unwind through the Rust ABI; raw input remains available. */ }
    }
    void queryImpl(std::string raw,uint32_t flags,Cowtype2Emit emit,void *output) {
        std::lock_guard lock(mutex_);
        if(raw.empty() || raw.size()>128) return;
        std::transform(raw.begin(),raw.end(),raw.begin(),[](unsigned char c){return std::tolower(c);});
        if(raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz' ")!=std::string::npos) return;
        std::replace(raw.begin(),raw.end(),' ','\'');
        // Decode without typo edges first. Correction frequency must never
        // displace an available literal reading, sentence or intentional initials.
        std::set<std::pair<std::string,size_t>> seen;
        for(unsigned pass=0;pass<((flags&2)?2u:1u);++pass) {
        libime::PinyinFuzzyFlags fuzzy;
        if(pass) fuzzy |= libime::PinyinFuzzyFlags(libime::PinyinFuzzyFlag::CommonTypo) | libime::PinyinFuzzyFlag::AdvancedTypo | libime::PinyinFuzzyFlag::Correction;
        if(flags&4) fuzzy |= libime::PinyinFuzzyFlag::PartialFinal;
        ime_->setFuzzyFlags(fuzzy);
        libime::PinyinContext context(ime_.get());context.setMaxSentenceLength(128);context.type(raw);
        auto candidates=context.candidates();
        auto literal=[&](const libime::SentenceResult &candidate) {
            auto reading=context.candidateFullPinyin(candidate);
            reading.erase(std::remove(reading.begin(),reading.end(),'\''),reading.end());
            auto typed=raw;typed.erase(std::remove(typed.begin(),typed.end(),'\''),typed.end());
            return !reading.empty() && reading==typed && !candidate.sentence().empty() && candidate.sentence().back()->to()->index()==raw.size();
        };
        std::stable_partition(candidates.begin(),candidates.end(),literal);
        for(const auto &candidate:candidates) {
            if(candidate.sentence().empty() || (!(flags&1) && candidate.size()>1)) continue;
            auto reading=context.candidateFullPinyin(candidate);
            if(reading.empty()) continue; // libime's literal fallback is handled by the session.
            const size_t consumed=candidate.sentence().back()->to()->index();
            // libime permits initials intrinsically. Enforce the independent user option
            // against the decoded reading, including mixed full/initial syllables.
            if(!(flags&8)) {
                std::string full;for(char c:reading) if(c!='\'') full+=c;
                std::string typed;for(char c:raw.substr(0,consumed)) if(c!='\'') typed+=c;
                if(full!=typed && !((flags&4) && full.starts_with(typed))) continue;
            }
            auto text=candidate.toString();
            if(!seen.emplace(text,consumed).second) continue;
            std::replace(reading.begin(),reading.end(),'\'',' ');
            emit(output,text.c_str(),reading.c_str(),consumed,pass);
        }
        }
    }
};
