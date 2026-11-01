#include "platform/capture.h"
#include <vector>
namespace ac {
bool capture_own_window(HWND window,const std::wstring& path){
 RECT r{};GetClientRect(window,&r);if(r.right<=0||r.bottom<=0)return false;
 HDC dc=GetDC(window),memory=CreateCompatibleDC(dc);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=r.right;info.bmiHeader.biHeight=-r.bottom;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
 HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);HGDIOBJ previous=SelectObject(memory,bitmap);BOOL ok=PrintWindow(window,memory,PW_CLIENTONLY|2);GdiFlush();
 if(ok&&pixels){auto* bytes=(uint8_t*)pixels;for(size_t i=3;i<(size_t)r.right*r.bottom*4;i+=4)bytes[i]=255;ok=save_png(path,r.right,r.bottom,bytes,r.right*4);}
 SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(window,dc);return ok;
}
}
