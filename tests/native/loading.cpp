// SPDX-License-Identifier: GPL-3.0-or-later
// Independent startup/reload probe; only synthetic or freshly staged public data.
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx-utils/event.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include "isolation.h"
using namespace fcitx;
struct LoadingField : InputContext {
    explicit LoadingField(InputContextManager &manager):InputContext(manager,"cowtype-loading-test"){created();setEnablePreedit(true);}
    ~LoadingField() override{destroy();}
    const char *frontend()const override{return "cowtype-synthetic";}
    void commitStringImpl(const std::string &)override{}
    void deleteSurroundingTextImpl(int,unsigned)override{}
    void forwardKeyImpl(const ForwardKeyEvent &)override{}
    void updatePreeditImpl()override{}
};
size_t builds() {
    std::ifstream log(std::filesystem::path(std::getenv("XDG_STATE_HOME"))/"cowtype/engine.log");
    std::string line;size_t count=0;
    while(std::getline(log,line)) if(line.find("event=dictionary_start ")!=std::string::npos) ++count;
    return count;
}
int main(int argc,char **argv) {
    try {
        if(argc!=2 || !isolatedPaths()) throw std::runtime_error("Missing isolated test paths");
        const auto config=std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"cowtype/settings-v1.ini";
        std::filesystem::remove(config);
        std::filesystem::remove(std::filesystem::path(std::getenv("XDG_STATE_HOME"))/"cowtype/engine.log");
        char name[]="cowtype-loading-test", disabled[]="--disable=all";char *args[]={name,disabled,nullptr};
        Instance instance(2,args);instance.initialize();
        auto *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!module)throw std::runtime_error(dlerror());
        auto factory=reinterpret_cast<AddonFactory *(*)()>(dlsym(module,cowtype::compat::factorySymbol));
        if(!factory)throw std::runtime_error("Missing factory");
        {
            const auto began=now(CLOCK_MONOTONIC);
            std::unique_ptr<AddonInstance> addon(factory()->create(&instance.addonManager()));
            auto *engine=dynamic_cast<InputMethodEngine *>(addon.get());
            if(!engine)throw std::runtime_error("Missing engine");
            LoadingField field(instance.inputContextManager());field.setCapabilityFlags(CapabilityFlag::Preedit);field.focusIn();
            InputMethodEntry entry("cowtype","CowType","zh_CN","cowtype");
            InputContextEvent activation(&field,EventType::InputContextInputMethodActivated);engine->activate(entry,activation);
            for(auto c:std::string("shan")){KeyEvent event(&field,Key(static_cast<KeySym>(c)),false);engine->keyEvent(entry,event);}
            const auto startupMs=(now(CLOCK_MONOTONIC)-began)/1000;
            bool passed=false;unsigned ticks=0;uint64_t readyMs=0;std::string failure;
            auto timer=instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC,now(CLOCK_MONOTONIC)+10000,1000,
                [&](EventSourceTime *source,uint64_t){
                    ++ticks;
                    if(field.inputPanel().candidateList()) {
                        try {
                            readyMs=(now(CLOCK_MONOTONIC)-began)/1000;
                            const auto before=builds();if(before!=2)throw std::runtime_error("Expected initial pinyin and Wubi builds");
                            std::filesystem::create_directories(config.parent_path());
                            {std::ofstream settings(config);settings<<"[CowType]\nVersion=1\nPageSize=7\nShowPinyin=False\n";}
                            addon->reloadConfig();
                            if(builds()!=before)throw std::runtime_error("Settings-only reload rebuilt dictionaries");
                            passed=true;
                        }catch(const std::exception &error){failure=error.what();}
                        instance.eventLoop().exit();return false;
                    }
                    if(now(CLOCK_MONOTONIC)-began>60000000){failure="No candidate refresh without another key";instance.eventLoop().exit();return false;}
                    source->setNextInterval(10000);source->setOneShot();return true;
                });
            instance.eventLoop().exec();field.focusOut();
            if(!passed)throw std::runtime_error(failure);
            std::cout<<"startup_ms="<<startupMs<<" candidate_ready_ms="<<readyMs<<" heartbeat_ticks="<<ticks<<" settings_reload_reused=true automatic_refresh=true\n";
        }
        dlclose(module);
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
