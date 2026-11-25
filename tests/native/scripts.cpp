// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../src-fcitx5/engine/scripts.h"
#include "isolation.h"
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputpanel.h>
#include <fcitx/instance.h>
#include <fcitx/inputmethodengine.h>
#include <fcitx/addonfactory.h>
#include <dlfcn.h>
#include <memory>
#include <cassert>
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
using namespace fcitx;
struct Field : InputContext {
    explicit Field(InputContextManager &manager) : InputContext(manager,"cowtype-script-test") {created();}
    ~Field() override {destroy();}
    const char *frontend() const override {return "cowtype-synthetic";}
    std::vector<std::string> committed;
    void commitStringImpl(const std::string &text) override {committed.push_back(text);}
    void deleteSurroundingTextImpl(int,unsigned) override {}
    void forwardKeyImpl(const ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}
};
int main(int argc,char **argv) {
    assert(argc==2);
    assert(isolatedPaths());
    char name[]="cowtype-lua-test",disabled[]="--disable=all",enabled[]="--enable=imeapi,luaaddonloader";
    char *args[]={name,disabled,enabled,nullptr};Instance instance(3,args);
    instance.addonManager().registerDefaultLoader(nullptr);instance.initialize();
    auto *addon=instance.addonManager().addon("imeapi",true);assert(addon);
    Field field(instance.inputContextManager());
    auto invoke=[&](unsigned mode,const std::string &source) {
        RawConfig arg;arg.setValue(std::to_string(mode)+"\n1\nriqi\n"+source);
        return addon->callWithSignature<cowtype::LuaCall>("LuaAddon::invokeLuaFunction",&field,std::string("cowtype_script"),arg);
    };
    auto result=invoke(1,"return {input, os.date('%Y-%m-%d')}");
    assert(result.valueByPath("Status") && *result.valueByPath("Status")=="ok");
    assert(*result.valueByPath("0/Text")=="riqi");assert(result.valueByPath("1/Text")->size()==10);
    for(const auto &code:{"return io.open('x')", "return os.execute('true')", "return require('io')", "return debug.getregistry()", "return _G", "return load('return 1')()", "while true do end", "return {42}", "return string.rep('a',4097)", "error('private text')"}) {
        result=invoke(1,code);
        assert(result.valueByPath("Status") && *result.valueByPath("Status")=="error");
        assert(!result.valueByPath("0/Text"));
    }
    result=invoke(2,"local ok = os.execute('true'); return {ok and 'ok' or 'failed'}");
    assert(result.valueByPath("0/Text") && *result.valueByPath("0/Text")=="ok");
    result=invoke(0,"error('must not execute')");assert(*result.valueByPath("Status")=="error");
    auto mergedInvoke=[&](const std::string &source) {
        RawConfig arg;arg.setValue("1\n0\nshan\n"+source);
        return addon->callWithSignature<cowtype::LuaCall>("LuaAddon::invokeLuaFunction",&field,std::string("cowtype_script"),arg);
    };
    result=mergedInvoke("return {{5, '日期'}, {6, '时间'}}");
    assert(*result.valueByPath("0/Text")=="日期" && *result.valueByPath("0/Priority")=="5");
    for(const auto &code:{"return 'bad'", "return {'bad'}", "return {0/0, 'bad'}", "return {1/0, 'bad'}", "return {5, 42}"}) {
        result=mergedInvoke(code);assert(*result.valueByPath("Status")=="error");
    }
    for(const auto &[code,reason]:std::vector<std::pair<std::string,std::string>>{
        {"return {", "syntax"}, {"while true do end", "limit"},
        {"return io.open('unused')", "permission"}, {"error('secret text')", "runtime"},
        {"return 'text'", "rank_required"}, {"return {5, 42}", "text_type"},
        {"return {1/0,'x'}", "priority"}, {"return {5,string.rep('x',4097)}", "text_size"}}) {
        auto error=mergedInvoke(code);
        assert(error.valueByPath("Reason") && *error.valueByPath("Reason")==reason);
        assert(!error.valueByPath("0/Text"));
    }
    const auto root=std::filesystem::path(std::getenv("XDG_CONFIG_HOME"))/"cowtype/scripts";
    std::filesystem::create_directories(root);
    {std::ofstream out(root/"index.tsv");out<<"# cowtype-scripts-v2\nexample\triqi\t1\n";}
    {std::ofstream out(root/"example.lua");out<<"return {input, '测试'}";}
    cowtype::Scripts scripts;scripts.reload();bool failed=false;
    assert(scripts.matches("riqi"));
    const auto candidates=scripts.query(addon,&field,"riqi",1,failed);
    assert(!failed && candidates.size()==2 && candidates[0].text=="riqi" && candidates[1].text=="测试" && candidates[0].independent);
    assert(scripts.query(addon,&field,"riqi",0,failed).empty());
    assert(scripts.query(nullptr,&field,"riqi",1,failed).empty() && failed);
    std::filesystem::remove(root/"example.lua");
    std::filesystem::create_symlink(root/"index.tsv",root/"example.lua");scripts.reload();
    assert(!scripts.matches("riqi"));
    std::filesystem::rename(root/"index.tsv",root/"saved.tsv");
    std::filesystem::create_symlink(root/"saved.tsv",root/"index.tsv");scripts.reload();
    assert(!scripts.matches("riqi"));
    std::filesystem::remove(root/"index.tsv");std::filesystem::rename(root/"saved.tsv",root/"index.tsv");
    std::filesystem::remove(root/"example.lua");
    {std::ofstream out(root/"example.lua");out<<"return {'日期一', '日期二'}";}
    {std::ofstream out(root.parent_path()/"settings-v2.ini");out<<"[CowType]\nVersion=2\nScriptMode=Strict\n";}
    void *module=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);assert(module);
    auto factory=reinterpret_cast<AddonFactory *(*)()>(dlsym(module,cowtype::compat::factorySymbol));assert(factory);
    {
        std::unique_ptr<AddonInstance> owner(factory()->create(&instance.addonManager()));
        auto *engine=dynamic_cast<InputMethodEngine *>(owner.get());assert(engine);
        Field input(instance.inputContextManager());input.setEnablePreedit(true);input.setCapabilityFlags(CapabilityFlag::Preedit);input.focusIn();
        InputMethodEntry entry("cowtype","CowType","zh_CN","cowtype");
        InputContextEvent activated(&input,EventType::InputContextInputMethodActivated);engine->activate(entry,activated);
        auto press=[&](KeySym sym) {KeyEvent key(&input,Key(sym),false);engine->keyEvent(entry,key);return key.accepted();};
        auto type=[&] {for(char c:std::string("riqi"))assert(press(static_cast<KeySym>(c)));};
        type();
        assert(input.inputPanel().candidateList() && input.inputPanel().candidateList()->candidate(0).text().toString()=="日期一");
        assert(press(FcitxKey_2));assert(input.committed.back()=="日期二");
        type();assert(press(FcitxKey_Down));assert(press(FcitxKey_space));assert(input.committed.back()=="日期二");
        type();assert(press(FcitxKey_Tab));
        assert(!input.inputPanel().candidateList() || input.inputPanel().candidateList()->candidate(0).text().toString()!="日期一");
        assert(press(FcitxKey_Tab));assert(input.inputPanel().candidateList()->candidate(0).text().toString()=="日期一");
        assert(press(FcitxKey_Escape));
        type();assert(press(FcitxKey_Escape));assert(input.committed.size()==2);
        type();input.inputPanel().candidateList()->candidate(0).select(&input);assert(input.committed.back()=="日期一");
        {std::ofstream out(root/"example.lua");out<<"return {'one','two','three','four','five','six','seven','eight','nine','ten','eleven','twelve'}";}
        engine->reloadConfig();type();
        input.inputPanel().candidateList()->toPageable()->next();
        assert(input.inputPanel().candidateList()->candidate(0).text().toString()=="six");
        assert(press(FcitxKey_1));assert(input.committed.back()=="six");
        type();assert(press(FcitxKey_bracketright));assert(press(FcitxKey_1));assert(input.committed.back()=="six");
        {std::ofstream out(root/"index.tsv");out<<"# cowtype-scripts-v2\nexample\tshan\t0\n";}
        {std::ofstream out(root/"example.lua");out<<"return {{5, '日期五'}, {6, '时间六'}}";}
        engine->reloadConfig();
        for(char c:std::string("shan"))assert(press(static_cast<KeySym>(c)));
        // Finish loading before comparing script ranks against ordinary candidates.
        for(unsigned i=0;i<100;++i) {
            const auto candidates=input.inputPanel().candidateList();
            if(candidates && candidates->size()>2) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));press(FcitxKey_F35);
        }
        auto merged=input.inputPanel().candidateList();assert(merged && merged->size()>=5);
        assert(merged->candidate(0).text().toString()!="日期五");
        assert(merged->candidate(4).text().toString()=="日期五");
        assert(press(FcitxKey_5));assert(input.committed.back()=="日期五");
        for(char c:std::string("shan"))assert(press(static_cast<KeySym>(c)));
        assert(!press(FcitxKey_Tab)); // Merged scripts do not claim Tab.
        assert(press(FcitxKey_Escape));
        {std::ofstream out(root.parent_path()/"settings-v2.ini");out<<"[CowType]\nVersion=2\nScriptMode=Off\n";}
        engine->reloadConfig();type();
        const auto list=input.inputPanel().candidateList();
        assert(!list || list->candidate(0).text().toString()!="日期一");
    }
    dlclose(module);
    std::cout<<"Real Fcitx Lua bridge: strict/full/off, candidates, errors and instruction limit verified\n";
}
