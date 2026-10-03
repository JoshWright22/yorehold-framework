#include "yorehold/framework/audio/Audio.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace yh
{

std::shared_ptr<Sound> Sound::fromWav(std::span<const unsigned char> bytes, std::string* error)
{
    if (error) error->clear();
    SDL_AudioSpec source{};
    Uint8* data = nullptr;
    Uint32 length = 0;
    SDL_IOStream* stream = bytes.empty() ? nullptr : SDL_IOFromConstMem(bytes.data(), bytes.size());
    if (!stream || !SDL_LoadWAV_IO(stream, true, &source, &data, &length))
    {
        if (error) *error = SDL_GetError();
        return nullptr;
    }
    const SDL_AudioSpec target{SDL_AUDIO_F32, 2, 48000};
    Uint8* converted = nullptr;
    int convertedLength = 0;
    const bool ok = length <= static_cast<Uint32>(INT_MAX)
        && SDL_ConvertAudioSamples(&source, data, static_cast<int>(length), &target, &converted, &convertedLength);
    SDL_free(data);
    if (!ok)
    {
        if (error) *error = SDL_GetError();
        return nullptr;
    }
    auto sound = std::make_shared<Sound>();
    const auto* floats = reinterpret_cast<const float*>(converted);
    sound->samples.assign(floats, floats + convertedLength / sizeof(float));
    SDL_free(converted);
    return sound;
}

std::shared_ptr<Sound> Sound::tone(float hertz, float seconds)
{
    if (!std::isfinite(hertz) || hertz <= 0 || hertz > 24000 || !std::isfinite(seconds) || seconds <= 0 || seconds > 60)
        throw std::invalid_argument("Invalid tone frequency or duration");
    auto sound = std::make_shared<Sound>();
    const size_t frames = static_cast<size_t>(seconds * 48000);
    sound->samples.resize(frames * 2);
    for (size_t i = 0; i < frames; ++i)
    {
        const float fade = std::min({1.0f, static_cast<float>(i) / 480, static_cast<float>(frames - i) / 480});
        const float value = 0.25f * fade * std::sin(static_cast<float>(i) * hertz * 6.2831853f / 48000);
        sound->samples[i * 2] = sound->samples[i * 2 + 1] = value;
    }
    return sound;
}

struct Audio::Impl
{
    struct Voice
    {
        VoiceId id;
        std::shared_ptr<const Sound> sound;
        AudioBus bus;
        float volume, pan, pitch;
        bool loop;
        double cursor = 0;
    };
    std::vector<Voice> voices;
    std::array<float, 3> gains{1, 1, 1};
    float master = 1;
    SDL_AudioStream* stream = nullptr;
    bool initialized = false;
    VoiceId nextId = 1;
    std::array<float, 960> buffer{};
};

Audio::Audio() : impl_(std::make_unique<Impl>()) { impl_->voices.reserve(64); }
Audio::~Audio() { close(); }

bool Audio::init(std::string* error)
{
    if (error) error->clear();
    if (impl_->stream) return true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) { if (error) *error = SDL_GetError(); return false; }
    impl_->initialized = true;
    const SDL_AudioSpec spec{SDL_AUDIO_F32, 2, 48000};
    impl_->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
    if (!impl_->stream || !SDL_ResumeAudioStreamDevice(impl_->stream))
    {
        if (error) *error = SDL_GetError();
        close();
        return false;
    }
    return true;
}

void Audio::close()
{
    if (impl_->stream && SDL_WasInit(SDL_INIT_AUDIO)) SDL_DestroyAudioStream(impl_->stream);
    impl_->stream = nullptr;
    if (impl_->initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    impl_->initialized = false;
    impl_->voices.clear();
}

Audio::VoiceId Audio::play(std::shared_ptr<const Sound> sound, AudioBus bus, float volume, bool loop, float pan, float pitch)
{
    if (!sound || sound->samples.empty() || sound->samples.size() % 2 != 0 || !std::isfinite(volume)
        || !std::isfinite(pan) || !std::isfinite(pitch) || pitch <= 0 || pitch > 8 || static_cast<size_t>(bus) >= 3)
        return 0;
    if (impl_->voices.size() >= 64 || impl_->nextId == 0) return 0;
    const VoiceId id = impl_->nextId++;
    impl_->voices.push_back({id, std::move(sound), bus, std::clamp(volume, 0.0f, 4.0f), std::clamp(pan, -1.0f, 1.0f), pitch, loop});
    return id;
}

void Audio::stop(VoiceId id) { std::erase_if(impl_->voices, [id](const auto& v) { return v.id == id; }); }
void Audio::stopAll()
{
    impl_->voices.clear();
    if (impl_->stream) SDL_ClearAudioStream(impl_->stream);
}
bool Audio::playing(VoiceId id) const { return std::any_of(impl_->voices.begin(), impl_->voices.end(), [id](const auto& v) { return v.id == id; }); }
size_t Audio::voices() const { return impl_->voices.size(); }
void Audio::setVolume(AudioBus bus, float volume)
{
    if (static_cast<size_t>(bus) < 3 && std::isfinite(volume)) impl_->gains[static_cast<size_t>(bus)] = std::clamp(volume, 0.0f, 1.0f);
}
void Audio::setMasterVolume(float volume) { if (std::isfinite(volume)) impl_->master = std::clamp(volume, 0.0f, 1.0f); }

void Audio::mix(std::span<float> out)
{
    if (out.size() % 2 != 0) throw std::invalid_argument("Audio output must be stereo");
    std::fill(out.begin(), out.end(), 0.0f);
    for (auto& v : impl_->voices)
    {
        const size_t frames = v.sound->samples.size() / 2;
        const float gain = v.volume * impl_->gains[static_cast<size_t>(v.bus)] * impl_->master;
        const float left = gain * std::min(1.0f, 1 - v.pan), right = gain * std::min(1.0f, 1 + v.pan);
        for (size_t i = 0; i < out.size() / 2; ++i)
        {
            if (v.cursor >= frames)
            {
                if (v.loop) v.cursor = std::fmod(v.cursor, static_cast<double>(frames));
                else break;
            }
            const size_t a = static_cast<size_t>(v.cursor);
            const size_t b = a + 1 < frames ? a + 1 : v.loop ? 0 : a;
            const float fraction = static_cast<float>(v.cursor - a);
            const auto& s = v.sound->samples;
            out[i * 2] += (s[a * 2] + (s[b * 2] - s[a * 2]) * fraction) * left;
            out[i * 2 + 1] += (s[a * 2 + 1] + (s[b * 2 + 1] - s[a * 2 + 1]) * fraction) * right;
            v.cursor += v.pitch;
        }
    }
    std::erase_if(impl_->voices, [](const auto& v) { return !v.loop && v.cursor >= v.sound->samples.size() / 2; });
    for (float& sample : out) sample = std::clamp(sample, -1.0f, 1.0f);
}

bool Audio::update()
{
    if (!impl_->stream) return false;
    const int queued = SDL_GetAudioStreamQueued(impl_->stream);
    if (queued < 0) return false;
    constexpr int blockBytes = 960 * sizeof(float);
    const int blocks = std::clamp((2400 * 8 - queued + blockBytes - 1) / blockBytes, 0, 5);
    for (int i = 0; i < blocks; ++i)
    {
        mix(impl_->buffer);
        if (!SDL_PutAudioStreamData(impl_->stream, impl_->buffer.data(), blockBytes)) return false;
    }
    return true;
}

}
