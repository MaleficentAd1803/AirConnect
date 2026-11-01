#include "video/video_renderer.h"
#include "video/viewer_geometry.h"
#include "video/video_recorder.h"
#include "airplay/live_settings.h"
#include "core/preferences.h"
#include "platform/capture.h"
#include "platform/window_state.h"
#include "log.h"
#include <d3d11.h>
#include <d3d10.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dwmapi.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <commctrl.h>
#include <filesystem>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <map>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <cstdlib>
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
using Microsoft::WRL::ComPtr;
namespace {
constexpr UINT WM_ACTION=WM_APP+20;
enum {Show=1,Fullscreen,Pip,Screenshot,Record,Quit,TestCapture};
std::wstring timestamp(const wchar_t* ext){SYSTEMTIME t{};GetLocalTime(&t);wchar_t n[128];swprintf(n,128,L"AirConnect-%04u%02u%02u-%02u%02u%02u-%03u%ls",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,ext);return n;}
bool png(const std::wstring& path,int w,int h,const uint8_t* pixels,UINT stride){
 ComPtr<IWICImagingFactory> f;ComPtr<IWICStream>s;ComPtr<IWICBitmapEncoder>e;ComPtr<IWICBitmapFrameEncode>b;
 HRESULT hr=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(f.GetAddressOf()));
 if(SUCCEEDED(hr))hr=f->CreateStream(&s);if(SUCCEEDED(hr))hr=s->InitializeFromFilename(path.c_str(),GENERIC_WRITE);
 if(SUCCEEDED(hr))hr=f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&e);if(SUCCEEDED(hr))hr=e->Initialize(s.Get(),WICBitmapEncoderNoCache);
 if(SUCCEEDED(hr))hr=e->CreateNewFrame(&b,nullptr);if(SUCCEEDED(hr))hr=b->Initialize(nullptr);if(SUCCEEDED(hr))hr=b->SetSize(w,h);
 WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;if(SUCCEEDED(hr))hr=b->SetPixelFormat(&format);if(SUCCEEDED(hr))hr=b->WritePixels(h,stride,stride*h,(BYTE*)pixels);if(SUCCEEDED(hr))hr=b->Commit();if(SUCCEEDED(hr))hr=e->Commit();return SUCCEEDED(hr);
}
}
namespace ac { bool save_png(const std::wstring& path,int w,int h,const uint8_t* p,UINT stride){return png(path,w,h,p,stride);} }
namespace ap::video {
struct VideoRenderer::Impl {
 VideoRenderer* owner=nullptr;std::thread thread;std::atomic<bool> running{false};std::atomic<HWND> hwnd{nullptr};HANDLE wake=nullptr;
 mutable std::mutex mutex;std::condition_variable ready_cv;bool ready=false,success=false;
 AVFrame* pending=nullptr;AVFrame* last=nullptr;int64_t pending_origin=0,last_origin=0;AVBufferRef* hw=nullptr;
 mutable std::mutex state_mutex;DeviceInfo device;bool connected=false;Statistics stats;
 std::string idle_name,ip,metadata;std::function<void()> disconnect;std::function<void(ap::airplay::MediaCommand,uint32_t)> audio_control;
 ap::airplay::LiveSettings* live=nullptr;std::atomic<int64_t> flush_until{0};
 std::atomic<uint64_t> received{0},decoded{0},presented{0},dropped{0},bytes{0};
 std::atomic<bool> clear_requested{false},show_stats{false},auto_show{true};
 ComPtr<ID3D11Device> gpu;ComPtr<ID3D11DeviceContext> context;ComPtr<IDXGISwapChain> swap;
 ComPtr<ID3D11VideoDevice> video;ComPtr<ID3D11VideoContext> video_context;
 ComPtr<ID3D11VideoProcessorEnumerator> enumerator;ComPtr<ID3D11VideoProcessor> processor;
 ComPtr<ID3D11Texture2D> software_texture;
 ComPtr<ID3D11VideoProcessorInputView> software_view;
 ComPtr<ID2D1Factory> fallback_factory;
 ComPtr<ID2D1RenderTarget> fallback_target;
 ComPtr<ID2D1Bitmap> fallback_bitmap;
 std::map<std::pair<ID3D11Texture2D*,UINT>,ComPtr<ID3D11VideoProcessorInputView>> views;
 SwsContext* sws=nullptr;std::vector<uint8_t> bgra;int proc_w=0,proc_h=0,out_w=0,out_h=0,soft_w=0,soft_h=0;
 HWND menu_button=nullptr,overlay=nullptr;bool menu_open=false;int aspect_w=0,aspect_h=0;bool restored=false;HFONT font=nullptr;bool fullscreen=false,pip=false,controls=true;RECT normal{};LONG_PTR style=0;DWORD last_mouse=0;
 VideoRecorder recorder;std::mutex options_mutex;std::wstring folder;VideoRecorder::Options options;
 std::string error;int64_t last_rate=0;uint64_t prev_recv=0,prev_dec=0,prev_pres=0,prev_bytes=0;
 void fail(const char* text,HRESULT hr=E_FAIL){std::ostringstream s;s<<text<<" (0x"<<std::hex<<(unsigned)hr<<")";{std::lock_guard lock(state_mutex);stats.error=s.str();}LOG_ERROR<<s.str();}
 static LRESULT CALLBACK wndproc(HWND w,UINT m,WPARAM a,LPARAM b){
  auto* p=(Impl*)GetWindowLongPtrW(w,GWLP_USERDATA);
  if(m==WM_NCCREATE){p=(Impl*)((CREATESTRUCTW*)b)->lpCreateParams;SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)p);}
  if(!p)return DefWindowProcW(w,m,a,b);
  switch(m){
  case WM_ERASEBKGND:return 1;
  case WM_CLOSE:p->save_placement();ShowWindow(w,SW_HIDE);return 0;
  case WM_EXITSIZEMOVE:p->save_placement();return 0;
  case WM_SIZE:if(a!=SIZE_MINIMIZED)p->resize();return 0;
  case WM_GETMINMAXINFO:{int minimum=MulDiv(160,GetDpiForWindow(w),96);RECT r{0,0,minimum,minimum};AdjustWindowRectExForDpi(&r,(DWORD)GetWindowLongPtrW(w,GWL_STYLE),FALSE,0,GetDpiForWindow(w));((MINMAXINFO*)b)->ptMinTrackSize={r.right-r.left,r.bottom-r.top};return 0;}
  case WM_SIZING:p->constrain((RECT*)b,(UINT)a);return TRUE;
  case WM_CONTEXTMENU:p->show_menu();return 0;
  case WM_DPICHANGED:{auto r=(RECT*)b;SetWindowPos(w,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOZORDER|SWP_NOACTIVATE);p->layout();return 0;}
  case WM_LBUTTONDBLCLK:p->toggle_fullscreen();return 0;
  case WM_MOUSEMOVE:p->last_mouse=GetTickCount();if(!p->controls){p->controls=true;p->layout();}return 0;
  case WM_KEYDOWN:if(a==VK_ESCAPE&&p->fullscreen)p->toggle_fullscreen();else if(a==VK_F11||a=='F')p->toggle_fullscreen();else if(a=='R')p->record();else if(a=='S')p->capture();else if(a=='M')ac::playback_muted=!ac::playback_muted.load();else if(a==VK_APPS||a==VK_F10)p->show_menu();else if(a==VK_TAB){p->controls=true;p->layout();SetFocus(p->menu_button);}return 0;
  case WM_COMMAND:if(LOWORD(a)==110)p->show_menu();else p->command(LOWORD(a));return 0;
  case WM_TIMER:p->update_stats();if(p->controls&&p->last&&GetTickCount()-p->last_mouse>3000&&!p->menu_open&&(GetFocus()==w||GetFocus()==p->menu_button)){SetFocus(w);p->controls=false;p->layout();}return 0;
  case WM_MEASUREITEM:{auto* m=(MEASUREITEMSTRUCT*)b;if(m->CtlType==ODT_MENU){m->itemWidth=MulDiv(260,GetDpiForWindow(w),96);m->itemHeight=MulDiv(34,GetDpiForWindow(w),96);return TRUE;}break;}
  case WM_DRAWITEM:{auto* d=(DRAWITEMSTRUCT*)b;if(d->CtlType==ODT_MENU){RECT r=d->rcItem;bool active=d->itemState&ODS_SELECTED;HBRUSH brush=CreateSolidBrush(active?RGB(32,32,32):RGB(0,0,0));FillRect(d->hDC,&r,brush);DeleteObject(brush);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,RGB(235,235,235));SelectObject(d->hDC,p->font);auto text=(const wchar_t*)d->itemData;int pad=MulDiv(30,GetDpiForWindow(w),96);if(d->itemState&ODS_CHECKED){RECT mark=r;mark.right=mark.left+pad;DrawTextW(d->hDC,L"\u2713",-1,&mark,DT_CENTER|DT_SINGLELINE|DT_VCENTER);}r.left+=pad;r.right-=12;auto tab=std::wcschr(text,L'\t');if(tab){std::wstring label(text,size_t(tab-text));DrawTextW(d->hDC,label.c_str(),-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);DrawTextW(d->hDC,tab+1,-1,&r,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);}else DrawTextW(d->hDC,text,-1,&r,DT_LEFT|DT_VCENTER|DT_SINGLELINE);return TRUE;}RECT r=d->rcItem;HBRUSH brush=CreateSolidBrush((d->itemState&ODS_SELECTED)?RGB(36,36,36):RGB(16,16,16));FillRect(d->hDC,&r,brush);DeleteObject(brush);SetBkMode(d->hDC,TRANSPARENT);SetTextColor(d->hDC,RGB(225,231,241));SelectObject(d->hDC,p->font);auto len=GetWindowTextLengthW(d->hwndItem);std::wstring label(len+1,0);GetWindowTextW(d->hwndItem,label.data(),len+1);DrawTextW(d->hDC,L"\u22ef",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(d->itemState&ODS_FOCUS){InflateRect(&r,-3,-3);DrawFocusRect(d->hDC,&r);}return TRUE;}
  case WM_ACTION:switch(a){case Show:ShowWindow(w,SW_SHOWNORMAL);SetForegroundWindow(w);break;case Fullscreen:p->toggle_fullscreen();break;case Pip:p->toggle_pip();break;case Screenshot:p->capture();break;case Record:p->record();break;case Quit:DestroyWindow(w);PostQuitMessage(0);break;}return 0;
  case WM_APP+21:return p->capture_presented((const wchar_t*)b)?1:0;
  case WM_CTLCOLORSTATIC:SetTextColor((HDC)a,RGB(225,228,233));SetBkColor((HDC)a,RGB(0,0,0));return (LRESULT)GetStockObject(BLACK_BRUSH);
  }return DefWindowProcW(w,m,a,b);
 }
 void save_placement(){ac::save_window_state(hwnd,L"viewer",fullscreen?&normal:nullptr);}
 void activity(){last_mouse=GetTickCount();if(!controls){controls=true;layout();}}
 static LRESULT CALLBACK button_proc(HWND w,UINT m,WPARAM a,LPARAM b,UINT_PTR,DWORD_PTR data){auto* self=(Impl*)data;if(m==WM_MOUSEMOVE||m==WM_SETFOCUS)self->activity();if(m==WM_KEYDOWN&&a==VK_ESCAPE){SetFocus(self->hwnd);return 0;}return DefSubclassProc(w,m,a,b);}
 void layout(){HWND w=hwnd;if(!w)return;RECT r{};GetClientRect(w,&r);UINT dpi=GetDpiForWindow(w);int gap=MulDiv(8,dpi,96),size=MulDiv(38,dpi,96);
  SetWindowPos(menu_button,HWND_TOP,std::max(gap,int(r.right)-size-gap),gap,size,size,SWP_NOACTIVATE);ShowWindow(menu_button,controls?SW_SHOW:SW_HIDE);
  SetWindowPos(overlay,HWND_TOP,gap,gap,std::max(1,int(r.right)-size-3*gap),MulDiv(78,dpi,96),SWP_NOACTIVATE);ShowWindow(overlay,show_stats?SW_SHOW:SW_HIDE);
 }
 void command(int id){switch(id){case 100:ac::playback_muted=!ac::playback_muted.load();break;case 101:capture();break;case 102:record();break;case 103:toggle_pip();break;case 104:toggle_fullscreen();break;case 105:if(disconnect)disconnect();break;case 106:show_stats=!show_stats.load();layout();break;case 107:fit_window();break;}activity();SetFocus(hwnd);}
 void show_menu(){activity();menu_open=true;HMENU menu=CreatePopupMenu();auto item=[&](UINT id,const wchar_t* text,bool checked=false){AppendMenuW(menu,MF_OWNERDRAW|(checked?MF_CHECKED:0),id,text);};
  item(100,L"Mute audio\tM",ac::playback_muted);item(101,L"Save screenshot\tS");item(102,recorder.status().recording?L"Stop recording\tR":L"Start recording\tR");AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
  item(104,L"Fullscreen\tF11",fullscreen);item(103,L"Always on top",pip);item(107,L"Fit to phone screen");item(106,L"Performance statistics",show_stats);AppendMenuW(menu,MF_SEPARATOR,0,nullptr);item(105,L"Disconnect");
  HBRUSH black=CreateSolidBrush(RGB(0,0,0));MENUINFO info{sizeof(info)};info.fMask=MIM_BACKGROUND;info.hbrBack=black;SetMenuInfo(menu,&info);RECT r{};GetWindowRect(menu_button,&r);SetForegroundWindow(hwnd);int id=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTALIGN|TPM_RIGHTBUTTON,r.right,r.bottom+4,0,hwnd,nullptr);DestroyMenu(menu);DeleteObject(black);menu_open=false;if(id)command(id);else{activity();SetFocus(hwnd);}
 }
 void fit_window(bool keep_size=false){if(!last||fullscreen||IsZoomed(hwnd))return;HWND w=hwnd;UINT dpi=GetDpiForWindow(w);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&mi);RECT border{};AdjustWindowRectExForDpi(&border,(DWORD)GetWindowLongPtrW(w,GWL_STYLE),FALSE,0,dpi);
  int pad=MulDiv(24,dpi,96),bw=border.right-border.left,bh=border.bottom-border.top;auto size=ac::viewer::fit(last->width,last->height,mi.rcWork.right-mi.rcWork.left-bw-2*pad,mi.rcWork.bottom-mi.rcWork.top-bh-2*pad,pip?MulDiv(520,dpi,96):keep_size?std::max(out_w,out_h):0);
  RECT r{};GetWindowRect(w,&r);int width=size.width+bw,height=size.height+bh;int x=std::clamp<int>(r.left,mi.rcWork.left,mi.rcWork.right-width),y=std::clamp<int>(r.top,mi.rcWork.top,mi.rcWork.bottom-height);
  SetWindowPos(w,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
 }
 void constrain(RECT* r,UINT edge){if(!last||fullscreen)return;RECT b{};AdjustWindowRectExForDpi(&b,(DWORD)GetWindowLongPtrW(hwnd,GWL_STYLE),FALSE,0,GetDpiForWindow(hwnd));int bw=b.right-b.left,bh=b.bottom-b.top;
  int cw=std::max(1,int(r->right-r->left)-bw),ch=std::max(1,int(r->bottom-r->top)-bh);bool vertical=edge==WMSZ_TOP||edge==WMSZ_BOTTOM;int width=vertical?int(std::lround(double(ch)*last->width/last->height))+bw:int(r->right-r->left);int height=vertical?int(r->bottom-r->top):int(std::lround(double(cw)*last->height/last->width))+bh;
  if(edge==WMSZ_LEFT||edge==WMSZ_TOPLEFT||edge==WMSZ_BOTTOMLEFT)r->left=r->right-width;else r->right=r->left+width;
  if(edge==WMSZ_TOP||edge==WMSZ_TOPLEFT||edge==WMSZ_TOPRIGHT)r->top=r->bottom-height;else r->bottom=r->top+height;
 }
 void toggle_fullscreen(){HWND w=hwnd;if(!w)return;if(!fullscreen){GetWindowRect(w,&normal);style=GetWindowLongPtrW(w,GWL_STYLE);MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(w,MONITOR_DEFAULTTONEAREST),&mi);fullscreen=true;SetWindowLongPtrW(w,GWL_STYLE,style&~WS_OVERLAPPEDWINDOW);SetWindowPos(w,pip?HWND_TOPMOST:HWND_TOP,mi.rcMonitor.left,mi.rcMonitor.top,mi.rcMonitor.right-mi.rcMonitor.left,mi.rcMonitor.bottom-mi.rcMonitor.top,SWP_FRAMECHANGED);}else{fullscreen=false;SetWindowLongPtrW(w,GWL_STYLE,style);SetWindowPos(w,pip?HWND_TOPMOST:HWND_NOTOPMOST,normal.left,normal.top,normal.right-normal.left,normal.bottom-normal.top,SWP_FRAMECHANGED);fit_window();}activity();SetFocus(w);}
 void toggle_pip(){pip=!pip;SetWindowPos(hwnd,pip?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);fit_window();}
 bool init_gpu(){
  bool force_warp=false;if(auto* value=std::getenv("AIRCONNECT_FORCE_WARP"))force_warp=std::strcmp(value,"1")==0;
  D3D_FEATURE_LEVEL level{};HRESULT hr=force_warp?E_FAIL:D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_VIDEO_SUPPORT|D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&gpu,&level,&context);
  if(FAILED(hr)){hr=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,&gpu,&level,&context);}
  if(FAILED(hr)){fail("Unable to create the Direct3D device",hr);return false;}
  ComPtr<ID3D10Multithread> multi;if(SUCCEEDED(context.As(&multi)))multi->SetMultithreadProtected(TRUE);
  ComPtr<IDXGIDevice1> latency;if(SUCCEEDED(gpu.As(&latency)))latency->SetMaximumFrameLatency(1);
  gpu.As(&video);context.As(&video_context);
  if(force_warp){video.Reset();video_context.Reset();}
  if(video&&video_context){hw=av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);if(hw){auto* ctx=(AVHWDeviceContext*)hw->data;auto* d=(AVD3D11VADeviceContext*)ctx->hwctx;d->device=gpu.Get();d->device->AddRef();if(av_hwdevice_ctx_init(hw)<0)av_buffer_unref(&hw);}}
  ComPtr<IDXGIDevice> dx;ComPtr<IDXGIAdapter> adapter;ComPtr<IDXGIFactory> factory;gpu.As(&dx);dx->GetAdapter(&adapter);adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
  DXGI_SWAP_CHAIN_DESC desc{};desc.BufferDesc.Width=960;desc.BufferDesc.Height=600;desc.BufferDesc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;desc.SampleDesc.Count=1;desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;desc.BufferCount=2;desc.OutputWindow=hwnd;desc.Windowed=TRUE;desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  hr=factory->CreateSwapChain(gpu.Get(),&desc,&swap);factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);if(FAILED(hr)){fail("Unable to create the video surface",hr);return false;}return true;
 }
 void resize(){if(!swap)return;RECT r{};GetClientRect(hwnd,&r);if(r.right<=0||r.bottom<=0)return;fallback_bitmap.Reset();fallback_target.Reset();context->OMSetRenderTargets(0,nullptr,nullptr);HRESULT hr=swap->ResizeBuffers(0,r.right,r.bottom,DXGI_FORMAT_UNKNOWN,0);if(FAILED(hr)){fail("Video resize failed",hr);return;}out_w=r.right;out_h=r.bottom;processor.Reset();enumerator.Reset();views.clear();software_view.Reset();layout();if(last)present(false);}
 bool software_present(bool fresh){
  if(!last||last->format==AV_PIX_FMT_D3D11)return false;
  sws=sws_getCachedContext(sws,last->width,last->height,(AVPixelFormat)last->format,last->width,last->height,AV_PIX_FMT_BGRA,SWS_FAST_BILINEAR,nullptr,nullptr,nullptr);if(!sws)return false;
  int cs=last->colorspace==AVCOL_SPC_BT470BG||last->colorspace==AVCOL_SPC_SMPTE170M?SWS_CS_ITU601:SWS_CS_ITU709;auto coeff=sws_getCoefficients(cs);sws_setColorspaceDetails(sws,coeff,last->color_range==AVCOL_RANGE_JPEG,coeff,1,0,1<<16,1<<16);
  bgra.resize((size_t)last->width*last->height*4);uint8_t* dst[4]={bgra.data()};int stride[4]={last->width*4};sws_scale(sws,last->data,last->linesize,0,last->height,dst,stride);
  HRESULT hr=S_OK;if(!fallback_factory)hr=D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,fallback_factory.GetAddressOf());
  if(SUCCEEDED(hr)&&!fallback_target){ComPtr<IDXGISurface> surface;hr=swap->GetBuffer(0,IID_PPV_ARGS(surface.GetAddressOf()));auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);if(SUCCEEDED(hr))hr=fallback_factory->CreateDxgiSurfaceRenderTarget(surface.Get(),&props,&fallback_target);}
  if(FAILED(hr)){fail("Software display initialization failed",hr);return false;}
  auto size=fallback_bitmap?fallback_bitmap->GetPixelSize():D2D1::SizeU(0,0);if(size.width!=(UINT)last->width||size.height!=(UINT)last->height)fallback_bitmap.Reset();
  if(!fallback_bitmap){auto props=D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE));hr=fallback_target->CreateBitmap(D2D1::SizeU(last->width,last->height),bgra.data(),last->width*4,&props,&fallback_bitmap);}else hr=fallback_bitmap->CopyFromMemory(nullptr,bgra.data(),last->width*4);
  if(FAILED(hr))return false;float fw=(float)out_w,fh=(float)out_h;if(fw*last->height>fh*last->width)fw=fh*last->width/last->height;else fh=fw*last->height/last->width;
  fallback_target->BeginDraw();fallback_target->Clear(D2D1::ColorF(D2D1::ColorF::Black));fallback_target->DrawBitmap(fallback_bitmap.Get(),D2D1::RectF((out_w-fw)/2,(out_h-fh)/2,(out_w+fw)/2,(out_h+fh)/2));hr=fallback_target->EndDraw();if(FAILED(hr)){fail("Software display rendering failed",hr);return false;}
  hr=swap->Present(live&&live->vsync_enabled?1:0,0);if(FAILED(hr)){fail("Software presentation failed",hr);return false;}
  if(fresh){if(hr==S_OK)++presented;std::lock_guard lock(state_mutex);++stats.rendered;stats.presentation_result=hr;stats.width=last->width;stats.height=last->height;stats.gpu=false;if(last_origin)stats.latency_ms=(ac::now_ns()-last_origin)/1e6;}
  return true;
 }
 bool present(bool fresh){
  if(!last||!swap)return false;ComPtr<ID3D11Texture2D> target;HRESULT hr=swap->GetBuffer(0,IID_PPV_ARGS(target.GetAddressOf()));if(FAILED(hr))return false;
  ComPtr<ID3D11RenderTargetView> rtv;hr=gpu->CreateRenderTargetView(target.Get(),nullptr,&rtv);if(FAILED(hr))return false;float black[4]={0,0,0,1};context->ClearRenderTargetView(rtv.Get(),black);
  if(!video||!video_context)return software_present(fresh);
  if(!processor||proc_w!=last->width||proc_h!=last->height){
   views.clear();software_view.Reset();processor.Reset();enumerator.Reset();D3D11_VIDEO_PROCESSOR_CONTENT_DESC d{};d.InputFrameFormat=D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;d.InputWidth=last->width;d.InputHeight=last->height;d.OutputWidth=out_w;d.OutputHeight=out_h;d.InputFrameRate=d.OutputFrameRate={60,1};d.Usage=D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;
   hr=video->CreateVideoProcessorEnumerator(&d,&enumerator);if(SUCCEEDED(hr))hr=video->CreateVideoProcessor(enumerator.Get(),0,&processor);if(FAILED(hr)){fail("Video processor initialization failed",hr);return false;}proc_w=last->width;proc_h=last->height;
  }
  ComPtr<ID3D11VideoProcessorInputView> in;bool on_gpu=last->format==AV_PIX_FMT_D3D11;
  if(on_gpu){auto* texture=(ID3D11Texture2D*)last->data[0];UINT slice=(UINT)(uintptr_t)last->data[1];auto key=std::make_pair(texture,slice);auto it=views.find(key);if(it!=views.end())in=it->second;else{D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC d{};d.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;d.Texture2D.ArraySlice=slice;hr=video->CreateVideoProcessorInputView(texture,enumerator.Get(),&d,&in);if(FAILED(hr)){fail("Decoder surface is incompatible with the video processor",hr);return false;}if(views.size()>128)views.clear();views.emplace(key,in);}}
  else{
   sws=sws_getCachedContext(sws,last->width,last->height,(AVPixelFormat)last->format,last->width,last->height,AV_PIX_FMT_BGRA,SWS_FAST_BILINEAR,nullptr,nullptr,nullptr);if(!sws)return false;
   int cs=last->colorspace==AVCOL_SPC_BT470BG||last->colorspace==AVCOL_SPC_SMPTE170M?SWS_CS_ITU601:SWS_CS_ITU709;const int* coeff=sws_getCoefficients(cs);sws_setColorspaceDetails(sws,coeff,last->color_range==AVCOL_RANGE_JPEG,coeff,1,0,1<<16,1<<16);
   bgra.resize((size_t)last->width*last->height*4);uint8_t* dst[4]={bgra.data()};int stride[4]={last->width*4};sws_scale(sws,last->data,last->linesize,0,last->height,dst,stride);
   if(!software_texture||soft_w!=last->width||soft_h!=last->height){software_texture.Reset();software_view.Reset();D3D11_TEXTURE2D_DESC d{};d.Width=last->width;d.Height=last->height;d.MipLevels=d.ArraySize=1;d.Format=DXGI_FORMAT_B8G8R8A8_UNORM;d.SampleDesc.Count=1;d.Usage=D3D11_USAGE_DEFAULT;d.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;hr=gpu->CreateTexture2D(&d,nullptr,&software_texture);if(FAILED(hr))return false;soft_w=last->width;soft_h=last->height;}
   context->UpdateSubresource(software_texture.Get(),0,nullptr,bgra.data(),last->width*4,0);
   if(!software_view){D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC d{};d.ViewDimension=D3D11_VPIV_DIMENSION_TEXTURE2D;hr=video->CreateVideoProcessorInputView(software_texture.Get(),enumerator.Get(),&d,&software_view);if(FAILED(hr)){fail("Software video upload failed",hr);return false;}}in=software_view;
  }
  D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC od{};od.ViewDimension=D3D11_VPOV_DIMENSION_TEXTURE2D;ComPtr<ID3D11VideoProcessorOutputView> output;hr=video->CreateVideoProcessorOutputView(target.Get(),enumerator.Get(),&od,&output);if(FAILED(hr))return false;
  RECT src{0,0,last->width,last->height};int rw=out_w,rh=out_h;if((int64_t)rw*last->height>(int64_t)rh*last->width)rw=(int)((int64_t)rh*last->width/last->height);else rh=(int)((int64_t)rw*last->height/last->width);RECT dest{(out_w-rw)/2,(out_h-rh)/2,(out_w+rw)/2,(out_h+rh)/2};
  video_context->VideoProcessorSetStreamFrameFormat(processor.Get(),0,D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE);video_context->VideoProcessorSetStreamSourceRect(processor.Get(),0,TRUE,&src);video_context->VideoProcessorSetStreamDestRect(processor.Get(),0,TRUE,&dest);video_context->VideoProcessorSetStreamAutoProcessingMode(processor.Get(),0,FALSE);
  D3D11_VIDEO_PROCESSOR_COLOR_SPACE color{};color.YCbCr_Matrix=last->colorspace!=AVCOL_SPC_BT470BG&&last->colorspace!=AVCOL_SPC_SMPTE170M;color.Nominal_Range=on_gpu?(last->color_range==AVCOL_RANGE_JPEG?2:1):2;video_context->VideoProcessorSetStreamColorSpace(processor.Get(),0,&color);D3D11_VIDEO_PROCESSOR_COLOR_SPACE rgb{};rgb.RGB_Range=0;rgb.Nominal_Range=2;video_context->VideoProcessorSetOutputColorSpace(processor.Get(),&rgb);
  D3D11_VIDEO_COLOR bg{};bg.RGBA.A=1;video_context->VideoProcessorSetOutputBackgroundColor(processor.Get(),FALSE,&bg);
  D3D11_VIDEO_PROCESSOR_STREAM stream{};stream.Enable=TRUE;stream.pInputSurface=in.Get();hr=video_context->VideoProcessorBlt(processor.Get(),output.Get(),0,1,&stream);if(FAILED(hr)){fail("Video presentation failed",hr);return false;}
  hr=swap->Present(live&&live->vsync_enabled?1:0,0);if(FAILED(hr)){fail("The display device was lost; restart the receiver",hr);return false;}
  if(fresh){if(hr==S_OK)++presented;std::lock_guard lock(state_mutex);++stats.rendered;stats.presentation_result=hr;stats.width=last->width;stats.height=last->height;stats.gpu=on_gpu;if(last_origin)stats.latency_ms=std::max(0.,(ac::now_ns()-last_origin)/1e6);}
  return true;
 }
 bool capture_presented(const std::wstring& path){
  if(!swap)return false;ComPtr<ID3D11Texture2D> texture,copy;if(FAILED(swap->GetBuffer(0,IID_PPV_ARGS(texture.GetAddressOf()))))return false;D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);desc.BindFlags=0;desc.MiscFlags=0;desc.Usage=D3D11_USAGE_STAGING;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;if(FAILED(gpu->CreateTexture2D(&desc,nullptr,&copy)))return false;context->CopyResource(copy.Get(),texture.Get());D3D11_MAPPED_SUBRESOURCE map{};if(FAILED(context->Map(copy.Get(),0,D3D11_MAP_READ,0,&map)))return false;bool ok=png(path,desc.Width,desc.Height,(const uint8_t*)map.pData,map.RowPitch);context->Unmap(copy.Get(),0);return ok;
 }
 void capture(const std::wstring& requested=L""){
  if(!last)return;AVFrame* cpu=av_frame_alloc();const AVFrame* input=last;
  if(last->format==AV_PIX_FMT_D3D11){if(av_hwframe_transfer_data(cpu,last,0)<0){av_frame_free(&cpu);fail("Screenshot readback failed");return;}input=cpu;}
  SwsContext* converter=sws_getContext(input->width,input->height,(AVPixelFormat)input->format,input->width,input->height,AV_PIX_FMT_BGRA,SWS_BILINEAR,nullptr,nullptr,nullptr);
  if(!converter){av_frame_free(&cpu);return;}std::vector<uint8_t> pixels((size_t)input->width*input->height*4);uint8_t* dst[4]={pixels.data()};int stride[4]={input->width*4};sws_scale(converter,input->data,input->linesize,0,input->height,dst,stride);sws_freeContext(converter);
  std::wstring directory;{std::lock_guard lock(options_mutex);directory=folder;}std::error_code ec;std::filesystem::create_directories(directory,ec);std::wstring path=requested.empty()?(std::filesystem::path(directory)/timestamp(L".png")).wstring():requested;
  if(!png(path,input->width,input->height,pixels.data(),input->width*4))fail("Unable to save the screenshot");else SetWindowTextW(hwnd,(L"AirConnect — Screenshot saved: "+std::filesystem::path(path).filename().wstring()).c_str());av_frame_free(&cpu);
 }
 void record(){if(recorder.status().recording){recorder.stop();SetWindowTextW(hwnd,L"AirConnect — Recording saved");return;}if(!last){fail("Start mirroring before recording");return;}std::lock_guard lock(options_mutex);if(!recorder.start(ac::utf8(folder),options))fail("Unable to start recording");}
 void update_stats(){
  int64_t now=ac::now_ns();double elapsed=(now-last_rate)/1e9;auto r=received.load(),d=decoded.load(),v=presented.load(),b=bytes.load();auto rs=recorder.status();
  int queue=0;{std::lock_guard lock(mutex);queue=pending?1:0;}
  std::lock_guard lock(state_mutex);stats.received=r;stats.decoded=d;stats.presented=v;stats.dropped=dropped;stats.bytes=b;stats.queue=queue;stats.recording=rs.recording;stats.recording_path=rs.output_path;if(!rs.error.empty())stats.error=rs.error;
  if(elapsed>=.75){stats.received_fps=(r-prev_recv)/elapsed;stats.decoded_fps=(d-prev_dec)/elapsed;stats.presented_fps=(v-prev_pres)/elapsed;stats.mbps=(b-prev_bytes)*8/elapsed/1e6;last_rate=now;prev_recv=r;prev_dec=d;prev_pres=v;prev_bytes=b;}
  if(show_stats){wchar_t text[512];swprintf(text,512,L"%s  %d × %d   %s\nReceive %.1f  Decode %.1f  Present %.1f fps   %.2f Mbps   Dropped %llu   Queue %d   Local %.1f ms",ac::wide(stats.codec).c_str(),stats.width,stats.height,stats.gpu?L"D3D11":L"Software",stats.received_fps,stats.decoded_fps,stats.presented_fps,stats.mbps,(unsigned long long)stats.dropped,stats.queue,stats.latency_ms);SetWindowTextW(overlay,text);}
 }
 void run(const std::string& title){
  HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);WNDCLASSW wc{};wc.style=CS_DBLCLKS;wc.lpfnWndProc=wndproc;wc.hInstance=GetModuleHandleW(nullptr);wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);wc.lpszClassName=L"AirConnect.Video";wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);RegisterClassW(&wc);
  hwnd=CreateWindowExW(0,wc.lpszClassName,ac::wide(title).c_str(),WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,960,640,nullptr,nullptr,wc.hInstance,this);
  restored=ac::restore_window_state(hwnd,L"viewer",false);BOOL dark=TRUE;COLORREF caption=RGB(0,0,0);DwmSetWindowAttribute(hwnd,20,&dark,sizeof(dark));DwmSetWindowAttribute(hwnd,35,&caption,sizeof(caption));bool ok=hwnd&&init_gpu();if(ok){font=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");menu_button=CreateWindowW(L"BUTTON",L"\u22ef",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,38,38,hwnd,(HMENU)110,wc.hInstance,nullptr);SendMessageW(menu_button,WM_SETFONT,(WPARAM)font,FALSE);SetWindowSubclass(menu_button,button_proc,1,(DWORD_PTR)this);SetWindowTextW(menu_button,L"Viewer menu");overlay=CreateWindowW(L"STATIC",L"",WS_CHILD|SS_LEFT,8,8,500,58,hwnd,nullptr,wc.hInstance,nullptr);SendMessageW(overlay,WM_SETFONT,(WPARAM)font,FALSE);SetTimer(hwnd,1,250,nullptr);last_rate=ac::now_ns();last_mouse=GetTickCount();resize();}
  {std::lock_guard lock(mutex);success=ok;ready=true;}ready_cv.notify_all();
  if(ok){MSG msg{};while(running){DWORD result=MsgWaitForMultipleObjects(1,&wake,FALSE,INFINITE,QS_ALLINPUT);if(result==WAIT_OBJECT_0){
    if(clear_requested.exchange(false)){save_placement();recorder.stop();av_frame_free(&last);views.clear();aspect_w=aspect_h=0;ShowWindow(hwnd,SW_HIDE);}
    AVFrame* f=nullptr;int64_t origin=0;{std::lock_guard lock(mutex);f=pending;pending=nullptr;origin=pending_origin;}
    if(f){av_frame_free(&last);last=f;last_origin=origin;bool first=auto_show.exchange(false);bool changed=!aspect_w||int64_t(aspect_w)*f->height!=int64_t(aspect_h)*f->width;aspect_w=f->width;aspect_h=f->height;if(first||changed)fit_window(first&&restored);if(first){ShowWindow(hwnd,SW_SHOWNOACTIVATE);last_mouse=GetTickCount();}present(true);}
   }
   while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}
  }}
  save_placement();recorder.stop();{std::lock_guard lock(mutex);av_frame_free(&pending);av_buffer_unref(&hw);}av_frame_free(&last);if(sws)sws_freeContext(sws);sws=nullptr;views.clear();fallback_bitmap.Reset();fallback_target.Reset();fallback_factory.Reset();software_view.Reset();software_texture.Reset();processor.Reset();enumerator.Reset();video_context.Reset();video.Reset();swap.Reset();context.Reset();gpu.Reset();if(IsWindow(hwnd))DestroyWindow(hwnd);hwnd=nullptr;if(font)DeleteObject(font);if(SUCCEEDED(co))CoUninitialize();
 }
};
VideoRenderer::VideoRenderer():p(std::make_unique<Impl>()){p->owner=this;p->folder=ac::default_recordings();}
VideoRenderer::~VideoRenderer(){stop();}
bool VideoRenderer::start(const std::string& title){if(p->running)return true;p->wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!p->wake)return false;p->running=true;p->ready=false;p->thread=std::thread([this,title]{p->run(title);});std::unique_lock lock(p->mutex);p->ready_cv.wait(lock,[this]{return p->ready;});return p->success;}
void VideoRenderer::stop(){p->running=false;if(p->wake)SetEvent(p->wake);if(p->thread.joinable())p->thread.join();if(p->wake){CloseHandle(p->wake);p->wake=nullptr;}}
void VideoRenderer::request_show(){if(p->hwnd)PostMessageW(p->hwnd,WM_ACTION,Show,0);}
void VideoRenderer::push_avframe(const AVFrame* f,int64_t origin){if(!f||!p->running||f->width<1||f->height<1||f->width>8192||f->height>8192)return;AVFrame* copy=av_frame_clone(f);if(!copy)return;p->recorder.submit(f);{std::lock_guard lock(p->mutex);if(p->pending){av_frame_free(&p->pending);++p->dropped;}p->pending=copy;p->pending_origin=origin;++p->decoded;}{std::lock_guard lock(p->state_mutex);p->stats.has_video=true;}SetEvent(p->wake);}
void VideoRenderer::push_frame(const uint8_t*y,int ys,const uint8_t*u,int us,const uint8_t*v,int vs,int w,int h,int64_t origin){if(w<1||h<1||w>8192||h>8192)return;AVFrame* f=av_frame_alloc();f->format=AV_PIX_FMT_YUV420P;f->width=w;f->height=h;if(av_frame_get_buffer(f,32)>=0){const uint8_t* src[3]={y,u,v};int stride[3]={ys,us,vs};for(int i=0;i<3;++i)for(int r=0;r<(i?(h+1)/2:h);++r)memcpy(f->data[i]+r*f->linesize[i],src[i]+r*stride[i],i?(w+1)/2:w);push_avframe(f,origin);}av_frame_free(&f);}
void VideoRenderer::push_frame_nv12(const uint8_t*y,int ys,const uint8_t*uv,int uvs,int w,int h,int64_t origin){if(w<1||h<1||w>8192||h>8192)return;AVFrame*f=av_frame_alloc();f->format=AV_PIX_FMT_NV12;f->width=w;f->height=h;if(av_frame_get_buffer(f,32)>=0){for(int r=0;r<h;++r)memcpy(f->data[0]+r*f->linesize[0],y+r*ys,w);for(int r=0;r<(h+1)/2;++r)memcpy(f->data[1]+r*f->linesize[1],uv+r*uvs,w);push_avframe(f,origin);}av_frame_free(&f);}
AVBufferRef* VideoRenderer::gpu_device_ref(){std::lock_guard lock(p->mutex);return p->hw?av_buffer_ref(p->hw):nullptr;}
void VideoRenderer::record_payload_bytes(size_t n){p->bytes+=n;}
void VideoRenderer::record_received_frame(){++p->received;}
void VideoRenderer::note_connection(int change){std::lock_guard lock(p->state_mutex);p->stats.connecting=std::max(0,p->stats.connecting+change);}
void VideoRenderer::push_audio_pcm(const int16_t*s,int n,int rate,int channels){p->recorder.submit_audio(s,n,rate,channels);}
void VideoRenderer::push_cover_art(const uint8_t*,size_t){} // Cover art is not retained or written to disk.
void VideoRenderer::push_metadata(const std::string&t,const std::string&a,const std::string&){std::lock_guard lock(p->state_mutex);p->metadata=t+" — "+a;}
void VideoRenderer::push_progress(uint32_t,uint32_t){}
void VideoRenderer::push_playback_rate(float){}
void VideoRenderer::clear_session(){{std::lock_guard lock(p->mutex);av_frame_free(&p->pending);p->clear_requested=true;}{std::lock_guard lock(p->state_mutex);p->stats.has_video=false;p->stats.width=p->stats.height=0;}SetEvent(p->wake);}
void VideoRenderer::set_idle_info(const std::string&n,const std::string&i){std::lock_guard lock(p->state_mutex);p->idle_name=n;p->ip=i;}
void VideoRenderer::set_active_device(DeviceInfo d){std::lock_guard lock(p->state_mutex);if(d.session_id!=p->device.session_id){p->auto_show=true;p->stats.has_video=false;p->stats.width=p->stats.height=0;p->stats.error.clear();}p->device=std::move(d);p->connected=true;}
void VideoRenderer::clear_active_device(){std::lock_guard lock(p->state_mutex);p->device={};p->connected=false;}
bool VideoRenderer::active_device_snapshot(DeviceInfo& d)const{std::lock_guard lock(p->state_mutex);d=p->device;return p->connected;}
bool VideoRenderer::clear_session_if_active(const std::string&id){{std::lock_guard lock(p->state_mutex);if(!p->connected||p->device.session_id!=id)return false;p->connected=false;p->device={};}clear_session();return true;}
void VideoRenderer::note_flush(){p->flush_until=ac::now_ns()+500000000LL;}
bool VideoRenderer::in_flush_grace()const{return ac::now_ns()<p->flush_until;}
void VideoRenderer::set_live_settings(ap::airplay::LiveSettings*s){p->live=s;}
void VideoRenderer::set_disconnect_handler(std::function<void()>f){p->disconnect=std::move(f);}
void VideoRenderer::set_audio_control_handler(std::function<void(ap::airplay::MediaCommand,uint32_t)>f){p->audio_control=std::move(f);}
VideoRenderer::Statistics VideoRenderer::statistics()const{std::lock_guard lock(p->state_mutex);return p->stats;}
void VideoRenderer::set_codec(bool h){std::lock_guard lock(p->state_mutex);p->stats.codec=h?"HEVC":"H.264";}
void VideoRenderer::request_fullscreen(){PostMessageW(p->hwnd,WM_ACTION,Fullscreen,0);}
void VideoRenderer::request_pip(){PostMessageW(p->hwnd,WM_ACTION,Pip,0);}
void VideoRenderer::request_screenshot(){PostMessageW(p->hwnd,WM_ACTION,Screenshot,0);}
void VideoRenderer::request_recording(){PostMessageW(p->hwnd,WM_ACTION,Record,0);}
void VideoRenderer::set_statistics_visible(bool b){p->show_stats=b;}
void VideoRenderer::set_recording_options(const std::wstring& dir,int codec,int encoder,int bitrate){std::lock_guard lock(p->options_mutex);p->folder=dir;p->options.codec=(VideoRecorder::VideoCodec)codec;p->options.encoder=(VideoRecorder::EncoderMode)encoder;p->options.bitrate_mbps=std::clamp(bitrate,2,40);}
HWND VideoRenderer::window()const{return p->hwnd;}
bool VideoRenderer::save_test_capture(const std::wstring& path){return SendMessageW(p->hwnd,WM_APP+21,0,(LPARAM)path.c_str())!=0;}
}

