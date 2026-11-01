#include "log.h"
#include <atomic>
#include <mutex>
#include <cstdio>
#include <windows.h>
namespace ap {
namespace {std::atomic<bool> verbose{false};std::mutex log_mtx;}
void set_log_enabled(bool b){verbose=b;}
bool is_log_enabled(){return verbose;}
void log_write(LogLevel level,const std::string& text){
 if(level==LogLevel::Debug||(!verbose&&level!=LogLevel::Error&&level!=LogLevel::Warn))return;
 // Do not persist packet/key material even when verbose diagnostics are selected.
 if(text.find("secret")!=std::string::npos||text.find("AES")!=std::string::npos||text.find("ekey")!=std::string::npos||text.find("seed")!=std::string::npos||text.find("signature")!=std::string::npos)return;
 std::lock_guard lock(log_mtx);SYSTEMTIME t{};GetLocalTime(&t);
 std::string safe=text.substr(0,2048);for(char& c:safe)if((unsigned char)c<32)c=' ';
 static size_t bytes=0;if(bytes>8*1024*1024)return;bytes+=safe.size()+40;
 if(bytes>8*1024*1024)safe="Diagnostic log reached its 8 MiB session limit; further entries are suppressed.";
 std::fprintf(stderr,"%04u-%02u-%02u %02u:%02u:%02u [%s] %s\n",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,level==LogLevel::Error?"error":level==LogLevel::Warn?"warning":"info",safe.c_str());std::fflush(stderr);
}
}
