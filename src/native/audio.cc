#define FDA_IMPLEMENTATION
#include "audio.hh"
#include <string.h>

template<typename T> class AutoDeleteArray {
  T* ptr;
public:
  AutoDeleteArray(T* ptr): ptr(ptr) { }
  ~AutoDeleteArray() {
    delete[] ptr;
    ptr = nullptr;
  }

  T* asPointer() {
    return ptr;
  }

  operator T*() {
    return ptr;
  }

  T* operator->() {
    return ptr;
  }
};

StreamedFile::StreamedFile(const char *filename): filename(filename), bufferOffset(0) {
  stream.open(filename, std::ios::binary);
  stream.seekg(0, std::ios::end);
  fileSize = stream.tellg();
  stream.seekg(0, std::ios::beg);
  fillBuffer();
}

StreamedFile::~StreamedFile() {

}

void StreamedFile::reset() {
  if (bufferOffset) {
    bufferOffset = 0;
    stream.seekg(0, std::ios::beg);
    fillBuffer();
  }
}

void StreamedFile::fillBuffer(int32_t alreadyLoaded) {
  int32_t bytesToRead = fileSize - bufferOffset;
  if (bytesToRead > sizeof(buffer) - alreadyLoaded)
    bytesToRead = sizeof(buffer) - alreadyLoaded;
  if (bytesToRead > 0) {
    stream.read(reinterpret_cast<char*>(buffer + alreadyLoaded), bytesToRead);
  }
}

const uint8_t* StreamedFile::makeAvailable(int32_t start, int32_t numBytes) {
  int32_t startIndex = start - bufferOffset;
  int32_t endIndex = startIndex + numBytes;
  if (startIndex < 0 || startIndex >= sizeof(buffer)) {
    // trying to read from before the buffer or way beyond the buffer range
    stream.clear();
    stream.seekg(start, std::ios::beg);
    bufferOffset = start;
    fillBuffer();
    startIndex = 0;
  } else if (endIndex > sizeof(buffer)) {
    // the end is beyond the end of the buffer
    // some bytes have already been loaded
    int32_t bytesLoaded = sizeof(buffer) - startIndex;
    memmove(buffer, buffer + startIndex, bytesLoaded);
    bufferOffset = start;
    fillBuffer(bytesLoaded);
    startIndex = 0;
  }
  return buffer + startIndex;
}

SoundBuffer::~SoundBuffer() {
  if (samples) delete[] samples;
}

void SoundBuffer::resize(uint32_t newNumSamples) {
  if (newNumSamples != numSamples) {
    if (samples) delete[] samples;
    numSamples = newNumSamples;
    samples = numSamples ? new uint32_t[newNumSamples] : nullptr;
  }
}

void SoundBuffer::generateMono(uint32_t newNumSamples, MonoSampleGenerator gen) {
  resize(newNumSamples);
  for (uint32_t i = 0; i < numSamples; ++i) {
    int s = gen(i) & 0xFFFF;
    samples[i] = (s << 16) | s;
  }
}

void Mixer::audioCallback(uint8_t *stream, int len) {
  uint64_t time = audioTime[currentTimes];
  int numSamples = len / 4;

  int nextWatch = (currentTimes + 1) & 3;
  times[nextWatch].reset();
  audioTime[nextWatch] = time + numSamples;
  currentTimes = nextWatch;

  // remove finished channels
  Condition *cond = nullptr;
  for (int i = numChannelsUsed - 1; i >= 0; --i) {
    if (channels[i].isOver(time)) {
      donePlaying[donePlayingWrite] = channels[i].playId;
      Condition *c = channels[i].buffer->condition;
      if (c) cond = c;
      donePlayingWrite = (donePlayingWrite+1) & (donePlayingQueueSize - 1);
      if (i < numChannelsUsed - 1) {
        // swap with last
        channels[i] = channels[numChannelsUsed - 1];
      }
      --numChannelsUsed;
    }
  }
  if (cond) cond->notify();
  // add new channels
  while (soundRead != soundWrite) {
    channels[numChannelsUsed++] = soundsToAdd[soundRead];
    soundRead = (soundRead + 1) & (soundQueueSize - 1);
  }

  uint32_t *s = reinterpret_cast<uint32_t*>(stream);
  for (int i = 0; i < numSamples; ++i) {
    int mix[2] { 0, 0 };
    for (int j = 0; j < numChannelsUsed; ++j) {
      MixChannel &ch(channels[j]);
      uint64_t start = ch.timeStart;
      if (start <= time && !ch.isOver(time)) {
        uint32_t sampleIndex = time - start;
        uint32_t sample = ch.buffer->samples[sampleIndex];
        for (int k = 0; k < 2; ++k) {
          mix[k] += static_cast<int16_t>(sample >> (k * 16));
        }
      }
    }
    for (int k = 0; k < 2; ++k) {
      if (mix[k] > 32767) mix[k] = 32767;
      if (mix[k] < -32768) mix[k] = -32768;
    }
    *s++ = (mix[1] << 16) | (mix[0] & 0xffff);
    ++time;
  }
}

