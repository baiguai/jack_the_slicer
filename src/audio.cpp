#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#include "audio.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

namespace jack {
namespace {

struct DecodedWav {
  std::vector<std::uint8_t> bytes;
  ma_format format = ma_format_s16;
  ma_uint32 channels = 0;
  ma_uint32 sample_rate = 0;
};

// Decodes |path| fully into memory as interleaved PCM frames. When |channels|
// is 0 the source's native channel count is preserved, otherwise the audio is
// converted to that many channels.
bool DecodeWav(const std::filesystem::path& path, DecodedWav& out,
               ma_uint32 channels) {
  ma_decoder decoder;
  ma_decoder_config config = ma_decoder_config_init(ma_format_s16, channels, 0);
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

// Writes interleaved 16-bit PCM samples as a standard RIFF/WAVE file.
bool WriteWavPcm(const std::filesystem::path& path, ma_uint32 channels,
                 ma_uint32 sample_rate,
                 const std::vector<std::uint8_t>& pcm_bytes) {
  const ma_uint32 data_size = static_cast<ma_uint32>(pcm_bytes.size());
  const ma_uint32 byte_rate = sample_rate * channels *
                              static_cast<ma_uint32>(sizeof(ma_int16));
  const ma_uint16 block_align =
      static_cast<ma_uint16>(channels * sizeof(ma_int16));
  const size_t total_size = 44 + data_size;

  std::vector<std::uint8_t> out;
  out.reserve(44 + pcm_bytes.size());
  auto put_bytes = [&](const char* bytes, size_t n) {
    out.insert(out.end(), bytes, bytes + n);
  };
  auto put_u32 = [&](ma_uint32 v) {
    out.push_back(v & 0xFF);
    out.push_back((v >> 8) & 0xFF);
    out.push_back((v >> 16) & 0xFF);
    out.push_back((v >> 24) & 0xFF);
  };
  auto put_u16 = [&](ma_uint16 v) {
    out.push_back(v & 0xFF);
    out.push_back(v >> 8);
  };

  put_bytes("RIFF", 4);
  put_u32(static_cast<ma_uint32>(total_size - 8));
  put_bytes("WAVE", 4);
  put_bytes("fmt ", 4);
  put_u32(16);                                // fmt chunk size
  put_u16(1);                                 // PCM
  put_u16(static_cast<ma_uint16>(channels));
  put_u32(sample_rate);
  put_u32(byte_rate);
  put_u16(block_align);
  put_u16(16);                                // bits per sample
  put_bytes("data", 4);
  put_u32(data_size);
  out.insert(out.end(), pcm_bytes.begin(), pcm_bytes.end());

  std::ofstream file(path, std::ios::binary);
  if (!file) {
    return false;
  }
  file.write(reinterpret_cast<const char*>(out.data()),
             static_cast<std::streamsize>(out.size()));
  return file.good();
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
  if (!DecodeWav(path, decoded, 2)) {
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

bool SliceWav(const std::filesystem::path& src, const std::filesystem::path& dst,
              int chunks, const std::vector<int>& effects, std::uint32_t seed) {
  if (chunks <= 0) {
    return false;
  }

  DecodedWav decoded;
  if (!DecodeWav(src, decoded, 0)) {
    return false;
  }

  const ma_uint32 bytes_per_frame =
      ma_get_bytes_per_frame(decoded.format, decoded.channels);
  const size_t total_frames = decoded.bytes.size() / bytes_per_frame;
  const size_t frames_per_chunk = total_frames / static_cast<size_t>(chunks);
  if (frames_per_chunk == 0) {
    return false;
  }
  const size_t chunk_bytes = frames_per_chunk * bytes_per_frame;

  const auto effect_of = [&](int chunk) -> int {
    if (chunk < 0 || chunk >= static_cast<int>(effects.size())) {
      return kEffectNone;
    }
    return effects[static_cast<size_t>(chunk)];
  };

  std::mt19937 rng(seed);
  std::vector<std::uint8_t> out;
  out.reserve(chunk_bytes * static_cast<size_t>(chunks));
  for (int chunk = 0; chunk < chunks; ++chunk) {
    const int effect = effect_of(chunk);
    int source = chunk;
    if (effect == kEffectShuffle && chunks > 1) {
      source = static_cast<int>(rng() % static_cast<unsigned>(chunks));
    }
    const size_t offset = static_cast<size_t>(source) * chunk_bytes;
    if (effect == kEffectReverse) {
      // Copy the frames in reverse order, keeping each frame's bytes intact.
      for (size_t f = 0; f < frames_per_chunk; ++f) {
        const size_t frame_offset =
            (frames_per_chunk - 1 - f) * bytes_per_frame;
        out.insert(out.end(), decoded.bytes.begin() + offset + frame_offset,
                   decoded.bytes.begin() + offset + frame_offset +
                       bytes_per_frame);
      }
    } else {
      out.insert(out.end(), decoded.bytes.begin() + offset,
                 decoded.bytes.begin() + offset + chunk_bytes);
    }
  }

  return WriteWavPcm(dst, decoded.channels, decoded.sample_rate, out);
}

}  // namespace jack