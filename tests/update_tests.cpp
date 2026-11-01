#include "core/updates.h"
#include "version.h"
#define JSON_HAS_CPP_17
#include <nlohmann/json.hpp>
#include <iostream>
#include <stdexcept>
void require(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(int argc,char** argv){try{
 require(ac::newer_version("0.10.0","0.9.9"),"numeric version ordering");require(!ac::newer_version("v0.2.0","0.2.0"),"same version is current");require(!ac::newer_version("0.1.9","0.2.0"),"downgrade not offered");
 nlohmann::json j={{"draft",false},{"prerelease",false},{"tag_name","v0.3.0"},{"assets",nlohmann::json::array({{{"name","AirConnect-0.3.0-Setup.exe"},{"browser_download_url","https://github.com/" AC_GITHUB_REPOSITORY "/releases/download/v0.3.0/AirConnect-0.3.0-Setup.exe"},{"digest","sha256:"+std::string(64,'a')},{"size",2048}}})}};
 require(ac::parse_release(j.dump()).version=="0.3.0","valid installer metadata");
 auto rejected=[&](nlohmann::json bad){bool error=false;try{ac::parse_release(bad.dump());}catch(...){error=true;}require(error,"unsafe release rejected");};
 auto bad=j;bad["assets"][0]["digest"]=nullptr;rejected(bad);bad=j;bad["assets"][0]["browser_download_url"]="https://example.org/installer.exe";rejected(bad);bad=j;bad["prerelease"]=true;rejected(bad);bad=j;bad["assets"][0]["size"]=200*1024*1024;rejected(bad);bad=j;bad["tag_name"]="v0.3.0/../../escape";rejected(bad);bad=j;bad["assets"].push_back(bad["assets"][0]);rejected(bad);
 std::cout<<"Update version, stable-release policy, source URL, hash, size and duplicate checks passed\n";
 if(argc==3&&std::string(argv[1])=="--live-download"){auto release=ac::latest_release();require(!ac::newer_version(release.version,release.version),"published version comparison");std::atomic<bool> stop{false};ac::download_release(release,std::filesystem::path(argv[2]),stop);std::cout<<"Published GitHub release "<<release.version<<" downloaded and SHA-256 verified\n";}
 return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
