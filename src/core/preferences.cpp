#include "core/preferences.h"
#include "airplay/receiver_name.h"
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <chrono>
#include <mutex>
#include <algorithm>

namespace ac {
namespace { std::filesystem::path override_dir; std::mutex output_mutex; std::wstring selected_output; }
std::atomic<float> playback_volume{1.f};
std::atomic<bool> playback_muted{false};
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);if(!n)return {};std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),w.data(),n);return w;}
std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string v(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),v.data(),n,nullptr,nullptr);return v;}
int64_t now_ns(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
void set_data_directory(const std::filesystem::path& p){override_dir=p;}
std::filesystem::path data_directory(){if(!override_dir.empty())return override_dir;PWSTR p=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&p))){std::filesystem::path out=std::filesystem::path(p)/L"AirConnect";CoTaskMemFree(p);return out;}return {};}
std::wstring default_recordings(){PWSTR p=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos,0,nullptr,&p))){auto out=(std::filesystem::path(p)/L"AirConnect").wstring();CoTaskMemFree(p);return out;}return (data_directory()/L"Recordings").wstring();}
Preferences load_preferences(const std::filesystem::path& path){
 Preferences p;p.folder=default_recordings();std::ifstream f(path);std::string line;
 while(std::getline(f,line)){if(line.size()>4096)continue;auto e=line.find('=');if(e==std::string::npos)continue;auto k=line.substr(0,e),v=line.substr(e+1);
  try{if(k=="name"){auto n=ap::airplay::normalize_receiver_name(v);if(!n.empty())p.name=n;}else if(k=="folder")p.folder=wide(v);else if(k=="output")p.output_device=wide(v);else {int n=std::stoi(v);if(k=="theme")p.theme=std::clamp(n,0,2);else if(k=="resolution"&&(n==1280||n==1920||n==2560||n==3840))p.resolution=n;else if(k=="fps"&&(n==30||n==60))p.fps=n;else if(k=="bitrate")p.bitrate=std::clamp(n,2,40);else if(k=="codec")p.codec=std::clamp(n,0,1);else if(k=="encoder")p.encoder=std::clamp(n,0,2);else if(k=="hardware")p.hardware=n!=0;else if(k=="hevc")p.hevc=n!=0;else if(k=="vsync")p.vsync=n!=0;else if(k=="close_to_tray")p.close_to_tray=n!=0;else if(k=="stats")p.stats=n!=0;}}
  catch(...){}
 }return p;
}
bool save_preferences(const Preferences& p,const std::filesystem::path& path){
 if(path.empty()||p.name.empty()||p.name.find_first_of("\r\n=")!=std::string::npos)return false;
 std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return false;
 auto tmp=path;tmp+=L".tmp";std::ofstream f(tmp,std::ios::trunc);if(!f)return false;
 f<<"name="<<p.name<<"\ntheme="<<p.theme<<"\nresolution="<<p.resolution<<"\nfps="<<p.fps<<"\nbitrate="<<p.bitrate<<"\ncodec="<<p.codec<<"\nencoder="<<p.encoder<<"\nhardware="<<p.hardware<<"\nhevc="<<p.hevc<<"\nvsync="<<p.vsync<<"\nclose_to_tray="<<p.close_to_tray<<"\nstats="<<p.stats<<"\nfolder="<<utf8(p.folder)<<"\noutput="<<utf8(p.output_device)<<"\n";f.flush();bool ok=f.good();f.close();return ok&&MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
void apply_preferences(const Preferences& p,ap::airplay::LiveSettings& s){s.set_receiver_name(p.name);s.mirror_width=p.resolution;s.mirror_height=p.resolution;s.max_fps=p.fps;s.refresh_rate=p.fps;s.mirror_hwaccel=p.hardware;s.hevc_enabled=p.hevc;s.vsync_enabled=p.vsync;set_output_device(p.output_device);}
void set_output_device(const std::wstring& id){std::lock_guard lock(output_mutex);selected_output=id;}
std::wstring output_device(){std::lock_guard lock(output_mutex);return selected_output;}
bool is_dark(int theme){if(theme)return theme==1;DWORD val=1,size=sizeof(val);RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&val,&size);return val==0;}
bool startup_enabled(){HKEY k=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,KEY_QUERY_VALUE,&k)!=ERROR_SUCCESS)return false;auto rc=RegQueryValueExW(k,L"AirConnect",nullptr,nullptr,nullptr,nullptr);RegCloseKey(k);return rc==ERROR_SUCCESS;}
bool set_startup(bool on){HKEY k=nullptr;if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr)!=ERROR_SUCCESS)return false;LONG rc;if(on){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);std::wstring command=L"\""+std::wstring(path)+L"\" --background";rc=RegSetValueExW(k,L"AirConnect",0,REG_SZ,(BYTE*)command.c_str(),(DWORD)((command.size()+1)*sizeof(wchar_t)));}else{rc=RegDeleteValueW(k,L"AirConnect");if(rc==ERROR_FILE_NOT_FOUND)rc=ERROR_SUCCESS;}RegCloseKey(k);return rc==ERROR_SUCCESS;}
}
