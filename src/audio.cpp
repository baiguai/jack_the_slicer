#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "audio.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <vector>

namespace jack {
namespace {

struct DecodedWav {
  std::vector<std::uint8_t> bytes;
  ma_format format = ma_format_s16;
  ma_uint32 channels = 0;
  ma_uint32 sample_rate = 0;
};

// Decodes |path| fully into memory as interleaved PCM frames.
bool DecodeWav(const std::filesystem::path& path, DecodedWav& out) {
  ma_decoder decoder;
  ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 2, 0);
  if (ma_decoder_init_file(path.string().c_str(), &config, &decoder) !=
      MA_SUCCESS) {
    return false;
  }

  out.format = decoder.outputFormat;
  out.channels = decoder.outputChannels;
  out.sample_rate = decoder.outputSampleRate;

  std::vector<std::uint8_t> buffer;
  std::vector<std::uint8_t> chunk(4096 * 2 * sizeof(ma_int16));
  const ma_uint32 bytes_per_frame =
      ma_get_bytes_per_frame(decoder.outputFormat, decoder.outputChannels);
  for (;;) {
    ma_uint64 frames_read = 0;
    const ma_result result = ma_decoder_read_pcm_frames(
        &decoder, chunk.data(), chunk.size() / bytes_per_frame, &frames_read);
    if (result != MA_SUCCESS || frames_read == 0) {
      break;
    }
    buffer.insert(buffer.end(), chunk.begin(),
                  chunk.begin() + frames_read * bytes_per_frame);
  }

  ma_decoder_uninit(&decoder);

  if (buffer.empty() || out.channels == 0) {
    return false;
  }
  out.bytes = std::move(buffer);
  return true;
}

}  // namespace

struct WavPlayer::Impl {
  ma_device device{};
  bool device_started = false;

  // Only mutated while the audio device is stopped, so the audio thread never
  // races on them.
  std::vector<std::uint8_t> bytes;
  ma_format format = ma_format_s16;
  ma_uint32 channels = 0;
  bool loop = false;
  size_t frame_index = 0;
  std::string current_file;

  // Shared with the audio thread via atomics.
  std::atomic<bool> playing{false};
  std::atomic<bool> finished{true};
};

namespace {

void DataCallback(ma_device* device, void* p_output, const void* /*p_input*/,
                  ma_uint32 frame_count) {
  auto* self = static_cast<WavPlayer::Impl*>(device->pUserData);

  if (self->bytes.empty() || self->channels == 0) {
    std::memset(p_output, 0,
                frame_count * ma_get_bytes_per_frame(self->format,
                                                     self->channels));
    self->finished = true;
    self->playing = false;
    return;
  }

  const ma_uint32 bytes_per_frame =
      ma_get_bytes_per_frame(self->format, self->channels);
  const size_t total_frames = self->bytes.size() / bytes_per_frame;
  auto* out = static_cast<std::uint8_t*>(p_output);
  size_t written = 0;

  while (written < frame_count) {
    if (self->frame_index >= total_frames) {
      if (self->loop) {
        self->frame_index = 0;
      } else {
        break;
      }
    }
    const size_t available = total_frames - self->frame_index;
    const size_t count =
        std::min<size_t>(available, frame_count - written);
    std::memcpy(out + written * bytes_per_frame,
                self->bytes.data() + self->frame_index * bytes_per_frame,
                count * bytes_per_frame);
    self->frame_index += count;
    written += count;
  }

  if (written < frame_count) {
    std::memset(out + written * bytes_per_frame, 0,
                (frame_count - written) * bytes_per_frame);
    self->finished = true;
    self->playing = false;
  }
}

}  // namespace

WavPlayer::WavPlayer() : impl_(std::make_unique<Impl>()) {}

WavPlayer::~WavPlayer() {
  Stop();
}

bool WavPlayer::Play(const std::filesystem::path& path, bool loop) {
  Stop();

  DecodedWav decoded;
  if (!DecodeWav(path, decoded)) {
    impl_->playing = false;
    impl_->finished = true;
    return false;
  }

  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format = decoded.format;
  config.playback.channels = decoded.channels;
  config.sampleRate = decoded.sample_rate;
  config.dataCallback = DataCallback;
  config.pUserData = impl_.get();

  if (ma_device_init(nullptr, &config, &impl_->device) != MA_SUCCESS) {
    impl_->playing = false;
    impl_->finished = true;
    return false;
  }
  impl_->device_started = true;

  // The device is stopped here, so the audio thread is idle while we publish
  // the new buffer. It only reads these once ma_device_start() lets it run.
  impl_->bytes = std::move(decoded.bytes);
  impl_->format = decoded.format;
  impl_->channels = decoded.channels;
  impl_->loop = loop;
  impl_->frame_index = 0;
  impl_->current_file = path.string();

  if (ma_device_start(&impl_->device) != MA_SUCCESS) {
    ma_device_uninit(&impl_->device);
    impl_->device_started = false;
    impl_->playing = false;
    impl_->finished = true;
    return false;
  }
  impl_->playing = true;
  impl_->finished = false;
  return true;
}

void WavPlayer::Stop() {
  if (impl_->device_started) {
    ma_device_uninit(&impl_->device);  // stops playback and waits for the thread
    impl_->device_started = false;
  }
  impl_->bytes.clear();
  impl_->channels = 0;
  impl_->loop = false;
  impl_->frame_index = 0;
  impl_->current_file.clear();
  impl_->playing = false;
  impl_->finished = true;
}

void WavPlayer::Poll() {
  if (impl_->device_started && impl_->finished.load()) {
    ma_device_stop(&impl_->device);
    impl_->device_started = false;
  }
}

bool WavPlayer::IsPlaying() const {
  return impl_->playing.load();
}

bool WavPlayer::IsLooping() const {
  return impl_->loop;
}

const std::string& WavPlayer::CurrentFile() const {
  return impl_->current_file;
}

}  // namespace jack