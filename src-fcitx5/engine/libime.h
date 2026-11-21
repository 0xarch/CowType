// SPDX-License-Identifier: GPL-3.0-or-later
// Adapter against libime public APIs with disjoint source layers and optional local LM.
#pragma once
#include "cowtype2.h"
#include "dictionary.h"
#include <libime/pinyin/pinyindecoder.h>
#include <cstdlib>
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

#ifndef COWTYPE_LIBIME_MODEL_FILE
#define COWTYPE_LIBIME_MODEL_FILE "/usr/lib/libime/zh_CN.lm"
#endif
class LibimeDecoder {
    std::mutex mutex_;
    std::unique_ptr<libime::PinyinIME> ime_;
    const libime::PinyinDictionary *dictionary_ = nullptr;
public:
    static constexpr unsigned ModelUnavailable = 4;
    unsigned warnings = 0;
    bool cacheHit = false;
    bool modelLoaded = false;
    static std::string installed(const char *name, const char *fallback) {
        const char *value = std::getenv(name); return value ? value : fallback;
    }
    static std::string systemPath() { return installed("COWTYPE_SYSTEM_PINYIN_DICT", "/usr/share/libime/sc.dict"); }
    static std::string modelPath() { return installed("COWTYPE_STATISTICAL_MODEL_FILE", COWTYPE_LIBIME_MODEL_FILE); }
    LibimeDecoder(const std::string &path, const std::string &optional)
        : LibimeDecoder(cowtype::PinyinSources{path, optional, "", "", ""}, false) {}
    LibimeDecoder(const cowtype::PinyinSources &sources, bool statisticalModel) {
        auto compiled = cowtype::loadPinyin(sources);
        warnings = compiled.warnings; cacheHit = compiled.cacheHit;
        dictionary_ = compiled.dictionary.get();
        auto model = std::make_unique<libime::UserLanguageModel>();
        if (statisticalModel) {
            try { const auto path = modelPath();
                if (path.empty()) throw std::runtime_error("Statistical model path is empty");
                model = std::make_unique<libime::UserLanguageModel>(path.c_str()); modelLoaded = true; }
            catch (const std::exception &) { warnings |= ModelUnavailable; }
        }
        ime_ = std::make_unique<libime::PinyinIME>(std::move(compiled.dictionary), std::move(model));
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
        // Decorate once: full literal matches first, then source tier. Keep the
        // decoder's statistical/frequency order inside each tier. A composed
        // sentence belongs to its least preferred constituent dictionary.
        auto tier = [&](const libime::SentenceResult &candidate) {
            unsigned result = 0;
            for (const auto *node : candidate.sentence()) {
                const auto *pinyin = dynamic_cast<const libime::PinyinLatticeNode *>(node);
                unsigned source = 2;
                if (pinyin) {
                    const auto &encoded = pinyin->encodedPinyin();
                    const auto reading = libime::PinyinEncoder::decodeFullPinyin(encoded.data(), encoded.size());
                    for (unsigned i = 0; i < 3; ++i) {
                        if (dictionary_->lookupWord(i, reading, node->word()).has_value()) { source = i; break; }
                    }
                }
                result = std::max(result, source);
            }
            return result;
        };
        struct Ranked { size_t index; bool full; unsigned source; };
        std::vector<Ranked> ranked;
        for (size_t i = 0; i < candidates.size(); ++i) ranked.push_back({i, literal(candidates[i]), tier(candidates[i])});
        std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) {
            if (a.full != b.full) return a.full > b.full;
            return a.source < b.source;
        });
        for(const auto &rank:ranked) {
            const auto &candidate = candidates[rank.index];
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
