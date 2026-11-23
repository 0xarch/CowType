// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/candidatelist.h>
#include <fcitx/instance.h>
#include <fcitx-utils/event.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "isolation.h"
using namespace fcitx;
#define CHECK(x) do {if(!(x))throw std::runtime_error("Double pinyin reload check failed at line "+std::to_string(__LINE__));}while(0)
struct Field : InputContext {
    Field(InputContextManager &m):InputContext(m,"cowtype-shuangpin-test"){created();}
    ~Field() override{destroy();}
    const char *frontend()const override{return "cowtype-synthetic";}
    std::vector<std::string> commits;
    void commitStringImpl(const std::string &text)override{commits.push_back(text);}
    void deleteSurroundingTextImpl(int,unsigned)override{}
    void forwardKeyImpl(const ForwardKeyEvent &)override{}
    void updatePreeditImpl()override{}
};
int main(int argc,char **argv) {
    try {
        CHECK(argc==2 && isolatedPaths());
        auto config=std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"cowtype/settings-v1.ini";
        std::filesystem::create_directories(config.parent_path());
        auto settings=[&](const std::string &profile,bool enabled=true) {
            std::ofstream out(config);out<<"[CowType]\nVersion=1\nLearning=False\nShuangpin="<<(enabled?"True":"False")<<"\nShuangpinProfile="<<profile<<"\nShuangpinCustom=/nonexistent/cowtype-test.conf\n";
        };
        settings("MS");
        char name[]="cowtype-shuangpin-test",disable[]="--disable=all";char *args[]={name,disable,nullptr};
        Instance instance(2,args);instance.initialize();
        void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(module);
        auto factory=reinterpret_cast<AddonFactory *(*)()>(dlsym(module,cowtype::compat::factorySymbol));CHECK(factory);
        std::unique_ptr<AddonInstance> addon(factory()->create(&instance.addonManager()));
        auto *engine=dynamic_cast<InputMethodEngine *>(addon.get());CHECK(engine);
        Field field(instance.inputContextManager());field.setCapabilityFlags(CapabilityFlag::Preedit);field.focusIn();
        InputMethodEntry entry("cowtype","CowType","zh_CN","cowtype");
        InputContextEvent activation(&field,EventType::InputContextInputMethodActivated);engine->activate(entry,activation);
        auto press=[&](KeySym key){KeyEvent event(&field,Key(key),false);engine->keyEvent(entry,event);};
        auto type=[&](const std::string &text){press(FcitxKey_Escape);for(auto c:text)press(static_cast<KeySym>(c));};
        auto hello=[&]{auto list=field.inputPanel().candidateList();return list && list->size() && list->candidate(0).text().toString()=="你好";};
        auto empty=[&]{auto list=field.inputPanel().candidateList();return !list || !list->size();};
        type("nihk");
        unsigned stage=0;uint64_t changed=now(CLOCK_MONOTONIC);std::string failure;
        auto timer=instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC,changed+10000,1000,[&](EventSourceTime *source,uint64_t){
            try {
                CHECK(now(CLOCK_MONOTONIC)-changed<5000000);
                if(stage==0 && hello()) {
                    type("l;");
                    CHECK(field.inputPanel().candidateList() && field.inputPanel().candidateList()->candidate(0).text().toString()=="林");
                    type("nihk");
                    settings("Xiaohe");addon->reloadConfig();CHECK(!hello());type("nihc");++stage;changed=now(CLOCK_MONOTONIC);
                } else if(stage==1 && hello()) {
                    press(FcitxKey_semicolon);CHECK(!field.commits.empty() && field.commits.back()=="你好；");
                    type("nihc");
                    settings("Custom");addon->reloadConfig();CHECK(empty());++stage;changed=now(CLOCK_MONOTONIC);
                } else if(stage==2) {
                    CHECK(empty());
                    if(now(CLOCK_MONOTONIC)-changed>200000) {
                        // Rapid superseding requests must not publish an earlier profile.
                        settings("MS");addon->reloadConfig();settings("Xiaohe");addon->reloadConfig();
                        settings("Custom",false);addon->reloadConfig();type("nihao");++stage;changed=now(CLOCK_MONOTONIC);
                    }
                } else if(stage==3 && hello()) {
                    instance.eventLoop().exit();return false;
                }
            } catch(const std::exception &e) {failure=e.what();instance.eventLoop().exit();return false;}
            source->setNextInterval(10000);source->setOneShot();return true;
        });
        instance.eventLoop().exec();CHECK(failure.empty() && stage==3);
        std::cout<<"Addon profile reload, invalid custom fail-closed and superseded decoder results passed\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
