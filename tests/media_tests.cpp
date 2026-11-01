#include "video/video_renderer.h"
#include "video/video_recorder.h"
#include "video/h264_decoder.h"
#include "audio/audio_output.h"
#include "core/preferences.h"
#include "log.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <cstring>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}
using namespace std::chrono_literals;
void require(bool v, const char *message) {
    if (!v) {
        std::cerr << "FAIL " << message << "\n";
        std::exit(1);
    }
}
std::vector<uint8_t> encode_fixture(AVCodecID id, int w, int h) {
    const AVCodec *codec = avcodec_find_encoder_by_name(id == AV_CODEC_ID_HEVC ? "hevc_nvenc" : "h264_nvenc");
    require(codec != nullptr, "NVENC test encoder unavailable");
    AVCodecContext *enc = avcodec_alloc_context3(codec);
    enc->width = w;
    enc->height = h;
    enc->pix_fmt = AV_PIX_FMT_NV12;
    enc->time_base = {1, 60};
    enc->framerate = {60, 1};
    enc->max_b_frames = 0;
    enc->gop_size = 1;
    enc->bit_rate = 2'000'000;
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "preset", "p1", 0);
    av_dict_set(&opts, "tune", "ull", 0);
    av_dict_set(&opts, "zerolatency", "1", 0);
    require(avcodec_open2(enc, codec, &opts) >= 0, "Test encoder open failed");
    av_dict_free(&opts);
    AVFrame *f = av_frame_alloc();
    f->format = enc->pix_fmt;
    f->width = w;
    f->height = h;
    require(av_frame_get_buffer(f, 32) >= 0, "Encoder frame allocation failed");
    for (int y = 0; y < h; ++y) {
        std::memset(f->data[0] + y * f->linesize[0], 16, w / 2);
        std::memset(f->data[0] + y * f->linesize[0] + w / 2, 235, w / 2);
    }
    for (int y = 0; y < h / 2; ++y)
        std::memset(f->data[1] + y * f->linesize[1], 128, w);
    require(avcodec_send_frame(enc, f) >= 0, "Test encode failed");
    avcodec_send_frame(enc, nullptr);
    AVPacket *packet = av_packet_alloc();
    require(avcodec_receive_packet(enc, packet) >= 0, "Test encoded packet missing");
    std::vector<uint8_t> result(packet->data, packet->data + packet->size);
    av_packet_free(&packet);
    av_frame_free(&f);
    avcodec_free_context(&enc);
    return result;
}

std::vector<uint8_t> fixture_config(const std::vector<uint8_t> &bytes, AVCodecID id) {
    std::vector<std::vector<uint8_t>> nals;
    std::size_t start = 0;
    for (std::size_t i = 0; i + 3 <= bytes.size();) {
        std::size_t prefix = 0;
        if (i + 4 <= bytes.size() && bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 0 &&
            bytes[i + 3] == 1)
            prefix = 4;
        else if (bytes[i] == 0 && bytes[i + 1] == 0 && bytes[i + 2] == 1)
            prefix = 3;
        if (prefix) {
            if (start)
                nals.emplace_back(bytes.begin() + start, bytes.begin() + i);
            start = i + prefix;
            i = start;
        } else
            ++i;
    }
    if (start)
        nals.emplace_back(bytes.begin() + start, bytes.end());
    auto find = [&](int type) {
        for (const auto &nal : nals)
            if ((id == AV_CODEC_ID_HEVC ? (nal[0] >> 1) & 63 : nal[0] & 31) == type)
                return nal;
        throw std::runtime_error("Encoded fixture parameter set missing");
    };
    auto append = [](std::vector<uint8_t> &dst, const std::vector<uint8_t> &nal) {
        dst.push_back(static_cast<uint8_t>(nal.size() >> 8));
        dst.push_back(static_cast<uint8_t>(nal.size()));
        dst.insert(dst.end(), nal.begin(), nal.end());
    };
    if (id == AV_CODEC_ID_H264) {
        const auto sps = find(7);
        std::vector<uint8_t> config{1, sps[1], sps[2], sps[3], 255, 225};
        append(config, sps);
        config.push_back(1);
        append(config, find(8));
        return config;
    }
    std::vector<uint8_t> config(23, 0);
    config[0] = 1;
    config[22] = 3;
    for (int type : {32, 33, 34}) {
        config.push_back(static_cast<uint8_t>(128 | type));
        config.push_back(0);
        config.push_back(1);
        append(config, find(type));
    }
    return config;
}

