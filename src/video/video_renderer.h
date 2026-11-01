#pragma once
#include "airplay/media_control.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <cstdint>
#include <windows.h>
extern "C" { struct AVFrame;struct AVBufferRef; }
namespace ap::airplay {struct LiveSettings;}
namespace ap::video {
class VideoRenderer {
public:
 struct DeviceInfo {std::string peer_ip,session_id,kind,name,model;};
 struct Statistics {uint64_t received=0,decoded=0,presented=0,rendered=0,dropped=0,bytes=0;int width=0,height=0,queue=0,connecting=0;long presentation_result=0;double received_fps=0,decoded_fps=0,presented_fps=0,mbps=0,latency_ms=0;bool gpu=false,recording=false,has_video=false;std::string codec="Unknown",error,recording_path;};
 VideoRenderer();~VideoRenderer();
 bool start(const std::string& title="AirConnect");void stop();void request_show();bool user_closed()const{return false;}
 void push_avframe(const AVFrame*,int64_t origin_ns=0);
 void push_frame(const uint8_t*,int,const uint8_t*,int,const uint8_t*,int,int,int,int64_t=0);
 void push_frame_nv12(const uint8_t*,int,const uint8_t*,int,int,int,int64_t=0);
 AVBufferRef* gpu_device_ref();
 void record_payload_bytes(std::size_t n);void push_audio_pcm(const int16_t*,int,int,int);
 void record_received_frame();
 void note_connection(int change);
 void push_cover_art(const uint8_t*,std::size_t);void push_metadata(const std::string&,const std::string&,const std::string&);
 void push_progress(uint32_t,uint32_t);void push_playback_rate(float);
 void clear_session();void set_idle_info(const std::string&,const std::string&);
 void set_active_device(DeviceInfo);void clear_active_device();bool clear_session_if_active(const std::string&);bool active_device_snapshot(DeviceInfo&)const;
 void note_flush();bool in_flush_grace()const;
 void set_live_settings(ap::airplay::LiveSettings*);
 void set_disconnect_handler(std::function<void()>);
 void set_audio_control_handler(std::function<void(ap::airplay::MediaCommand,uint32_t)>);
 void set_audio_control_available(bool){}void audio_command_finished(bool){}
 Statistics statistics()const;
 void set_codec(bool hevc);
 void request_fullscreen();void request_pip();void request_screenshot();void request_recording();void set_statistics_visible(bool);
 void set_recording_options(const std::wstring& folder,int codec,int encoder,int bitrate);
 HWND window()const;
 bool save_test_capture(const std::wstring& path);
private:
 struct Impl;std::unique_ptr<Impl> p;
};
}
