// SPDX-License-Identifier: GPL-3.0-or-later
#include <glib.h>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#include <unistd.h>
static std::string capture(const std::vector<std::string> &args){std::vector<char*> argv;for(const auto &s:args)argv.push_back(const_cast<char*>(s.c_str()));argv.push_back(nullptr);char *out=nullptr;int status=0;GError *e=nullptr;bool ok=g_spawn_sync(nullptr,argv.data(),nullptr,G_SPAWN_DEFAULT,nullptr,nullptr,&out,nullptr,&status,&e);std::string text=out?out:"";g_free(out);g_clear_error(&e);return ok&&status==0?text:"";}
int main(){
    namespace fs=std::filesystem;fs::path marker=fs::path(g_get_user_config_dir())/"mnos/wallpaper-set";if(fs::exists(marker))return 0;
    for(int retry=0;retry<15;++retry){std::istringstream lines(capture({"/usr/bin/xrandr","--query"}));std::string line;std::set<std::string> monitors;std::regex active(R"(^([^ /]+) connected( primary)? [0-9]+x[0-9]+.*)");std::smatch match;
        while(std::getline(lines,line))if(std::regex_match(line,match,active))monitors.insert(match[1]);
        if(!monitors.empty()){
            bool ok=true;
            for(const auto &monitor:monitors)for(int workspace=0;workspace<2;++workspace){std::string prop="/backdrop/screen0/monitor"+monitor+"/workspace"+std::to_string(workspace)+"/last-image";
                std::vector<std::string> args={"/usr/bin/xfconf-query","-c","xfce4-desktop","-p",prop,"--create","--type","string","--set","/usr/share/backgrounds/mnos/coast.png"};
                std::vector<char*> av;for(auto &s:args)av.push_back(s.data());av.push_back(nullptr);int status=0;GError *e=nullptr;ok=g_spawn_sync(nullptr,av.data(),nullptr,G_SPAWN_DEFAULT,nullptr,nullptr,nullptr,nullptr,&status,&e)&&status==0&&ok;g_clear_error(&e);
            }
            if(ok){fs::create_directories(marker.parent_path());std::ofstream f(marker);return f?0:1;}
        }
        sleep(1);
    }return 1;
}
