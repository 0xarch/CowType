// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <libime/pinyin/shuangpinprofile.h>
#include "shuangpin-gb.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace cowtype {
// Scheme data stays in libime. CowType owns selection, validation and lifecycle.
inline std::shared_ptr<const libime::ShuangpinProfile> shuangpinProfile(unsigned profile,const std::string &path,const libime::PinyinCorrectionProfile *correction) {
    auto fromStream=[&](std::istream &input) {
#if COWTYPE_SHUANGPIN_CORRECTION && !defined(COWTYPE_TEST_LEGACY_SHUANGPIN)
        return std::make_shared<libime::ShuangpinProfile>(input,correction);
#else
        (void)correction;
        return std::make_shared<libime::ShuangpinProfile>(input);
#endif
    };
#if !COWTYPE_SHUANGPIN_GB || defined(COWTYPE_TEST_LEGACY_SHUANGPIN)
    if(profile==7) {std::istringstream input(gbShuangpinProfile);return fromStream(input);}
#endif
    if(profile<8) {
#if COWTYPE_SHUANGPIN_CORRECTION && !defined(COWTYPE_TEST_LEGACY_SHUANGPIN)
        return std::make_shared<libime::ShuangpinProfile>(static_cast<libime::ShuangpinBuiltinProfile>(profile),correction);
#else
        return std::make_shared<libime::ShuangpinProfile>(static_cast<libime::ShuangpinBuiltinProfile>(profile));
#endif
    }
    if(profile!=8 || path.empty() || !std::filesystem::path(path).is_absolute())
        throw std::runtime_error("Invalid custom shuangpin profile path");
    std::error_code error;
    const auto size=std::filesystem::file_size(path,error);
    if(error || size>65536) throw std::runtime_error("Custom shuangpin profile is unavailable or too large");
    std::ifstream file(path);
    if(!file) throw std::runtime_error("Custom shuangpin profile cannot be read");
    std::string text(65537, '\0');
    file.read(text.data(),text.size());text.resize(file.gcount());
    if(text.size()>65536) throw std::runtime_error("Custom shuangpin profile is too large");
    if(text.find('\0')!=std::string::npos) throw std::runtime_error("Invalid custom shuangpin profile");
    std::istringstream lines(text);
    std::string line;
    size_t mappings=0;
    std::string validated;
    while(std::getline(lines,line)) {
        const auto first=line.find_first_not_of(" \t\r");
        if(first==std::string::npos) continue;
        line=line.substr(first,line.find_last_not_of(" \t\r")-first+1);
        if(line.starts_with("方案名称=")) continue;
        validated+=line+"\n";
        if(line[0]=='#' || (line.front()=='[' && line.back()==']')) continue;
        const auto equal=line.find('=');
        if(equal==std::string::npos) throw std::runtime_error("Invalid custom shuangpin mapping");
        const auto reading=line.substr(0,equal),code=line.substr(equal+1);
        if(code.size()>2 || code.find_first_not_of(equal==0?"abcdefghijklmnopqrstuvwxyz;*":"abcdefghijklmnopqrstuvwxyz;")!=std::string::npos)
            throw std::runtime_error("Invalid custom shuangpin key");
        if(equal==0) continue;
        if(code.empty() || reading.find_first_not_of("abcdefghijklmnopqrstuvwxyz")!=std::string::npos)
            throw std::runtime_error("Invalid custom shuangpin reading");
        if(code.size()==1) {
            if(libime::PinyinEncoder::stringToInitial(reading)==libime::PinyinInitial::Invalid &&
               libime::PinyinEncoder::stringToFinal(reading)==libime::PinyinFinal::Invalid)
                throw std::runtime_error("Unknown custom shuangpin reading");
        } else {
            try {if(libime::PinyinEncoder::encodeFullPinyin(reading).size()!=2) throw std::runtime_error("Invalid syllable");}
            catch(const std::exception &) {throw std::runtime_error("Invalid custom shuangpin syllable");}
        }
        ++mappings;
    }
    if(!mappings) throw std::runtime_error("Custom shuangpin profile has no mappings");
    std::istringstream input(validated);
    auto result=fromStream(input);
    if(result->table().empty()) throw std::runtime_error("Custom shuangpin profile has no usable mappings");
    return result;
}
}
