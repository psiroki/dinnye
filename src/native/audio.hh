#pragma once

#include <stdint.h>
#include <fstream>

#define FDA_NO_STDIO
#include "fda.h"
#include "util.hh"

typedef int (*MonoSampleGenerator)(uint32_t sampleIndex);

class StreamedFile {
  const char * const filename;
  uint8_t buffer[64*1024];
  int32_t bufferOffset;
  int32_t fileSize;
  std::ifstream stream;

  void fillBuffer(int32_t alreadyLoaded=0);
public:
  StreamedFile(const char *filename);
  ~StreamedFile();
  void reset();
  inline int32_t getFileSize() {
    return fileSize;
  }
  const uint8_t* makeAvailable(int32_t start, int32_t numBytes);
};

namespace SoundFlag {
  enum {
    music = 1,
    sound = 2,
  };
}

struct SoundBufferView {
  uint32_t *samples;
  uint32_t numSamples;
  uint32_t flags;
  Condition *condition;

  inline SoundBufferView(): samples(0), numSamples(0), condition(0) { }
  inline SoundBufferView(SoundBufferView &other, uint32_t startSample):
      samples(other.samples + startSample), numSamples(other.numSamples - startSample), condition(0) { }
  inline SoundBufferView(SoundBufferView &other, uint32_t startSample, uint32_t endSample):
      samples(other.samples + startSample), numSamples(endSample - startSample), condition(0) { }
};

struct SoundBuffer: public SoundBufferView {
  inline SoundBuffer(): SoundBufferView() { }
  ~SoundBuffer();

  void resize(uint32_t newNumSamples);
  void generateMono(uint32_t newNumSamples, MonoSampleGenerator gen);
};

struct MixChannel {
  const SoundBufferView *buffer;
  uint32_t playId;
  uint64_t timeStart;

  inline bool isOver(uint64_t audioTime) {
    return !buffer || timeStart < audioTime && (timeStart + buffer->numSamples) <= audioTime;
  }

  inline bool isMutedSound(uint32_t mask) {
    return (buffer->flags & SoundFlag::sound) && (buffer->flags & mask);
  }

  inline bool isMutedMusic(uint32_t mask) {
    return (buffer->flags & SoundFlag::music) && (buffer->flags & mask);
  }
};

class Mixer {
  static const int maxNumChannels = 64;
  static const int soundQueueSize = 64;
  static const int donePlayingQueueSize = 128;

  uint32_t playIdCounter;
  uint64_t audioTime[4];
  Timestamp times[4];
  MixChannel soundsToAdd[soundQueueSize];
  int soundRead;
  int soundWrite;
  MixChannel channels[maxNumChannels];
  int numChannelsUsed;
  int currentTimes;
  uint32_t donePlaying[donePlayingQueueSize];
  int donePlayingRead;
  int donePlayingWrite;
  Mutex playLock;
  uint32_t flagsMuted;
  uint64_t musicPauseTime;
public:
  inline Mixer():
      audioTime { 0, 0, 0, 0 },
      currentTimes(0),
      soundRead(0),
      soundWrite(0),
      donePlayingRead(0),
      donePlayingWrite(0),
      numChannelsUsed(0),
      playIdCounter(0),
      flagsMuted(0),
      musicPauseTime(0) { }
  void audioCallback(uint8_t *stream, int len);
  uint32_t playSound(const SoundBufferView *buffer);
  uint32_t playSoundAt(const SoundBufferView *buffer, uint64_t at);
  inline uint64_t getMusicPauseTime() {
    return musicPauseTime;
  }
  inline uint64_t getAudioTime() {
    return audioTime[currentTimes];
  }
  inline uint64_t getAudioTimeNow() {
    int w = currentTimes;
    return audioTime[w] + times[w].elapsedSeconds() * 44100;
  }
  inline uint32_t getNumChannelsUsed() const {
    return numChannelsUsed;
  }
  inline void setFlagsMuted(uint32_t newValue) {
    flagsMuted = newValue;
  }
  inline uint32_t getFlagsMuted() {
    return flagsMuted;
  }
  /// Returns the next playId that has just finished, or 0
  /// if no more are available (0 will never be used as an id)
  uint32_t nextDonePlaying();
};

class FdaStreamer {
  Mixer &mixer;
  StreamedFile compressed;
  SoundBuffer buffers[2];
  SoundBufferView views[2];
  uint32_t compressedPosition;
  uint32_t pendingPlayIds[2];
  uint64_t timeNext;
  uint32_t samplesPerFrame;
  fda_desc fda;
  Condition *condition;
  uint64_t lastMusicPauseTime;

  void fillBuffer(int index);
public:
  inline FdaStreamer(Mixer &mixer, const char *filename, Condition *condition):
      mixer(mixer),
      compressed(filename),
      condition(condition),
      timeNext(0),
      samplesPerFrame(0),
      lastMusicPauseTime(0) {
    buffers[0].resize(5120*4);
    buffers[1].resize(5120*4);
    views[0].condition = condition;
    views[1].condition = condition;
  }

  void reset();
  void startPlaying();
  void handleDone(uint32_t playId);
};

class ThreadedFdaStreamer {
  Mixer &mixer;
  Condition condition;
  FdaStreamer memoryStreamer;
  pthread_t thread;
  bool running;

  static void* threadMain(void* ptr);
  void loader();
public:
  inline ThreadedFdaStreamer(Mixer &mixer, const char *filename):
    mixer(mixer),
    condition(),
    memoryStreamer(mixer, filename, &condition) {
  }

  void startThread();
  void stopThread();
};
