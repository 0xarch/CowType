// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/inputpanel.h>
#include <fcitx-utils/dbus/bus.h>
#include <dlfcn.h>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "isolation.h"
#define CHECK(x) do { if(!(x))throw std::runtime_error("D-Bus check failed at line "+std::to_string(__LINE__)); } while(0)
using namespace fcitx;
struct Field : InputContext {
    Field(InputContextManager &manager):InputContext(manager,"cowtype-dbus-test"){created();}
    ~Field()override{destroy();}
    const char *frontend()const override{return "cowtype-synthetic";}
    std::vector<std::string> commits;
    void commitStringImpl(const std::string &text)override{commits.push_back(text);}
    void deleteSurroundingTextImpl(int,unsigned)override{}
    void forwardKeyImpl(const ForwardKeyEvent &)override{}
    void updatePreeditImpl()override{}
};
struct Call {const char *method;const char *argument;const char *expected;};
std::string invoke(const Call &call) {
    dbus::Bus bus(dbus::BusType::Session);
    auto message=bus.createMethodCall("org.cowtype.Input","/org/cowtype/Input","org.cowtype.Input1",call.method);
    if(call.argument) message<<std::string(call.argument);
    auto reply=message.call(3000000);
    if(reply.isError()) return reply.errorName();
    std::string value;reply>>value;return value;
}
int main(int argc,char **argv) {
    try {
        CHECK(argc==3 && isolatedPaths());
        char name[]="cowtype-dbus-test",disable[]="--disable=all";char *args[]={name,disable,nullptr};
        Instance instance(2,args);instance.initialize();
        void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(module);
        auto factory=reinterpret_cast<AddonFactory *(*)()>(dlsym(module,cowtype::compat::factorySymbol));CHECK(factory);
        {
            std::unique_ptr<AddonInstance> addon(factory()->create(&instance.addonManager()));
            auto *engine=dynamic_cast<InputMethodEngine *>(addon.get());CHECK(engine);
            Field field(instance.inputContextManager());field.setCapabilityFlags(CapabilityFlag::Preedit);
            InputMethodEntry entry("cowtype","CowType","zh_CN","cowtype");
            InputContextEvent activate(&field,EventType::InputContextInputMethodActivated);
            std::vector<Call> calls{
                {"Version",nullptr,argv[2]},
                {"Mode",nullptr,"org.cowtype.Input.Error.Inactive"},
                {"Mode",nullptr,"zh"},
                {"SetMode","zh","zh"},
                {"SetMode","en","en"},
                {"SetMode","en","en"},
                {"SetMode","toggle","zh"},
                {"SetMode","invalid","org.freedesktop.DBus.Error.InvalidArgs"},
                {"SetMode","en","org.cowtype.Input.Error.Unavailable"},
                {"Mode",nullptr,"org.cowtype.Input.Error.Unavailable"},
                {"Mode",nullptr,"org.cowtype.Input.Error.Inactive"},
                {"SetMode","zh","org.cowtype.Input.Error.Inactive"},
                {"Mode",nullptr,"org.cowtype.Input.Error.Inactive"}
            };
            size_t step=0;bool pending=false;std::future<std::string> reply;std::string failure;
            auto deadline=now(CLOCK_MONOTONIC)+10000000;
            auto timer=instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC,now(CLOCK_MONOTONIC)+1000,1000,
                [&](EventSourceTime *source,uint64_t){
                    try {
                        CHECK(now(CLOCK_MONOTONIC)<deadline);
                        if(!pending) {
                            if(step==2){field.focusIn();engine->activate(entry,activate);}
                            if(step==3) {
                                for(auto sym:{FcitxKey_z,FcitxKey_z}){KeyEvent event(&field,Key(sym),false);engine->keyEvent(entry,event);CHECK(event.accepted());}
                            }
                            if(step==8)field.setCapabilityFlags(CapabilityFlag::Password);
                            if(step==10){field.setCapabilityFlags(CapabilityFlag::Preedit);InputContextSwitchInputMethodEvent event(InputMethodSwitchedReason::Enumerate,"other",&field);engine->deactivate(entry,event);}
                            if(step==12)field.focusOut();
                            reply=std::async(std::launch::async,[call=calls[step]]{return invoke(call);});pending=true;
                        } else if(reply.wait_for(std::chrono::seconds(0))==std::future_status::ready) {
                            auto value=reply.get();
                            if(value!=calls[step].expected)throw std::runtime_error("D-Bus unexpected reply at step "+std::to_string(step)+": "+value);
                            if(step==3){CHECK(field.commits.empty());CHECK(field.inputPanel().clientPreedit().toString()=="zz");}
                            pending=false;++step;
                            if(step<=4)CHECK(field.commits.empty());
                            else {CHECK(field.commits.size()==1);CHECK(field.commits.front()=="zz");}
                            if(step==calls.size()){instance.eventLoop().exit();return false;}
                        }
                    } catch(const std::exception &error){failure=error.what();instance.eventLoop().exit();return false;}
                    source->setNextInterval(10000);source->setOneShot();return true;
                });
            instance.eventLoop().exec();
            if(!failure.empty())throw std::runtime_error(failure);
        }
        dlclose(module);std::cout<<"D-Bus private session: version, focused modes, idempotency, invalid arguments and restricted contexts passed\n";
    } catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
