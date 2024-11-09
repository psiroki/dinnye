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
#include "serialization.hh"
#include "renderer.hh"
#include "util.hh"
#include "image.hh"
#include "audio.hh"
#include "menu.hh"

struct TimeHistogram {
  uint32_t counts[256];

  TimeHistogram(): counts { } { }

  void add(uint32_t value) {
    const uint32_t numCounts = sizeof(counts) / sizeof(*counts);
    if (value >= numCounts) value = numCounts - 1;
    ++counts[value];
  }
};

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
      std::cout << name << " " << percentile << " percentile millis: " << i/10 << "." << i%10 << std::endl;
      return;
    }
  }
  std::cout << name << " " << percentile << " percentile millis: over " << N/10 << "." << N%10 << std::endl;
}

template <typename T, std::size_t N>
void printPercentiles(const char *name, int start, const T (&arr)[N]) {
  T sum(sumArray(arr));
  printPercentile(sum, 95, name, arr);
  printPercentile(sum, 99, name, arr);
  T overSum { };
  for (int i = start; i < N; ++i) {
    overSum += arr[i];
  }
  std::cout << "Percentage over " << start/10 << "." << start%10 << ": " << (100.0f*overSum) / sum << "%" << std::endl;
}

enum class Control {
  UNMAPPED, UP, DOWN, LEFT, RIGHT, NORTH, SOUTH, WEST, EAST, R1, L1, R2, L2, START, SELECT, MENU, LAST_ITEM,
};

const int dropOffset = 4953;

struct ControlState {
  bool controlState[static_cast<int>(Control::LAST_ITEM)];
  bool prevControlState[static_cast<int>(Control::LAST_ITEM)];

  inline bool& operator [](int index) {
    return controlState[index];
  }

  inline bool& operator [](Control control) {
    return controlState[static_cast<int>(control)];
  }

  inline bool justPressed(Control control) const {
    int index = static_cast<int>(control);
    return controlState[index] && !prevControlState[index];
  }

  inline void flush() {
    memcpy(&prevControlState, &controlState, sizeof(controlState));
  }
};

inline int32_t bitExtend(int16_t val) {
  return val;
}

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
  Scalar x;  // y is always -1
  Scalar xv;
  Scalar zoom;
  int radIndex;
  int seed;
  bool valid;

  inline void copy(NextDrop &n) {
    n.x = x;
    n.xv = xv;
    n.radIndex = radIndex;
    n.seed = seed;
  }

  inline void ingest(NextDrop &n) {
    x = n.x;
    xv = n.xv;
    radIndex = n.radIndex;
    seed = n.seed;
  }

  inline void constrainInside(FruitSim &sim) {
    Scalar r = sim.getRadius(radIndex);
    if (x < r) {
      x = r;
      if (xv < Scalar(0.0f)) xv = Scalar(0.0f);
    }
    if (x > sim.getWorldWidth() - r) {
      x = sim.getWorldWidth() - r;
      if (xv > Scalar(0.0f)) xv = Scalar(0.0f);
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
    Fruit *f = sim.previewFruit(x, -1.0f, radIndex, seed);
    valid = f && !sim.touchesAny(*f);
  }

  inline bool place(FruitSim &sim, int newSeed) {
    if (valid) {
      Scalar random = (Scalar(newSeed & 0xFF) - Scalar(128)) / Scalar(512);
      sim.addFruit(x + random / zoom, -1.0f, radIndex, seed);
      reset(sim, newSeed);
      return true;
    } else {
      return false;
    }
  }
};

enum class GameState { game, lost, menu };

class Planets: private GameSettings {
  GameState state;
  GameState returnState;
  FruitSim sim;
  SDL_Surface *screen;
  SDL_Surface *background;
  SDL_Surface *snapshot;
  Mixer mixer;
  SoundBuffer allSounds;
  SoundBufferView pop;
  SoundBufferView drop;
  SDL_AudioSpec desiredAudioSpec;
  SDL_AudioSpec actualAudioSpec;
  AutoDelete<ThreadedFdaStreamer> music;
  AutoDelete<FruitRenderer> renderer;
  AutoDelete<Menu> menu;
  NextPlacement next;
  ControlState controls;
  Scalar zoom;
  Scalar rightAligned;
  Scalar centered;
  Scalar offsetX;

