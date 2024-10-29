#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <iostream>
#include <fstream>
#include <time.h>
#include <string.h>

#ifdef BITTBOY
#include <sys/sysinfo.h>
#include <sys/resource.h>
#include <iomanip>
#endif

#include "../common/sim.hh"
#include "renderer.hh"
#include "util.hh"
#include "image.hh"
#include "audio.hh"

template <typename T, std::size_t N>
T sumArray(const T (&arr)[N]) {
  T sum { };
  for (const auto& elem : arr) {
    sum += elem;
  }
  return sum;
}

template <typename T, std::size_t N>
void printPercentile(T sum, int percentile, const char *name, const T (&arr)[N]) {
  T threshold(sum * percentile / 100);
  T n { };
  for (int i = 0; i < N; ++i) {
    n += arr[i];
    if (n >= threshold) {
      std::cout << name << " " << percentile << " percentile millis: " << i << std::endl;
      return;
    }
  }
  std::cout << name << " " << percentile << " percentile millis: over " << N << std::endl;
}

template <typename T, std::size_t N>
void printPercentiles(const char *name, const T (&arr)[N]) {
  T sum(sumArray(arr));
  printPercentile(sum, 95, name, arr);
  printPercentile(sum, 99, name, arr);
}

enum class Control {
  UNMAPPED, UP, DOWN, LEFT, RIGHT, NORTH, SOUTH, WEST, EAST, R1, L1, R2, L2, START, SELECT, MENU, LAST_ITEM,
};

const int dropOffset = 4953;

struct ControlState {
  bool controlState[static_cast<int>(Control::LAST_ITEM)];

  inline bool& operator [](int index) {
    return controlState[index];
  }

  inline bool& operator [](Control control) {
    return controlState[static_cast<int>(control)];
  }
};

// Initialize SDL and create a window with the specified dimensions
SDL_Surface *initSDL(int width, int height) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return nullptr;
  }

  TTF_Init();

  bool fullscreen = width == 0 || height == 0;
  if (fullscreen) {
    const SDL_VideoInfo *videoInfo = SDL_GetVideoInfo();
    width = videoInfo->current_w;
    height = videoInfo->current_h;
  }

  SDL_Surface *screen = SDL_SetVideoMode(width, height, 32, SDL_HWSURFACE |
      SDL_DOUBLEBUF |
      (fullscreen ? SDL_FULLSCREEN : 0));
  if (screen == nullptr) {
    std::cerr << "Failed to set video mode: " << SDL_GetError() << std::endl;
    return nullptr;
  }

  return screen;
}

struct NextPlacement {
  float x;  // y is always 0
  float xv;
  int radIndex;
  int seed;

  inline void constrainInside(FruitSim &sim) {
    float r = sim.getRadius(radIndex);
    if (x < r) {
      x = r;
      if (xv < 0.0f) xv = 0.0f;
    }
    if (x > sim.getWorldWidth() - r) {
      x = sim.getWorldWidth() - r;
      if (xv > 0.0f) xv = 0.0f;
    }
  }

  inline void step(FruitSim &sim) {
    x += xv;
    xv *= 0.95f;
    constrainInside(sim);
  }

  inline void reset(FruitSim &sim, int newSeed) {
    seed = newSeed;
    radIndex = (seed >> 4) % sim.getNumRandomRadii();
  }

  inline void setupPreview(FruitSim &sim) {
    sim.previewFruit(x, 0.0f, radIndex, seed);
  }

  inline void place(FruitSim &sim, int newSeed) {
    sim.addFruit(x, 0.0f, radIndex, seed);
    reset(sim, newSeed);
  }
};

class Planets {
  SDL_Surface *screen;
  Mixer mixer;
  static void callAudioCallback(void *userData, uint8_t *stream, int len);
public:
  void start();
};

void Planets::callAudioCallback(void *userData, uint8_t *stream, int len) {
  reinterpret_cast<Planets*>(userData)->mixer.audioCallback(stream, len);
}

