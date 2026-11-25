// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <optional>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
#include <charconv>
#include <map>
#include <string>
#include <vector>
#include <fcitx/inputcontext.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addoninstance.h>
#include <fcitx-config/rawconfig.h>
namespace cowtype {
// Public fcitx5-lua invokeLuaFunction ABI; no interpreter linked into CowType.
using LuaCall=fcitx::RawConfig(fcitx::InputContext *,const std::string &,const fcitx::RawConfig &);
struct ScriptCandidate {std::string text;double priority;bool independent;};
class Scripts {
public:
    void reload() {
        records_.clear();
        const auto *xdg=std::getenv("XDG_CONFIG_HOME"), *home=std::getenv("HOME");
        root_=(xdg && *xdg ? std::filesystem::path(xdg) : std::filesystem::path(home?home:"")/".config")/"cowtype/scripts";
        auto index=readFile(root_/"index.tsv",16384);
        if(!index) return;
        std::istringstream manifest(*index);std::string line;size_t count=0;
        if(!std::getline(manifest,line) || line!="# cowtype-scripts-v2") return;
        while(count++<128 && std::getline(manifest,line)) {
            const auto tab=line.find('\t');
            if(tab==std::string::npos) continue;
            const auto last=line.find('\t',tab+1);
            if(last==std::string::npos || (line.substr(last+1)!="0" && line.substr(last+1)!="1")) continue;
            auto id=line.substr(0,tab),trigger=line.substr(tab+1,last-tab-1);
            const bool independent=line.substr(last+1)=="1";
            if(!letters(id) || !letters(trigger)) continue;
            if(auto source=readFile(root_/(id+".lua"),65536)) records_[trigger].push_back({std::move(*source),independent});
        }
    }
    bool matches(const std::string &trigger) const {return records_.count(trigger);}
    bool prefix(const std::string &trigger) const {
        auto found=records_.lower_bound(trigger);
        return found!=records_.end() && found->first.starts_with(trigger);
    }
    std::vector<ScriptCandidate> query(fcitx::AddonInstance *addon,fcitx::InputContext *ic,const std::string &trigger,unsigned mode,bool &failed,std::string *reason=nullptr) const {
        std::vector<ScriptCandidate> result;
        failed=false;
        if(reason)reason->clear();
        auto fail=[&](const std::string &code){failed=true;if(reason && reason->empty())*reason=code;};
        auto found=records_.find(trigger);
        if(!mode || found==records_.end()) return result;
        if(!addon) {fail("backend");return result;}
        for(const auto &[source,independent]:found->second) {
            fcitx::RawConfig arg;arg.setValue(std::to_string(mode)+"\n"+(independent?"1":"0")+"\n"+trigger+"\n"+source);
            try {
                const auto response=addon->callWithSignature<LuaCall>("LuaAddon::invokeLuaFunction",ic,std::string("cowtype_script"),arg);
                const auto *status=response.valueByPath("Status");
                if(!status || *status!="ok") {
                    const auto *code=response.valueByPath("Reason");
                    const std::string allowed="|protocol|syntax|limit|permission|runtime|assertion|undefined|type|rank_required|return_type|priority|text_type|text_size|text_nul|";
                    fail(code && allowed.find("|"+*code+"|")!=std::string::npos?*code:"protocol");
                    if(reason && reason->find("\t")==std::string::npos) {
                        const auto *line=response.valueByPath("Line");
                        if(line && line->size()<=6 && *line!="0" && std::all_of(line->begin(),line->end(),[](char c){return c>='0' && c<='9';})) *reason+="\t"+*line;
                    }
                    continue;
                }
                for(unsigned i=0;i<12;++i) {
                    const auto *text=response.valueByPath(std::to_string(i)+"/Text");
                    if(!text) break;
                    if(text->size()>4096 || text->find('\0')!=std::string::npos || !fcitx::utf8::validate(*text)) {fail("protocol");continue;}
                    const auto *rank=response.valueByPath(std::to_string(i)+"/Priority");
                    if(!rank) {fail("protocol");continue;}
                    double priority=0;const auto parsed=std::from_chars(rank->data(),rank->data()+rank->size(),priority);
                    if(!std::isfinite(priority) || parsed.ec!=std::errc{} || parsed.ptr!=rank->data()+rank->size()) {fail("protocol");continue;}
                    if(!text->empty()) result.push_back({*text,priority,independent});
                }
            } catch(const std::exception &) {fail("protocol");}
        }
        std::stable_sort(result.begin(),result.end(),[](const auto &a,const auto &b){return a.priority<b.priority;});
        std::vector<ScriptCandidate> unique;
        for(auto &candidate:result) {
            if(std::none_of(unique.begin(),unique.end(),[&](const auto &other){return other.text==candidate.text && other.independent==candidate.independent;})) unique.push_back(std::move(candidate));
            if(unique.size()==12) break;
        }
        return unique;
    }
private:
    // Walk from / using descriptors so symlink swaps cannot redirect script reads.
    static std::optional<std::string> readFile(const std::filesystem::path &path,size_t limit) {
        if(!path.is_absolute()) return {};
        int directory=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(directory<0) return {};
        const auto relative=path.relative_path();
        for(auto it=relative.begin();it!=relative.end();++it) {
            if(*it=="..") {close(directory);return {};}
            const bool last=std::next(it)==relative.end();
            int fd=openat(directory,it->c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|(last?O_NONBLOCK:O_DIRECTORY));
            close(directory);
            if(fd<0) return {};
            directory=fd;
        }
        struct stat info{};
        if(fstat(directory,&info) || !S_ISREG(info.st_mode) || info.st_size<0 || size_t(info.st_size)>limit) {close(directory);return {};}
        std::string result(limit+1,'\0');size_t offset=0;
        while(offset<result.size()) {
            const auto bytes=read(directory,result.data()+offset,result.size()-offset);
            if(bytes<0 && errno==EINTR) continue;
            if(bytes<0) {close(directory);return {};}
            if(!bytes) break;
            offset+=size_t(bytes);
        }
        close(directory);
        if(offset>limit) return {};
        result.resize(offset);return result;
    }
    static bool letters(const std::string &text) {return !text.empty() && text.size()<=64 && std::all_of(text.begin(),text.end(),[](char c){return c>='a' && c<='z';});}
    std::filesystem::path root_;
    std::map<std::string,std::vector<std::pair<std::string,bool>>> records_;
};
}
