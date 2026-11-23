// SPDX-License-Identifier: GPL-3.0-or-later
// Adapter against libime public APIs with disjoint source layers and optional local LM.
#pragma once
#include "cowtype2.h"
#include "dictionary.h"
#include "spelling.h"
#include "shuangpin.h"
#include <libime/pinyin/pinyindecoder.h>
#include <cstdlib>
#include <libime/pinyin/pinyinime.h>
#include <libime/pinyin/pinyincontext.h>
#include <libime/pinyin/pinyindictionary.h>
#include <libime/core/userlanguagemodel.h>
#include <libime/core/lattice.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <unordered_map>
#include <stdexcept>

#ifndef COWTYPE_LIBIME_MODEL_FILE
#define COWTYPE_LIBIME_MODEL_FILE "/usr/lib/libime/zh_CN.lm"
#endif
class LibimeDecoder {
    std::mutex mutex_;
    std::unique_ptr<libime::PinyinIME> ime_;
    const libime::PinyinDictionary *dictionary_ = nullptr;
    // Stateless query workspace only: never select, learn or set context words.
    // Reuse append/backspace graphs; unrelated inputs and option changes reset it.
    std::array<std::unique_ptr<libime::PinyinContext>,2> contexts_;
    std::array<uint32_t,2> contextFlags_{};
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
    LibimeDecoder(const cowtype::PinyinSources &sources, bool statisticalModel, unsigned profile=0, const std::string &customProfile="") {
        (void)cowtype::spellingSyllables();
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
        ime_->setShuangpinProfile(cowtype::shuangpinProfile(profile,customProfile,ime_->correctionProfile().get()));
    }
    bool shuangpinSemicolon() const { return ime_->shuangpinProfile()->validInput().contains(';'); }
    static void destroy(void *p) { delete static_cast<LibimeDecoder *>(p); }
    static void query(void *p,const char *raw,uint32_t flags,Cowtype2Emit emit,void *output) noexcept {
        try { static_cast<LibimeDecoder *>(p)->queryImpl(raw,flags,emit,output); }
        catch(const std::exception &) { /* Never unwind through the Rust ABI; raw input remains available. */ }
    }
    void queryImpl(std::string raw,uint32_t flags,Cowtype2Emit emit,void *output) {
        std::lock_guard lock(mutex_);
        if(raw.empty() || raw.size()>128) return;
        std::transform(raw.begin(),raw.end(),raw.begin(),[](unsigned char c){return std::tolower(c);});
        const bool shuangpin=flags&16;
        if(raw.find_first_not_of(shuangpin?"abcdefghijklmnopqrstuvwxyz;' ":"abcdefghijklmnopqrstuvwxyz' ")!=std::string::npos) return;
        std::replace(raw.begin(),raw.end(),' ','\'');
        auto workspace = [&](bool correction) -> libime::PinyinContext & {
            const unsigned slot=correction?1:0;
            const uint32_t options=(flags&~2u)|(correction?2u:0u);
            libime::PinyinFuzzyFlags fuzzy;
            if(correction) fuzzy |= libime::PinyinFuzzyFlags(libime::PinyinFuzzyFlag::CommonTypo) | libime::PinyinFuzzyFlag::AdvancedTypo | libime::PinyinFuzzyFlag::Correction;
            if(flags&4) fuzzy |= libime::PinyinFuzzyFlag::PartialFinal;
            // libime clears every attached Context on optionChanged, even
            // when a setter receives the existing value.
            if(ime_->fuzzyFlags()!=fuzzy) ime_->setFuzzyFlags(fuzzy);
            auto &context=contexts_[slot];
            if(!context || contextFlags_[slot]!=options ||
               (!raw.starts_with(context->userInput()) && !context->userInput().starts_with(raw))) {
                context=std::make_unique<libime::PinyinContext>(ime_.get());
                context->setMaxSentenceLength(128);
                context->setUseShuangpin(shuangpin);
                contextFlags_[slot]=options;
            }
            const auto previous=context->userInput();
            if(raw.size()<previous.size()) context->erase(raw.size(),previous.size());
            else if(raw.size()>previous.size()) context->type(raw.substr(previous.size()));
            return *context;
        };
        auto hasCorrection=[](const libime::SentenceResult &candidate) {
            return std::any_of(candidate.sentence().begin(),candidate.sentence().end(),[](const auto *node) {
                const auto *pinyin=dynamic_cast<const libime::PinyinLatticeNode *>(node);
                return pinyin && pinyin->isCorrection();
            });
        };
        auto &primary=workspace(flags&2);
        const auto &primaryCandidates=primary.candidates();
        // Typo edges can prune a literal sentence even when the surviving
        // multiword path has no correction marker. Preserve the plain sentence
        // search for composed best paths, as well as explicit corrections.
        const bool recover=!shuangpin && (flags&2) && !primaryCandidates.empty() &&
            (hasCorrection(primaryCandidates.front()) || primaryCandidates.front().size()>1);
        std::string typed=raw;
        typed.erase(std::remove(typed.begin(),typed.end(),'\''),typed.end());
        // Lattice nodes recur in sentence candidates: identify each source once.
        std::unordered_map<const libime::LatticeNode *,unsigned> sources;
        auto tier = [&](const libime::SentenceResult &candidate) {
            unsigned result = 0;
            for(const auto *node:candidate.sentence()) {
                auto [entry,inserted]=sources.try_emplace(node,2);
                if(inserted) {
                    const auto *pinyin=dynamic_cast<const libime::PinyinLatticeNode *>(node);
                    if(pinyin && !pinyin->encodedPinyin().empty()) {
                        const auto &encoded=pinyin->encodedPinyin();
                        const auto reading=libime::PinyinEncoder::decodeFullPinyin(encoded.data(),encoded.size());
                        for(unsigned i=0;i<3;++i) {
                            if(dictionary_->lookupWord(i,reading,node->word()).has_value()) {entry->second=i;break;}
                        }
                    }
                }
                result=std::max(result,entry->second);
            }
            return result;
        };
        struct Ranked {
            std::string text,reading;
            size_t consumed;
            bool corrected,literal,recovered;
            unsigned source;
            bool protectedReading=false, supplemental=false, completion=false;
            float quality=0;
        };
        // Validate actual syllable boundaries rather than treating every
        // non-correction path (including arbitrary mixed fallback) as intentional.
        auto typedReading=[&](const std::string &reading,size_t consumed,bool allowInitials=true) {
            auto input=raw.substr(0,consumed);
            input.erase(std::remove(input.begin(),input.end(),'\''),input.end());
            std::istringstream syllables(reading);
            std::vector<size_t> positions{0};
            std::string syllable;
            while(std::getline(syllables,syllable,'\'')) {
                std::vector<size_t> next;
                for(auto position:positions) {
                    if(input.compare(position,syllable.size(),syllable)==0) next.push_back(position+syllable.size());
                    if(allowInitials && (flags&8) && position<input.size() && input[position]==syllable[0]) next.push_back(position+1);
                }
                std::sort(next.begin(),next.end());
                next.erase(std::unique(next.begin(),next.end()),next.end());
                positions=std::move(next);
                if(positions.empty()) return false;
            }
            return std::find(positions.begin(),positions.end(),input.size())!=positions.end();
        };
        std::vector<Ranked> ranked;
        for(unsigned pass=0;pass<(recover?2u:1u);++pass) {
            auto &context=pass?workspace(false):primary;
            // Changing fuzzy options clears the other lattice: node addresses
            // are valid only within this pass, never across the mode switch.
            sources.clear();
            const auto &candidates=context.candidates();
            ranked.reserve(ranked.size()+candidates.size());
            for(const auto &candidate:candidates) {
                if(candidate.sentence().empty() || (!(flags&1) && candidate.size()>1)) continue;
                auto reading=context.candidateFullPinyin(candidate);
                if(reading.empty()) continue;
                const size_t consumed=candidate.sentence().back()->to()->index();
                std::string full=reading;
                full.erase(std::remove(full.begin(),full.end(),'\''),full.end());
                const bool literal=full==typed && consumed==raw.size();
                if(pass && !literal) continue;
                const bool corrected=hasCorrection(candidate);
                if(!shuangpin && !(flags&8)) {
                    auto prefix=raw.substr(0,consumed);
                    prefix.erase(std::remove(prefix.begin(),prefix.end(),'\''),prefix.end());
                    if(full!=prefix && !((flags&4) && full.starts_with(prefix))) continue;
                }
                const bool protectedReading=!corrected && (shuangpin || typedReading(reading,consumed));
                std::replace(reading.begin(),reading.end(),'\'',' ');
                ranked.push_back({candidate.toString(),std::move(reading),consumed,corrected,literal,bool(pass),tier(candidate),protectedReading});
            }
        }
        // Supplementary word search gets its own budget. Exact words and
        // genuine initials stay ahead; speculative native sentence paths do not
        // automatically suppress a reliable repaired whole word.
        const bool exactReading=std::any_of(ranked.begin(),ranked.end(),[&](const auto &c) {
            return c.consumed==raw.size() && !c.corrected && typedReading(c.reading,c.consumed,false);
        });
        const bool searchCorrections=!shuangpin && (flags&2) && !exactReading;
        const bool searchCompletions=!shuangpin && (flags&4) && !exactReading;
        if(searchCorrections || searchCompletions) {
            for(auto &match:cowtype::spellingMatches(*dictionary_,raw,searchCompletions)) {
                if(match.edits && !searchCorrections) continue;
                if(match.completion && !searchCompletions) continue;
                ranked.push_back({std::move(match.text),std::move(match.reading),raw.size(),match.edits>0,false,false,match.source,
                    false,true,match.completion,match.weight-match.penalty});
            }
        }
        auto category=[&](const Ranked &c) {
            if(c.consumed!=raw.size()) return 3u;
            if(c.protectedReading) return 0u;
            if(c.supplemental) return 1u;
            return 2u;
        };
        std::stable_sort(ranked.begin(),ranked.end(),[&](const auto &a,const auto &b) {
            const auto ac=category(a),bc=category(b);
            if(ac!=bc) return ac<bc;
            if(ac==1) {
                if(a.quality!=b.quality) return a.quality>b.quality;
                return a.source<b.source;
            }
            if(a.corrected!=b.corrected) return a.corrected<b.corrected;
            if(a.literal!=b.literal) return a.literal>b.literal;
            if(a.source!=b.source) return a.source<b.source;
            return a.recovered>b.recovered;
        });
        std::set<std::pair<std::string,size_t>> seen;
        const unsigned correctionLimit=(flags>>8)&0x0fu;
        unsigned correctionCount=0;
        for(const auto &candidate:ranked) {
            if(candidate.corrected && correctionCount>=correctionLimit) continue;
            if(seen.emplace(candidate.text,candidate.consumed).second) {
                emit(output,candidate.text.c_str(),candidate.reading.c_str(),candidate.consumed,candidate.completion?2u:unsigned(candidate.corrected));
                if(candidate.corrected) ++correctionCount;
            }
        }
    }
};
