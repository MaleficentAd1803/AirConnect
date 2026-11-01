#include "core/receiver.h"
#include "airplay/server.h"
#include "mdns/mdns_service.h"
#include "crypto/identity.h"
#include "net/socket.h"
#include "log.h"
#include "platform/network_policy.h"
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>
#include <fstream>
namespace ac {
const wchar_t* state_text(State s){switch(s){case State::Idle:return L"Starting receiver";case State::Discoverable:return L"Ready to receive";case State::Connecting:return L"Connecting";case State::Connected:return L"Connected";case State::Mirroring:return L"Screen mirroring";case State::AudioOnly:return L"Playing audio";case State::Disconnecting:return L"Disconnecting";case State::Error:return L"Receiver needs attention";}return L"";}
struct Receiver::Impl {
 std::thread worker;std::atomic<bool>running{false},disconnect{false},readvertise{false};mutable std::mutex mutex;ReceiverStatus status;Preferences preferences;ap::airplay::LiveSettings live;ap::video::VideoRenderer renderer;
 void run(uint16_t port){
  if(!ap::net::global_init()){std::lock_guard lock(mutex);status.state=State::Error;status.message="Windows networking could not start.";return;}
  std::error_code ec;auto dir=data_directory();std::filesystem::create_directories(dir,ec);auto id=ap::crypto::Identity::load_or_create(utf8((dir/L"identity.dpapi").wstring()));
  if(!id){std::lock_guard lock(mutex);status.state=State::Error;status.message="The receiver identity could not be created. Check the application data folder.";ap::net::global_shutdown();return;}
  ap::airplay::DeviceContext ctx;ctx.name=live.receiver_name();ctx.deviceid=ap::net::primary_mac();ctx.model="AppleTV3,2";ctx.pi="";
  static const char* hex="0123456789abcdef";for(size_t i=0;i<16;++i){auto b=id->public_key()[i];ctx.pi+=hex[b>>4];ctx.pi+=hex[b&15];}
  ctx.features="0x5A7FFEE6,0x400";ctx.srcvers="220.68";ctx.public_key=id->public_key();ctx.identity=id.get();ctx.live=&live;
  renderer.set_live_settings(&live);renderer.set_disconnect_handler([this]{disconnect=true;});
  if(!renderer.start()){std::lock_guard lock(mutex);status.state=State::Error;status.message="The native video renderer could not initialize.";renderer.stop();ap::net::global_shutdown();return;}
  ctx.renderer=&renderer;ap::airplay::Server server;ap::mdns::MdnsService mdns;
  bool listening=server.start(ctx,port),advertised=false;
  if(listening)advertised=mdns.start(ctx,server.port());
  std::string address=ap::net::primary_ipv4();auto network_notice=network_policy_notice();
  {std::lock_guard lock(mutex);status.port=server.port();status.address=address;status.name=ctx.name;status.advertised=advertised;status.network_notice=network_notice;status.state=listening&&advertised?State::Discoverable:State::Error;status.message=!listening?"AirPlay could not listen on TCP port "+std::to_string(port)+". Another receiver may be using it.":!advertised?"The AirPlay listener started, but Windows could not advertise it. Use Retry discovery and check your network.":"";}
  auto next_check=std::chrono::steady_clock::now()+std::chrono::seconds(2);bool have_video=false;uint64_t previous_frames=0;std::string session_id;
  while(running){
   if(disconnect.exchange(false)){std::lock_guard lock(mutex);status.state=State::Disconnecting;}
   bool disc;{std::lock_guard lock(mutex);disc=status.state==State::Disconnecting;}if(disc){server.disconnect_clients();renderer.clear_session();renderer.clear_active_device();}
   auto now=std::chrono::steady_clock::now();if(now>=next_check||readvertise.load()||live.receiver_name()!=ctx.name){
    auto addr=ap::net::primary_ipv4();if(readvertise.exchange(false)||addr!=address||ctx.name!=live.receiver_name()){
     mdns.stop();address=addr;ctx.name=live.receiver_name();advertised=listening&&mdns.start(ctx,server.port());std::lock_guard lock(mutex);status.advertised=advertised;status.address=address;status.name=ctx.name;status.message=advertised?"":"Discovery failed. Check the local network and firewall.";
    }next_check=now+std::chrono::seconds(2);
   }
   ap::video::VideoRenderer::DeviceInfo d;bool active=renderer.active_device_snapshot(d);auto v=renderer.statistics();
   {std::lock_guard lock(mutex);status.video=v;status.device=d.name.empty()?d.model:d.name;
    if(!active){have_video=false;session_id.clear();status.state=v.connecting?State::Connecting:listening&&advertised?State::Discoverable:State::Error;}else{if(session_id!=d.session_id){session_id=d.session_id;have_video=false;previous_frames=v.decoded;}if(v.has_video)have_video=true;status.state=d.kind=="Audio"?State::AudioOnly:have_video?State::Mirroring:State::Connected;}
   }
   std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  mdns.stop();server.stop();renderer.stop();ap::net::global_shutdown();{std::lock_guard lock(mutex);status.state=State::Idle;status.advertised=false;}
 }
};
Receiver::Receiver():p(std::make_unique<Impl>()){}
Receiver::~Receiver(){stop();}
void Receiver::start(const Preferences& pref,uint16_t port){if(p->running)return;configure(pref);p->running=true;p->worker=std::thread([this,port]{p->run(port);});}
void Receiver::stop(){p->running=false;if(p->worker.joinable())p->worker.join();}
void Receiver::configure(const Preferences& pref){{std::lock_guard lock(p->mutex);p->preferences=pref;}apply_preferences(pref,p->live);p->renderer.set_recording_options(pref.folder,pref.codec,pref.encoder,pref.bitrate);p->renderer.set_statistics_visible(pref.stats);}
void Receiver::disconnect(){p->disconnect=true;}
void Receiver::restart_discovery(){p->readvertise=true;}
ReceiverStatus Receiver::status()const{std::lock_guard lock(p->mutex);return p->status;}
ap::video::VideoRenderer& Receiver::renderer(){return p->renderer;}
}