  int outlierIndex;

  uint32_t seed;
  uint32_t simTime;
  uint32_t renderTime;
  uint32_t simulationFrame;
  uint32_t blurFrame;
  uint32_t frameCounter;
  uint32_t maxSimTime;
  uint32_t maxRenderTime;
  TimeHistogram renderTimeHistogram;
  TimeHistogram simTimeHistogram;
  TimeHistogram frameTimeHistogram;
  const char * const configFilePath;

  bool running;

  static void callAudioCallback(void *userData, uint8_t *stream, int len);

  GameState processInput(const Timestamp &frame);
  void initAudio();
  void simulate();
  void renderGame();
  void saveState();
  void loadState();

  bool isMusicEnabled() override;
  void setMusicEnabled(bool val) override;
  bool isSoundEnabled() override;
  void setSoundEnabled(bool val) override;
public:
  Planets(const char *configFilePath):
      next { .x = 0.0f, .xv = 0.0f, },
      controls { },
      state(GameState::game),
      returnState(GameState::game),
      outlierIndex(-1),
      simTime(0),
      renderTime(0),
      simulationFrame(0),
      frameCounter(0),
      blurFrame(0),
      maxSimTime(0),
      maxRenderTime(0),
      configFilePath(configFilePath) {
    std::cout << "Config file: " << configFilePath << std::endl;
  }
  void start();
};

void Planets::callAudioCallback(void *userData, uint8_t *stream, int len) {
  reinterpret_cast<Planets*>(userData)->mixer.audioCallback(stream, len);
}

bool Planets::isMusicEnabled() {
  return !(mixer.getFlagsMuted() & SoundFlag::music);
}

void Planets::setMusicEnabled(bool val) {
  if (val) {
    mixer.setFlagsMuted(mixer.getFlagsMuted() & ~SoundFlag::music);
  } else {
    mixer.setFlagsMuted(mixer.getFlagsMuted() | SoundFlag::music);
  }
}

bool Planets::isSoundEnabled() {
  return !(mixer.getFlagsMuted() & SoundFlag::sound);
}

void Planets::setSoundEnabled(bool val) {
  if (val) {
    mixer.setFlagsMuted(mixer.getFlagsMuted() & ~SoundFlag::sound);
  } else {
    mixer.setFlagsMuted(mixer.getFlagsMuted() | SoundFlag::sound);
  }
}

GameState Planets::processInput(const Timestamp &frame) {
  // Event handling
  controls.flush();
  GameState nextState = state;

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_QUIT) {
      running = false;
    }
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
      Control c = Control::UNMAPPED;
      switch (event.key.keysym.sym) {
        case SDLK_UP:
          c = Control::UP;
          break;
        case SDLK_DOWN:
          c = Control::DOWN;
          break;
        case SDLK_LEFT:
          c = Control::LEFT;
          break;
        case SDLK_RIGHT:
          c = Control::RIGHT;
          break;
        case SDLK_SPACE:
          c = Control::NORTH;
          break;
        case SDLK_LCTRL:
          c = Control::EAST;
          break;
        case SDLK_LALT:
          c = Control::SOUTH;
          break;
        case SDLK_LSHIFT:
          c = Control::WEST;
          break;
        case SDLK_RETURN:
          c = Control::START;
          break;
        case SDLK_ESCAPE:
          c = Control::SELECT;
          break;
        case SDLK_RCTRL:
          c = Control::MENU;
          break;
      }
      controls[c] = event.type == SDL_KEYDOWN;
    }
    if (state == GameState::game && event.type == SDL_MOUSEMOTION) {
      next.x = (event.motion.x - offsetX) / zoom;
      next.constrainInside(sim);
    }
    if (event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) {
      controls[Control::EAST] = event.button.state;
    }
  }

  if (state == GameState::menu) {
    if (controls.justPressed(Control::UP)) menu->moveVertical(-1);
    if (controls.justPressed(Control::DOWN)) menu->moveVertical(1);
    if (controls.justPressed(Control::LEFT)) menu->moveHorizontal(-1);
    if (controls.justPressed(Control::RIGHT)) menu->moveHorizontal(1);
    if (controls.justPressed(Control::EAST) ||
        controls.justPressed(Control::SOUTH) ||
        controls.justPressed(Control::START)) {
      Command cmd = menu->execute();
      switch (cmd) {
        case Command::quit:
          running = false;
          break;
        case Command::resume:
          nextState = returnState;
          break;
        case Command::reset:
          sim.newGame();
          next.reset(sim, frame.getTime().tv_nsec);
          // since we started a new game, we are returning to the game
          // state explicitly (returnState may be the lost state)
          returnState = nextState = GameState::game;
          break;
      }
    }
  }

