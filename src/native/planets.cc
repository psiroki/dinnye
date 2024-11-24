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
#include "platform.hh"
#include "serialization.hh"
#include "renderer.hh"
#include "util.hh"
#include "image.hh"
#include "audio.hh"
#include "menu.hh"
#include "input.hh"
#include "miyoo_audio.hh"

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
void printPercentiles(const char *name, int start, const T (&arr)[N], std::ostream &os = std::cout) {
  T sum(sumArray(arr));
  printPercentile(sum, 95, name, arr);
  printPercentile(sum, 99, name, arr);
  T overSum { };
  for (int i = start; i < N; ++i) {
    overSum += arr[i];
  }
  os << "Percentage at or over " << start/10 << "." << start%10 << ": " << (100.0f*overSum) / sum << "%\n";
}

struct SectionTime {
  const char *name;
  Timestamp startTime;
  uint64_t allMicros;
  uint32_t maxMicros;
  uint32_t minMicros;
  uint32_t count;
  TimeHistogram histogram;

  SectionTime(const char *name = nullptr): name(name), allMicros(0), maxMicros(0), minMicros(~0u), count(0) { }

  void start() {
    startTime.reset();
  }

  uint64_t end() {
    ++count;
    uint64_t micros = startTime.elapsedMicros();
    allMicros += micros;
    if (micros > maxMicros) maxMicros = micros;
    if (micros < minMicros) minMicros = micros;
    histogram.add(micros/100);
    return micros;
  }

  void print(std::ostream &s = std::cout) const {
    if (!count) {
      s << "No " << name << " times reported\n";
      return;
    }
    s << "min(" << name << ") micros: " << minMicros << "\n";
    s << "max(" << name << ") micros: " << maxMicros << "\n";
    s << "avg(" << name << ") micros: " << allMicros / count << "\n";
    printPercentiles(name, 167, histogram.counts, s);
  }
};

std::ostream& operator <<(std::ostream& s, const SectionTime &t) {
  t.print(s);
  return s;
}

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

  inline bool isDown(Control control) const {
    return controlState[static_cast<int>(control)];
  }

  inline bool justPressed(Control control) const {
    int index = static_cast<int>(control);
    return controlState[index] && !prevControlState[index];
  }

  inline bool comboPressed(Control a, Control b) const {
    return isDown(a) && justPressed(b) || isDown(b) && justPressed(a);
  }

  inline void flush() {
    memcpy(&prevControlState, &controlState, sizeof(controlState));
  }
};

inline int32_t bitExtend(int16_t val) {
  return val;
}

struct NextPlacement {
  Scalar x;  // y is always -1
  Scalar xv;
  int intendedX;
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

  inline void setIntendedX(int x) {
    intendedX = x;
  }

