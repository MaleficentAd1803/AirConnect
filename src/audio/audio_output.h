#pragma once
#include <memory>
#include <cstdint>
namespace ap::audio {
// Historical class name retained at the protocol boundary; implementation is WASAPI.
class SdlAudioOutput {
public:
 SdlAudioOutput();~SdlAudioOutput();
 bool start(int rate,int channels);void stop();void push(const int16_t*,int);
 void set_volume_db(float);uint32_t queued_bytes()const;int64_t pending_ns()const;void clear();
 bool initialized() const;
private:struct Impl;std::unique_ptr<Impl> p;
};
}