#ifdef DESKTOP
  if (controls.justPressed(Control::SELECT)) {
#else
  if (controls.justPressed(Control::MENU)) {
#endif
    switch (state) {
      case GameState::game:
      case GameState::lost:
        returnState = state;
        nextState = GameState::menu;
        menu->reset();
        break;
      case GameState::menu:
        nextState = returnState;
        break;
    }
  }

  if (state == GameState::game) {
    if (controls[Control::LEFT]) next.xv -= 0.01f;
    if (controls[Control::RIGHT]) next.xv += 0.01f;
    Control drops[] { Control::NORTH, Control::EAST, Control::SOUTH, Control::WEST };
    for (int i = 0; i < sizeof(drops)/sizeof(*drops); ++i) {
      if (controls.justPressed(drops[i])) {
        if (next.place(sim, frame.getTime().tv_nsec)) {
          mixer.playSound(&drop);
        }
        break;
      }
    }
  }

  return nextState;
}

void Planets::saveState() {
  std::ofstream file(configFilePath, std::ios::binary);

  if (file.is_open()) {
    SaveState state;
    uint32_t t = time(nullptr);
    uint32_t nsec = Timestamp().getTime().tv_nsec;
    file.write(reinterpret_cast<const char*>(&t), sizeof(t));
    file.write(reinterpret_cast<const char*>(&nsec), sizeof(nsec));
    next.copy(state.next);
    state.audioFlagsMuted = mixer.getFlagsMuted();
    state.outlierIndex = outlierIndex;
    state.simulationFrame = simulationFrame;
    state.score = sim.getScore();
    state.numFruits = sim.getNumFruits();
    struct FileWriter: public Writer {
      std::ofstream &file;
      uint64_t seed;

      FileWriter(std::ofstream &file, uint64_t seed): file(file), seed(seed) { }

      void write(const uint32_t *buf, uint32_t numWords) override {
        const uint32_t numScrambled = 1024;
        uint32_t scrambled[numScrambled];
        while (numWords > 0) {
          uint32_t len = numWords > numScrambled ? numScrambled : numWords;
          for (int i = 0; i < len; ++i) {
            uint32_t w = buf[i];
            seed ^= seed * 3779 + (seed >> 32) * 149 + 7639;
            scrambled[i] = w ^ seed;
            seed ^= w;
          }
          file.write(reinterpret_cast<const char*>(scrambled), len * sizeof(uint32_t));
          numWords -= len;
        }
      }
    } w(file, static_cast<uint64_t>(t)*1000000000ll + nsec);
    state.write(sim.getFruits(), w);
    file.close();
  }
}