void check_aspect(ap::video::VideoRenderer &renderer, int width, int height) {
    RECT r{};
    require(GetClientRect(renderer.window(), &r), "viewer client rectangle available");
    require(std::abs(double(r.right) / r.bottom - double(width) / height) < .006,
            "actual viewer follows incoming phone aspect ratio");
    require(GetDlgItem(renderer.window(), 110) != nullptr && GetDlgItem(renderer.window(), 100) == nullptr,
            "viewer has one menu instead of a toolbar");
}
struct Stream {
    AVCodecContext *enc = nullptr;
    AVFrame *frame = nullptr;
    AVPacket *packet = nullptr;
    Stream(int w, int h) {
        auto *codec = avcodec_find_encoder_by_name("libx264");
        require(codec, "H.264 encoder available");
        enc = avcodec_alloc_context3(codec);
        enc->width = w;
        enc->height = h;
        enc->pix_fmt = AV_PIX_FMT_YUV420P;
        enc->time_base = {1, 60};
        enc->framerate = {60, 1};
        enc->gop_size = 30;
        enc->max_b_frames = 0;
        AVDictionary *opts = nullptr;
        av_dict_set(&opts, "preset", "ultrafast", 0);
        av_dict_set(&opts, "tune", "zerolatency", 0);
        av_dict_set(&opts, "repeat-headers", "1", 0);
        require(avcodec_open2(enc, codec, &opts) >= 0, "H.264 encoder opened");
        av_dict_free(&opts);
        frame = av_frame_alloc();
        frame->width = w;
        frame->height = h;
        frame->format = enc->pix_fmt;
        frame->color_range = AVCOL_RANGE_MPEG;
        frame->colorspace = AVCOL_SPC_BT709;
        require(av_frame_get_buffer(frame, 32) >= 0, "test picture allocated");
        packet = av_packet_alloc();
    }
    ~Stream() {
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&enc);
    }
    void send(int index, ap::video::H264Decoder &decoder, ap::video::VideoRenderer &renderer,
              ap::video::VideoRecorder *recorder = nullptr) {
        require(av_frame_make_writable(frame) >= 0, "test frame writable");
        for (int y = 0; y < frame->height; ++y)
            for (int x = 0; x < frame->width; ++x)
                frame->data[0][y * frame->linesize[0] + x] =
                    (uint8_t)(32 + (x * 180 / frame->width) + (index % 10));
        for (int plane = 1; plane < 3; ++plane)
            for (int y = 0; y < frame->height / 2; ++y)
                memset(frame->data[plane] + y * frame->linesize[plane], plane == 1 ? 92 : 174,
                       frame->width / 2);
        frame->pts = index;
        require(avcodec_send_frame(enc, frame) >= 0, "encode frame");
        while (avcodec_receive_packet(enc, packet) >= 0) {
            bool got = false;
            int w = 0, h = 0;
            auto origin = ac::now_ns();
            renderer.record_payload_bytes(packet->size);
            renderer.record_received_frame();
            require(
                decoder.decode(packet->data, packet->size, (packet->flags & AV_PKT_FLAG_KEY) != 0, got, w, h),
                "decode packet");
            if (got) {
                auto *decoded = decoder.current_avframe();
                require(decoded && decoded->width == frame->width && decoded->height == frame->height,
                        "decoded dimensions");
                renderer.push_avframe(decoded, origin);
                if (recorder)
                    recorder->submit(decoded);
            }
            av_packet_unref(packet);
        }
    }
};
void inspect_recording(const std::string &path) {
    AVFormatContext *f = nullptr;
    require(avformat_open_input(&f, path.c_str(), nullptr, nullptr) >= 0, "recorded MP4 opens");
    require(avformat_find_stream_info(f, nullptr) >= 0, "MP4 stream metadata");
    int video = av_find_best_stream(f, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0),
        audio = av_find_best_stream(f, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    require(video >= 0 && audio >= 0, "MP4 contains video and audio");
    auto *stream = f->streams[video];
    auto *codec = avcodec_find_decoder(stream->codecpar->codec_id);
    auto *c = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(c, stream->codecpar);
    require(avcodec_open2(c, codec, nullptr) >= 0, "MP4 video decoder opens");
    AVPacket *packet = av_packet_alloc();
    AVFrame *image = av_frame_alloc();
    int frames = 0;
    int64_t last_pts = INT64_MIN;
    while (av_read_frame(f, packet) >= 0) {
        if (packet->stream_index == video) {
            require(packet->pts > last_pts, "MP4 monotonic video timestamps");
            last_pts = packet->pts;
            require(avcodec_send_packet(c, packet) >= 0, "MP4 video packet accepted");
            while (avcodec_receive_frame(c, image) >= 0) {
                require(image->width == 640 && image->height == 360, "recording keeps initial canvas");
                ++frames;
            }
        }
        av_packet_unref(packet);
    }
    require(frames >= 15, "recorded pictures decoded");
    av_frame_free(&image);
    av_packet_free(&packet);
    avcodec_free_context(&c);
    avformat_close_input(&f);
    std::cout << "MP4 verified: " << frames << " decoded pictures; audio stream present\n";
}
int main() {
    ap::set_log_enabled(true);
    auto dir = std::filesystem::current_path() / L"media-test-output";
    std::filesystem::create_directories(dir);
    ac::set_data_directory(dir);
    ac::playback_muted = true;
    ap::audio::SdlAudioOutput output;
    require(output.start(44100, 2), "WASAPI worker started");
    for (int n = 0; n < 100 && !output.initialized(); ++n)
        std::this_thread::sleep_for(20ms);
    require(output.initialized(), "Windows audio output initialized");
    std::vector<int16_t> silence(4410 * 2);
    output.push(silence.data(), (int)silence.size());
    for (int n = 0; n < 50 && output.queued_bytes(); ++n)
        std::this_thread::sleep_for(20ms);
    require(output.queued_bytes() == 0, "WASAPI drains queued PCM");
    output.clear();
    output.stop();
    std::cout << "WASAPI initialization, silent PCM playback, flush and shutdown passed\n";
    ap::airplay::LiveSettings live;
    live.vsync_enabled = false;
    ap::video::VideoRenderer renderer;
    renderer.set_live_settings(&live);
    renderer.set_recording_options(dir.wstring(), 0, 0, 8);
    require(renderer.start("AirConnect media test — generated fixture"), "native renderer initialized");
    std::this_thread::sleep_for(200ms);
    auto *gpu = renderer.gpu_device_ref();
    require(gpu, "D3D11 device available");
    for (bool hw : {false, true}) {
        ap::video::H264Decoder decoder;
        require(decoder.init(hw, gpu), "H.264 decoder initialized");
        Stream stream(640, 360);
        for (int n = 0; n < 45; ++n) {
            stream.send(n, decoder, renderer);
            std::this_thread::sleep_for(17ms);
        }
        std::this_thread::sleep_for(300ms);
        check_aspect(renderer, 640, 360);
        auto s = renderer.statistics();
        std::cout << "Native frame counts: rendered " << s.rendered << ", presented " << s.presented
                  << ", DXGI result 0x" << std::hex << s.presentation_result << std::dec << "\n";
        require(s.rendered > 10, "native video processing and DXGI submission");
        require(s.presentation_result == 0 || s.presentation_result == 0x087A0001,
                "DXGI presentation status");
        require(renderer.save_test_capture(
                    (dir / (hw ? L"hardware-display.png" : L"software-display.png")).wstring()),
                "GPU presentation surface readback for test");
        require(s.error.empty(), s.error.c_str());
        require(s.gpu == hw, "actual decoder mode matches request");
        std::cout << (hw ? "D3D11" : "Software")
                  << " decoding, video processing and DXGI submission passed; DXGI presentation "
                  << (s.presented ? "accepted" : "occluded") << "\n";
    }
    ap::video::H264Decoder decoder;
    require(decoder.init(true, gpu), "recording decoder initialized");
    ap::video::VideoRecorder recorder;
    ap::video::VideoRecorder::Options opts;
    opts.encoder = ap::video::VideoRecorder::EncoderMode::Auto;
    require(recorder.start(ac::utf8(dir.wstring()), opts), "recording armed by test");
    std::vector<int16_t> tone(1470);
    for (size_t i = 0; i < tone.size() / 2; ++i)
        tone[2 * i] = tone[2 * i + 1] = (int16_t)(std::sin(i * 6.283185307 * 440 / 44100) * 1200);
    {
        Stream stream(640, 360);
        for (int n = 0; n < 60; ++n) {
            stream.send(n, decoder, renderer, &recorder);
            recorder.submit_audio(tone.data(), (int)tone.size(), 44100, 2);
            std::this_thread::sleep_for(17ms);
        }
    }
    {
        Stream portrait(360, 640);
        for (int n = 0; n < 30; ++n) {
            portrait.send(n, decoder, renderer, &recorder);
            recorder.submit_audio(tone.data(), (int)tone.size(), 44100, 2);
            std::this_thread::sleep_for(17ms);
        }
    }
    renderer.request_screenshot();
    std::this_thread::sleep_for(300ms);
    recorder.stop();
    auto rec = recorder.status();
    require(rec.error.empty(), rec.error.c_str());
    require(rec.frames >= 30, "recording wrote frames");
    require(rec.gpu_resident && rec.readback_frames == 0, "GPU recording has no CPU pixel readbacks");
    std::cout << "Recorder: " << rec.codec << "; readbacks " << rec.readback_frames << "\n";
    inspect_recording(rec.output_path);
    for (bool hardware : {false, true}) {
        ap::video::H264Decoder dec;
        require(dec.init(hardware, gpu), "codec-switch decoder initialized");
        for (auto codec : {AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_H264})
            for (auto dimensions : {std::pair<int, int>{512, 256}, {256, 512}}) {
                auto packet = encode_fixture(codec, dimensions.first, dimensions.second),
                     config = fixture_config(packet, codec);
                require(dec.set_parameter_sets_from_avcc(config.data(), config.size()),
                        "AVCC/HVCC parameter negotiation");
                for (int n = 0; n < 12; ++n) {
                    bool got = false;
                    int w = 0, h = 0;
                    require(dec.decode(packet.data(), packet.size(), true, got, w, h) && got,
                            "H.264/HEVC fixture decoded");
                    auto *frame = dec.current_avframe();
                    require(frame->format == (hardware ? AV_PIX_FMT_D3D11 : AV_PIX_FMT_YUV420P),
                            "requested hardware/software decoder format");
                    AVFrame *pixels = av_frame_alloc();
                    const AVFrame *image = frame;
                    if (hardware) {
                        require(av_hwframe_transfer_data(pixels, frame, 0) >= 0,
                                "test-only decoded pixel verification");
                        image = pixels;
                    }
                    require(image->data[0][(h / 2) * image->linesize[0] + w / 4] < 40 &&
                                image->data[0][(h / 2) * image->linesize[0] + 3 * w / 4] > 210,
                            "decoded full/limited-range fixture colors");
                    av_frame_free(&pixels);
                    renderer.set_codec(codec == AV_CODEC_ID_HEVC);
                    renderer.push_avframe(frame, ac::now_ns());
                    std::this_thread::sleep_for(17ms);
                }
            }
        check_aspect(renderer, 256, 512);
        std::cout << (hardware ? "D3D11" : "Software")
                  << " H.264/HEVC switching, portrait/landscape and decoded pixels passed\n";
    }
    int screenshots = 0;
    for (auto &file : std::filesystem::directory_iterator(dir))
        if (file.path().extension() == L".png")
            ++screenshots;
    require(screenshots > 0, "native-resolution PNG screenshot saved");
    // Keep the fixture window hidden so real desktop pointer activity cannot reset
    // its idle timer. Inspect the child's own style, independently of its parent.
    ShowWindow(renderer.window(), SW_HIDE);
    std::this_thread::sleep_for(3300ms);
    auto menu_visible = [&] {
        return (GetWindowLongPtrW(GetDlgItem(renderer.window(), 110), GWL_STYLE) & WS_VISIBLE) != 0;
    };
    require(!menu_visible(), "viewer menu hides while inactive");
    SendMessageW(renderer.window(), WM_MOUSEMOVE, 0, 0);
    require(menu_visible(), "pointer activity reveals viewer menu");
    renderer.request_fullscreen();
    std::this_thread::sleep_for(100ms);
    renderer.request_fullscreen();
    renderer.request_pip();
    std::this_thread::sleep_for(100ms);
    renderer.request_pip();
    std::this_thread::sleep_for(100ms);
    check_aspect(renderer, 256, 512);
    renderer.stop();
    av_buffer_unref(&gpu);
    std::cout << "Orientation, fullscreen, PiP, PNG capture and shutdown passed\n";
    _putenv_s("AIRCONNECT_FORCE_WARP", "1");
    ap::video::VideoRenderer warp;
    warp.set_live_settings(&live);
    require(warp.start("AirConnect WARP fallback test"), "WARP fallback initialized");
    ap::video::H264Decoder cpu;
    require(cpu.init(false), "WARP software decoder initialized");
    {
        Stream fixture(640, 360);
        for (int n = 0; n < 20; ++n) {
            fixture.send(n, cpu, warp);
            std::this_thread::sleep_for(20ms);
        }
    }
    std::this_thread::sleep_for(300ms);
    auto fallback = warp.statistics();
    require(fallback.error.empty(), fallback.error.c_str());
    require(fallback.rendered > 5 && !fallback.gpu,
            "WARP/Direct2D CPU display works without a video processor");
    require(warp.save_test_capture((dir / L"warp-display.png").wstring()),
            "WARP presentation pixels captured");
    warp.stop();
    _putenv_s("AIRCONNECT_FORCE_WARP", "");
    std::cout << "WARP/Direct2D software presentation fallback passed\n";
    return 0;
}
