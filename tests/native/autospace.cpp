// SPDX-License-Identifier: GPL-3.0-or-later
#include "autospace.h"
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/instance.h>
#include <fcitx/inputpanel.h>
#include <fcitx/candidatelist.h>
#include <fcitx-utils/event.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "isolation.h"
#define CHECK(x) do { if(!(x))throw std::runtime_error("Spacing check failed at line "+std::to_string(__LINE__)); } while(0)
using namespace fcitx;
struct Field : InputContext {
    Field(InputContextManager &manager):InputContext(manager,"cowtype-spacing-test"){created();}
    ~Field()override{destroy();}
    const char *frontend()const override{return "cowtype-synthetic";}
    std::vector<std::string> commits;
    void commitStringImpl(const std::string &text)override{commits.push_back(text);}
    void deleteSurroundingTextImpl(int,unsigned)override{}
    void forwardKeyImpl(const ForwardKeyEvent &)override{}
    void updatePreeditImpl()override{}
};
int main(int argc,char **argv) {
    try {
        CHECK(cowtype::autoSpace("Linux输入法2026",0)=="Linux 输入法 2026");
        CHECK(cowtype::autoSpace("中文",'a')==" 中文");
        CHECK(cowtype::autoSpace("ABC",0x4e2d)==" ABC");
        CHECK(cowtype::autoSpace(" 中文，ABC。42",'x')==" 中文，ABC。42");
        CHECK(cowtype::autoSpace("👍ABC🙂中文",0)=="👍ABC🙂中文");
        CHECK(cowtype::autoSpace("𠀀A",0)=="𠀀 A");
        CHECK(argc==2 && isolatedPaths());
        auto config=std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"cowtype/settings-v1.ini";
        std::filesystem::create_directories(config.parent_path());
        {std::ofstream out(config);out<<"[CowType]\nVersion=1\nAutoSpace=True\n";}
        char name[]="cowtype-spacing-test",disable[]="--disable=all";char *args[]={name,disable,nullptr};
        Instance instance(2,args);instance.initialize();
        void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(module);
        auto factory=reinterpret_cast<AddonFactory *(*)()>(dlsym(module,cowtype::compat::factorySymbol));CHECK(factory);
        {
            std::unique_ptr<AddonInstance> addon(factory()->create(&instance.addonManager()));
            auto *engine=dynamic_cast<InputMethodEngine *>(addon.get());CHECK(engine);
            Field a(instance.inputContextManager());
            const auto ordinary=CapabilityFlags(CapabilityFlag::Preedit)|CapabilityFlag::SurroundingText;
            a.setCapabilityFlags(ordinary);a.focusIn();
            InputMethodEntry entry("cowtype","CowType","zh_CN","cowtype");
            InputContextEvent activate(&a,EventType::InputContextInputMethodActivated);engine->activate(entry,activate);
            auto press=[&](KeySym key,bool release=false){KeyEvent event(&a,Key(key),release);engine->keyEvent(entry,event);return event.accepted();};
            auto tap=[&](KeySym key){CHECK(press(key));CHECK(press(key,true));};
            tap(FcitxKey_F6); // English mode: only the boundary character is consumed.
            a.surroundingText().setText("中文",2,2);
            CHECK(!press(FcitxKey_Shift_L));CHECK(press(FcitxKey_L));CHECK(a.commits.back()==" L");
            CHECK(!press(FcitxKey_Shift_L,true));
            a.surroundingText().setText("中文",2,2);
            CHECK(press(FcitxKey_a));CHECK(a.commits.back()==" a");
            CHECK(!press(FcitxKey_b)); // Client has not refreshed surrounding text yet.
            a.surroundingText().setText("中文 a",4,4);CHECK(!press(FcitxKey_b));
            a.surroundingText().setText("中文 ",3,3);CHECK(!press(FcitxKey_c));
            a.surroundingText().setText("中文",2,2);CHECK(!press(FcitxKey_period));CHECK(!press(FcitxKey_a));
            a.surroundingText().setText("中文",2,2);CHECK(!press(FcitxKey_Left));CHECK(!press(FcitxKey_a));
            a.surroundingText().setText("中文",2,2);CHECK(!press(FcitxKey_BackSpace));CHECK(!press(FcitxKey_a));
            a.surroundingText().setText("中文",2,2);
            a.setCapabilityFlags(ordinary|CapabilityFlag::Url);CHECK(!press(FcitxKey_d));
            a.setCapabilityFlags(ordinary|CapabilityFlag::Email);CHECK(!press(FcitxKey_d));
            a.setCapabilityFlags(ordinary|CapabilityFlag::Terminal);CHECK(!press(FcitxKey_d));
            a.setCapabilityFlags(ordinary|CapabilityFlag::Password);CHECK(!press(FcitxKey_d));
            a.setCapabilityFlags(ordinary);a.surroundingText().setText("中文",2,0);CHECK(!press(FcitxKey_e));
            a.surroundingText().invalidate();CHECK(!press(FcitxKey_f));
            a.surroundingText().setText("中文",2,2);
            {std::ofstream out(config);out<<"[CowType]\nVersion=1\nAutoSpace=False\n";}
            addon->reloadConfig();CHECK(!press(FcitxKey_g));
            {std::ofstream out(config);out<<"[CowType]\nVersion=1\nAutoSpace=True\n";}
            addon->reloadConfig();tap(FcitxKey_F6);
            a.surroundingText().setText("Linux",5,5);
            for(auto ch:std::string("shan"))CHECK(press(static_cast<KeySym>(ch)));
            std::string failure;
            const auto deadline=now(CLOCK_MONOTONIC)+5000000;
            auto timer=instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC,now(CLOCK_MONOTONIC)+20000,1000,
                [&](EventSourceTime *source,uint64_t) {
                    try {
                        CHECK(now(CLOCK_MONOTONIC)<deadline);
                        auto list=a.inputPanel().candidateList();
                        if(!list || list->size()==0) {source->setNextInterval(20000);source->setOneShot();return true;}
                        CHECK(press(FcitxKey_space));CHECK(a.commits.back()==" 山");
                        // Explicit Unicode output remains exactly the selected code point.
                        a.surroundingText().setText("中",1,1);tap(FcitxKey_F9);
                        for(auto ch:std::string("0041"))CHECK(press(static_cast<KeySym>(ch)));
                        CHECK(press(FcitxKey_Return));CHECK(a.commits.back()=="A");
                    } catch(const std::exception &error) {failure=error.what();}
                    instance.eventLoop().exit();return false;
                });
            instance.eventLoop().exec();if(!failure.empty())throw std::runtime_error(failure);
        }
        std::cout<<"Automatic spacing boundaries and input context guards passed\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
