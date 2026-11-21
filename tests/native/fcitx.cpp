// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcitx/addonfactory.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <dlfcn.h>
#include <chrono>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <fcitx-utils/event.h>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include "isolation.h"
#define CHECK(x) do { if (!(x)) { throw std::runtime_error("Fcitx check failed at line " + std::to_string(__LINE__)); } } while(0)
using namespace fcitx;
struct Field : InputContext {
    explicit Field(InputContextManager &manager) : InputContext(manager, "cowtype-test") { created(); }
    ~Field() override { destroy(); }
    const char *frontend() const override { return "cowtype-synthetic"; }
    std::vector<std::string> committed;
    void commitStringImpl(const std::string &text) override { committed.push_back(text); }
    void deleteSurroundingTextImpl(int, unsigned) override {}
    void forwardKeyImpl(const ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};
bool press(InputMethodEngine *engine, const InputMethodEntry &entry, Field &field, KeySym key, bool release=false) {
    KeyEvent event(&field, Key(key), release); engine->keyEvent(entry, event); return event.accepted();
}
void type(InputMethodEngine *engine, const InputMethodEntry &entry, Field &field, const std::string &input) {
    for (char ch : input) { CHECK(press(engine, entry, field, static_cast<KeySym>(ch))); }
}
int main(int argc, char **argv) {
    try {
        CHECK(argc == 2);
        CHECK(isolatedPaths());
        std::filesystem::remove(std::filesystem::path(std::getenv("XDG_CONFIG_HOME")) / "cowtype/settings-v1.ini");
        char name[]="cowtype-test", disabled[]="--disable=all";
        char *arguments[]={name, disabled, nullptr}; Instance instance(2, arguments); instance.initialize();
        // Open exactly this build's module. Never load any installed CowType addon.
        void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!module) { throw std::runtime_error(dlerror()); }
        auto factory = reinterpret_cast<AddonFactory *(*)()>(dlsym(module, "fcitx_addon_factory_instance_cowtype")); CHECK(factory);
        {
            std::unique_ptr<AddonInstance> addon(factory()->create(&instance.addonManager()));
            auto *engine = dynamic_cast<InputMethodEngine *>(addon.get()); CHECK(engine);
            Field a(instance.inputContextManager()), b(instance.inputContextManager());
            a.setCapabilityFlags(CapabilityFlag::Preedit);
            InputMethodEntry pinyin("cowtype", "CowType", "zh_CN", "cowtype");
            InputContextEvent activate(&a, EventType::InputContextInputMethodActivated); engine->activate(pinyin, activate);
            type(engine, pinyin, a, "shan");
            for (unsigned i=0; i<100 && !a.inputPanel().candidateList(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); press(engine, pinyin, a, FcitxKey_F35);
            }
            CHECK(a.inputPanel().clientPreedit().toString() == "shan");
            CHECK(a.inputPanel().clientPreedit().cursor() == 4);
            auto list = a.inputPanel().candidateList(); CHECK(list && list->size() > 0);
            auto firstCandidate=list->candidate(0).text().toString();
            CHECK(press(engine,pinyin,a,FcitxKey_bracketright));
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()!=firstCandidate);
            CHECK(press(engine,pinyin,a,FcitxKey_bracketleft));
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()==firstCandidate);
            CHECK(press(engine,pinyin,a,FcitxKey_equal));
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()!=firstCandidate);
            CHECK(press(engine,pinyin,a,FcitxKey_minus));
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()==firstCandidate);
            CHECK(press(engine, pinyin, a, FcitxKey_Page_Down));
            list = a.inputPanel().candidateList(); CHECK(list && list->size() > 0);
            list->candidate(0).select(&a);
            CHECK(a.committed.size() == 1 && !a.committed.back().empty());
            CHECK(a.inputPanel().clientPreedit().empty());
            CHECK(!press(engine, pinyin, a, FcitxKey_BackSpace));
            type(engine, pinyin, a, "shan"); type(engine, pinyin, b, "hai");
            a.focusIn(); a.focusOut();
            CHECK(a.committed.size() == 1); CHECK(a.inputPanel().clientPreedit().empty());
            CHECK(press(engine, pinyin, b, FcitxKey_space)); CHECK(b.committed.back() == "海");
            type(engine, pinyin, a, "shan");
            InputContextSwitchInputMethodEvent switched(InputMethodSwitchedReason::Enumerate, "cowtype", &a);
            engine->deactivate(pinyin, switched); CHECK(a.committed.back() == "shan");
            a.setCapabilityFlags(CapabilityFlag::Password);
            CHECK(a.capabilityFlags().test(CapabilityFlag::Password));
            CHECK(!press(engine, pinyin, a, FcitxKey_a));
            CHECK(a.inputPanel().preedit().empty());
            a.setCapabilityFlags(CapabilityFlag::Preedit);
            CHECK(press(engine, pinyin, a, FcitxKey_F6)); CHECK(!press(engine, pinyin, a, FcitxKey_a));
            CHECK(press(engine, pinyin, a, FcitxKey_F6));
            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L));
            CHECK(press(engine,pinyin,a,FcitxKey_Shift_L,true));
            CHECK(!press(engine,pinyin,a,FcitxKey_a));
            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L));
            CHECK(press(engine,pinyin,a,FcitxKey_Shift_L,true));
            type(engine,pinyin,a,"kn");
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="可能");
            CHECK(press(engine,pinyin,a,FcitxKey_space));CHECK(a.committed.back()=="可能");
            CHECK(press(engine,pinyin,a,FcitxKey_asciicircum));CHECK(a.committed.back()=="……");
            const auto configPath = std::filesystem::path(std::getenv("XDG_CONFIG_HOME")) / "cowtype/settings-v1.ini";
            std::filesystem::create_directories(configPath.parent_path());
            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nPageSize=7\nPageBrackets=False\nPageMinusEqual=False\nHorizontal=True\nShowPinyin=False\nVoice=True\nVoiceHotkey=F9\n"; }
            addon->reloadConfig();
            type(engine, pinyin, a, "shan");
            CHECK(a.inputPanel().candidateList()->size()==7);
            CHECK(a.inputPanel().candidateList()->layoutHint()==CandidateLayoutHint::Horizontal);
            CHECK(a.inputPanel().candidateList()->candidate(0).comment().empty());
            auto fixedCandidate=a.inputPanel().candidateList()->candidate(0).text().toString();
            CHECK(!press(engine,pinyin,a,FcitxKey_equal)); // Disabled shortcut passes through.
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()==fixedCandidate);
            CHECK(press(engine, pinyin, a, FcitxKey_Escape));
            a.focusIn();
            const auto before = a.committed.size();
            std::filesystem::remove(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-pids");
            std::filesystem::remove(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-error");
            int stage=0; std::string voiceFailure; std::unique_ptr<Field> destroyedField;
            CHECK(press(engine, pinyin, a, FcitxKey_F9));
            CHECK(press(engine, pinyin, a, FcitxKey_F9,true));
            auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC)+200000, 1000,
                [&](EventSourceTime *source, uint64_t) {
                    try {
                        switch(stage++) {
                        case 0:
                            CHECK(a.inputPanel().clientPreedit().toString()=="合成中间结果");
                            CHECK(a.inputPanel().auxDown().toString().find("█▂")!=std::string::npos);
                            CHECK(a.inputPanel().auxDown().toString().size()==36);
                            CHECK(a.inputPanel().auxDown().toString().find(' ')==std::string::npos);
                            CHECK(a.inputPanel().auxDown().toString().find("F8")==std::string::npos);
                            CHECK(press(engine,pinyin,a,FcitxKey_F9)); CHECK(press(engine,pinyin,a,FcitxKey_F9,true)); break;
                        case 1:
                            CHECK(a.committed.size()==before+1 && a.committed.back()=="合成语音结果");
                            CHECK(press(engine,pinyin,a,FcitxKey_F9)); CHECK(press(engine,pinyin,a,FcitxKey_F9,true)); break;
                        case 2: a.focusOut(); break;
                        case 3:
                            CHECK(a.committed.size()==before+1 && a.inputPanel().clientPreedit().empty()); a.focusIn();
                            CHECK(press(engine,pinyin,a,FcitxKey_F9)); CHECK(press(engine,pinyin,a,FcitxKey_F9,true)); break;
                        case 4: CHECK(press(engine,pinyin,a,FcitxKey_Escape)); break;
                        case 5:
                            CHECK(a.committed.size()==before+1 && a.inputPanel().clientPreedit().empty());
                            { std::ofstream error(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-error"); error << "fail"; }
                            CHECK(press(engine,pinyin,a,FcitxKey_F9)); CHECK(press(engine,pinyin,a,FcitxKey_F9,true)); break;
                        case 6:
                            CHECK(a.inputPanel().auxDown().toString()=="合成后端错误");
                            std::filesystem::remove(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-error");
                            CHECK(press(engine,pinyin,a,FcitxKey_F9)); break;
                        case 7: break; // Held F8 continues recording before release.
                        case 8: CHECK(press(engine,pinyin,a,FcitxKey_F9,true)); break;
                        case 9:
                            CHECK(a.committed.size()==before+2 && a.committed.back()=="合成语音结果");
                            a.focusOut();
                            destroyedField=std::make_unique<Field>(instance.inputContextManager());
                            destroyedField->setCapabilityFlags(CapabilityFlag::Preedit);destroyedField->focusIn();
                            CHECK(press(engine,pinyin,*destroyedField,FcitxKey_F9));
                            CHECK(press(engine,pinyin,*destroyedField,FcitxKey_F9,true)); break;
                        case 10: destroyedField.reset();break;
                        case 11:
                            CHECK(a.committed.size()==before+2);
                            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nVoice=True\nVoiceHotkey=F9\nVoiceResident=False\n"; }
                            addon->reloadConfig();a.focusIn();
                            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));break;
                        case 12: CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));break;
                        case 13:
                            CHECK(a.committed.size()==before+3);
                            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));break;
                        case 14: CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));break;
                        case 15:
                            CHECK(a.committed.size()==before+4);
                            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nVoice=True\nVoiceHotkey=Alt_L\nVoiceModifierHold=True\n"; }
                            addon->reloadConfig();
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L));break;
                        case 16: CHECK(a.inputPanel().clientPreedit().empty());break;
                        case 17:
                            CHECK(a.inputPanel().clientPreedit().toString()=="合成中间结果");
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L,true));break;
                        case 18:
                            CHECK(a.committed.size()==before+5);
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L));break;
                        case 19: break;
                        case 20:
                            CHECK(!press(engine,pinyin,a,FcitxKey_Tab));
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L,true));break;
                        case 21:
                            CHECK(a.committed.size()==before+5 && a.inputPanel().clientPreedit().empty());
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L));
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L,true));break;
                        case 22:
                            CHECK(a.inputPanel().clientPreedit().toString()=="合成中间结果");
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L));
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L,true));break;
                        case 23:
                            CHECK(a.committed.size()==before+6);
                            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nVoice=True\nVoiceHotkey=Alt_L\nVoiceModifierHold=False\n"; }
                            addon->reloadConfig();
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L));break;
                        case 24: break;
                        case 25: break;
                        case 26:
                            CHECK(a.inputPanel().clientPreedit().empty());
                            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L,true));
                            CHECK(a.committed.size()==before+6);break;
                        case 27:
                            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nVoice=True\nVoiceResident=True\nVoiceAlwaysResident=True\nVoiceHotkey=F9\n"; }
                            addon->reloadConfig();break;
                        case 28: {
                            auto prepared=std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-prepared";
                            CHECK(std::filesystem::exists(prepared));
                            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));break;
                        }
                        case 29:
                            CHECK(a.inputPanel().clientPreedit().toString()=="合成中间结果");
                            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));break;
                        default:
                            CHECK(a.committed.size()==before+7);
                            instance.eventLoop().exit(); return false;
                        }
                    } catch(const std::exception &error) { voiceFailure=error.what();instance.eventLoop().exit();return false; }
                    source->setNextInterval(200000);source->setOneShot();return true;
                });
            instance.eventLoop().exec();
            if(!voiceFailure.empty()){throw std::runtime_error(voiceFailure);}
            std::ifstream pids(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-pids");
            std::string currentPid;size_t starts=0;
            std::vector<std::string> workerPids;
            while(std::getline(pids,currentPid)){workerPids.push_back(currentPid);++starts;}
            CHECK(starts==12);
            std::ifstream prepared(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-prepared");
            std::string preparedPid;std::getline(prepared,preparedPid);CHECK(preparedPid==workerPids.back());
            for(size_t n=1;n<6;++n){CHECK(workerPids[n]==workerPids[0]);}
            CHECK(workerPids[6]!=workerPids[0] && workerPids[7]!=workerPids[6]);
            a.focusOut();
            std::filesystem::remove(configPath);
            addon->reloadConfig();
            InputMethodEntry wubi("cowtype-wubi", "CowType Wubi", "zh_CN", "cowtype");
            InputContextEvent wbactivate(&a, EventType::InputContextInputMethodActivated); engine->activate(wubi, wbactivate);
            type(engine, wubi, a, "a");
            for (unsigned i=0; i<100 && !a.inputPanel().candidateList(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); press(engine, wubi, a, FcitxKey_F35);
            }
            CHECK(press(engine, wubi, a, FcitxKey_space)); CHECK(a.committed.back() == "工");
        }
        dlclose(module);
        std::cout << "Fcitx shared addon: candidate UI, page/click, two fields, focus, switch, sensitive field, mode, Wubi passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