  inline void step(FruitSim &sim) {
    x += xv;
    if (intendedX) xv += intendedX * Scalar(0.01f);
    Scalar mul(0.9f);
    if (intendedX < 0 && xv < Scalar(0) || intendedX > 0 && xv > Scalar(0)) mul = Scalar(0.95f);
    xv *= mul;
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

const uint32_t highscoreCap = 10;

class Planets: private GameSettings {
  static const int numBlurFrames = 32;
  static const int numBlurCallsPerFrame = 2;

#if defined(MIYOOA30)
  static const int numSimStepsPerFrame = 3;
#elif defined(MIYOO) || defined(RG35XX)
  static const int numSimStepsPerFrame = 2;
#else
  static const int numSimStepsPerFrame = 1;
#endif

  GameState state;
  GameState returnState;
  FruitSim sim;
  Highscore highscores[highscoreCap];
  uint32_t numHighscores;
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
  int loseAnimationFrame;

  uint32_t seed;
  uint32_t simulationFrame;
  uint32_t blurCallsLeft;
  uint32_t frameCounter;

  int32_t lastHatBits;

  SectionTime flipTime;
  SectionTime eventTime;
  SectionTime frameTime;
  SectionTime gameFrame;
  SectionTime blurTime;
  SectionTime renderTime;
  SectionTime simTime;
  const char * const configFilePath;

  InputMapping inputMapping;

#ifdef USE_GAME_CONTROLLER
  SDL_GameController *controller;
#endif

  bool running;
  bool showFps;

  static void callAudioCallback(void *userData, uint8_t *stream, int len);

  GameState processInput(const Timestamp &frame);
  void initAudio();
  void simulate();
  void renderGame(GameState nextState);
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
      numHighscores(0),
      outlierIndex(-1),
      showFps(false),
      frameTime("frame"),
      gameFrame("gameFrame"),
      blurTime("blur"),
      renderTime("render"),
      simTime("sim"),
      flipTime("flip"),
      eventTime("events"),
      simulationFrame(0),
      frameCounter(0),
      blurCallsLeft(0),
#ifdef USE_GAME_CONTROLLER
      controller(nullptr),
#endif
      lastHatBits(0),
      configFilePath(configFilePath) {
    std::cout << "Config file: " << configFilePath << std::endl;
  }
  void start();
  void insertHighscore(int score);
  void dumpHighscore();
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
#ifdef USE_GAME_CONTROLLER
    if (event.type == SDL_CONTROLLERBUTTONDOWN || event.type == SDL_CONTROLLERBUTTONUP) {
      Control c = inputMapping.mapGameControllerButtonIndex(event.cbutton.button);
      controls[c] = event.type == SDL_CONTROLLERBUTTONDOWN;
    }
#else
    if (event.type == SDL_JOYBUTTONDOWN || event.type == SDL_JOYBUTTONUP) {
      if (event.type == SDL_JOYBUTTONDOWN) std::cout << "Button " << int(event.jbutton.button) << std::endl;
      Control c = inputMapping.mapButton(event.jbutton.button);
      controls[c] = event.type == SDL_JOYBUTTONDOWN;
    }
    if (event.type == SDL_JOYHATMOTION) {
      int32_t hatBits = event.jhat.value;
      std::cout << "Hat " << hatBits << std::endl;
      for (int i = 0; i < 4; ++i) {
        int32_t mask = 1 << i;
        int32_t bit = hatBits & mask;
        if (bit != (lastHatBits & mask)) {
          Control c = inputMapping.mapHatDirection(mask);
          controls[c] = bit;
        }
      }
      lastHatBits = hatBits;
    }
#endif
    if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
#ifdef USE_SDL2
      Control c = inputMapping.mapKey(static_cast<int32_t>(event.key.keysym.scancode));
#else
      Control c = inputMapping.mapKey(static_cast<int32_t>(event.key.keysym.sym));
#endif
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

  if (controls.comboPressed(Control::L1, Control::R1) ||
      controls.comboPressed(Control::L2, Control::R2)) {
    showFps = !showFps;
  }

  if (controls.comboPressed(Control::START, Control::SELECT)) {
    running = false;
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
          if (returnState != GameState::lost) nextState = returnState;
          break;
        case Command::reset:
          sim.newGame();
          next.reset(sim, frame.getTime().tv_nsec);
          outlierIndex = -1;
          // since we started a new game, we are returning to the game
          // state explicitly (returnState may be the lost state)
          returnState = nextState = GameState::game;
          break;
      }
    }
  }

  if (controls.justPressed(Control::MENU) || controls.justPressed(Control::START)) {
    switch (state) {
      case GameState::game:
      case GameState::lost:
        returnState = state;
        nextState = GameState::menu;
        menu->reset();
        break;
      case GameState::menu:
        if (returnState != GameState::lost) nextState = returnState;
        break;
    }
  }

  if (state == GameState::game) {
    int ix = 0;
    if (controls[Control::LEFT]) ix = -1;
    if (controls[Control::RIGHT]) ix = 1;
    next.setIntendedX(ix);

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
    state.numHighscores = numHighscores;
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
    state.write(sim.getFruits(), highscores, w);
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
    RecordBuffer<Fruit> fruits(sim.getFruits(), sim.getMaxNumFruits());
    RecordBuffer<Highscore> scores(highscores, highscoreCap);
    bool success = state.read(fruits, scores, r);
    file.close();

    if (success) {
      next.ingest(state.next);
      mixer.setFlagsMuted(state.audioFlagsMuted);
      outlierIndex = state.outlierIndex;
      simulationFrame = state.simulationFrame;
      sim.setNumFruits(state.numFruits);
      sim.setScore(state.score);
      numHighscores = state.numHighscores;
    }
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
#ifdef MIYOO_AUDIO
#pragma message "Using Miyoo audio instead of SDL"
  if (initMiyooAudio(desiredAudioSpec)) {
    std::cerr << "Failed to set up audio. Running without it." << std::endl;
    return;
  }
#else
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
#endif

  std::cerr << "Starting music streamer" << std::endl;
  music = new ThreadedFdaStreamer(mixer, "assets/wiggle-until-you-giggle.fda");
  music->startThread();
}

void Planets::simulate() {
  simTime.start();
  bool lostAlready = outlierIndex >= 0;
  if (state == GameState::game && !lostAlready) next.step(sim);

  int popCountBefore = sim.getPopCount();

  if (state == GameState::game && !lostAlready) sim.simulate(++seed, simulationFrame);

  if (popCountBefore != sim.getPopCount())
    mixer.playSound(&pop);

  next.setupPreview(sim);
  if (state == GameState::game) simTime.end();
}

void Planets::renderGame(GameState nextState) {
  renderTime.start();
  Timestamp renderStart;
  SDL_BlitSurface(background, nullptr, screen, nullptr);

  Fruit *fruits = sim.getFruits();
  int count = sim.getNumFruits();
  renderer->renderFruits(sim, count + 1, next.radIndex, outlierIndex, simulationFrame, nextState == GameState::lost);

  renderTime.end();
}

Platform platform;

void Planets::start() {
#if defined(BITTBOY)
#pragma message "BittBoy build"
  screen = platform.initSDL(0, 0);
#elif defined(LOREZ)
  screen = platform.initSDL(320, 240);
#elif defined(MIYOOA30)
  screen = platform.initSDL(0, 0, 3, false);
#elif defined(MIYOO)
  screen = platform.initSDL(0, 0, 2, false);
#elif defined(DESKTOP)
  screen = platform.initSDL(640, 480, 0, false);
#else
  screen = platform.initSDL(0, 0, 0, false, true);
#endif

#ifndef DESKTOP
  SDL_ShowCursor(false);
#endif
  if (!screen) return;

  initAudio();

#ifdef USE_GAME_CONTROLLER
  for (int i = 0; i < SDL_NumJoysticks(); i++) {
    if (SDL_IsGameController(i)) {
      controller = SDL_GameControllerOpen(i);
      break;
    }
  }
#endif

  std::cerr << "Initializing sim..." << std::endl;

  running = true;

  timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);