void Planets::start() {
#ifdef BITTBOY
#pragma message "BittBoy build"
  SDL_Surface *screen = initSDL(320, 240);
  SDL_ShowCursor(false);
#else
  SDL_Surface *screen = initSDL(640, 480);
#endif
  if (!screen) return;

  SoundBuffer allSounds;
  allSounds.resize(10886);
  std::ifstream file("assets/sounds.dat", std::ios::binary);

  if (file.is_open()) {
    file.read(reinterpret_cast<char*>(allSounds.samples), 21772);
    file.close();
    // unpack mono to stereo
    for (int i = (allSounds.numSamples >> 1) - 1; i > 0; --i) {
      uint32_t twoSamples = allSounds.samples[i];
      uint32_t *p = allSounds.samples + i * 2;
      p[0] = (twoSamples & 0xFFFF) | (twoSamples << 16);
      p[1] = (twoSamples >> 16) | (twoSamples & 0xFFFF0000);
    }
  } else {
    memset(allSounds.samples, 0, allSounds.numSamples*sizeof(*allSounds.samples));
  }

  SoundBufferView pop(allSounds, 0, dropOffset);
  SoundBufferView drop(allSounds, dropOffset);

  for (int i = 0; i < drop.numSamples; ++i) {
    uint32_t sample = (drop.samples[i] & 0xFFFF) >> 3;
    drop.samples[i] = sample | (sample << 16);
  }

  SDL_AudioSpec desiredAudioSpec;
  SDL_AudioSpec actualAudioSpec;
  desiredAudioSpec.freq = 44100;
  desiredAudioSpec.format = AUDIO_S16;
  desiredAudioSpec.channels = 2;
  desiredAudioSpec.samples = 512;
  desiredAudioSpec.userdata = this;
  desiredAudioSpec.callback = callAudioCallback;
  memcpy(&actualAudioSpec, &desiredAudioSpec, sizeof(actualAudioSpec));
  char log[256] { 0 };
  SDL_AudioDriverName(log, sizeof(log));
  std::cerr << "Audio driver: " << log << std::endl;
  std::cerr << "Opening audio device" << std::endl;
  if (SDL_OpenAudio(&desiredAudioSpec, &actualAudioSpec)) {
    std::cerr << "Failed to set up audio. Running without it." << std::endl;
    return;
  }
  std::cerr << "Freq: " << actualAudioSpec.freq << std::endl;
  std::cerr << "Format: " << actualAudioSpec.format << std::endl;
  std::cerr << "Channels: " << static_cast<int>(actualAudioSpec.channels) << std::endl;
  std::cerr << "Samples: " << actualAudioSpec.samples << std::endl;
  std::cerr << "Starting audio" << std::endl;
  SDL_PauseAudio(0);

  ThreadedFdaStreamer music(mixer, "assets/wiggle-until-you-giggle.fda");
  music.startThread();

  bool running = true;
  SDL_Event event;

  FruitRenderer renderer(screen);
  FruitSim sim;

  timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);

  uint32_t seed = time.tv_nsec;
  sim.init(seed);
  sim.setGravity(0.0078125f * 0.5f);

  const float zoom = screen->h / sim.getWorldHeight();
  const float rightAligned = screen->w * 0.9875f - sim.getWorldWidth() * zoom;
  const float centered = (screen->w - sim.getWorldWidth() * zoom) * 0.5f;
  const float offsetX = rightAligned * 0.75f + centered * 0.25f;
  renderer.setLayout(zoom, offsetX, sim);
  SDL_Surface *background = loadImage(screen->w <= 640 ? "assets/background.png" : "assets/hi_background.jpg");
  SDL_Surface *backgroundScreen = SDL_DisplayFormat(screen);
  if (background->w < backgroundScreen->w || background->h < backgroundScreen->h)
    SDL_FillRect(backgroundScreen, nullptr, 0);
  SDL_Rect bgPos = {
    .x = static_cast<Sint16>((backgroundScreen->w - background->w) / 2),
    .y = static_cast<Sint16>((backgroundScreen->h - background->h) / 2),
  };
  SDL_BlitSurface(background, nullptr, backgroundScreen, &bgPos);
  SDL_FreeSurface(background);
  background = backgroundScreen;
  SDL_SetAlpha(background, 0, 255);
  renderer.renderBackground(background);

  NextPlacement next = { .x = 0.0f, .xv = 0.0f };
  next.reset(sim, seed);

  ControlState controls;
  memset(&controls, 0, sizeof(controls));
  bool placed = false;
  uint32_t simTime = 0;
  uint32_t renderTime = 0;
  uint32_t numFrames = 0;
  uint32_t maxSimTime = 0;
  uint32_t maxRenderTime = 0;
  uint32_t renderTimeHistogram[32] { }, simTimeHistogram[32] { };

  while (running) {
    ++numFrames;
    Timestamp frame;
    // Event handling
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT || 
        (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE)) {
        running = false;
      }
      if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
        Control c = Control::UNMAPPED;
        switch (event.key.keysym.sym) {
          case SDLK_LEFT:
            c = Control::LEFT;
            break;
          case SDLK_RIGHT:
            c = Control::RIGHT;
            break;
          case SDLK_SPACE:
            c = Control::EAST;
            break;
          case SDLK_RETURN:
            c = Control::START;
            break;
        }
        controls[c] = event.type == SDL_KEYDOWN;
      }
      if (event.type == SDL_MOUSEMOTION) {
        next.x = (event.motion.x - offsetX) / zoom;
        next.constrainInside(sim);
      }
      if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
        controls[Control::EAST] = event.button.state;
      }
    }

    if (controls[Control::LEFT]) next.xv -= 0.01f;
    if (controls[Control::RIGHT]) next.xv += 0.01f;
    if (!placed && (controls[Control::EAST] || controls[Control::SOUTH])) {
      next.place(sim, frame.getTime().tv_nsec);
      placed = true;
      mixer.playSound(&drop);
    } else if (!(controls[Control::EAST] || controls[Control::SOUTH])) {
      placed = false;
    }

    Timestamp simStart;
    next.step(sim);

    int popCountBefore = sim.getPopCount();

    Fruit *fruits = sim.simulate(++seed);
    int count = sim.getNumFruits();

    if (popCountBefore != sim.getPopCount())
      mixer.playSound(&pop);

    next.setupPreview(sim);
    uint32_t simMicros = simStart.elapsedSeconds() * 1000000.0f;
    simTime += simMicros;
    if (simMicros > maxSimTime) maxSimTime = simMicros;
    ++simTimeHistogram[min(simMicros/1000, 32u)];

    Timestamp renderStart;
    SDL_BlitSurface(background, nullptr, screen, nullptr);

    renderer.renderFruits(fruits, count + 1, next.radIndex);

    uint32_t renderMicros = renderStart.elapsedSeconds() * 1000000.0f;
    renderTime += renderMicros;
    if (renderMicros > maxRenderTime) maxRenderTime = renderMicros;
    ++renderTimeHistogram[min(renderMicros/1000, 32u)];

    // Update the screen
    SDL_Flip(screen);

