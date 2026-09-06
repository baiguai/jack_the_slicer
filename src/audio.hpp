#ifndef JACK_THE_SLICER_AUDIO_HPP
#define JACK_THE_SLICER_AUDIO_HPP

#include <filesystem>
#include <memory>
#include <string>

namespace jack {

// Plays .wav files through the system audio output (using miniaudio). Playback
// runs on an internal audio thread; call Play()/Stop() from the UI thread.
class WavPlayer {
 public:
  WavPlayer();
  ~WavPlayer();
  WavPlayer(const WavPlayer&) = delete;
  WavPlayer& operator=(const WavPlayer&) = delete;

  // Loads |path| and starts playing. When |loop| is true, it repeats until
  // Stop() is called. Returns false if the file cannot be decoded or no audio
  // device is available.
  bool Play(const std::filesystem::path& path, bool loop);

  void Stop();

  // Idle housekeeping. Call frequently (e.g. every frame) so a finished
  // playback releases the audio device.
  void Poll();

  bool IsPlaying() const;
  bool IsLooping() const;
  const std::string& CurrentFile() const;

  // Implementation detail (hidden in audio.cpp), but must be public so the
  // miniaudio data callback can name the type.
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

// Slices |src| into |chunks| equal-length pieces, stitches them back together
// in their original order, and writes the result as a new 16-bit PCM .wav at
// |dst|. (Per-slice effects that reorder or transform the pieces are applied in
// a later step.) Returns false if the source cannot be decoded or the write
// fails.
bool SliceWav(const std::filesystem::path& src, const std::filesystem::path& dst,
              int chunks);

}  // namespace jack

#endif  // JACK_THE_SLICER_AUDIO_HPP