  seed = time.tv_nsec;
  sim.init(seed);
  sim.setGravity(Scalar(0.0078125f * 0.5f));

  std::cerr << "Initializing video..." << std::endl;

  std::cout << screen->w << "x" << screen->h << std::endl;
  zoom = screen->h / (sim.getWorldHeight() + Scalar(2));
  rightAligned = screen->w * Scalar(0.9875f) - sim.getWorldWidth() * zoom;
  centered = (screen->w - sim.getWorldWidth() * zoom) * Scalar(0.5f);
  offsetX = rightAligned * Scalar(0.75f) + centered * Scalar(0.25f);

  next.zoom = zoom;
  next.reset(sim, seed);

  std::cerr << "Loading state..." << std::endl;

  loadState();

  std::cerr << "Loading textures..." << std::endl;
  
  snapshot = platform.createSurface(screen->w, screen->h);
  if (!snapshot) {
    std::cerr << "Couldn't allocate snapshot surface" << std::endl;
    SDL_Quit();
  }
  platform.makeOpaque(snapshot);
  background = loadImage(screen->w <= 640 ? "assets/background.png" : "assets/hi_background.jpg");
  if (!background) {
    std::cerr << "Couldn't load background" << std::endl;
    SDL_Quit();
  }
  platform.makeOpaque(background);
  SDL_Surface *backgroundScreen = platform.createSurface(screen->w, screen->h);
  if (!backgroundScreen) {
    std::cerr << "Couldn't allocate memory for background" << std::endl;
    SDL_Quit();
  }
  if (background->w < backgroundScreen->w || background->h < backgroundScreen->h)
    SDL_FillRect(backgroundScreen, nullptr, 0);
  SDL_Rect bgPos = makeRect(
    (backgroundScreen->w - background->w) / 2,
    (backgroundScreen->h - background->h) / 2
  );
  SDL_BlitSurface(background, nullptr, backgroundScreen, &bgPos);
  SDL_FreeSurface(background);
  background = backgroundScreen;
  platform.makeOpaque(background);

  renderer = new FruitRenderer(screen);
  menu = new Menu(*renderer, *this);
  renderer->setLayout(zoom, offsetX, sim);
  renderer->renderBackground(background);
  platform.makeOpaque(background);

  Fruit *fruits;

  std::cerr << "Entering main loop..." << std::endl;