void Planets::loadState() {
  std::ifstream file(configFilePath, std::ios::binary);

  if (file.is_open()) {
    SaveState state;
    uint32_t t;
    uint32_t nsec;
    file.read(reinterpret_cast<char*>(&t), sizeof(t));
    file.read(reinterpret_cast<char*>(&nsec), sizeof(nsec));
    struct FileReader: public Reader {
      std::ifstream &file;
      uint64_t seed;

      FileReader(std::ifstream &file, uint64_t seed): file(file), seed(seed) { }

      void read(uint32_t *buf, uint32_t numWords) override {
        const uint32_t numScrambled = 1024;
        uint32_t scrambled[numScrambled];
        while (numWords > 0) {
          uint32_t len = numWords > numScrambled ? numScrambled : numWords;
          file.read(reinterpret_cast<char*>(scrambled), len * sizeof(uint32_t));
          for (int i = 0; i < len; ++i) {
            uint32_t w = scrambled[i];
            seed ^= seed * 3779 + (seed >> 32) * 149 + 7639;
            w ^= seed;
            buf[i] = w;
            seed ^= w;
          }
          numWords -= len;
        }
      }
    } r(file, static_cast<uint64_t>(t)*1000000000ll + nsec);
    state.read(sim.getFruits(), r);
    file.close();

    next.ingest(state.next);
    mixer.setFlagsMuted(state.audioFlagsMuted);
    outlierIndex = state.outlierIndex;
    simulationFrame = state.simulationFrame;
    sim.setNumFruits(state.numFruits);
    sim.setScore(state.score);
  }
}

void Planets::initAudio() {
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

  pop = SoundBufferView(allSounds, 0, dropOffset);
  drop = SoundBufferView(allSounds, dropOffset);

  pop.flags = SoundFlag::sound;
  drop.flags = SoundFlag::sound;

  for (int i = 0; i < drop.numSamples; ++i) {
    int32_t sample = bitExtend(drop.samples[i] & 0xFFFF) >> 2;
    drop.samples[i] = sample | (static_cast<uint32_t>(sample) << 16);
  }

  for (int i = 0; i < pop.numSamples; ++i) {
    uint32_t sample = bitExtend(pop.samples[i] & 0xFFFF) >> 1;
    pop.samples[i] = sample | (sample << 16);
  }

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

  music = new ThreadedFdaStreamer(mixer, "assets/wiggle-until-you-giggle.fda");
  music->startThread();
}

void Planets::simulate() {
  Timestamp simStart;
  if (state == GameState::game) next.step(sim);

  int popCountBefore = sim.getPopCount();

  if (state == GameState::game) sim.simulate(++seed, simulationFrame);

  if (popCountBefore != sim.getPopCount())
    mixer.playSound(&pop);

  next.setupPreview(sim);
  if (state == GameState::game) {
    uint32_t simMicros = simStart.elapsedMicros();
    simTime += simMicros;
    if (simMicros > maxSimTime) maxSimTime = simMicros;
    simTimeHistogram.add(simMicros/100);
  }
}

void Planets::renderGame() {
  Timestamp renderStart;
  SDL_BlitSurface(background, nullptr, screen, nullptr);

  Fruit *fruits = sim.getFruits();
  int count = sim.getNumFruits();
  renderer->renderFruits(sim, count + 1, next.radIndex, outlierIndex, simulationFrame);

  uint32_t renderMicros = renderStart.elapsedMicros();
  renderTime += renderMicros;
  if (renderMicros > maxRenderTime) maxRenderTime = renderMicros;
  renderTimeHistogram.add(renderMicros/100);
}

