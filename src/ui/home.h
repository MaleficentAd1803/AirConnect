#pragma once
#include "core/receiver.h"
#include <windows.h>
namespace ac {
class Home {
public:
 Home(Receiver&,Preferences);~Home();int run(HINSTANCE,bool background=false,int smoke_seconds=0);
 void show_settings();
private:struct Impl;std::unique_ptr<Impl> p;
};
}
