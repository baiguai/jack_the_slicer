#ifndef JACK_THE_SLICER_AUDIO_HPP
#define JACK_THE_SLICER_AUDIO_HPP

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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

// Per-slice effect codes. The numeric values match the option order in the
// UI (see kEffects in main.cpp), so they can be passed straight through.
enum SliceEffect : int {
  kEffectNone = 0,
  kEffectShuffle = 1,
  kEffectReverse = 2,
  kEffectStretch = 3,
  kEffectSquish = 4,
};

// Slices |src| into |chunks| equal-length pieces, applies each piece's effect,
// stitches the pieces back together, and writes the result as a new 16-bit
// PCM .wav at |dst|.
//
// |effects| holds one effect code per piece. A piece set to |kEffectShuffle| is
// replaced by the data of a uniformly random piece (which may be itself); a
// piece set to |kEffectReverse| has its frames played back in reverse order.
// Pieces with any other code keep their original data for now. When |effects|
// is empty or shorter than |chunks|, the missing pieces are treated as
// |kEffectNone|. Returns false if the source cannot be decoded or the write
// fails.
bool SliceWav(const std::filesystem::path& src, const std::filesystem::path& dst,
              int chunks, const std::vector<int>& effects, std::uint32_t seed);

}  // namespace jack

#endif  // JACK_THE_SLICER_AUDIO_HPP