  uint32_t timeSum = 0;
  uint32_t timeCount = 0;
  while (running) {
    frameTime.start();
    if (state == GameState::game) gameFrame.start();

    Timestamp frame(frameTime.startTime);

    eventTime.start();
    GameState nextState = processInput(frame);
    eventTime.end();

    bool justLost = false;
    if (state == GameState::game) {
      bool savedLostState = outlierIndex >= 0;
      for (int iter = 0; iter < numSimStepsPerFrame; ++iter) {
        ++simulationFrame;
        simulate();

        if (!justLost && state == GameState::game && simulationFrame) {
          if (!savedLostState) outlierIndex = sim.findGroundedOutside(simulationFrame);
          if (outlierIndex >= 0) {
            justLost = true;
            loseAnimationFrame = 0;
            nextState = GameState::lost;
            if (!savedLostState) insertHighscore(sim.getScore());
            break;
          }
        }
      }

      renderGame(nextState);
    } else {
      SDL_BlitSurface(snapshot, nullptr, screen, nullptr);
      if (state == GameState::menu) {
        menu->render(screen, returnState != GameState::lost);
        if (returnState == GameState::lost) {
          renderer->renderMenuScores(sim.getScore(), highscores[0].score);
        }
      } else {
        renderer->renderLostScreen(sim.getScore(), highscores[0].score, snapshot, loseAnimationFrame++);
#ifdef MIYOOA30
        loseAnimationFrame += 2;
#endif
      }
    }

    if (state != nextState && nextState == GameState::menu || justLost) {
      if (nextState == GameState::menu || justLost) {
        SDL_BlitSurface(screen, nullptr, snapshot, nullptr);
        if (nextState == GameState::menu) blurCallsLeft = numBlurCallsPerFrame * numBlurFrames;
      }
      timespec t = frame.getTime();
      menu->setAppearanceSeed(t.tv_nsec + t.tv_sec);
    }
    if (justLost) {
      renderer->renderLostScreen(sim.getScore(), highscores[0].score, nullptr, 0);
    }
    state = nextState;

    if (state == GameState::menu && blurCallsLeft > 0) {
      blurTime.start();
      for (int i = 0; i < numBlurCallsPerFrame && blurCallsLeft > 0; ++i) {
        blur(snapshot, --blurCallsLeft);
      }
      blurTime.end();
    }

    frameTime.end();
    if (state == GameState::game) {
      gameFrame.end();
    }
    flipTime.start();

    // Update the screen
    platform.present();

#ifdef DESKTOP
    // Cap the frame rate to ~100 FPS
    int millisToWait = 10 - frame.elapsedMicros()/1000;
    if (millisToWait > 0) SDL_Delay(millisToWait);
#endif
    ++frameCounter;
    flipTime.end();
    if (showFps) {
      uint32_t overallMicros = frameTime.startTime.elapsedMicros();
      timeSum += overallMicros;
      ++timeCount;
      if (timeCount >= 4) {
        uint32_t fps = timeSum ? timeCount*1000000 / timeSum : 0;
        timeCount = timeSum = 0;
        renderer->setFps(fps);
      }
    }
  }
  std::cout << frameTime << std::endl;
  std::cout << gameFrame << std::endl;
  std::cout << blurTime << std::endl;
  std::cout << renderTime << std::endl;
  std::cout << simTime << std::endl;
  std::cout << eventTime << std::endl;
  std::cout << flipTime << std::endl;

  std::cout << std::endl;
  std::cout << "sphereCacheMisses: " << SphereCache::numCacheMisses << std::endl;
  std::cout << "sphereCacheAngleMisses: " << SphereCache::numCacheAngleMisses << std::endl;
  std::cout << "sphereCacheReassignMisses: " << SphereCache::numCacheReassignMisses << std::endl;
  std::cout << "sphereCacheHits: " << SphereCache::numCacheHits << std::endl;

  if (numHighscores) {
    dumpHighscore();
  }

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

void Planets::insertHighscore(int score) {
  int pos = numHighscores;
  for (int i = 0; i < numHighscores; ++i) {
    if (score > highscores[i].score) {
      pos = i;
      break;
    }
  }
  if (pos < numHighscores) {
    int numToMove = numHighscores - pos;
    if (numHighscores >= highscoreCap) --numToMove;
    if (numToMove) memmove(highscores + pos + 1, highscores + pos, sizeof(*highscores) * numToMove);
  }
  if (pos <= highscoreCap - 1) {
    highscores[pos].score = score;
    if (numHighscores < highscoreCap) ++numHighscores;
  }
}

void Planets::dumpHighscore() {
  const char *endings[] = { "th", "st", "nd", "rd" };
  std::cout << "High scores:" << std::endl;
  for (int i = 0; i < numHighscores; ++i) {
    int rank = i + 1;
    int m = rank % 100 / 10, d = rank % 10;
    if (m == 1 || d > 3) d = 0;
    std::cout << rank << endings[d] << " " << highscores[i].score << std::endl;
  }
}

int main(int argc, char **argv) {
  const char *configHome = SDL_getenv("XDG_CONFIG_HOME");
  const char *appData = SDL_getenv("APPDATA");
  const char *home = SDL_getenv("HOME");
  const char *relFile = "/planetmerge/state.bin";
  AutoDeleteArray<char> configFilePath = nullptr;
  AutoDeleteArray<char> customHome = nullptr;
  const char *configFilePathPtr = nullptr;
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
  configFilePathPtr = configFilePath;
  if (argc > 1 && strncmp("--s", argv[1], 5) == 0) {
    return 0;
  } else if (argc > 1) {
    configFilePathPtr = argv[1];
  }
  Planets planets(configFilePathPtr);
  planets.start();
  return 0;
}
