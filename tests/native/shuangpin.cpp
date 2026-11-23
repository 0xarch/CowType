// SPDX-License-Identifier: GPL-3.0-or-later
#include "libime.h"
#include <cassert>
#include <iostream>
#include <cstdlib>
#include <unistd.h>

struct Row { std::string text,reading; size_t consumed; unsigned kind; };
void collect(void *p,const char *text,const char *reading,size_t consumed,uint32_t kind) {
    static_cast<std::vector<Row> *>(p)->push_back({text,reading,consumed,kind});
}
std::vector<Row> query(LibimeDecoder &decoder,const std::string &raw) {
    std::vector<Row> rows;
    decoder.queryImpl(raw,1|2|4|8|16|(5u<<8),collect,&rows);
    return rows;
}
int main(int argc,char **argv) {
    assert(argc==2);
    char temporary[]="/tmp/cowtype-shuangpin-XXXXXX";
    assert(mkdtemp(temporary));
    const std::filesystem::path root=temporary;
    setenv("XDG_CONFIG_HOME",temporary,1);setenv("XDG_DATA_HOME",temporary,1);
    setenv("XDG_CACHE_HOME",temporary,1);
    std::filesystem::create_directories(root/"cowtype");
    const std::array<std::string,8> names={"Ziranma","MS","Ziguang","ABC","Zhongwenzhixing","PinyinJiajia","Xiaohe","GB"};
    // Expected codes are fixed independently from the dictionary under test.
    const std::array<std::string,8> hello={"nihk","nihk","nihq","nihk","nihd","nihd","nihc","nihc"};
    const std::array<std::string,8> zero={"an","an","or","oj","an","an","an","an"};
    for(unsigned profile=0;profile<names.size();++profile) {
        LibimeDecoder decoder({argv[1],"","","",""},false,profile);
        auto rows=query(decoder,hello[profile]);
        assert(std::any_of(rows.begin(),rows.end(),[](const auto &c){return c.text=="你好" && c.reading=="ni hao" && c.consumed==4 && c.kind==0;}));
        assert(!query(decoder,hello[profile].substr(0,3)).empty());
        rows=query(decoder,zero[profile]);
        assert(std::any_of(rows.begin(),rows.end(),[](const auto &c){return c.text=="安" && c.consumed==2;}));
        {std::ofstream config(root/"cowtype/settings-v2.ini");config<<"[CowType]\nVersion=2\nShuangpin=True\nShuangpinProfile="<<names[profile]<<"\nLearning="<<(profile==0?"True":"False")<<"\n";}
        auto *engine=cowtype2_engine_external_new();
        auto native=std::make_unique<LibimeDecoder>(cowtype::PinyinSources{argv[1],"","","",""},false,profile);
        assert(cowtype2_engine_shuangpin_semicolon(engine,native->shuangpinSemicolon()));
        assert(cowtype2_engine_set_decoder(engine,native.get(),LibimeDecoder::query,LibimeDecoder::destroy));native.release();
        auto *session=cowtype2_session_new(engine);auto *runtime=cowtype2_runtime_new();
        cowtype2_session_configure(session,runtime);
        for(char c:hello[profile])assert(cowtype2_session_key(session,c,0,false,UINT32_MAX));
        assert(std::string(cowtype2_session_word(session,0))=="你好");
        bool partial=false;
        do {
            for(size_t i=0;i<cowtype2_session_count(session);++i) {
                if(std::string(cowtype2_session_word(session,i))=="你") {
                    assert(cowtype2_session_select(session,i));
                    const std::string preedit=cowtype2_session_preedit(session);
                    assert(preedit=="你"+hello[profile].substr(1) || preedit=="你"+hello[profile].substr(2));
                    assert(cowtype2_session_key(session,CT2_BACKSPACE,0,false,UINT32_MAX));
                    assert(std::string(cowtype2_session_preedit(session))==hello[profile]);
                    partial=true;break;
                }
            }
            if(partial || !cowtype2_session_has_page(session,true)) break;
            cowtype2_session_key(session,CT2_PAGE_DOWN,0,false,UINT32_MAX);
        } while(true);
        assert(partial);
        while(cowtype2_session_has_page(session,false)) cowtype2_session_key(session,CT2_PAGE_UP,0,false,UINT32_MAX);
        assert(cowtype2_session_key(session,CT2_SPACE,0,false,UINT32_MAX));
        assert(std::string(cowtype2_session_commit(session))=="你好");
        cowtype2_session_free(session);cowtype2_runtime_free(runtime);cowtype2_engine_free(engine);
        if(profile==0) {
            std::ifstream memory(root/"cowtype/learning-v1.tsv");
            const std::string text((std::istreambuf_iterator<char>(memory)),{});
            assert(text.find("ni hao\t你好\t")!=std::string::npos);
            assert(text.find("nihk")==std::string::npos);
        }
    }
    // A semicolon final belongs to double pinyin input instead of punctuation.
    LibimeDecoder ms({argv[1],"","","",""},false,1);
    auto rows=query(ms,"l;");
    assert(std::any_of(rows.begin(),rows.end(),[](const auto &c){return c.reading=="ling" && c.consumed==2;}));
    const auto custom=root/"manual.conf";
    {std::ofstream file(custom);file<<"[方案]\n方案名称=CowType test\n[零声母标识]\n=o\n[声母]\nsh=u\n[韵母]\nao=k\nan=j\n[音节]\nni=n;\nhao=h;\nan=oa\n";}
    LibimeDecoder decoder({argv[1],"","","",""},false,8,custom);
    rows=query(decoder,"n;h;");
    assert(std::any_of(rows.begin(),rows.end(),[](const auto &c){return c.text=="你好" && c.consumed==4;}));
    bool rejected=false;try{LibimeDecoder invalid({argv[1],"","","",""},false,8,"relative.conf");}catch(const std::exception &){rejected=true;}assert(rejected);
    for(const auto &text:{"", "garbage", "[方案]\n方案名称=empty\n", "notpinyin=x\n", "ni=11\n"}) {
        {std::ofstream file(custom);file<<text;}
        bool invalid=false;try{LibimeDecoder bad({argv[1],"","","",""},false,8,custom);}catch(const std::exception &){invalid=true;}
        assert(invalid);
    }
#if COWTYPE_SHUANGPIN_GB
    // The compatibility data must be identical to the library builtin without correction expansion.
    std::istringstream legacy(cowtype::gbShuangpinProfile);
    libime::ShuangpinProfile oldProfile(legacy),newProfile(static_cast<libime::ShuangpinBuiltinProfile>(7));
    assert(oldProfile.table()==newProfile.table());
#endif
    std::filesystem::remove_all(root);
    std::cout<<"Eight double pinyin schemes, zero initials, partial input, custom mappings and native sessions passed\n";
}
