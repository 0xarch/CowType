// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "messages.h"
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
#include <charconv>
#include <functional>
#include <string>
#include <memory>
extern char **environ;
// One private resident child. No listening socket, idle polling or audio device.
// Generation-tagged replies are accepted only by the original live focused owner.
class VoiceWorker {
public:
 using Callback=std::function<void(fcitx::InputContext *,char,const std::string &)>;
 VoiceWorker(fcitx::EventLoop &loop, Callback callback):callback_(std::move(callback)) {
  timer_=loop.addTimeEvent(CLOCK_MONOTONIC,fcitx::now(CLOCK_MONOTONIC)+20000,1000,[this](fcitx::EventSourceTime *source,uint64_t){
   poll();if(running_){source->setNextInterval(20000);source->setOneShot();}return true;
  });timer_->setEnabled(false);
 }
 ~VoiceWorker(){close();}
 fcitx::InputContext *owner()const{return owner_.get();}
 void setResident(bool value){resident_=value;if(!value && !running_){close();}}
 bool active()const{return running_ && !preparing_;}
 bool owns(fcitx::InputContext *ic)const{return active() && !cancelled_ && owner_.get()==ic;}
 void cancel(fcitx::InputContext *ic){if(owns(ic)){cancelCurrent();}}
 void cancelAll(){close();}
 bool prepare(const std::string &executable){
  if(running_ || fd_>=0){return true;}
  if(!spawn(executable)){return false;}
  ++generation_;if(!generation_){++generation_;}
  preparing_=true;running_=true;cancelled_=false;stopping_=0;started_=fcitx::now(CLOCK_MONOTONIC);
  if(!sendCommand("prepare")){close();return false;}
  timer_->setTime(started_+20000);timer_->setOneShot();return true;
 }
 bool start(fcitx::InputContext *ic,const std::string &executable){
  if(active()){return false;}
  if(fd_>=0){
   const auto reaped=waitpid(pid_,nullptr,WNOHANG);
   if(reaped==pid_ || (reaped<0 && errno==ECHILD)){pid_=-1;close();}
   else if(executable_!=executable){close();}
  }
  if(fd_<0 && !spawn(executable)){return false;}
  ++generation_;if(!generation_){++generation_;}
  owner_=ic->watch();preparing_=false;running_=true;cancelled_=false;stopping_=0;started_=fcitx::now(CLOCK_MONOTONIC);
  if(!sendCommand("start")){close();return false;}
  timer_->setTime(started_+20000);timer_->setOneShot();return true;
 }
 void stop(){if(running_ && !cancelled_ && !stopping_){stopping_=fcitx::now(CLOCK_MONOTONIC);if(!sendCommand("stop")){fail(ct_tr("message.5c7e93923246"));}}}
private:
 bool spawn(const std::string &executable){
  int pair[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,pair)!=0){return false;}
  // Reserve ordinary descriptor numbers even if the host launched without stdio.
  for(auto &fd:pair){if(fd<=STDERR_FILENO){int copy=fcntl(fd,F_DUPFD_CLOEXEC,3);::close(fd);if(copy<0){for(auto other:pair){if(other>STDERR_FILENO){::close(other);}}return false;}fd=copy;}}
  posix_spawn_file_actions_t actions;
  int result=posix_spawn_file_actions_init(&actions);
  if(result){::close(pair[0]);::close(pair[1]);return false;}
  result=posix_spawn_file_actions_adddup2(&actions,pair[1],STDIN_FILENO);
  if(!result){result=posix_spawn_file_actions_adddup2(&actions,pair[1],STDOUT_FILENO);}
  if(!result){result=posix_spawn_file_actions_addclose(&actions,pair[0]);}
  if(!result){result=posix_spawn_file_actions_addclose(&actions,pair[1]);}
  if(!result){result=posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);}
  char serve[]="--serve";char *args[]={const_cast<char *>(executable.c_str()),serve,nullptr};
  if(!result){result=posix_spawn(&pid_,executable.c_str(),&actions,nullptr,args,environ);}
  posix_spawn_file_actions_destroy(&actions);::close(pair[1]);
  if(result){::close(pair[0]);pid_=-1;return false;}
  fd_=pair[0];executable_=executable;return true;
 }
 bool sendCommand(const char *action){
  const auto message=std::string(action)+" "+std::to_string(generation_)+"\n";
  return fd_>=0 && send(fd_,message.data(),message.size(),MSG_NOSIGNAL|MSG_DONTWAIT)==static_cast<ssize_t>(message.size());
 }
 void cancelCurrent(){
  cancelled_=true;owner_.unwatch();stopping_=fcitx::now(CLOCK_MONOTONIC);
  if(!sendCommand("cancel")){close();}
 }
 void close(){
  if(timer_){timer_->setEnabled(false);}if(fd_>=0){::close(fd_);fd_=-1;}
  if(pid_>0){kill(pid_,SIGKILL);while(waitpid(pid_,nullptr,0)<0 && errno==EINTR){}}pid_=-1;owner_.unwatch();buffer_.clear();running_=false;preparing_=false;cancelled_=false;
 }
 void fail(const std::string &error){auto *ic=owner_.get();close();if(ic){callback_(ic,'E',error);}}
 void poll(){
  auto *ic=owner_.get();
  if(!preparing_ && !cancelled_ && (!ic || !ic->hasFocus() || ic->capabilityFlags().testAny(fcitx::CapabilityFlags(fcitx::CapabilityFlag::PasswordOrSensitive) | fcitx::CapabilityFlag::Disable))){cancelCurrent();if(!running_){return;}}
  const auto time=fcitx::now(CLOCK_MONOTONIC);
  if(time-started_>150000000 || (stopping_ && time-stopping_>(cancelled_?5000000:30000000))){fail(ct_tr("message.2271656204e4"));return;}
  char bytes[4096];
  while(fd_>=0){
   const auto count=recv(fd_,bytes,sizeof(bytes),MSG_DONTWAIT);
   if(count==0){fail(ct_tr("message.cf9746cec391"));return;}
   if(count<0){if(errno==EAGAIN || errno==EWOULDBLOCK){break;}if(errno==EINTR){continue;}fail(ct_tr("message.8eb75ca37861"));return;}
   buffer_.append(bytes,count);if(buffer_.size()>65536){fail(ct_tr("message.15350d9cee46"));return;}
   size_t newline;
   while((newline=buffer_.find('\n'))!=std::string::npos){
    auto line=buffer_.substr(0,newline);buffer_.erase(0,newline+1);
    auto tab=line.find('\t');uint64_t id=0;
    if(tab==std::string::npos){fail(ct_tr("message.e3bbd74cca85"));return;}
    const auto parsed=std::from_chars(line.data(),line.data()+tab,id);
    if(parsed.ec!=std::errc{} || parsed.ptr!=line.data()+tab || line.size()<tab+3 || line[tab+2]!='\t'){fail(ct_tr("message.e3bbd74cca85"));return;}
    if(id!=generation_){continue;}
    const char kind=line[tab+1];if(kind!='R' && kind!='P' && kind!='F' && kind!='E' && kind!='D' && kind!='V'){fail(ct_tr("message.cafaa54a21bf"));return;}
    auto text=line.substr(tab+3);if(!fcitx::utf8::validate(text)){fail(ct_tr("message.9bcf5a1e4f0b"));return;}
    if(kind=='F' || kind=='E' || kind=='D'){
     auto *target=cancelled_?nullptr:owner_.get();owner_.unwatch();running_=false;preparing_=false;timer_->setEnabled(false);
     if(!resident_){close();}
     if(target && target->hasFocus() && kind!='D'){callback_(target,kind,text);}return;
    }
    if(!cancelled_ && ic){callback_(ic,kind,text);}
   }
  }
 }
 Callback callback_;fcitx::TrackableObjectReference<fcitx::InputContext> owner_;
 std::unique_ptr<fcitx::EventSourceTime> timer_;int fd_=-1;pid_t pid_=-1;
 uint64_t started_=0,stopping_=0,generation_=0;bool preparing_=false,running_=false,cancelled_=false,resident_=true;std::string buffer_,executable_;
};