#ifndef BITTBOY
    // Cap the frame rate to ~100 FPS
    int millisToWait = 10 - frame.elapsedSeconds()*1000.0f;
    if (millisToWait > 0) SDL_Delay(millisToWait);
#endif
  }
  std::cout << "maxSimMicros: " << maxSimTime << std::endl;
  std::cout << "maxRenderMicros: " << maxRenderTime << std::endl;
  std::cout << "simMicros avg: " << simTime/numFrames << std::endl;
  std::cout << "renderMicros avg: " << renderTime/numFrames << std::endl;
  printPercentiles("sim", simTimeHistogram);
  printPercentiles("render", renderTimeHistogram);

  std::cout << std::endl;
  std::cout << "sphereCacheMisses: " << SphereCache::numCacheMisses << std::endl;
  std::cout << "sphereCacheAngleMisses: " << SphereCache::numCacheAngleMisses << std::endl;
  std::cout << "sphereCacheReassignMisses: " << SphereCache::numCacheReassignMisses << std::endl;
  std::cout << "sphereCacheHits: " << SphereCache::numCacheHits << std::endl;

#ifdef BITTBOY
  std::cout << std::endl;
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) == 0) {
    // Maximum resident set size in kilobytes
    long maxrss = usage.ru_maxrss;
    std::cout << "maxRSS: " << maxrss << " kB" << std::endl;
  }
  struct sysinfo si;
  if (sysinfo(&si) == 0) {
    unsigned long totalRam = si.totalram * si.mem_unit;
    unsigned long freeRam = si.freeram * si.mem_unit;
    const double mb = 1024 * 1024;
    std::cout << std::fixed << std::setprecision(2)
              << "Memory Statistics:\n"
              << "Total RAM:      " << (totalRam / mb) << " MB\n"
              << "Free RAM:       " << (freeRam / mb) << " MB\n"
              << "Usage:          " << (100.0 * (totalRam - freeRam) / totalRam) << "%" << std::endl;
  }
#endif

  music.stopThread();

  SDL_Quit();
}

int main() {
  Planets planets;
  planets.start();
  return 0;
}
