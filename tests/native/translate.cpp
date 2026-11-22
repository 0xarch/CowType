// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/surroundingtext.h>
#include <fcitx-utils/event.h>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "isolation.h"
#include "messages.h"
#define CHECK(x) do { if(!(x))throw std::runtime_error("Translation check failed at line "+std::to_string(__LINE__)); } while(0)
using namespace fcitx;
struct Field : InputContext {
    Field(InputContextManager &manager):InputContext(manager,"cowtype-translation-test"){created();setEnablePreedit(true);}
    ~Field()override{destroy();}
    const char *frontend()const override{return "cowtype-synthetic";}
    std::vector<std::string> commits;
    void commitStringImpl(const std::string &text)override{commits.push_back(text);}
    void deleteSurroundingTextImpl(int,unsigned)override{}
    void forwardKeyImpl(const ForwardKeyEvent &)override{}
    void updatePreeditImpl()override{}
};
int main(int argc,char **argv){
    try {
        CHECK(argc==2 && isolatedPaths());
        auto config=std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"cowtype/settings-v1.ini";
        std::filesystem::create_directories(config.parent_path());std::filesystem::remove(config);
        char name[]="cowtype-translate-test",disable[]="--disable=all";char *args[]={name,disable,nullptr};
        Instance instance(2,args);instance.initialize();
        void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);CHECK(module);
        auto factory=reinterpret_cast<AddonFactory *(*)()>(dlsym(module,cowtype::compat::factorySymbol));CHECK(factory);
        {
            std::unique_ptr<AddonInstance> addon(factory()->create(&instance.addonManager()));
            auto *engine=dynamic_cast<InputMethodEngine *>(addon.get());CHECK(engine);
            Field a(instance.inputContextManager()),b(instance.inputContextManager());
            a.setCapabilityFlags(CapabilityFlag::Preedit);a.focusIn();
            InputMethodEntry entry("cowtype","CowType","zh_CN","cowtype");
            InputContextEvent activate(&a,EventType::InputContextInputMethodActivated);engine->activate(entry,activate);
            auto press=[&](KeySym key,bool release=false){KeyEvent event(&a,Key(key),release);engine->keyEvent(entry,event);return event.accepted();};
            auto tap=[&](KeySym key){CHECK(press(key));CHECK(press(key,true));};
            auto type=[&](std::string text){for(unsigned char c:text)CHECK(press(static_cast<KeySym>(c)));};
            auto settings=[&](std::string extra){std::ofstream out(config);out<<"[CowType]\nVersion=1\n"<<extra;out.close();addon->reloadConfig();};
            auto result=[&](){auto list=a.inputPanel().candidateList();CHECK(list && list->size()==1);CHECK(list->candidate(0).text().toString()=="合成译文");};
            auto starts=[&](){std::ifstream file(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"translate-test-starts");size_t count=0;std::string line;while(std::getline(file,line))++count;return count;};
            size_t beforeSelection=0;
            std::vector<std::function<void()>> steps{
                [&]{CHECK(!press(FcitxKey_F10));CHECK(!press(FcitxKey_F10,true));settings("Translate=True\n");type("shan");tap(FcitxKey_F10);CHECK(a.commits.empty());},
                [&]{result();CHECK(a.commits.empty());CHECK(press(FcitxKey_Escape));CHECK(a.inputPanel().clientPreedit().toString()=="shan");tap(FcitxKey_F10);},
                [&]{result();CHECK(press(FcitxKey_Return));CHECK(a.commits.size()==1 && a.commits.back()=="合成译文");CHECK(a.inputPanel().clientPreedit().empty());tap(FcitxKey_F10);type("shan");CHECK(press(FcitxKey_space));CHECK(a.commits.size()==1);CHECK(press(FcitxKey_Return));},
                [&]{result();a.inputPanel().candidateList()->candidate(0).select(&a);CHECK(a.commits.size()==2);a.setCapabilityFlags(CapabilityFlags(CapabilityFlag::Preedit)|CapabilityFlag::SurroundingText);a.surroundingText().setText("selected text",13,0);beforeSelection=starts();tap(FcitxKey_F10);},
                [&]{CHECK(starts()==beforeSelection);CHECK(!a.inputPanel().candidateList());CHECK(!a.inputPanel().auxDown().empty());CHECK(a.commits.size()==2);CHECK(a.inputPanel().clientPreedit().empty());CHECK(a.surroundingText().text()=="selected text");a.surroundingText().invalidate();a.setCapabilityFlags(CapabilityFlag::Preedit);tap(FcitxKey_F6);tap(FcitxKey_F10);type("hello world");CHECK(press(FcitxKey_BackSpace));CHECK(a.inputPanel().clientPreedit().toString()=="hello worl");CHECK(press(FcitxKey_Return));},
                [&]{result();CHECK(press(FcitxKey_Return));CHECK(a.commits.size()==3);tap(FcitxKey_F10);type("failure");CHECK(press(FcitxKey_Return));},
                [&]{CHECK(!a.inputPanel().candidateList());CHECK(a.inputPanel().auxDown().toString()==ct_tr("translate.error.rate"));CHECK(press(FcitxKey_Escape));tap(FcitxKey_F6);type("shan");tap(FcitxKey_F10);a.focusOut();a.focusIn();},
                [&]{CHECK(a.commits.size()==3 && a.inputPanel().clientPreedit().empty());type("shan");tap(FcitxKey_F10);addon->reloadConfig();CHECK(a.inputPanel().clientPreedit().toString()=="shan");CHECK(press(FcitxKey_Escape));},
                [&]{CHECK(a.commits.size()==3 && a.inputPanel().clientPreedit().empty());type("shan");tap(FcitxKey_F10);tap(FcitxKey_F9);CHECK(press(FcitxKey_Escape));CHECK(press(FcitxKey_Escape));},
                [&]{CHECK(a.commits.size()==3 && a.inputPanel().clientPreedit().empty());type("shan");tap(FcitxKey_F10);a.setCapabilityFlags(CapabilityFlag::Password);CHECK(!press(FcitxKey_a));a.setCapabilityFlags(CapabilityFlag::Preedit);},
                [&]{CHECK(a.commits.size()==3 && a.inputPanel().clientPreedit().empty());settings("Translate=True\nTranslateHotkey=Control_R\n");type("shan");CHECK(!press(FcitxKey_Control_R));CHECK(!press(FcitxKey_Control_R,true));},
                [&]{result();CHECK(press(FcitxKey_Escape));CHECK(press(FcitxKey_Escape));settings("Translate=True\n");type("shan");tap(FcitxKey_F10);a.focusOut();b.focusIn();},
                [&]{CHECK(b.inputPanel().clientPreedit().empty());b.focusOut();a.focusIn();CHECK(a.commits.size()==3);type("shan");tap(FcitxKey_F10);InputContextEvent reset(&a,EventType::InputContextReset);engine->reset(entry,reset);},
                [&]{CHECK(a.commits.size()==3 && a.inputPanel().clientPreedit().empty());type("shan");tap(FcitxKey_F10);InputContextSwitchInputMethodEvent change(InputMethodSwitchedReason::Enumerate,"cowtype",&a);engine->deactivate(entry,change);engine->activate(entry,activate);},
                [&]{CHECK(a.commits.size()==3 && a.inputPanel().clientPreedit().empty());entry=InputMethodEntry("cowtype-wubi","CowType Wubi","zh_CN","cowtype");engine->activate(entry,activate);type("a");tap(FcitxKey_F10);},
                [&]{result();a.setCapabilityFlags(CapabilityFlags(CapabilityFlag::Preedit)|CapabilityFlag::SurroundingText);a.surroundingText().setText("selected text",13,0);CHECK(press(FcitxKey_1));CHECK(a.commits.size()==3);CHECK(!a.inputPanel().candidateList());a.surroundingText().invalidate();a.setCapabilityFlags(CapabilityFlag::Preedit);CHECK(press(FcitxKey_Escape));tap(FcitxKey_F10);},
                [&]{result();CHECK(press(FcitxKey_1));CHECK(a.commits.size()==4 && a.commits.back()=="合成译文");}
            };
            size_t step=0;std::string failure;
            auto stepDeadline=now(CLOCK_MONOTONIC)+10000000;
            auto timer=instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC,now(CLOCK_MONOTONIC)+400000,1000,
                [&](EventSourceTime *source,uint64_t){
                    bool ready=true;
                    switch(step) {
                    case 1: case 2: case 3: case 5: case 11: case 15: case 16: {
                        const auto list=a.inputPanel().candidateList();
                        ready=list && list->size()==1 && list->candidate(0).text().toString()=="合成译文";
                        break;
                    }
                    case 6: ready=a.inputPanel().auxDown().toString()==ct_tr("translate.error.rate");break;
                    }
                    if(!ready && now(CLOCK_MONOTONIC)<stepDeadline) {
                        source->setNextInterval(20000);source->setOneShot();return true;
                    }
                    try {steps[step++]();}
                    catch(const std::exception &error){failure=error.what();instance.eventLoop().exit();return false;}
                    if(step==steps.size()){instance.eventLoop().exit();return false;}
                    stepDeadline=now(CLOCK_MONOTONIC)+10000000;
                    source->setNextInterval(400000);source->setOneShot();return true;
                });
            instance.eventLoop().exec();
            if(!failure.empty())throw std::runtime_error(failure);
        }
        dlclose(module);std::cout<<"Translation addon: async preview, composition, deferred selection, English, Wubi, shortcuts, error and lifecycle isolation passed\n";
    }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}
