// SPDX-License-Identifier: GPL-3.0-or-later
// Uses public Fcitx5 APIs; optional version differences live in compat/.
#include "cowtype2.h"
#include "messages.h"
#include "diagnostics.h"
#include "dbus.h"
#include "voice.h"
#include "level.h"
#include "libime.h"
#include "shortcuts.h"
#include "translate.h"
#include "autospace.h"
#include "traditional.h"
#include "scripts.h"
#include "../gb18030/decoder.h"
#include "../unicode/decoder.h"
#include "../compat/addon.h"
#include <fcitx-config/configuration.h>
#include <fcitx/addonfactory.h>
#include <fcitx/addonmanager.h>
#include <fcitx/candidatelist.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputcontextproperty.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/surroundingtext.h>
#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/key.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using EnginePtr = std::unique_ptr<Cowtype2Engine, decltype(&cowtype2_engine_free)>;
using SessionPtr = std::unique_ptr<Cowtype2Session, decltype(&cowtype2_session_free)>;
using namespace fcitx;
std::string environment(const char *name, const std::string &fallback) {
    const char *value = std::getenv(name); return value && *value ? value : fallback;
}
class SettingsLauncher final : public Configuration {
public:
    const char *typeName() const override { return "CowtypeSettingsLauncher"; }
    ExternalOption settings{this, "Settings", ct_tr("action.settings"), "cowtype-config"};
};
struct State : InputContextProperty {
    SessionPtr session{cowtype2_session_new(nullptr), cowtype2_session_free};
    unsigned generation = 0;
    int scheme = -1;
    unsigned configGeneration = 0;
    bool active=false;
    Shortcuts::Gesture shortcuts;
    std::unique_ptr<EventSourceTime> voiceHold;
    bool internalCode = false;
    bool unicodeCode = false;
    bool unicodePrefix = false;
    bool traditional = false;
    std::string unicodeBuffer;
    void clearUnicode() { unicodeCode=false;unicodePrefix=false;unicodeBuffer.clear(); }
    std::string internalBuffer;
    int internalCursor = 0;
    VoiceLevel voiceLevel;
    std::string voiceStatus, voicePartial;
    bool translating=false, translateDraft=false;
    std::string translateInput, translateResult, translateStatus;
    std::string scriptTrigger;
    std::vector<std::string> scriptCandidates;
    size_t scriptCursor=0;
    bool scriptFailed=false;
    std::string scriptReason;
    bool scriptDismissed=false;
    bool scriptMerged=false;
};
class InternalWord final : public CandidateWord {
    std::function<void(InputContext *)> select_;
public:
    InternalWord(std::string text, std::string code, std::function<void(InputContext *)> select)
        : CandidateWord(Text(std::move(text))), select_(std::move(select)) { setComment(Text(std::move(code))); }
    void select(InputContext *ic) const override { select_(ic); }
};
class Cowtype;
class Word final : public CandidateWord {
public:
    Word(std::string text, std::string reading, Cowtype *owner, size_t index)
        : CandidateWord(Text(std::move(text))), owner_(owner), index_(index) { if(!reading.empty()) setComment(Text(std::move(reading))); }
    void select(InputContext *) const override;
private:
    Cowtype *owner_; size_t index_;
};
class Page final : public CommonCandidateList {
public:
    Page(Cowtype *owner, InputContext *ic, bool previous, bool next)
        : owner_(owner), ic_(ic), previous_(previous), next_(next) {}
    bool hasPrev() const override { return previous_; }
    bool hasNext() const override { return next_; }
    void prev() override;
    void next() override;
private:
    Cowtype *owner_; InputContext *ic_; bool previous_, next_;
};
struct Resource {
    EnginePtr engine{nullptr, cowtype2_engine_free};
    std::future<std::pair<EnginePtr, std::string>> pending;
    unsigned generation = 0;
    bool reload = false;
    bool pendingSemicolon = false;
    std::string failure, requestedStamp;
};

