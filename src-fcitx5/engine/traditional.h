// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <dlfcn.h>
#include <mutex>
#include <string>

namespace cowtype {
// OpenCC is an optional runtime backend. It is loaded dynamically so a missing
// optional library never prevents the input method from starting.
class TraditionalConverter {
    using Handle = void *;
    using Open = Handle (*)(const char *);
    using Close = int (*)(Handle);
    using Convert = char *(*)(Handle, const char *, size_t);
    using Free = void (*)(char *);
    void *library_ = nullptr;
    Open open_ = nullptr;
    Close close_ = nullptr;
    Convert convert_ = nullptr;
    Free free_ = nullptr;
    std::array<Handle,2> converters_{};
    mutable std::mutex mutex_;
public:
    TraditionalConverter() {
        for(const char *name:{"libopencc.so.1.4","libopencc.so.1","libopencc.so.2","libopencc.so"}) {
            library_=dlopen(name,RTLD_NOW|RTLD_LOCAL);
            if(library_) break;
        }
        if(!library_) return;
        open_=reinterpret_cast<Open>(dlsym(library_,"opencc_open"));
        close_=reinterpret_cast<Close>(dlsym(library_,"opencc_close"));
        convert_=reinterpret_cast<Convert>(dlsym(library_,"opencc_convert_utf8"));
        free_=reinterpret_cast<Free>(dlsym(library_,"opencc_convert_utf8_free"));
        if(!open_ || !close_ || !convert_ || !free_) { dlclose(library_);library_=nullptr;return; }
        converters_[0]=open_("s2tw.json");
        if(!converters_[0] || converters_[0]==reinterpret_cast<Handle>(-1)) converters_[0]=open_("s2t.json");
        converters_[1]=open_("tw2s.json");
        if(!converters_[1] || converters_[1]==reinterpret_cast<Handle>(-1)) converters_[1]=open_("t2s.json");
        for(auto &converter:converters_)
            if(converter==reinterpret_cast<Handle>(-1)) converter=nullptr;
    }
    ~TraditionalConverter() {
        for(auto converter:converters_) if(converter && close_) close_(converter);
        if(library_) dlclose(library_);
    }
    TraditionalConverter(const TraditionalConverter&)=delete;
    TraditionalConverter& operator=(const TraditionalConverter&)=delete;
    bool available() const { return converters_[0] && converters_[1]; }
    std::string convert(const std::string &text,bool traditional) const {
        std::lock_guard lock(mutex_);
        auto converter=converters_[traditional?0:1];
        if(!converter || text.empty()) return text;
        char *value=convert_(converter,text.data(),text.size());
        if(!value) return text;
        std::string result(value);
        free_(value);
        return result;
    }
};
} // namespace cowtype
