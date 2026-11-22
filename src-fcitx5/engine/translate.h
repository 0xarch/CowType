// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <fcitx/inputcontext.h>
#include <fcitx-utils/event.h>
#include <fcitx-utils/utf8.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <spawn.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <cerrno>
#include <functional>
#include <memory>
#include <string>
extern char **environ;

// One cancellable child per request. The Fcitx event loop only exchanges bounded
// nonblocking socket data; HTTP and Secret Service access run in the child.
class TranslateWorker {
public:
    using Callback = std::function<void(fcitx::InputContext *, bool, const std::string &)>;
    TranslateWorker(fcitx::EventLoop &loop, Callback callback):callback_(std::move(callback)) {
        timer_=loop.addTimeEvent(CLOCK_MONOTONIC,fcitx::now(CLOCK_MONOTONIC)+20000,1000,
            [this](fcitx::EventSourceTime *source,uint64_t) {
                poll();if(fd_>=0){source->setNextInterval(20000);source->setOneShot();}return true;
            });timer_->setEnabled(false);
    }
    ~TranslateWorker(){cancel();}
    bool owns(fcitx::InputContext *ic)const{return fd_>=0 && owner_.get()==ic;}
    bool active()const{return fd_>=0;}
    void cancel(fcitx::InputContext *ic){if(owns(ic))cancel();}
    void cancel(){
        timer_->setEnabled(false);owner_.unwatch();
        if(fd_>=0){::close(fd_);fd_=-1;}
        if(pid_>0){kill(pid_,SIGKILL);while(waitpid(pid_,nullptr,0)<0 && errno==EINTR){}}
        pid_=-1;input_.clear();output_.clear();offset_=0;
    }
    bool start(fcitx::InputContext *ic,const std::string &text,const std::string &executable){
        cancel();if(text.empty() || text.size()>32768)return false;
        int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair))return false;
        for(auto &fd:pair){
            if(fd<=STDERR_FILENO){int copy=fcntl(fd,F_DUPFD_CLOEXEC,3);::close(fd);
                if(copy<0){for(auto other:pair)if(other>STDERR_FILENO)::close(other);return false;}fd=copy;}
        }
        posix_spawn_file_actions_t actions;
        int status=posix_spawn_file_actions_init(&actions);
        if(status){::close(pair[0]);::close(pair[1]);return false;}
        status=posix_spawn_file_actions_adddup2(&actions,pair[1],STDIN_FILENO);
        if(!status)status=posix_spawn_file_actions_adddup2(&actions,pair[1],STDOUT_FILENO);
        if(!status)status=posix_spawn_file_actions_addclose(&actions,pair[0]);
        if(!status)status=posix_spawn_file_actions_addclose(&actions,pair[1]);
        if(!status)status=posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
        char *args[]={const_cast<char *>(executable.c_str()),nullptr};
        if(!status)status=posix_spawn(&pid_,executable.c_str(),&actions,nullptr,args,environ);
        posix_spawn_file_actions_destroy(&actions);::close(pair[1]);
        if(status){::close(pair[0]);pid_=-1;return false;}
        constexpr char hex[]="0123456789abcdef";
        for(unsigned char c:text){input_+=hex[c>>4];input_+=hex[c&15];}input_+='\n';
        fd_=pair[0];owner_=ic->watch();started_=fcitx::now(CLOCK_MONOTONIC);
        timer_->setTime(started_+20000);timer_->setOneShot();return true;
    }
private:
    void finish(bool success,const std::string &text){auto *ic=owner_.get();auto value=text;cancel();if(ic)callback_(ic,success,value);}
    static int digit(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;}
    void poll(){
        auto *ic=owner_.get();
        if(!ic || !ic->hasFocus() || ic->capabilityFlags().testAny(fcitx::CapabilityFlags(fcitx::CapabilityFlag::PasswordOrSensitive)|fcitx::CapabilityFlag::Disable)){cancel();return;}
        if(fcitx::now(CLOCK_MONOTONIC)-started_>35000000){finish(false,"translate.error.timeout");return;}
        while(offset_<input_.size()){
            auto sent=send(fd_,input_.data()+offset_,input_.size()-offset_,MSG_NOSIGNAL|MSG_DONTWAIT);
            if(sent<0){if(errno==EINTR)continue;if(errno==EAGAIN||errno==EWOULDBLOCK)return;finish(false,"translate.error.process");return;}
            offset_+=sent;
        }
        char bytes[4096];
        while(fd_>=0){
            auto count=recv(fd_,bytes,sizeof(bytes),MSG_DONTWAIT);
            if(count<0){if(errno==EINTR)continue;if(errno==EAGAIN||errno==EWOULDBLOCK)return;finish(false,"translate.error.process");return;}
            if(!count){finish(false,"translate.error.process");return;}
            output_.append(bytes,count);
            if(output_.size()>131076){finish(false,"translate.error.response");return;}
            auto newline=output_.find('\n');if(newline==std::string::npos)continue;
            auto line=output_.substr(0,newline);
            if(line.starts_with("E\ttranslate.error.") && line.size()<96){finish(false,line.substr(2));return;}
            if(!line.starts_with("F\t") || (line.size()-2)%2){finish(false,"translate.error.response");return;}
            std::string result;
            for(size_t i=2;i<line.size();i+=2){
                auto a=digit(line[i]),b=digit(line[i+1]);
                if(a<0||b<0){finish(false,"translate.error.response");return;}
                auto c=char(a*16+b);if((static_cast<unsigned char>(c)<32 && c!='\n' && c!='\t' && c!='\r') || c==127){finish(false,"translate.error.response");return;}result+=c;
            }
            if(result.empty()||!fcitx::utf8::validate(result)){finish(false,"translate.error.response");return;}
            finish(true,result);return;
        }
    }
    Callback callback_;fcitx::TrackableObjectReference<fcitx::InputContext> owner_;
    std::unique_ptr<fcitx::EventSourceTime> timer_;
    int fd_=-1;pid_t pid_=-1;uint64_t started_=0;size_t offset_=0;
    std::string input_,output_;
};
