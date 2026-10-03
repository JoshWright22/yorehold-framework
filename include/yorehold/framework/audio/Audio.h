#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace yh
{

// Immutable 48 kHz stereo floats. Shared by voices so loading/playing never copies a clip.
struct Sound
{
    std::vector<float> samples;
    double seconds() const { return static_cast<double>(samples.size()) / 96000; }
    static std::shared_ptr<Sound> fromWav(std::span<const unsigned char> bytes, std::string* error = nullptr);
    static std::shared_ptr<Sound> tone(float hertz, float seconds);
};

enum class AudioBus { Effects, Music, Ui };

// Main-thread mixer feeding an SDL3 audio stream. update() keeps a 50 ms queue filled.
// Missing audio hardware is a recoverable init() failure. Supports overlapping voices,
// looping, pitch, stereo pan, per-bus gain and master gain. stop() has at most 50 ms latency.
class Audio
{
public:
    using VoiceId = uint64_t;
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    bool init(std::string* error = nullptr);
    void close();
    VoiceId play(std::shared_ptr<const Sound> sound, AudioBus bus = AudioBus::Effects,
        float volume = 1, bool loop = false, float pan = 0, float pitch = 1);
    void stop(VoiceId id);
    void stopAll();
    bool playing(VoiceId id) const;
    size_t voices() const;
    void setVolume(AudioBus bus, float volume);
    void setMasterVolume(float volume);
    // Device-free mixing supports deterministic tests and offline previews.
    void mix(std::span<float> stereoSamples);
    bool update();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
