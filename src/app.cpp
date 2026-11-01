#include "ui/home.h"
#include "core/preferences.h"
#include "log.h"
#include <shellapi.h>
#include <objbase.h>
#include <filesystem>
#include <cstdio>
#include <stdexcept>
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int){
 HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);int argc=0;auto args=CommandLineToArgvW(GetCommandLineW(),&argc);bool background=false,verbose=false;int smoke=0;uint16_t port=7000;
 for(int i=1;i<argc;++i){std::wstring a=args[i];if(a==L"--data-dir"&&i+1<argc)ac::set_data_directory(args[++i]);else if(a==L"--background")background=true;else if(a==L"--log")verbose=true;else if(a==L"--smoke-test"&&i+1<argc)smoke=_wtoi(args[++i]);else if(a==L"--port"&&i+1<argc){int n=_wtoi(args[++i]);if(n>0&&n<65536)port=(uint16_t)n;}}
 LocalFree(args);int result=1;
 HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\AirConnect.Instance.v1");if(GetLastError()==ERROR_ALREADY_EXISTS&&!smoke){HWND window=FindWindowW(L"AirConnect.Home",nullptr);if(window){ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);}if(mutex)CloseHandle(mutex);if(SUCCEEDED(co))CoUninitialize();return 0;}
 try{
  auto dir=ac::data_directory();if(dir.empty())throw std::runtime_error("The application data directory is unavailable.");std::error_code ec;std::filesystem::create_directories(dir,ec);if(ec)throw std::runtime_error("Unable to create the application data directory.");
  FILE* log=nullptr;_wfreopen_s(&log,(dir/L"receiver.log").c_str(),L"w",stderr);ap::set_log_enabled(verbose);
  auto prefs=ac::load_preferences(dir/L"settings.ini");ac::Receiver receiver;receiver.start(prefs,port);ac::Home home(receiver,prefs);result=home.run(instance,background,smoke);receiver.stop();
 }catch(const std::exception&e){MessageBoxW(nullptr,ac::wide(e.what()).c_str(),L"AirConnect",MB_OK|MB_ICONERROR);}
 if(mutex)CloseHandle(mutex);if(SUCCEEDED(co))CoUninitialize();return result;
}