class Cowtype final : public InputMethodEngineV2 {
public:
    explicit Cowtype(Instance *instance) : instance_(instance), factory_([](InputContext &) { return new State; }), voice_(instance->eventLoop(), [this](InputContext *ic,char kind,const std::string &text) {
        auto *state=ic->propertyFor(&factory_);
        if(kind=='F'){state->voicePartial.clear();state->voiceStatus=text.empty()?ct_tr("message.9f30c9b3ac26"):"";if(!text.empty() && ic->hasFocus() && !ic->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)){commitText(ic,text);}}
        else if(kind=='E'){state->voicePartial.clear();state->voiceStatus=text;}
        else if(kind=='P'){state->voicePartial=text;}
        else if(kind=='R'){state->voiceLevel.clear();state->voiceStatus=state->voiceLevel.push("0");}
        else if(kind=='V'){auto value=state->voiceLevel.push(text);if(!value.empty()) state->voiceStatus=std::move(value);}
        else {state->voiceStatus=text;}
        render(ic,false);
    }), translator_(instance->eventLoop(), [this](InputContext *ic,bool success,const std::string &value) {
        auto *state=ic->propertyFor(&factory_);
        if(!state->translating)return;
        if(success){state->translateResult=value;state->translateStatus=ct_tr("translate.ready");}
        else {state->translateResult.clear();state->translateStatus=ct_tr(value);}
        render(ic,false);
    }) {
        if (!runtime_) { throw std::runtime_error("CowType settings runtime allocation failed"); }
        if (cowtype2_abi_version() / 1000 != 2) { throw std::runtime_error("CowType ABI major mismatch"); }
        instance_->inputContextManager().registerProperty("cowtype-v2-session", &factory_);
        focus_ = instance_->watchEvent(EventType::InputContextFocusOut, EventWatcherPhase::PreInputMethod,
            [this](Event &event) { auto *ic = static_cast<InputContextEvent &>(event).inputContext();
                auto *state = ic->propertyFor(&factory_); cancelVoice(ic);cancelTranslation(ic);state->internalCode=false;state->internalBuffer.clear();state->clearUnicode();state->shortcuts.reset(); cowtype2_session_reset(state->session.get(), false); render(ic, false); });
        resourceTimer_=instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC,now(CLOCK_MONOTONIC)+20000,1000,
            [this](EventSourceTime *timer,uint64_t) {
                for(int scheme=0;scheme<2;++scheme) {
                    if(!finishResource(scheme)) continue;
                    instance_->inputContextManager().foreach([this,scheme](InputContext *ic) {
                        auto *state=ic->propertyFor(&factory_);
                        if(state->scheme==scheme) {
                            attach(ic,scheme);
                            if(ic->hasFocus() && state->active) render(ic,false);
                        }
                        return true;
                    });
                }
                if(resources_[0].pending.valid() || resources_[1].pending.valid()) {timer->setNextInterval(20000);timer->setOneShot();}
                return true;
            });
        resourceTimer_->setEnabled(false);
        reloadConfig();
        try {
            inputBus_=std::make_unique<cowtype::InputBus>(instance_->eventLoop(),
                [this] { return controlMode(); },
                [this](const std::string &mode) { return controlSetMode(mode); }, COWTYPE_VERSION);
        } catch(const std::exception &) { ctDiagnostic("event=dbus_unavailable"); }
        traditionalOutput_=instance_->connect<Instance::OutputFilter>(
            [this](InputContext *ic,Text &text) {
                if(ic->propertyFor(&factory_)->traditional) convertOutputText(text,true);
            });
        traditionalCommit_=instance_->connect<Instance::CommitFilter>(
            [this](InputContext *ic,std::string &text) {
                if(ic->propertyFor(&factory_)->traditional) text=traditional_.convert(text,true);
            });
        ctDiagnostic("event=addon_ready version=" COWTYPE_VERSION " elapsed_ms="+std::to_string((now(CLOCK_MONOTONIC)-initializedAt_)/1000));
    }
    const Configuration *getConfig() const override { return &settingsLauncher_; }
    void reloadConfig() override {
        const auto began=now(CLOCK_MONOTONIC);
        if(auto *ic=voice_.owner()){cancelVoice(ic);render(ic,false);}voice_.cancelAll();
        translator_.cancel();
        cowtype2_runtime_reload(runtime_.get());++configGeneration_;
        scripts_.reload();
        instance_->inputContextManager().foreach([this](InputContext *ic) {
            auto *state=ic->propertyFor(&factory_);
            const bool wasTranslating=state->translating;
            state->scriptTrigger.clear();state->scriptCandidates.clear();state->scriptCursor=0;
            cowtype2_session_scripts(state->session.get(),"",nullptr,nullptr,0);state->scriptMerged=false;
            cancelTranslation(ic);state->shortcuts.reset();state->voiceHold.reset();
            if(state->internalCode || state->unicodeCode || wasTranslating){state->internalCode=false;state->internalBuffer.clear();state->clearUnicode();render(ic,false);}
            return true;
        });
        voice_.setResident(cowtype2_runtime_option(runtime_.get(),4));
        if(!cowtype2_runtime_option(runtime_.get(),3)){if(auto *ic=voice_.owner()){cancelVoice(ic);render(ic,false);}}
        if(cowtype2_runtime_option(runtime_.get(),3) && cowtype2_runtime_option(runtime_.get(),4) && cowtype2_runtime_option(runtime_.get(),8))
            voice_.prepare(environment("COWTYPE_VOICE_HELPER",COWTYPE_VOICE_EXECUTABLE));
        for (int scheme = 0; scheme < 2; ++scheme) start(scheme);
        instance_->inputContextManager().foreach([this](InputContext *ic) {
            auto *state=ic->propertyFor(&factory_);
            if(state->scheme>=0) {
                attach(ic,state->scheme);
                if(state->active && ic->hasFocus()) render(ic,false);
            }
            return true;
        });
        ctDiagnostic("event=config_reload elapsed_ms="+std::to_string((now(CLOCK_MONOTONIC)-began)/1000));
    }
    void activate(const InputMethodEntry &entry, InputContextEvent &event) override {
        const auto began=now(CLOCK_MONOTONIC);
        const int scheme=entry.uniqueName()=="cowtype-wubi"?1:0;
        event.inputContext()->propertyFor(&factory_)->active=true;
        attach(event.inputContext(),scheme);
        auto *session = event.inputContext()->propertyFor(&factory_)->session.get();
        if (!cowtype2_runtime_option(runtime_.get(),11) && cowtype2_session_mode(session)) {
            cowtype2_session_key(session,CT2_TOGGLE,0,false,0xffffffff);
        }
        render(event.inputContext(), false);
        ctDiagnostic("event=activate scheme="+std::to_string(scheme)+" ready="+std::to_string(bool(resources_[scheme].engine))+" elapsed_ms="+std::to_string((now(CLOCK_MONOTONIC)-began)/1000));
    }
    void deactivate(const InputMethodEntry &, InputContextEvent &event) override {
        const bool wasTranslating=event.inputContext()->propertyFor(&factory_)->translating;
        event.inputContext()->propertyFor(&factory_)->active=false;
        event.inputContext()->propertyFor(&factory_)->internalCode=false;
        event.inputContext()->propertyFor(&factory_)->internalBuffer.clear();
        event.inputContext()->propertyFor(&factory_)->clearUnicode();
        cancelVoice(event.inputContext());cancelTranslation(event.inputContext());
        bool commit = !wasTranslating && event.type() == EventType::InputContextSwitchInputMethod;
        if (commit) { commit = static_cast<InputContextSwitchInputMethodEvent &>(event).reason() != InputMethodSwitchedReason::CapabilityChanged; }
        cowtype2_session_reset(event.inputContext()->propertyFor(&factory_)->session.get(), commit);
        render(event.inputContext(), true);
    }
    void reset(const InputMethodEntry &, InputContextEvent &event) override {
        cancelVoice(event.inputContext());cancelTranslation(event.inputContext());
        event.inputContext()->propertyFor(&factory_)->internalCode=false;
        event.inputContext()->propertyFor(&factory_)->internalBuffer.clear();
        event.inputContext()->propertyFor(&factory_)->clearUnicode();
        cowtype2_session_reset(event.inputContext()->propertyFor(&factory_)->session.get(), false); render(event.inputContext(), false);
    }
    std::string subMode(const InputMethodEntry &, InputContext &ic) override {
        if(ic.propertyFor(&factory_)->translating)return ct_tr("translate.mode");
        if(ic.propertyFor(&factory_)->unicodeCode) return ct_tr("unicode.mode");
        if(ic.propertyFor(&factory_)->internalCode) return ct_tr("gb18030.mode");
        if(ic.propertyFor(&factory_)->traditional) return ct_tr("message.traditional_enabled");
        return cowtype2_session_mode(ic.propertyFor(&factory_)->session.get()) ? ct_tr("message.348ea84c6046") : ct_tr("message.72726d8818f6");
    }
    void keyEvent(const InputMethodEntry &entry, KeyEvent &event) override {
        CtSlowEvent latency;
        auto *ic = event.inputContext();
        struct SnapshotGuard {
            InputContext *ic;
            KeyEvent &event;
            bool enabled;
            ~SnapshotGuard() {
                if(enabled && !event.isRelease() && !event.accepted() && !event.key().isModifier())
                    ic->surroundingText().invalidate();
            }
        } snapshot{ic,event,bool(cowtype2_runtime_option(runtime_.get(),23))};
        if (ic->capabilityFlags().testAny(CapabilityFlags(CapabilityFlag::PasswordOrSensitive) | CapabilityFlag::Disable)) {
            cancelVoice(ic);cancelTranslation(ic);
            ic->propertyFor(&factory_)->internalCode=false;ic->propertyFor(&factory_)->internalBuffer.clear();
            ic->propertyFor(&factory_)->clearUnicode();
            cowtype2_session_reset(ic->propertyFor(&factory_)->session.get(), false); render(ic, false); return;
        }
        attach(ic, entry.uniqueName() == "cowtype-wubi" ? 1 : 0);
        uint32_t code = 0xffffffff;
        auto sym = event.key().sym();
        auto *state=ic->propertyFor(&factory_);
        if (const char *warning = cowtype2_runtime_warning(runtime_.get());
            warning && std::string_view(warning).find("cowtype-config-migrate") != std::string_view::npos) {
            render(ic, false);
            return;
        }
        const auto modifiers=event.key().states().toInteger() | event.rawKey().states().toInteger();
        const auto time=fcitx::now(CLOCK_MONOTONIC);

        const std::array<std::string_view,6> hotkeys{
            cowtype2_runtime_string(runtime_.get(),1), cowtype2_runtime_string(runtime_.get(),2),
            cowtype2_runtime_string(runtime_.get(),3), cowtype2_runtime_string(runtime_.get(),4),
            cowtype2_runtime_string(runtime_.get(),5), cowtype2_runtime_string(runtime_.get(),6)};
        const auto action=Shortcuts::resolveSpecs(sym,modifiers,hotkeys,
            cowtype2_runtime_option(runtime_.get(),3),cowtype2_runtime_option(runtime_.get(),17),
            traditional_.available());
        auto shortcut=state->shortcuts.handle(sym,modifiers,event.isRelease(),time,action,cowtype2_runtime_option(runtime_.get(),6));
        if(shortcut.interrupted || (event.isRelease() && shortcut.handled))state->voiceHold.reset();
        if(shortcut.cancelVoice && voice_.owns(ic)){cancelVoice(ic);render(ic,false);}
        if(shortcut.armHold) {
            state->voiceHold=instance_->eventLoop().addTimeEvent(CLOCK_MONOTONIC,time+300000,1000,
                [this,ic,generation=configGeneration_](EventSourceTime *source,uint64_t) {
                    source->setEnabled(false);auto *s=ic->propertyFor(&factory_);
                    if(generation==configGeneration_ && s->shortcuts.canHold() && ic->hasFocus() && !ic->capabilityFlags().testAny(CapabilityFlags(CapabilityFlag::PasswordOrSensitive)|CapabilityFlag::Disable)) {
                        s->shortcuts.held();invokeShortcut(ic,Shortcuts::Action::Voice);
                    }
                    return true;
                });
            state->voiceHold->setOneShot();
        }
        if(shortcut.trigger!=Shortcuts::Action::None)invokeShortcut(ic,shortcut.trigger);
        if(shortcut.stopVoice && voice_.owns(ic)){voice_.stop();state->voiceStatus=ct_tr("message.50698e404b8c");render(ic,false);}
        if(shortcut.accept)event.filterAndAccept();
        if(shortcut.handled)return;
        if(state->translating && translationKey(ic,event,modifiers))return;
        if(state->unicodeCode){unicodeKey(ic,event,modifiers);return;}
        if(state->internalCode){internalKey(ic,event,modifiers);return;}
        if(!state->translating && scriptKey(ic,event,modifiers))return;
        if(voice_.owns(ic) && !event.isRelease()){
            cancelVoice(ic);render(ic,false);
            if(sym==FcitxKey_Escape){event.filterAndAccept();return;}
        }
        if(!event.isRelease() && (sym==FcitxKey_Delete || sym==FcitxKey_BackSpace) && !(modifiers & (8|64)) && ((modifiers & 4) || (sym==FcitxKey_Delete && (modifiers & 1)))){
            const char *preedit=cowtype2_session_preedit(state->session.get());
            if(preedit && *preedit){cowtype2_session_forget(state->session.get());render(ic,false);event.filterAndAccept();return;}
        }
        if(!event.isRelease()){state->voiceStatus.clear();}
        switch (sym) {
        case FcitxKey_space: case FcitxKey_KP_Space: code = CT2_SPACE; break;
        case FcitxKey_Return: case FcitxKey_KP_Enter: code = CT2_ENTER; break;
        case FcitxKey_Escape: code = CT2_ESCAPE; break;
        case FcitxKey_BackSpace: code = CT2_BACKSPACE; break;
        case FcitxKey_Delete: case FcitxKey_KP_Delete: code = CT2_DELETE; break;
        case FcitxKey_Left: case FcitxKey_KP_Left: code = CT2_LEFT; break;
        case FcitxKey_Right: case FcitxKey_KP_Right: code = CT2_RIGHT; break;
        case FcitxKey_Up: case FcitxKey_KP_Up: code = CT2_PREVIOUS; break;
        case FcitxKey_Down: case FcitxKey_KP_Down: code = CT2_NEXT; break;
        case FcitxKey_Home: case FcitxKey_KP_Home: code = CT2_HOME; break;
        case FcitxKey_End: case FcitxKey_KP_End: code = CT2_END; break;
        case FcitxKey_Page_Up: case FcitxKey_KP_Page_Up: code = CT2_PAGE_UP; break;
        case FcitxKey_Page_Down: case FcitxKey_KP_Page_Down: code = CT2_PAGE_DOWN; break;
        default:
            code = Key::keySymToUnicode(sym);
            if (sym >= FcitxKey_KP_0 && sym <= FcitxKey_KP_9) { code = '0' + sym - FcitxKey_KP_0; }
        }
        // Keep a lone u's ordinary candidates; claim the sequence only when
        // its next character starts a Unicode code point. Digits must reach
        // the code buffer before the ordinary numeric candidate selection.
        if(!event.isRelease() && !(modifiers&(4|8|64|128)) && code<128
           && !state->translating && (state->scheme!=0 || !cowtype2_runtime_option(runtime_.get(),25)) && cowtype2_runtime_option(runtime_.get(),16) && !cowtype2_session_mode(state->session.get())) {
            const std::string_view preedit=cowtype2_session_preedit(state->session.get());
            if((preedit=="u" || preedit=="U") && cowtype2_session_cursor(state->session.get())==1
               && cowtype::UnicodeCode::accepts(preedit,char(code))
               && !(cowtype2_runtime_option(runtime_.get(),27) && scripts_.prefix(std::string(preedit)+char(code)))
               && !cowtype2_session_custom_prefix(state->session.get(),(std::string(preedit)+char(code)).c_str())) {
                state->unicodeBuffer=preedit;
                cowtype2_session_reset(state->session.get(),false);
                state->unicodeCode=true;state->unicodePrefix=true;
                unicodeKey(ic,event,modifiers);return;
            }
        }
        // Paging shortcuts only apply to a visible candidate list in Chinese mode.
        if(!event.isRelease() && !(modifiers & (1|4|8|64|128)) && !cowtype2_session_mode(state->session.get()) && ic->inputPanel().candidateList()) {
            if(cowtype2_runtime_option(runtime_.get(),9) && (sym==FcitxKey_bracketleft || sym==FcitxKey_bracketright))
                code=sym==FcitxKey_bracketleft?CT2_PAGE_UP:CT2_PAGE_DOWN;
            if(cowtype2_runtime_option(runtime_.get(),10) && (sym==FcitxKey_minus || sym==FcitxKey_equal))
                code=sym==FcitxKey_minus?CT2_PAGE_UP:CT2_PAGE_DOWN;
        }
        uint32_t previous = 0xffffffff;
        // Only ASCII numeric context is needed for the current punctuation guard.
        const auto &surrounding = ic->surroundingText();
        if (ic->capabilityFlags().test(CapabilityFlag::SurroundingText) && surrounding.isValid()
            && surrounding.cursor() == surrounding.anchor() && surrounding.cursor()) {
            const auto &text = surrounding.text(); size_t offset = 0; unsigned count = 0;
            while (offset < text.size() && count < surrounding.cursor()) {
                size_t start = offset++; while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) { ++offset; }
                if (++count == surrounding.cursor() && offset - start == 1) { previous = static_cast<unsigned char>(text[start]); }
            }
        }
        auto *session = ic->propertyFor(&factory_)->session.get();
        bool handled = cowtype2_session_key(session, code, event.key().states().toInteger(), event.isRelease(), previous);
        render(ic, true);
        // English mode and idle digits normally pass through. Only consume a
        // printable character when an actual Han/Latin boundary needs a space.
        if(!handled && !event.isRelease() && !(modifiers&(4|8|64|128)) &&
           cowtype::latinNumber(code) && cowtype2_runtime_option(runtime_.get(),23) &&
           cowtype::spacingContext(ic) && cowtype::han(cowtype::previousCharacter(ic))) {
            commitText(ic,std::string(1,static_cast<char>(code)));handled=true;
        }
        if (handled) { event.filterAndAccept(); }
    }
    void choose(InputContext *ic, size_t index) { cowtype2_session_select(ic->propertyFor(&factory_)->session.get(), index); render(ic, true); }
    void page(InputContext *ic, bool next) {
        auto *state=ic->propertyFor(&factory_);
        if(!state->scriptCandidates.empty() && !state->scriptDismissed) {
            const size_t size=cowtype2_runtime_option(runtime_.get(),0), start=state->scriptCursor/size*size;
            if(next && start+size<state->scriptCandidates.size())state->scriptCursor=start+size;
            if(!next)state->scriptCursor=start>=size?start-size:0;
            render(ic,false);return;
        }
        cowtype2_session_key(ic->propertyFor(&factory_)->session.get(), next ? CT2_PAGE_DOWN : CT2_PAGE_UP, 0, false, 0xffffffff); render(ic, true);
    }
