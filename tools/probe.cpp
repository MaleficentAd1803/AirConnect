#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <cstdio>
#include <string>
int main(){
 MEMORYSTATUSEX m{sizeof(m)}; GlobalMemoryStatusEx(&m);
 printf("RAM total: %llu MiB; available: %llu MiB\n",m.ullTotalPhys/1048576,m.ullAvailPhys/1048576);
 IDXGIFactory* f=nullptr;
 if(SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory),(void**)&f))){
  for(UINT i=0;;++i){IDXGIAdapter* a=nullptr;if(f->EnumAdapters(i,&a)==DXGI_ERROR_NOT_FOUND)break; DXGI_ADAPTER_DESC d{};a->GetDesc(&d);printf("GPU: %ls; dedicated %llu MiB\n",d.Description,(unsigned long long)d.DedicatedVideoMemory/1048576); a->Release();}f->Release();
 }
 ID3D11Device* dev=nullptr;ID3D11DeviceContext* ctx=nullptr;D3D_FEATURE_LEVEL level{};
 HRESULT hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&dev,&level,&ctx);
 printf("D3D11: 0x%08lx feature level 0x%x\n",(unsigned long)hr,level);
 if(dev){ID3D11VideoDevice* vd=nullptr;if(SUCCEEDED(dev->QueryInterface(__uuidof(ID3D11VideoDevice),(void**)&vd))){printf("Video decoder profiles: %u\n",vd->GetVideoDecoderProfileCount());vd->Release();}ctx->Release();dev->Release();}
 SetDllDirectoryW(L"C:\\Program Files\\obs-studio\\bin\\64bit");
 auto h=LoadLibraryW(L"C:\\Program Files\\obs-studio\\bin\\64bit\\avutil-59.dll");
 if(h){auto v=(const char*(*)())GetProcAddress(h,"av_version_info");printf("FFmpeg: %s\n",v?v():"unknown");FreeLibrary(h);}else printf("FFmpeg load error %lu\n",GetLastError());
 return 0;
}
