#include "audio/audio_output.h"
#include "core/preferences.h"
#include "log.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <deque>
#include <mutex>
#include <thread>
#include <condition_variable>
#ifndef AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM
#define AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM 0x80000000
#define AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY 0x08000000
#endif
using Microsoft::WRL::ComPtr;
namespace ac {
std::vector<std::pair<std::wstring,std::wstring>> audio_devices(){
 std::vector<std::pair<std::wstring,std::wstring>> out{{L"",L"Windows default"}};HRESULT init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 ComPtr<IMMDeviceEnumerator> e;ComPtr<IMMDeviceCollection> devices;
 if(SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(e.GetAddressOf())))&&SUCCEEDED(e->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,&devices))){UINT n=0;devices->GetCount(&n);for(UINT i=0;i<n;++i){ComPtr<IMMDevice>d;ComPtr<IPropertyStore>s;LPWSTR id=nullptr;PROPVARIANT v;PropVariantInit(&v);if(SUCCEEDED(devices->Item(i,&d))&&SUCCEEDED(d->GetId(&id))){if(SUCCEEDED(d->OpenPropertyStore(STGM_READ,&s))&&SUCCEEDED(s->GetValue(PKEY_Device_FriendlyName,&v))&&v.vt==VT_LPWSTR)out.emplace_back(id,v.pwszVal);CoTaskMemFree(id);}PropVariantClear(&v);}}
 devices.Reset();e.Reset();if(SUCCEEDED(init))CoUninitialize();return out;
}
}
namespace ap::audio {
struct SdlAudioOutput::Impl {
 std::atomic<bool> running{false},reset{false},initialized{false};std::atomic<float> gain{1.f};std::atomic<UINT32> padding{0};
 int rate=44100,channels=2;std::thread thread;mutable std::mutex mutex;std::deque<int16_t> queue;
 void run(){
  HRESULT co=CoInitializeEx(nullptr,COINIT_MULTITHREADED);HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  ComPtr<IMMDeviceEnumerator> enumerator;CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,IID_PPV_ARGS(enumerator.GetAddressOf()));
  ComPtr<IAudioClient> client;ComPtr<IAudioRenderClient> render;UINT32 capacity=0;std::wstring current,actual;int retry=0,device_check=0;bool started=false;
  while(running){
   auto selected=ac::output_device();
   bool default_changed=false;if(client&&selected.empty()&&++device_check>=50){device_check=0;ComPtr<IMMDevice>d;LPWSTR id=nullptr;if(enumerator&&SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&d))&&SUCCEEDED(d->GetId(&id))){default_changed=actual!=id;CoTaskMemFree(id);}}
   if(client&&(selected!=current||default_changed)){client->Stop();render.Reset();client.Reset();initialized=false;started=false;retry=0;}
   if(!client){
    if(retry++%50!=0){Sleep(20);continue;}
    current=selected;ComPtr<IMMDevice>d;
    HRESULT hr=enumerator?(selected.empty()?enumerator->GetDefaultAudioEndpoint(eRender,eMultimedia,&d):enumerator->GetDevice(selected.c_str(),&d)):E_FAIL;
    if(SUCCEEDED(hr)){LPWSTR id=nullptr;if(SUCCEEDED(d->GetId(&id))){actual=id;CoTaskMemFree(id);}hr=d->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,(void**)client.GetAddressOf());}
    WAVEFORMATEX fmt{};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=(WORD)channels;fmt.nSamplesPerSec=rate;fmt.wBitsPerSample=16;fmt.nBlockAlign=(WORD)(channels*2);fmt.nAvgBytesPerSec=rate*fmt.nBlockAlign;
    if(SUCCEEDED(hr))hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,400000,0,&fmt,nullptr);
    if(SUCCEEDED(hr))hr=client->SetEventHandle(event);
    if(SUCCEEDED(hr))hr=client->GetBufferSize(&capacity);
    if(SUCCEEDED(hr))hr=client->GetService(IID_PPV_ARGS(render.GetAddressOf()));
    if(FAILED(hr)){initialized=false;render.Reset();client.Reset();Sleep(20);continue;}
    initialized=true;LOG_INFO<<"WASAPI initialized at "<<rate<<" Hz";started=false;
   }
   if(reset.exchange(false)){client->Stop();client->Reset();started=false;}
   UINT32 pad=0;HRESULT hr=client->GetCurrentPadding(&pad);padding=pad;
   if(FAILED(hr)){initialized=false;render.Reset();client.Reset();started=false;continue;}
   UINT32 available=capacity-pad;
   if(available){
    std::lock_guard lock(mutex);UINT32 frames=std::min(available,(UINT32)(queue.size()/channels));
    if(frames){BYTE* data=nullptr;hr=render->GetBuffer(frames,&data);if(SUCCEEDED(hr)){
     float g=gain.load()*ac::playback_volume.load()*(ac::playback_muted?0.f:1.f);auto* dst=(int16_t*)data;
     for(UINT32 i=0;i<frames*(UINT32)channels;++i){dst[i]=(int16_t)std::clamp(queue.front()*g,-32768.f,32767.f);queue.pop_front();}
     render->ReleaseBuffer(frames,0);if(!started){hr=client->Start();started=SUCCEEDED(hr);}
    }}
   }
   if(FAILED(hr)){initialized=false;render.Reset();client.Reset();started=false;continue;}
   WaitForSingleObject(event,20);
  }
  initialized=false;if(client)client->Stop();render.Reset();client.Reset();enumerator.Reset();if(event)CloseHandle(event);if(SUCCEEDED(co))CoUninitialize();
 }
};
SdlAudioOutput::SdlAudioOutput():p(std::make_unique<Impl>()){}
SdlAudioOutput::~SdlAudioOutput(){stop();}
bool SdlAudioOutput::start(int rate,int channels){stop();if(rate<8000||rate>192000||channels<1||channels>2)return false;p->rate=rate;p->channels=channels;p->running=true;p->thread=std::thread([this]{p->run();});return true;}
void SdlAudioOutput::stop(){p->running=false;if(p->thread.joinable())p->thread.join();clear();p->padding=0;}
void SdlAudioOutput::push(const int16_t* s,int count){if(!s||count<=0||!p->running)return;std::lock_guard lock(p->mutex);size_t limit=(size_t)p->rate*p->channels/4;size_t n=std::min((size_t)count,limit);while(p->queue.size()+n>limit)p->queue.pop_front();p->queue.insert(p->queue.end(),s+count-n,s+count);}
void SdlAudioOutput::set_volume_db(float db){p->gain=db<=-100?0.f:db>=0?1.f:std::pow(10.f,db/20.f);}
uint32_t SdlAudioOutput::queued_bytes()const{std::lock_guard lock(p->mutex);return (uint32_t)p->queue.size()*2;}
int64_t SdlAudioOutput::pending_ns()const{return (queued_bytes()/2/p->channels+p->padding)*1000000000LL/p->rate;}
void SdlAudioOutput::clear(){std::lock_guard lock(p->mutex);p->queue.clear();p->reset=true;}
bool SdlAudioOutput::initialized()const{return p->initialized;}
}