uint32_t Mixer::playSound(const SoundBufferView *buffer) {
  return playSoundAt(buffer, getAudioTimeNow());
}

uint32_t Mixer::playSoundAt(const SoundBufferView *buffer, uint32_t at) {
  playLock.lock();
  MixChannel &ch(soundsToAdd[soundWrite]);
  ch.buffer = buffer;
  ch.playId = ++playIdCounter;
  if (ch.playId == 0) ch.playId = ++playIdCounter;
  ch.timeStart = at;
  soundWrite = (soundWrite + 1) & (soundQueueSize - 1);
  playLock.unlock();
  return ch.playId;
}

uint32_t Mixer::nextDonePlaying() {
  if (donePlayingRead == donePlayingWrite) return 0;
  uint32_t result = donePlaying[donePlayingRead];
  donePlayingRead = (donePlayingRead+1) & (donePlayingQueueSize - 1);
  return result;
}

void FdaStreamer::fillBuffer(int index) {
  SoundBuffer &buf(buffers[index]);
  views[index] = buf;
  views[index].condition = condition;
  int16_t *start = reinterpret_cast<int16_t*>(buf.samples);
  int16_t *end = reinterpret_cast<int16_t*>(buf.samples + buf.numSamples);
  int samplesLeft = buf.numSamples;
  while (start < end && samplesLeft >= samplesPerFrame) {
    if (compressedPosition >= compressed.getFileSize()) {
      compressedPosition = fda_decode_header(compressed.makeAvailable(0, 16), 16, &fda);
    }
    unsigned numSamples = samplesLeft;
    const uint8_t *p = compressed.makeAvailable(compressedPosition, 8192);
    uint32_t bytesLeftFromFile = compressed.getFileSize() - compressedPosition;
    uint32_t bytesLeft = bytesLeftFromFile > 8192 ? 8192 : bytesLeftFromFile;
    unsigned frameSize = fda_decode_frame(p, bytesLeft, &fda, start, &numSamples);
    if (!samplesPerFrame) samplesPerFrame = numSamples;
    if (!frameSize) {
      compressedPosition = compressed.getFileSize();
    } else {
      compressedPosition += frameSize;
    }
    start += numSamples * 2;
    samplesLeft -= numSamples;
  }
  if (samplesLeft) {
    views[index].numSamples = buf.numSamples - samplesLeft;
  }
}

void FdaStreamer::reset() {
  pendingPlayIds[0] = pendingPlayIds[1] = 0;
  compressed.reset();
}

void FdaStreamer::startPlaying() {
  compressed.reset();
  compressedPosition = fda_decode_header(compressed.makeAvailable(0, 16), 16, &fda);
  fillBuffer(0);
  fillBuffer(1);
  timeNext = mixer.getAudioTimeNow();
  for (int i = 0; i < 2; ++i) {
    pendingPlayIds[i] = mixer.playSoundAt(views + i, timeNext);
    timeNext += views[i].numSamples;
  }
}

void FdaStreamer::handleDone(uint32_t playId) {
  for (int i = 0; i < 2; ++i) {
    if (playId == pendingPlayIds[i]) {
      fillBuffer(i);
      pendingPlayIds[i] = mixer.playSoundAt(views + i, timeNext);
      timeNext += views[i].numSamples;
    }
  }
}

void* ThreadedFdaStreamer::threadMain(void* ptr) {
  reinterpret_cast<ThreadedFdaStreamer*>(ptr)->loader();
  return nullptr;
}

void ThreadedFdaStreamer::loader() {
  memoryStreamer.startPlaying();
  while (running) {
    condition.wait();
    uint32_t id;
    while ((id = mixer.nextDonePlaying())) {
      memoryStreamer.handleDone(id);
    }
  }
}

void ThreadedFdaStreamer::startThread() {
  running = true;
  pthread_create(&thread, nullptr, threadMain, this);
}

void ThreadedFdaStreamer::stopThread() {
  running = false;
  condition.notify();
}
