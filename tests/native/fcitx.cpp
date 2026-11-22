// SPDX-License-Identifier: GPL-3.0-or-later
#include <fcitx-config/configuration.h>
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
    explicit Field(InputContextManager &manager) : InputContext(manager, "cowtype-test") { created(); setEnablePreedit(true); }
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
        auto factory = reinterpret_cast<AddonFactory *(*)()>(dlsym(module, cowtype::compat::factorySymbol)); CHECK(factory);
        {
            std::unique_ptr<AddonInstance> addon(factory()->create(&instance.addonManager()));
            auto *engine = dynamic_cast<InputMethodEngine *>(addon.get()); CHECK(engine);
            Field a(instance.inputContextManager()), b(instance.inputContextManager());
            a.setCapabilityFlags(CapabilityFlag::Preedit);
            InputMethodEntry pinyin("cowtype", "CowType", "zh_CN", "cowtype");
            const auto *config = engine->getConfigForInputMethod(pinyin); CHECK(config);
            RawConfig description; config->dumpDescription(description);
            const auto external = description.get("CowtypeSettingsLauncher/Settings/External");
            CHECK(external && external->value() == "cowtype-config");
            CHECK(engine->getConfigForInputMethod(InputMethodEntry("cowtype-wubi", "CowType Wubi", "zh_CN", "cowtype")) == config);
            InputContextEvent activate(&a, EventType::InputContextInputMethodActivated); engine->activate(pinyin, activate);
            type(engine, pinyin, a, "shan");
            for (unsigned i=0; i<100 && !a.inputPanel().candidateList(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); press(engine, pinyin, a, FcitxKey_F35);
            }
            if (a.inputPanel().clientPreedit().toString() != "shan") {
                std::cerr << "Synthetic preedit mismatch: client_bytes=" << a.inputPanel().clientPreedit().toString().size()
                          << " panel_bytes=" << a.inputPanel().preedit().toString().size()
                          << " cursor=" << a.inputPanel().clientPreedit().cursor()
                          << " client_capability=" << a.capabilityFlags().test(CapabilityFlag::Preedit)
                          << " commits=" << a.committed.size() << '\n';
                CHECK(false);
            }
            CHECK(a.inputPanel().clientPreedit().cursor() == 4);
            // Distribution defaults may disable inline preedit. Both display
            // paths must work, while each test field selects its own policy.
            a.setEnablePreedit(false); press(engine, pinyin, a, FcitxKey_F35);
            CHECK(a.inputPanel().clientPreedit().empty());
            CHECK(a.inputPanel().preedit().toString() == "shan");
            a.setEnablePreedit(true); press(engine, pinyin, a, FcitxKey_F35);
            CHECK(a.inputPanel().clientPreedit().toString() == "shan");
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
            // F6 is the sole default binding; a legacy Shift flag cannot add a second trigger.
            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L));CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L,true));
            CHECK(press(engine,pinyin,a,FcitxKey_a));CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            auto earlyConfig=std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"cowtype/settings-v1.ini";
            std::filesystem::create_directories(earlyConfig.parent_path());
            {std::ofstream settings(earlyConfig);settings<<"[CowType]\nVersion=1\nModeHotkey=Shift\n";}
            addon->reloadConfig();
            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L));
            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L,true));
            CHECK(!press(engine,pinyin,a,FcitxKey_a));
            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L));
            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L,true));
            {std::ofstream settings(earlyConfig);settings<<"[CowType]\nVersion=1\nModeHotkey=F6\nShiftToggle=True\n";}
            addon->reloadConfig();
            type(engine,pinyin,a,"kn");
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="可能");
            CHECK(press(engine,pinyin,a,FcitxKey_space));CHECK(a.committed.back()=="可能");
            CHECK(press(engine,pinyin,a,FcitxKey_asciicircum));CHECK(a.committed.back()=="……");
            const auto configPath = std::filesystem::path(std::getenv("XDG_CONFIG_HOME")) / "cowtype/settings-v1.ini";
            std::filesystem::create_directories(configPath.parent_path());
            // Internal code mode suspends normal composition and is per field.
            a.focusIn();
            // Long operation hints disappear while typing and return when empty.
            for(auto key : {FcitxKey_F7,FcitxKey_F9}) {
                CHECK(press(engine,pinyin,a,key));CHECK(press(engine,pinyin,a,key,true));
                CHECK(!a.inputPanel().auxUp().empty());
                type(engine,pinyin,a,"4");CHECK(a.inputPanel().auxUp().empty());
                CHECK(press(engine,pinyin,a,FcitxKey_BackSpace));
                CHECK(!a.inputPanel().auxUp().empty());
                CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            }
            type(engine,pinyin,a,"shan");
            CHECK(press(engine,pinyin,a,FcitxKey_F7)); CHECK(press(engine,pinyin,a,FcitxKey_F7,true));
            type(engine,pinyin,a,"d6d0");
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="中");
            CHECK(press(engine,pinyin,a,FcitxKey_space));CHECK(a.committed.back()=="中");
            CHECK(a.inputPanel().clientPreedit().toString()=="shan");
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            CHECK(press(engine,pinyin,a,FcitxKey_F7)); CHECK(press(engine,pinyin,a,FcitxKey_F7,true));type(engine,pinyin,a,"9439fc36");
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="😀");
            CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.back()=="😀");
            CHECK(press(engine,pinyin,a,FcitxKey_F7)); CHECK(press(engine,pinyin,a,FcitxKey_F7,true));type(engine,pinyin,a,"817f");
            CHECK(a.inputPanel().candidateList()->size()==0);
            CHECK(press(engine,pinyin,a,FcitxKey_Return));
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            // Unicode is an independent module and shortcut; preserve normal composition.
            type(engine,pinyin,a,"shan");
            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));
            type(engine,pinyin,a,"U+1F600");
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="😀");
            CHECK(press(engine,pinyin,a,FcitxKey_space));CHECK(a.committed.back()=="😀");
            CHECK(a.inputPanel().clientPreedit().toString()=="shan");CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));
            type(engine,pinyin,a,"D800");CHECK(a.inputPanel().candidateList()->size()==0);
            const auto invalidBefore=a.committed.size();CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.size()==invalidBefore);
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));
            type(engine,pinyin,a,"20000");a.inputPanel().candidateList()->candidate(0).select(&a);CHECK(a.committed.back()=="𠀀");
            // Automatic u-prefix input is default-on and shares only decoding
            // with the explicit module, not its shortcut or normal composition.
            for(auto [input,expected] : {std::pair{"u0020"," "}, {"u4e2d","中"}, {"U1F600","😀"}, {"u+20000","𠀀"}}) {
                const auto beforePrefix=a.committed.size();type(engine,pinyin,a,input);
                CHECK(a.inputPanel().clientPreedit().toString()==input);
                CHECK(a.inputPanel().auxUp().empty());
                CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()==expected);
                CHECK(a.committed.size()==beforePrefix); // Never commit a shorter intermediate code point.
                CHECK(press(engine,pinyin,a,FcitxKey_space));
                CHECK(a.committed.size()==beforePrefix+1 && a.committed.back()==expected);
                CHECK(a.inputPanel().clientPreedit().empty()); // No suspended u to restore.
            }
            for(auto input : {"ud800","u110000","u0000"}) {
                const auto beforePrefix=a.committed.size();type(engine,pinyin,a,input);
                CHECK(a.inputPanel().candidateList()->size()==0);
                CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.size()==beforePrefix);
                CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            }
            type(engine,pinyin,a,"u4e2d");a.inputPanel().candidateList()->candidate(0).select(&a);
            CHECK(a.committed.back()=="中" && a.inputPanel().clientPreedit().empty());
            type(engine,pinyin,a,"u0");CHECK(press(engine,pinyin,a,FcitxKey_BackSpace));
            CHECK(a.inputPanel().clientPreedit().toString()=="u");
            CHECK(press(engine,pinyin,a,FcitxKey_BackSpace));CHECK(a.inputPanel().clientPreedit().empty());
            for(auto input : {"unicode","ubuntu","abc","shu"}) {
                type(engine,pinyin,a,input);CHECK(a.inputPanel().clientPreedit().toString()==input);
                CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            }
            type(engine,pinyin,a,"shan");
            auto ordinaryCandidate=a.inputPanel().candidateList()->candidate(0).text().toString();
            CHECK(press(engine,pinyin,a,FcitxKey_1));CHECK(a.committed.back()==ordinaryCandidate);
            // A cursor moved before u cannot start an automatic prefix.
            type(engine,pinyin,a,"u");CHECK(press(engine,pinyin,a,FcitxKey_Home));
            CHECK(press(engine,pinyin,a,FcitxKey_0));CHECK(a.inputPanel().clientPreedit().toString()=="u");
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            CHECK(press(engine,pinyin,a,FcitxKey_F6));CHECK(press(engine,pinyin,a,FcitxKey_F6,true));
            for(char c:std::string("u0020"))CHECK(!press(engine,pinyin,a,static_cast<KeySym>(c)));
            CHECK(a.inputPanel().clientPreedit().empty());
            CHECK(press(engine,pinyin,a,FcitxKey_F6));CHECK(press(engine,pinyin,a,FcitxKey_F6,true));
            {std::ofstream settings(configPath);settings<<"[CowType]\nVersion=1\nUnicodeHotkey=None\nEmoji=True\n";}
            addon->reloadConfig();type(engine,pinyin,a,"u0020");CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.back()==" ");
            type(engine,pinyin,a,"u");
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="🧲");
            CHECK(press(engine,pinyin,a,FcitxKey_0));CHECK(press(engine,pinyin,a,FcitxKey_BackSpace));
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="🧲");
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            type(engine,pinyin,a,"u");
            for(auto key:{FcitxKey_KP_0,FcitxKey_KP_0,FcitxKey_KP_2,FcitxKey_KP_0})CHECK(press(engine,pinyin,a,key));
            CHECK(press(engine,pinyin,a,FcitxKey_KP_Enter));CHECK(a.committed.back()==" ");
            CHECK(!press(engine,pinyin,a,FcitxKey_F9));CHECK(!press(engine,pinyin,a,FcitxKey_F9,true));
            type(engine,pinyin,a,"u4e2d");a.focusOut();a.focusIn();
            CHECK(a.inputPanel().clientPreedit().empty());
            type(engine,pinyin,a,"u4e2d");
            InputContextEvent prefixReset(&a,EventType::InputContextReset);engine->reset(pinyin,prefixReset);
            CHECK(a.inputPanel().clientPreedit().empty());
            type(engine,pinyin,a,"u4e2d");a.setCapabilityFlags(CapabilityFlag::Password);
            CHECK(!press(engine,pinyin,a,FcitxKey_0));CHECK(a.inputPanel().clientPreedit().empty());
            a.setCapabilityFlags(CapabilityFlag::Preedit);
            type(engine,pinyin,a,"u4e2d");
            {std::ofstream settings(configPath);settings<<"[CowType]\nVersion=1\nUnicodePrefix=False\n";}
            addon->reloadConfig();CHECK(a.inputPanel().clientPreedit().empty());
            type(engine,pinyin,a,"u0");CHECK(a.inputPanel().clientPreedit().toString()=="u");
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));
            type(engine,pinyin,a,"0020");CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.back()==" ");
            // The two module hotkeys switch directly without reinterpreting a buffer.
            CHECK(press(engine,pinyin,a,FcitxKey_F7));CHECK(press(engine,pinyin,a,FcitxKey_F7,true));type(engine,pinyin,a,"d6");
            CHECK(press(engine,pinyin,a,FcitxKey_F9));CHECK(press(engine,pinyin,a,FcitxKey_F9,true));
            CHECK(a.inputPanel().clientPreedit().empty());type(engine,pinyin,a,"4e2d");
            CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.back()=="中");
            {std::ofstream settings(configPath);settings<<"[CowType]\nVersion=1\nModeHotkey=Shift\nInternalCodeHotkey=Alt_L\nUnicodeHotkey=Control_R\n";}
            addon->reloadConfig();
            CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L));CHECK(!press(engine,pinyin,a,FcitxKey_Alt_L,true));type(engine,pinyin,a,"b9fa");
            CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.back()=="国");
            CHECK(!press(engine,pinyin,a,FcitxKey_Control_R));CHECK(!press(engine,pinyin,a,FcitxKey_Control_R,true));type(engine,pinyin,a,"1f600");
            CHECK(press(engine,pinyin,a,FcitxKey_Return));CHECK(a.committed.back()=="😀");
            {std::ofstream settings(configPath);settings<<"[CowType]\nVersion=1\nModeHotkey=F6\n";}
            addon->reloadConfig();
            // Click selection and keypad input use the same guarded commit path.
            CHECK(press(engine,pinyin,a,FcitxKey_F7));CHECK(press(engine,pinyin,a,FcitxKey_F7,true));
            type(engine,pinyin,a,"b9fa");
            a.inputPanel().candidateList()->candidate(0).select(&a);CHECK(a.committed.back()=="国");
            CHECK(press(engine,pinyin,a,FcitxKey_F7));CHECK(press(engine,pinyin,a,FcitxKey_F7,true));
            CHECK(press(engine,pinyin,a,FcitxKey_D));CHECK(press(engine,pinyin,a,FcitxKey_KP_6));
            CHECK(press(engine,pinyin,a,FcitxKey_D));CHECK(press(engine,pinyin,a,FcitxKey_KP_0));
            CHECK(press(engine,pinyin,a,FcitxKey_KP_Enter));CHECK(a.committed.back()=="中");
            // Key-repeat cannot toggle twice; release ends the trigger latch.
            const auto committedBeforeInternal=a.committed.size();
            CHECK(press(engine,pinyin,a,FcitxKey_F7));
            CHECK(press(engine,pinyin,a,FcitxKey_F7));
            CHECK(press(engine,pinyin,a,FcitxKey_F7,true));
            CHECK(a.committed.size()==committedBeforeInternal);
            type(engine,pinyin,a,"d6d");
            CHECK(press(engine,pinyin,a,FcitxKey_Down));
            auto expectedInternal=a.inputPanel().candidateList()->candidate(1).text().toString();
            CHECK(press(engine,pinyin,a,FcitxKey_space));CHECK(a.committed.back()==expectedInternal);
            // Focus loss clears internal mode instead of exposing it in another field.
            CHECK(press(engine,pinyin,a,FcitxKey_F7));CHECK(press(engine,pinyin,a,FcitxKey_F7,true));
            type(engine,pinyin,a,"d6d0");a.focusOut();a.focusIn();
            type(engine,pinyin,a,"shan");
            CHECK(a.inputPanel().clientPreedit().toString()=="shan");
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            a.setCapabilityFlags(CapabilityFlag::Password);
            CHECK(!press(engine,pinyin,a,FcitxKey_F7));
            CHECK(a.inputPanel().clientPreedit().empty());
            a.setCapabilityFlags(CapabilityFlag::Preedit);
            {std::ofstream settings(configPath);settings<<"[CowType]\nVersion=1\nModeHotkey=F11\nInternalCodeHotkey=F12\n";}
            addon->reloadConfig();
            CHECK(!press(engine,pinyin,a,FcitxKey_F7));
            CHECK(press(engine,pinyin,a,FcitxKey_F12));CHECK(press(engine,pinyin,a,FcitxKey_F12,true));
            type(engine,pinyin,a,"d6d0");CHECK(press(engine,pinyin,a,FcitxKey_space));CHECK(a.committed.back()=="中");
            CHECK(press(engine,pinyin,a,FcitxKey_F11));CHECK(press(engine,pinyin,a,FcitxKey_F11,true));
            CHECK(!press(engine,pinyin,a,FcitxKey_a));
            CHECK(press(engine,pinyin,a,FcitxKey_F11));CHECK(press(engine,pinyin,a,FcitxKey_F11,true));
            {std::ofstream settings(configPath);settings<<"[CowType]\nVersion=1\nInternalCodeHotkey=None\n";}
            addon->reloadConfig();CHECK(!press(engine,pinyin,a,FcitxKey_F7));
            // CLDR candidates travel through the real native decoder/session ABI.
            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nEmoji=True\nLearning=False\n"; }
            addon->reloadConfig();
            type(engine,pinyin,a,"shan");
            CHECK(a.inputPanel().candidateList()->candidate(0).text().toString()=="山");
            CHECK(a.inputPanel().candidateList()->candidate(1).text().toString()=="⛰️");
            CHECK(press(engine,pinyin,a,FcitxKey_2)); CHECK(a.committed.back()=="⛰️");
            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nEmoji=False\nLearning=False\n"; }
            addon->reloadConfig(); type(engine,pinyin,a,"shan");
            CHECK(a.inputPanel().candidateList()->candidate(1).text().toString()!="⛰️");
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            // Default activation restores Chinese, including after an F6 switch.
            CHECK(press(engine,pinyin,a,FcitxKey_F6));
            engine->deactivate(pinyin,switched); engine->activate(pinyin,activate);
            CHECK(press(engine,pinyin,a,FcitxKey_a));
            CHECK(press(engine,pinyin,a,FcitxKey_Escape));
            for (const auto &options : {std::string("ModeHotkey=F6\nRememberMode=True\n"),
                                        std::string("ModeHotkey=F6\nRememberMode=False\n"),
                                        std::string("ModeHotkey=F6\nShiftToggle=False\nRememberMode=True\n")}) {
                { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\n" << options; }
                addon->reloadConfig();
                CHECK(press(engine,pinyin,a,FcitxKey_F6));
                engine->deactivate(pinyin,switched); engine->activate(pinyin,activate);
                const bool remember = options.find("RememberMode=True")!=std::string::npos;
                CHECK(press(engine,pinyin,a,FcitxKey_a) == !remember);
                if (remember) {
                    // A different input field starts in Chinese; this field keeps English.
                    InputContextEvent activateB(&b,EventType::InputContextInputMethodActivated);
                    engine->activate(pinyin,activateB);
                    CHECK(press(engine,pinyin,b,FcitxKey_a));
                    CHECK(press(engine,pinyin,b,FcitxKey_Escape));
                    CHECK(!press(engine,pinyin,a,FcitxKey_a));
                    CHECK(press(engine,pinyin,a,FcitxKey_F6));
                } else { CHECK(press(engine,pinyin,a,FcitxKey_Escape)); }
            }
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
            auto stageDeadline=now(CLOCK_MONOTONIC)+10000000;
            auto timer = instance.eventLoop().addTimeEvent(CLOCK_MONOTONIC, now(CLOCK_MONOTONIC)+200000, 1000,
                [&](EventSourceTime *source, uint64_t) {
                    try {
                        // Subprocess startup is slower under QEMU. Await the
                        // observable result, retaining a bounded failure time;
                        // leave the modifier-hold timing assertions unchanged.
                        bool ready=true;
                        switch(stage) {
                        case 0: case 17: case 22: case 29: case 31:
                            ready=a.inputPanel().clientPreedit().toString()=="合成中间结果";break;
                        case 1: ready=a.committed.size()>=before+1;break;
                        case 6: ready=a.inputPanel().auxDown().toString()=="合成后端错误";break;
                        case 9: ready=a.committed.size()>=before+2;break;
                        case 13: ready=a.committed.size()>=before+3;break;
                        case 15: ready=a.committed.size()>=before+4;break;
                        case 18: ready=a.committed.size()>=before+5;break;
                        case 23: ready=a.committed.size()>=before+6;break;
                        case 28: ready=std::filesystem::exists(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-prepared");break;
                        case 30: ready=a.committed.size()>=before+7;break;
                        case 32: ready=a.committed.size()>=before+8;break;
                        }
                        if(!ready && now(CLOCK_MONOTONIC)<stageDeadline) {
                            source->setNextInterval(20000);source->setOneShot();return true;
                        }
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
                        case 30:
                            CHECK(a.committed.size()==before+7);
                            { std::ofstream settings(configPath); settings << "[CowType]\nVersion=1\nModeHotkey=F6\nVoice=True\nVoiceHotkey=Shift\nVoiceModifierHold=False\n"; }
                            addon->reloadConfig();
                            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_R));CHECK(!press(engine,pinyin,a,FcitxKey_Shift_R,true));break;
                        case 31:
                            CHECK(a.inputPanel().clientPreedit().toString()=="合成中间结果");
                            CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L));CHECK(!press(engine,pinyin,a,FcitxKey_Shift_L,true));break;
                        default:
                            CHECK(a.committed.size()==before+8);
                            CHECK(press(engine,pinyin,a,FcitxKey_a));CHECK(press(engine,pinyin,a,FcitxKey_Escape));
                            instance.eventLoop().exit(); return false;
                        }
                    } catch(const std::exception &error) { voiceFailure=error.what();instance.eventLoop().exit();return false; }
                    stageDeadline=now(CLOCK_MONOTONIC)+10000000;
                    source->setNextInterval(200000);source->setOneShot();return true;
                });
            instance.eventLoop().exec();
            if(!voiceFailure.empty()){throw std::runtime_error(voiceFailure);}
            std::ifstream pids(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-pids");
            std::string currentPid;size_t starts=0;
            std::vector<std::string> workerPids;
            while(std::getline(pids,currentPid)){workerPids.push_back(currentPid);++starts;}
            CHECK(starts==13);
            std::ifstream prepared(std::filesystem::path(std::getenv("COWTYPE_TEST_ROOT"))/"voice-test-prepared");
            std::string preparedPid;std::getline(prepared,preparedPid);CHECK(preparedPid==workerPids[11]);
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
            a.focusIn();type(engine,wubi,a,"u4e2d");
            CHECK(press(engine,wubi,a,FcitxKey_Return));CHECK(a.committed.back()=="中");
        }
        dlclose(module);
        std::cout << "Fcitx shared addon: candidate UI, page/click, two fields, focus, switch, sensitive field, mode, Wubi passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