void Planets::start() {
#if defined(BITTBOY)
#pragma message "BittBoy build"
  screen = initSDL(0, 0);
  SDL_ShowCursor(false);
#elif defined(LOREZ)
  screen = initSDL(320, 240);
#else
  screen = initSDL(640, 480);
#endif
  if (!screen) return;

  initAudio();

  running = true;

  timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);

  seed = time.tv_nsec;
  sim.init(seed);
  sim.setGravity(Scalar(0.0078125f * 0.5f));

  zoom = screen->h / (sim.getWorldHeight() + Scalar(2));
  rightAligned = screen->w * Scalar(0.9875f) - sim.getWorldWidth() * zoom;
  centered = (screen->w - sim.getWorldWidth() * zoom) * Scalar(0.5f);
  offsetX = rightAligned * Scalar(0.75f) + centered * Scalar(0.25f);

  next.zoom = zoom;
  next.reset(sim, seed);

  loadState();
  
  snapshot = SDL_DisplayFormat(screen);
  background = loadImage(screen->w <= 640 ? "assets/background.png" : "assets/hi_background.jpg");
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

  renderer = new FruitRenderer(screen);
  menu = new Menu(*renderer, *this);
  renderer->setLayout(zoom, offsetX, sim);
  renderer->renderBackground(background);

  Fruit *fruits;

  while (running) {
    bool justLost = false;
    if (state == GameState::game && simulationFrame) {
      outlierIndex = sim.findGroundedOutside(simulationFrame);
      if (outlierIndex >= 0) {
        justLost = true;
        state = GameState::lost;
      }
    }
    if (state == GameState::game) ++simulationFrame;
    Timestamp frame;

    GameState nextState = processInput(frame);

    if (state == GameState::game || state == GameState::lost) {
      simulate();

      renderGame();
    } else {
      SDL_BlitSurface(snapshot, nullptr, screen, nullptr);
      menu->render(screen);
    }

    if (state != nextState && nextState == GameState::menu) {
      SDL_BlitSurface(screen, nullptr, snapshot, nullptr);
      blurFrame = 32;
    }
    state = nextState;

    if ((state == GameState::menu || state == GameState::lost) && blurFrame > 0) {
      blur(snapshot, --blurFrame);
    }

    // Update the screen
    SDL_Flip(screen);

    uint32_t frameMicros = frame.elapsedMicros();
    frameTimeHistogram.add(frameMicros/100);

#ifndef BITTBOY
    // Cap the frame rate to ~100 FPS
    int millisToWait = 10 - frame.elapsedMicros()/1000;
    if (millisToWait > 0) SDL_Delay(millisToWait);
#endif
    ++frameCounter;
  }
  std::cout << "maxSimMicros: " << maxSimTime << std::endl;
  std::cout << "maxRenderMicros: " << maxRenderTime << std::endl;
  std::cout << "simMicros avg: " << simTime/simulationFrame << std::endl;
  std::cout << "renderMicros avg: " << renderTime/simulationFrame << std::endl;
  printPercentiles("sim", 167, simTimeHistogram.counts);
  printPercentiles("render", 167, renderTimeHistogram.counts);
  printPercentiles("frame", 167, frameTimeHistogram.counts);

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

  music->stopThread();

  SDL_FreeSurface(background);
  SDL_FreeSurface(snapshot);

  saveState();
  SDL_Quit();
}

int main(int argc, char **argv) {
  const char *configHome = SDL_getenv("XDG_CONFIG_HOME");
  const char *appData = SDL_getenv("APPDATA");
  const char *home = SDL_getenv("HOME");
  const char *relFile = "/planetmerge/state.bin";
  AutoDeleteArray<char> configFilePath = nullptr;
  AutoDeleteArray<char> customHome = nullptr;
  if (!configHome && appData) {
    configHome = appData;
  } else if (!configHome && !appData && home) {
    relFile = "/.config/planetmerge/state.bin";
    configHome = home;
  } else if (!configHome && !appData && !home) {
    relFile = "/.config/planetmerge/state.bin";
    const char *end = SDL_strrchr(argv[0], '/');
    if (!end) end = argv[0];
    customHome = new char[end - argv[0] + 1];
    strncpy(customHome, argv[0], end - argv[0] + 1);
    customHome[end - argv[0]] = 0;
    configHome = customHome;
  }
  const size_t subdirLen = strnlen(relFile, 256);
  if (configHome) {
    size_t len = strnlen(configHome, 65536);
    size_t cfpLen = len + subdirLen + 1;
    configFilePath = new char[cfpLen];
    snprintf(configFilePath, cfpLen, "%s%s", configHome, relFile);
#ifdef _WIN32
    // In case I'm going to support Windows
    for (int i = 0; i < cfpLen; ++i) {
      if (configFilePath[i] == '\\') configFilePath[i] = '/';
    }
#endif
    createDirectoryForFile(configFilePath);
  } else {
    configFilePath = nullptr;
  }
  Planets planets(configFilePath);
  planets.start();
  return 0;
}
