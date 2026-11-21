// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx-utils/log.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>

// Only fixed stage names, durations and flags. Never input text or resource paths.
inline void ctDiagnostic(const std::string &fields) noexcept {
    try {
        FCITX_INFO() << "CowType " << fields;
        static std::mutex lock;
        std::lock_guard guard(lock);
        const char *state=std::getenv("XDG_STATE_HOME"), *home=std::getenv("HOME");
        if((!state || !*state) && (!home || !*home)) return;
        auto dir=std::filesystem::path(state && *state?state:std::string(home)+"/.local/state")/"cowtype";
        std::error_code error;std::filesystem::create_directories(dir,error);if(error) return;
        int fd=open((dir/"engine.log").c_str(),O_WRONLY|O_APPEND|O_CREAT|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK,0600);
        if(fd<0) return;
        struct stat info{};
        if(fstat(fd,&info)!=0 || !S_ISREG(info.st_mode) || info.st_uid!=getuid()) {::close(fd);return;}
        if(fchmod(fd,0600)!=0) {::close(fd);return;}
        if(info.st_size>1024*1024 && ftruncate(fd,0)!=0) {::close(fd);return;}
        const auto epoch=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        auto line="epoch_ms="+std::to_string(epoch)+" pid="+std::to_string(getpid())+" "+fields+"\n";
        (void)write(fd,line.data(),line.size());::close(fd);
    } catch(...) { /* Diagnostics never interrupt input. */ }
}

class CtSlowEvent {
    std::chrono::steady_clock::time_point began_=std::chrono::steady_clock::now();
public:
    ~CtSlowEvent() {
        const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-began_).count();
        if(ms>=20) ctDiagnostic("event=slow_key_event elapsed_ms="+std::to_string(ms));
    }
};