private:
    void chooseScript(InputContext *ic,size_t index) {
        auto *s=ic->propertyFor(&factory_);
        if(index>=s->scriptCandidates.size() || !ic->hasFocus() || ic->capabilityFlags().testAny(CapabilityFlags(CapabilityFlag::PasswordOrSensitive)|CapabilityFlag::Disable))return;
        const auto text=s->scriptCandidates[index];
        cowtype2_session_reset(s->session.get(),false);
        s->scriptTrigger.clear();s->scriptCandidates.clear();
        commitText(ic,text);render(ic,false);
    }
    bool scriptKey(InputContext *ic,KeyEvent &event,uint32_t modifiers) {
        auto *s=ic->propertyFor(&factory_);
        if(s->scriptCandidates.empty() || event.isRelease() || (modifiers&(4|8|64|128)))return false;
        const auto sym=event.key().sym();const size_t size=s->scriptCandidates.size();
        if(sym==FcitxKey_Tab) {
            s->scriptDismissed=!s->scriptDismissed;render(ic,false);event.filterAndAccept();return true;
        }
        if(s->scriptDismissed)return false;
        const size_t pageSize=cowtype2_runtime_option(runtime_.get(),0);
        const bool plain=!(modifiers&1);
        const bool brackets=plain && cowtype2_runtime_option(runtime_.get(),9);
        const bool symbols=plain && cowtype2_runtime_option(runtime_.get(),10);
        if(sym==FcitxKey_space || sym==FcitxKey_Return || sym==FcitxKey_KP_Enter)chooseScript(ic,s->scriptCursor);
        else if(sym>=FcitxKey_1 && sym<FcitxKey_1+pageSize)chooseScript(ic,s->scriptCursor/pageSize*pageSize+sym-FcitxKey_1);
        else if(sym==FcitxKey_Up)s->scriptCursor=(s->scriptCursor+size-1)%size;
        else if(sym==FcitxKey_Down)s->scriptCursor=(s->scriptCursor+1)%size;
        else if(sym==FcitxKey_Page_Down || (brackets && sym==FcitxKey_bracketright) || (symbols && sym==FcitxKey_equal)){page(ic,true);event.filterAndAccept();return true;}
        else if(sym==FcitxKey_Page_Up || (brackets && sym==FcitxKey_bracketleft) || (symbols && sym==FcitxKey_minus)){page(ic,false);event.filterAndAccept();return true;}
        else return false;
        render(ic,false);event.filterAndAccept();return true;
    }
    void convertOutputText(Text &text,bool traditional) {
        if(text.empty() || !traditional_.available()) return;
        const auto original=text.toString();
        const auto oldLength=fcitx::utf8::lengthValidated(original);
        if(oldLength==fcitx::utf8::INVALID_LENGTH) return;
        auto converted=traditional_.convert(original,traditional);
        const auto newLength=fcitx::utf8::lengthValidated(converted);
        if(newLength==fcitx::utf8::INVALID_LENGTH || converted==original) return;
        Text output;
        size_t offset=0,remaining=newLength;
        for(size_t i=0;i<text.size();++i) {
            auto length=fcitx::utf8::lengthValidated(text.stringAt(i));
            if(length==fcitx::utf8::INVALID_LENGTH) return;
            length=std::min(remaining,length);
            remaining-=length;
            const auto bytes=fcitx::utf8::ncharByteLength(converted.begin()+offset,length);
            output.append(converted.substr(offset,bytes),text.formatAt(i));
            offset+=bytes;
        }
        if(text.cursor()>0) {
            auto cursor=fcitx::utf8::lengthValidated(original.substr(0,text.cursor()));
            cursor=std::min(cursor,newLength);
            output.setCursor(fcitx::utf8::ncharByteLength(output.toString().begin(),cursor));
        } else output.setCursor(text.cursor());
        text=std::move(output);
    }
    void commitText(InputContext *ic,const std::string &text,bool allowSpacing=true) {
        const bool spacing=cowtype2_runtime_option(runtime_.get(),23);
        if(spacing && allowSpacing && cowtype::spacingContext(ic))
            ic->commitString(cowtype::autoSpace(text,cowtype::previousCharacter(ic)));
        else ic->commitString(text);
        // A commit does not update Fcitx's cached surrounding snapshot. Reusing
        // it before the client reports fresh text would insert repeated spaces.
        if(spacing) ic->surroundingText().invalidate();
    }
    void cancelTranslation(InputContext *ic) {
        translator_.cancel(ic);auto *s=ic->propertyFor(&factory_);
        s->translating=s->translateDraft=false;
        s->translateInput.clear();s->translateResult.clear();s->translateStatus.clear();
    }
    void requestTranslation(InputContext *ic) {
        auto *s=ic->propertyFor(&factory_);
        if(s->translateDraft) {
            std::string text=s->translateInput+cowtype2_session_preview(s->session.get());
            if(text.empty())return;
            if(text.size()>32768){s->translateStatus=ct_tr("translate.error.input");render(ic,false);return;}
            s->translateInput=std::move(text);s->translateDraft=false;
        }
        if(s->translateInput.empty())return;
        if(s->translateInput.size()>32768){s->translateStatus=ct_tr("translate.error.input");render(ic,false);return;}
        if(translator_.active() && !translator_.owns(ic)) {s->translateStatus=ct_tr("translate.error.busy");render(ic,false);return;}
        s->translateResult.clear();s->translateStatus=ct_tr("translate.working");
        if(!translator_.start(ic,s->translateInput,environment("COWTYPE_TRANSLATE_HELPER",COWTYPE_TRANSLATE_EXECUTABLE)))s->translateStatus=ct_tr("translate.error.process");
        render(ic,false);
    }
    void invokeTranslation(InputContext *ic) {
        auto *s=ic->propertyFor(&factory_);
        if(s->translating){cancelTranslation(ic);render(ic,false);return;}
        voice_.cancel(ic);s->voiceHold.reset();s->voiceStatus.clear();s->voicePartial.clear();
        s->internalCode=false;s->internalBuffer.clear();s->clearUnicode();
        s->translating=true;
        if(const auto *preedit=cowtype2_session_preedit(s->session.get());preedit && *preedit) {
            s->translateInput=cowtype2_session_preview(s->session.get());requestTranslation(ic);return;
        }
        const auto &surrounding=ic->surroundingText();
        if(ic->capabilityFlags().test(CapabilityFlag::SurroundingText) && surrounding.isValid() && surrounding.cursor()!=surrounding.anchor()) {
            cancelTranslation(ic);s->voiceStatus=ct_tr("translate.error.selection");render(ic,false);return;
        }
        s->translateDraft=true;render(ic,false);
    }
    void chooseTranslation(InputContext *ic) {
        auto *s=ic->propertyFor(&factory_);
        if(!s->translating || s->translateResult.empty() || !ic->hasFocus()
           || ic->capabilityFlags().testAny(CapabilityFlags(CapabilityFlag::PasswordOrSensitive)|CapabilityFlag::Disable))return;
        const auto &surrounding=ic->surroundingText();
        if(ic->capabilityFlags().test(CapabilityFlag::SurroundingText) && surrounding.isValid() && surrounding.cursor()!=surrounding.anchor()) {
            s->translateResult.clear();s->translateStatus=ct_tr("translate.error.selection");render(ic,false);return;
        }
        auto text=s->translateResult;cancelTranslation(ic);cowtype2_session_reset(s->session.get(),false);
        // Frontends replace their current selection on commit; do not delete
        // arbitrary surrounding text or write translated text to learning.
        commitText(ic,text);render(ic,false);
    }
    bool translationKey(InputContext *ic,KeyEvent &event,uint32_t modifiers) {
        auto *s=ic->propertyFor(&factory_);auto sym=event.key().sym();
        if(event.isRelease() || (modifiers&(4|8|64|128)))return false;
        if(sym==FcitxKey_Escape){cancelTranslation(ic);render(ic,false);event.filterAndAccept();return true;}
        const bool enter=sym==FcitxKey_Return || sym==FcitxKey_KP_Enter;
        const bool space=sym==FcitxKey_space || sym==FcitxKey_KP_Space;
        if(!s->translateDraft) {
            if(enter || space || (!s->translateResult.empty() && (sym==FcitxKey_1 || sym==FcitxKey_KP_1))) {
                if(!s->translateResult.empty())chooseTranslation(ic);
                else if(!translator_.owns(ic))requestTranslation(ic);
                event.filterAndAccept();return true;
            }
            // Ordinary editing cancels the preview and restores composition.
            cancelTranslation(ic);render(ic,false);return false;
        }
        if(enter){requestTranslation(ic);event.filterAndAccept();return true;}
        const auto *preedit=cowtype2_session_preedit(s->session.get());
        if(s->translateInput.size()+std::char_traits<char>::length(preedit)>32768
           && sym!=FcitxKey_BackSpace && sym!=FcitxKey_Delete && sym!=FcitxKey_Left && sym!=FcitxKey_Right) {
            s->translateStatus=ct_tr("translate.error.input");render(ic,false);event.filterAndAccept();return true;
        }
        if(preedit && *preedit)return false;
        if(sym==FcitxKey_BackSpace) {
            if(!s->translateInput.empty()){
                size_t i=s->translateInput.size()-1;
                while(i && (static_cast<unsigned char>(s->translateInput[i])&0xc0)==0x80)--i;
                s->translateInput.erase(i);
            }
        } else if(space) {
            if(s->translateInput.size()<32768)s->translateInput+=' ';
        } else if(cowtype2_session_mode(s->session.get())) {
            auto code=Key::keySymToUnicode(sym);
            if(sym>=FcitxKey_KP_0 && sym<=FcitxKey_KP_9)code='0'+sym-FcitxKey_KP_0;
            if(code<32 || code==127 || code>0x10ffff)return false;
            auto text=fcitx::utf8::UCS4ToUTF8(code);
            if(s->translateInput.size()+text.size()<=32768)s->translateInput+=text;
        } else return false;
        render(ic,false);event.filterAndAccept();return true;
    }
    void renderTranslation(InputContext *ic) {
        auto *s=ic->propertyFor(&factory_);auto &panel=ic->inputPanel();panel.reset();
        Text text(s->translateInput,TextFormatFlags(TextFormatFlag::Underline)|TextFormatFlag::DontCommit);text.setCursor(s->translateInput.size());
        if(ic->capabilityFlags().test(CapabilityFlag::Preedit))panel.setClientPreedit(text);else panel.setPreedit(text);
        if(!s->translateResult.empty()) {
            auto list=std::make_unique<CommonCandidateList>();list->setPageSize(1);
            list->setSelectionKey({Key(FcitxKey_1)});
            list->append<InternalWord>(s->translateResult,"",[this](InputContext *context){chooseTranslation(context);});
            list->setGlobalCursorIndex(0);panel.setCandidateList(std::move(list));
        }
        panel.setAuxDown(Text(s->translateStatus));ic->updatePreedit();ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    void chooseInternal(InputContext *ic,size_t index) {
        auto *state=ic->propertyFor(&factory_);
        if(!state->internalCode || !ic->hasFocus() || ic->capabilityFlags().testAny(CapabilityFlags(CapabilityFlag::PasswordOrSensitive)|CapabilityFlag::Disable)) return;
        auto candidates=cowtype::InternalCode::candidates(state->internalBuffer);
        if(index>=candidates.size())return;
        commitText(ic,candidates[index].text,false);
        state->internalCode=false;state->internalBuffer.clear();state->clearUnicode();state->internalCursor=0;
        render(ic,false);
    }
    void internalKey(InputContext *ic,KeyEvent &event,uint32_t modifiers) {
        auto *state=ic->propertyFor(&factory_);
        if(event.isRelease())return;
        if(modifiers&(4|8|64|128))return;
        auto sym=event.key().sym();
        if(sym==FcitxKey_Escape) {state->internalCode=false;state->internalBuffer.clear();state->clearUnicode();}
        else if(sym==FcitxKey_BackSpace) {if(!state->internalBuffer.empty())state->internalBuffer.pop_back();state->internalCursor=0;}
        else if(sym==FcitxKey_space || sym==FcitxKey_KP_Space || sym==FcitxKey_Return || sym==FcitxKey_KP_Enter) {
            chooseInternal(ic,state->internalCursor);event.filterAndAccept();return;
        } else if(sym==FcitxKey_Up || sym==FcitxKey_Down) {
            auto candidates=cowtype::InternalCode::candidates(state->internalBuffer);
            if(!candidates.empty())state->internalCursor=std::clamp(state->internalCursor+(sym==FcitxKey_Down?1:-1),0,int(candidates.size())-1);
        } else {
            uint32_t code=Key::keySymToUnicode(sym);
            if(sym>=FcitxKey_KP_0 && sym<=FcitxKey_KP_9)code='0'+sym-FcitxKey_KP_0;
            if(code<128 && cowtype::InternalCode::accepts(char(code)) && state->internalBuffer.size()<8) {
                state->internalBuffer+=char(std::tolower(code));state->internalCursor=0;
            }
        }
        render(ic,false);event.filterAndAccept();
    }
    void renderInternal(InputContext *ic) {
        auto *state=ic->propertyFor(&factory_);auto &panel=ic->inputPanel();panel.reset();
        if(state->internalBuffer.empty())panel.setAuxUp(Text(ct_tr("gb18030.hint")));
        Text preedit(state->internalBuffer,TextFormatFlags(TextFormatFlag::Underline)|TextFormatFlag::DontCommit);
        preedit.setCursor(state->internalBuffer.size());
        if(ic->capabilityFlags().test(CapabilityFlag::Preedit))panel.setClientPreedit(preedit);else panel.setPreedit(preedit);
        auto candidates=cowtype::InternalCode::candidates(state->internalBuffer);
        auto list=std::make_unique<CommonCandidateList>();list->setPageSize(16);
        for(size_t i=0;i<candidates.size();++i) {
            list->append<InternalWord>(candidates[i].text,candidates[i].code,[this,i](InputContext *context){chooseInternal(context,i);});
        }
        if(!candidates.empty())list->setGlobalCursorIndex(state->internalCursor);
        if(candidates.empty() && (state->internalBuffer.size()==4 || state->internalBuffer.size()==8))panel.setAuxDown(Text(ct_tr("gb18030.invalid")));
        panel.setCandidateList(std::move(list));
        ic->updatePreedit();ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    void chooseUnicode(InputContext *ic) {
        auto *state=ic->propertyFor(&factory_);
        if(!state->unicodeCode || !ic->hasFocus() || ic->capabilityFlags().testAny(CapabilityFlags(CapabilityFlag::PasswordOrSensitive)|CapabilityFlag::Disable))return;
        auto candidates=cowtype::UnicodeCode::candidates(state->unicodeBuffer);
        if(candidates.empty())return;
        commitText(ic,candidates[0].text,false);state->clearUnicode();render(ic,false);
    }
    void unicodeKey(InputContext *ic,KeyEvent &event,uint32_t modifiers) {
        auto *state=ic->propertyFor(&factory_);
        if(event.isRelease() || (modifiers&(4|8|64|128)))return;
        auto sym=event.key().sym();
        if(sym==FcitxKey_Escape){state->clearUnicode();}
        else if(sym==FcitxKey_BackSpace){
            if(!state->unicodeBuffer.empty())state->unicodeBuffer.pop_back();
            if(state->unicodePrefix && (state->unicodeBuffer=="u" || state->unicodeBuffer=="U")) {
                auto prefix=state->unicodeBuffer;state->clearUnicode();
                cowtype2_session_key(state->session.get(),prefix[0],0,false,0xffffffff);
            }
        }
        else if(sym==FcitxKey_space || sym==FcitxKey_KP_Space || sym==FcitxKey_Return || sym==FcitxKey_KP_Enter){chooseUnicode(ic);event.filterAndAccept();return;}
        else {
            uint32_t code=Key::keySymToUnicode(sym);
            if(sym>=FcitxKey_KP_0 && sym<=FcitxKey_KP_9)code='0'+sym-FcitxKey_KP_0;
            if(code<128 && cowtype::UnicodeCode::accepts(state->unicodeBuffer,char(code)))state->unicodeBuffer+=char(code);
            else if(state->unicodePrefix && code<128 && std::isalpha(static_cast<unsigned char>(code))
                    && std::all_of(state->unicodeBuffer.begin(),state->unicodeBuffer.end(),[](unsigned char c){return std::isalpha(c);})) {
                // Alphabetic input such as "ubuntu" must remain ordinary
                // composition when it stops being a hexadecimal sequence.
                auto text=state->unicodeBuffer+char(code);state->clearUnicode();
                for(unsigned char c:text)cowtype2_session_key(state->session.get(),c,0,false,0xffffffff);
            }
        }
        render(ic,false);event.filterAndAccept();
    }
    void renderUnicode(InputContext *ic) {
        auto *state=ic->propertyFor(&factory_);auto &panel=ic->inputPanel();panel.reset();
        if(state->unicodeBuffer.empty())panel.setAuxUp(Text(ct_tr("unicode.hint")));
        Text preedit(state->unicodeBuffer,TextFormatFlags(TextFormatFlag::Underline)|TextFormatFlag::DontCommit);
        preedit.setCursor(state->unicodeBuffer.size());
        if(ic->capabilityFlags().test(CapabilityFlag::Preedit))panel.setClientPreedit(preedit);else panel.setPreedit(preedit);
        auto candidates=cowtype::UnicodeCode::candidates(state->unicodeBuffer);
        auto list=std::make_unique<CommonCandidateList>();list->setPageSize(1);
        if(!candidates.empty()) {
            list->append<InternalWord>(candidates[0].text,candidates[0].code,[this](InputContext *context){chooseUnicode(context);});
            list->setGlobalCursorIndex(0);
        } else if(!state->unicodeBuffer.empty() && state->unicodeBuffer!="u" && state->unicodeBuffer!="U" && !cowtype::UnicodeCode::digits(state->unicodeBuffer).empty())panel.setAuxDown(Text(ct_tr("unicode.invalid")));
        panel.setCandidateList(std::move(list));ic->updatePreedit();ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    InputContext *controlContext() {
        auto *ic=instance_->inputContextManager().lastFocusedInputContext();
        if(!ic || !ic->hasFocus() || !ic->propertyFor(&factory_)->active)
            throw dbus::MethodCallError("org.cowtype.Input.Error.Inactive","No focused CowType input context");
        if(ic->capabilityFlags().testAny(CapabilityFlags(CapabilityFlag::PasswordOrSensitive)|CapabilityFlag::Disable))
            throw dbus::MethodCallError("org.cowtype.Input.Error.Unavailable","Input context does not allow control");
        return ic;
    }
    std::string controlMode() {
        auto *ic=controlContext();
        return cowtype2_session_mode(ic->propertyFor(&factory_)->session.get()) ? "en" : "zh";
    }
    std::string controlSetMode(const std::string &mode) {
        auto *ic=controlContext();
        const bool english=cowtype2_session_mode(ic->propertyFor(&factory_)->session.get());
        if(mode=="toggle" || english!=(mode=="en")) invokeShortcut(ic,Shortcuts::Action::Mode);
        return controlMode();
    }
    void invokeShortcut(InputContext *ic,Shortcuts::Action action) {
        auto *state=ic->propertyFor(&factory_);
        if(action==Shortcuts::Action::Translate){invokeTranslation(ic);return;}
        cancelTranslation(ic);
        if(action==Shortcuts::Action::Voice) {
            state->internalCode=false;state->internalBuffer.clear();state->clearUnicode();
            bool starting=!voice_.active();toggleVoice(ic);
            state->shortcuts.voiceStarted(starting && voice_.owns(ic));render(ic,false);return;
        }
        // Do not reset the shortcut gesture here: its release/repeat belongs
        // to the shared state machine even after changing the input mode.
        voice_.cancel(ic);state->voiceHold.reset();state->voiceStatus.clear();state->voicePartial.clear();
        state->internalBuffer.clear();state->internalCursor=0;state->unicodeBuffer.clear();state->unicodePrefix=false;
        if(action==Shortcuts::Action::InternalCode) {
            state->unicodeCode=false;
            if(!cowtype::InternalCode::supported()){state->voiceStatus=ct_tr("gb18030.unavailable");ctDiagnostic("event=gb18030_unavailable reason=system_mapping");}
            else {state->unicodeCode=false;state->internalCode=!state->internalCode;}
        } else if(action==Shortcuts::Action::Unicode) {
            state->internalCode=false;state->unicodeCode=!state->unicodeCode;
        } else if(action==Shortcuts::Action::Mode) {
            state->internalCode=false;state->unicodeCode=false;cowtype2_session_key(state->session.get(),CT2_TOGGLE,0,false,0xffffffff);
        } else if(action==Shortcuts::Action::Traditional) {
            state->traditional=!state->traditional;
        }
        render(ic,action==Shortcuts::Action::Mode);
        if(action==Shortcuts::Action::Traditional) {
            instance_->showCustomInputMethodInformation(ic,
                ct_tr(state->traditional?"message.traditional_enabled":"message.simplified_enabled"));
        }
        if(action==Shortcuts::Action::Mode) {
            instance_->showCustomInputMethodInformation(ic,
                cowtype2_session_mode(state->session.get()) ? ct_tr("message.348ea84c6046") : ct_tr("message.72726d8818f6"));
        }
    }
    void toggleVoice(InputContext *ic) {
        auto *state=ic->propertyFor(&factory_);
        if(voice_.owns(ic)) { voice_.stop();state->voiceStatus=ct_tr("message.50698e404b8c"); }
        else if(voice_.active()) state->voiceStatus=ct_tr("message.3a1b85f91ecc");
        else if(const char *p=cowtype2_session_preedit(state->session.get());p && *p) state->voiceStatus=ct_tr("message.fae536a183cb");
        else {state->voiceStatus=ct_tr("message.71cea6a7bc11");if(!voice_.start(ic,environment("COWTYPE_VOICE_HELPER",COWTYPE_VOICE_EXECUTABLE))) state->voiceStatus=ct_tr("message.f1e43bff56c4");}
    }
    void cancelVoice(InputContext *ic){
        voice_.cancel(ic);auto *state=ic->propertyFor(&factory_);state->voiceHold.reset();state->voiceLevel.clear();state->voiceStatus.clear();state->voicePartial.clear();state->shortcuts.reset();
    }
    void start(int scheme) {
        auto &resource = resources_[scheme];
        auto data = environment("COWTYPE_DATA_DIR", COWTYPE_SYSTEM_DATA_DIR);
        std::string home = environment("XDG_DATA_HOME", environment("HOME", "") + "/.local/share");
        std::string path = data + (scheme ? "/wubi/lexicon.tsv" : "/ice/lexicon.tsv");
        std::string optional = scheme ? "" : home + "/cowtype/dictionaries/wanxiang/lexicon.tsv";
        const bool statisticalModel = !scheme && cowtype2_runtime_option(runtime_.get(),12);
        const auto system = scheme || !cowtype2_runtime_option(runtime_.get(),19) ? "" : LibimeDecoder::systemPath();
        const auto model = statisticalModel ? LibimeDecoder::modelPath() : "";
        const auto profile=scheme || !cowtype2_runtime_option(runtime_.get(),25)?0:cowtype2_runtime_option(runtime_.get(),24);
        const std::string customProfile=profile==8?cowtype2_runtime_string(runtime_.get(),0):"";
        const auto cache = LibimeDecoder::installed("XDG_CACHE_HOME", (environment("HOME", "") + "/.cache").c_str());
        cowtype::PinyinSources sources{path, optional, system, cache.empty() ? "" : cache + "/cowtype/dictionaries", data + "/compiled"};
        std::string stamp = statisticalModel ? "model-on;" : "model-off;";
        stamp+=std::to_string(profile)+";";
        for(const auto &file:{path,optional,system,model,customProfile}) {
            std::error_code error;
            const auto size=std::filesystem::file_size(file,error);
            const auto modified=std::filesystem::last_write_time(file,error);
            stamp+=file+":"+(error?"missing":std::to_string(size)+":"+std::to_string(modified.time_since_epoch().count()))+";";
        }
        if(resource.requestedStamp==stamp && (resource.engine || resource.pending.valid())) {
            ctDiagnostic("event=dictionary_reuse scheme="+std::to_string(scheme));return;
        }
        resource.pendingSemicolon=false;
        if(!scheme && cowtype2_runtime_option(runtime_.get(),25)) {
            // Read only the bounded profile during configuration reload; never
            // guess whether a punctuation key belongs to a loading decoder.
            try {resource.pendingSemicolon=cowtype::shuangpinProfile(profile,customProfile,nullptr)->validInput().contains(';');}
            catch(const std::exception &) { /* Worker reports the profile error without its contents. */ }
        }
        // A different decoder configuration must never reinterpret input using
        // the previous profile while its replacement is loading or has failed.
        if(resource.engine) {resource.engine.reset();++resource.generation;}
        if(resource.pending.valid()) {resource.reload=true;return;}
        resource.requestedStamp=std::move(stamp);
        ctDiagnostic("event=dictionary_start scheme="+std::to_string(scheme));
        resourceTimer_->setTime(now(CLOCK_MONOTONIC)+20000);resourceTimer_->setOneShot();
        resource.pending = std::async(std::launch::async, [path, optional, scheme, sources, statisticalModel, profile, customProfile] {
            const auto began=now(CLOCK_MONOTONIC);
            EnginePtr engine(scheme ? cowtype2_engine_new(path.c_str(), optional.c_str(), 1) : cowtype2_engine_external_new(), cowtype2_engine_free);
            std::string error = engine ? "" : cowtype2_last_error();
            ctDiagnostic("event=dictionary_stage scheme="+std::to_string(scheme)+" stage=rust elapsed_ms="+std::to_string((now(CLOCK_MONOTONIC)-began)/1000));
            const auto decoderBegan=now(CLOCK_MONOTONIC);
            if(engine && !scheme) {
                try {
                    auto decoder=std::make_unique<LibimeDecoder>(sources,statisticalModel,profile,customProfile);
                    if(!cowtype2_engine_shuangpin_semicolon(engine.get(),decoder->shuangpinSemicolon())) throw std::runtime_error("Decoder input configuration failed");
                    if(!cowtype2_engine_set_decoder(engine.get(),decoder.get(),LibimeDecoder::query,LibimeDecoder::destroy)) throw std::runtime_error("Decoder attachment failed");
                    if (decoder->warnings) {
                        error = ct_tr("warning.dictionary_optional");
                        ctDiagnostic("event=dictionary_optional_unavailable flags="+std::to_string(decoder->warnings));
                    }
                    ctDiagnostic("event=dictionary_cache hit="+std::to_string(decoder->cacheHit)+" model="+std::to_string(decoder->modelLoaded));
                    decoder.release();
                } catch(const std::exception &e) { error=e.what(); engine.reset(); }
            }
            ctDiagnostic("event=dictionary_built scheme="+std::to_string(scheme)+" ready="+std::to_string(bool(engine))+" libime_ms="+std::to_string((now(CLOCK_MONOTONIC)-decoderBegan)/1000)+" total_ms="+std::to_string((now(CLOCK_MONOTONIC)-began)/1000));
            return std::make_pair(std::move(engine), error);
        });
    }
    bool finishResource(int scheme) {
        auto &resource = resources_[scheme];
        if (resource.pending.valid() && resource.pending.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            const auto began=now(CLOCK_MONOTONIC);
            auto result = resource.pending.get();
            if (resource.reload) {
                // This result belongs to an earlier configuration generation.
                resource.reload = false; start(scheme);
            } else {
                resource.failure = std::move(result.second);
                resource.engine = std::move(result.first); ++resource.generation;
            }
            ctDiagnostic("event=dictionary_attached scheme="+std::to_string(scheme)+" elapsed_ms="+std::to_string((now(CLOCK_MONOTONIC)-began)/1000));
            return true;
        }
        return false;
    }
    void attach(InputContext *ic, int scheme) {
        finishResource(scheme);
        auto &resource=resources_[scheme];
        const auto began=now(CLOCK_MONOTONIC);
        auto *state = ic->propertyFor(&factory_);
        if (state->scheme != scheme) {
            state->session.reset(cowtype2_session_new(resource.engine.get())); state->scheme = scheme; state->generation = resource.generation; state->configGeneration=0;
        } else if (state->generation != resource.generation) {
            cowtype2_session_set_engine(state->session.get(), resource.engine.get()); state->generation = resource.generation;
        }
        cowtype2_session_pending_shuangpin_semicolon(state->session.get(),scheme==0 && !resource.engine && resource.pendingSemicolon);
        if(state->configGeneration!=configGeneration_){cowtype2_session_configure(state->session.get(),runtime_.get());state->configGeneration=configGeneration_;}
        const auto elapsed=(now(CLOCK_MONOTONIC)-began)/1000;
        if(elapsed>=20) ctDiagnostic("event=slow_session_attach scheme="+std::to_string(scheme)+" elapsed_ms="+std::to_string(elapsed));
    }
    void render(InputContext *ic, bool consumeCommit) {
        auto *state = ic->propertyFor(&factory_);
        if(state->translating && !state->translateDraft){renderTranslation(ic);return;}
        if(state->unicodeCode){renderUnicode(ic);return;}
        if(state->internalCode){renderInternal(ic);return;}
        auto *session = state->session.get();
        if (!session) { return; }
        if (consumeCommit) { const char *commit = cowtype2_session_commit(session); if (commit && *commit) {
            if(state->translating){state->translateInput+=commit;if(state->translateInput.size()>32768)state->translateStatus=ct_tr("translate.error.input");}
            else commitText(ic,commit);
        } }
        auto &panel = ic->inputPanel(); panel.reset();
        Text preedit;
        if (const char *value = cowtype2_session_preedit(session); value && *value) {
            preedit.append(value, TextFormatFlags(TextFormatFlag::Underline) | TextFormatFlag::DontCommit);
        }
        preedit.setCursor(cowtype2_session_cursor(session));
        if(state->translating) {
            auto current=preedit.toString();auto cursor=preedit.cursor();
            preedit=Text(state->translateInput+current,TextFormatFlags(TextFormatFlag::Underline)|TextFormatFlag::DontCommit);
            preedit.setCursor(state->translateInput.size()+cursor);
        }
        if(voice_.owns(ic) && !state->voicePartial.empty()){preedit=Text();preedit.append(state->voicePartial,TextFormatFlags(TextFormatFlag::Underline)|TextFormatFlag::DontCommit);preedit.setCursor(state->voicePartial.size());}
        if (ic->capabilityFlags().test(CapabilityFlag::Preedit)) { panel.setClientPreedit(preedit); }
        else { panel.setPreedit(preedit); }
        if (state->scheme >= 0 && !preedit.empty()) {
            auto &resource = resources_[state->scheme];
            if (!resource.engine) { panel.setAuxUp(Text(resource.failure.empty() ? ct_tr("message.c28e670cc9d0") : ct_tr("message.b016e0472c97"))); }
            else if (!resource.failure.empty()) { panel.setAuxUp(Text(resource.failure)); }
            else if (const char *warning = cowtype2_engine_warning(resource.engine.get()); warning && *warning) { panel.setAuxUp(Text(warning)); }
        }
        const auto scriptMode=cowtype2_runtime_option(runtime_.get(),27);
        const std::string trigger=cowtype2_session_preedit(session);
        if(!scriptMode || state->translating || cowtype2_session_mode(session) || ic->capabilityFlags().testAny(CapabilityFlag::PasswordOrSensitive)) {
            state->scriptTrigger.clear();state->scriptCandidates.clear();state->scriptFailed=false;
            if(state->scriptMerged) {cowtype2_session_scripts(session,"",nullptr,nullptr,0);state->scriptMerged=false;}
        } else if(state->scriptTrigger!=trigger) {
            state->scriptTrigger=trigger;state->scriptCursor=0;state->scriptFailed=false;state->scriptDismissed=false;state->scriptCandidates.clear();
            std::vector<std::string> merged;std::vector<double> priorities;
            if(scripts_.matches(trigger)) {
                for(const auto &candidate:scripts_.query(instance_->addonManager().addon("imeapi",true),ic,trigger,scriptMode,state->scriptFailed,&state->scriptReason)) {
                    if(candidate.independent) state->scriptCandidates.push_back(candidate.text);
                    else {merged.push_back(candidate.text);priorities.push_back(candidate.priority);}
                }
            }
            if(!merged.empty() || state->scriptMerged) {
                std::vector<const char *> texts;for(const auto &text:merged)texts.push_back(text.c_str());
                cowtype2_session_scripts(session,trigger.c_str(),texts.data(),priorities.data(),texts.size());
                state->scriptMerged=!merged.empty();
            }
        }
        if(!state->scriptCandidates.empty() && !state->scriptDismissed) {
            const auto pageSize=cowtype2_runtime_option(runtime_.get(),0);
            auto list=std::make_unique<Page>(this,ic,state->scriptCursor>=pageSize,(state->scriptCursor/pageSize+1)*pageSize<state->scriptCandidates.size());
            list->setPageSize(pageSize);list->setLayoutHint(cowtype2_runtime_option(runtime_.get(),1)?CandidateLayoutHint::Horizontal:CandidateLayoutHint::Vertical);
            std::vector<Key> keys;for(unsigned n=0;n<pageSize;++n)keys.emplace_back(static_cast<KeySym>(FcitxKey_1+n));list->setSelectionKey(keys);
            const auto start=state->scriptCursor/pageSize*pageSize;
            for(size_t i=start;i<std::min(state->scriptCandidates.size(),start+pageSize);++i)list->append<InternalWord>(state->scriptCandidates[i],ct_tr("scripts.candidate"),[this,i](InputContext *context){chooseScript(context,i);});
            list->setGlobalCursorIndex(state->scriptCursor-start);panel.setCandidateList(std::move(list));
        } else if (cowtype2_session_count(session)) {
            auto list = std::make_unique<Page>(this, ic, cowtype2_session_has_page(session, false), cowtype2_session_has_page(session, true));
            const auto pageSize=cowtype2_runtime_option(runtime_.get(),0);
            list->setPageSize(pageSize); list->setLayoutHint(cowtype2_runtime_option(runtime_.get(),1)?CandidateLayoutHint::Horizontal:CandidateLayoutHint::Vertical);
            std::vector<Key> selectionKeys;for(unsigned n=0;n<pageSize;++n){selectionKeys.emplace_back(static_cast<KeySym>(FcitxKey_1+n));}list->setSelectionKey(selectionKeys);
            for (size_t i = 0; i < cowtype2_session_count(session); ++i) {
                list->append<Word>(cowtype2_session_word(session, i), (state->scheme==0 && !cowtype2_runtime_option(runtime_.get(),7))?"":cowtype2_session_reading(session, i), this, i);
            }
            list->setGlobalCursorIndex(cowtype2_session_candidate_cursor(session)); panel.setCandidateList(std::move(list));
        }
        if(state->translating){panel.setAuxDown(Text(state->translateStatus.empty()?ct_tr(preedit.empty()?"translate.input_hint":"translate.send_hint"):state->translateStatus));}
        else if(state->scriptFailed){
            const auto tab=state->scriptReason.find('\t');
            std::string reason=ct_tr("scripts.reason."+state->scriptReason.substr(0,tab));
            if(tab!=std::string::npos)reason+=" ("+std::string(ct_tr("scripts.line"))+" "+state->scriptReason.substr(tab+1)+")";
            std::string message=ct_tr("scripts.failed");const auto placeholder=message.find("{reason}");
            if(placeholder!=std::string::npos)message.replace(placeholder,8,reason);
            panel.setAuxDown(Text(message));
        }
        else if(!state->scriptCandidates.empty()){panel.setAuxDown(Text(ct_tr(state->scriptDismissed?"scripts.show_scripts":"scripts.show_words")));}
        else if(!state->voiceStatus.empty()){panel.setAuxDown(Text(state->voiceStatus));}
        else if(const char *warning=cowtype2_runtime_warning(runtime_.get());warning && *warning){panel.setAuxDown(Text(warning));}
        ic->updatePreedit(); ic->updateUserInterface(UserInterfaceComponent::InputPanel);
    }
    uint64_t initializedAt_=[]{const auto began=now(CLOCK_MONOTONIC);ctDiagnostic("event=addon_start version=" COWTYPE_VERSION);return began;}();
    std::unique_ptr<Cowtype2Runtime,decltype(&cowtype2_runtime_free)> runtime_{cowtype2_runtime_new(),cowtype2_runtime_free};
    SettingsLauncher settingsLauncher_;
    unsigned configGeneration_=0;
    Instance *instance_;
    FactoryFor<State> factory_;
    VoiceWorker voice_;
    TranslateWorker translator_;
    std::array<Resource, 2> resources_;
    std::unique_ptr<HandlerTableEntry<EventHandler>> focus_;
    std::unique_ptr<EventSourceTime> resourceTimer_;
    std::unique_ptr<cowtype::InputBus> inputBus_;
    cowtype::TraditionalConverter traditional_;
    cowtype::Scripts scripts_;
    ScopedConnection traditionalOutput_,traditionalCommit_;
};
void Word::select(InputContext *ic) const { owner_->choose(ic, index_); }
void Page::prev() { if (previous_) { owner_->page(ic_, false); } }
void Page::next() { if (next_) { owner_->page(ic_, true); } }
class Factory final : public AddonFactory {
    AddonInstance *create(AddonManager *manager) override { return new Cowtype(manager->instance()); }
};
} // namespace
COWTYPE_ADDON_FACTORY(Factory)
