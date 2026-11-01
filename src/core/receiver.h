#pragma once
#include "core/preferences.h"
#include "video/video_renderer.h"
#include <memory>
namespace ac {
enum class State {Idle,Discoverable,Connecting,Connected,Mirroring,AudioOnly,Disconnecting,Error};
struct ReceiverStatus {State state=State::Idle;bool advertised=false;std::string name,address,device,message,network_notice;uint16_t port=7000;ap::video::VideoRenderer::Statistics video;};
class Receiver {
public:
 Receiver();~Receiver();void start(const Preferences&,uint16_t port=7000);void stop();
 void configure(const Preferences&);void disconnect();void restart_discovery();ReceiverStatus status()const;
 ap::video::VideoRenderer& renderer();
private:struct Impl;std::unique_ptr<Impl> p;
};
const wchar_t* state_text(State state);
}
