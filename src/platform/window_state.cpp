#include "platform/window_state.h"
#include "core/preferences.h"
#include <fstream>
#include <algorithm>
namespace ac {
void save_window_state(HWND w,const wchar_t* name,const RECT* override_rect){
 if(!IsWindow(w)||IsIconic(w))return;RECT r{};bool maximized=IsZoomed(w);
 if(override_rect)r=*override_rect;
 else if(maximized){WINDOWPLACEMENT p{sizeof(p)};if(!GetWindowPlacement(w,&p))return;r=p.rcNormalPosition;MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&m);OffsetRect(&r,m.rcWork.left-m.rcMonitor.left,m.rcWork.top-m.rcMonitor.top);}
 else if(!GetWindowRect(w,&r))return;
 if(r.right<=r.left||r.bottom<=r.top)return;
 auto path=data_directory()/(std::wstring(name)+L"-window.ini"),tmp=path;tmp+=L".tmp";std::error_code ec;std::filesystem::create_directories(path.parent_path(),ec);if(ec)return;
 std::ofstream f(tmp,std::ios::trunc);f<<r.left<<' '<<r.top<<' '<<r.right-r.left<<' '<<r.bottom-r.top<<' '<<GetDpiForWindow(w)<<' '<<maximized<<'\n';f.flush();bool ok=f.good();f.close();if(ok)MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
bool restore_window_state(HWND w,const wchar_t* name,bool allow_maximize){
 std::ifstream f(data_directory()/(std::wstring(name)+L"-window.ini"));int x,y,width,height,dpi,maximized;if(!(f>>x>>y>>width>>height>>dpi>>maximized)||x< -100000||x>100000||y< -100000||y>100000||width<160||height<160||width>20000||height>20000||dpi<48||dpi>768)return false;
 RECT saved{x,y,x+width,y+height};MONITORINFO m{sizeof(m)};GetMonitorInfoW(MonitorFromRect(&saved,MONITOR_DEFAULTTONEAREST),&m);
 // Apply the saved monitor first so GetDpiForWindow reflects the new display.
 SetWindowPos(w,nullptr,m.rcWork.left,m.rcWork.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
 UINT current_dpi=GetDpiForWindow(w);width=std::min(MulDiv(width,current_dpi,dpi),int(m.rcWork.right-m.rcWork.left));height=std::min(MulDiv(height,current_dpi,dpi),int(m.rcWork.bottom-m.rcWork.top));
 x=std::clamp(x,int(m.rcWork.left),int(m.rcWork.right)-width);y=std::clamp(y,int(m.rcWork.top),int(m.rcWork.bottom)-height);
 SetWindowPos(w,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
 if(maximized&&allow_maximize){WINDOWPLACEMENT p{sizeof(p)};GetWindowPlacement(w,&p);p.showCmd=SW_SHOWMAXIMIZED;SetWindowPlacement(w,&p);}return true;
}
}
