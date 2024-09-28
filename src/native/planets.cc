#include <SDL/SDL.h>
#include <iostream>
#include <time.h>
#include <string.h>

#include "../common/sim.hh"
#include "renderer.hh"
#include "util.hh"
#include "image.hh"

enum class Control {
  UNMAPPED, UP, DOWN, LEFT, RIGHT, NORTH, SOUTH, WEST, EAST, R1, L1, R2, L2, START, SELECT, MENU, LAST_ITEM,
};

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
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return nullptr;
  }

  if (width == 0 || height == 0) {
    const SDL_VideoInfo *videoInfo = SDL_GetVideoInfo();
    width = videoInfo->current_w;
    height = videoInfo->current_h;
  }

  SDL_Surface *screen = SDL_SetVideoMode(width, height, 32, SDL_HWSURFACE | SDL_DOUBLEBUF | SDL_FULLSCREEN);
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

int main() {
  SDL_Surface *screen = initSDL(0, 0);
  if (!screen) return 1;

  bool running = true;
  SDL_Event event;

  FruitRenderer renderer(screen);
  FruitSim sim;

  timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);

  uint32_t seed = time.tv_nsec;
  sim.init(seed);

  float zoom = screen->h / sim.getWorldHeight();
  float offsetX = (screen->w - sim.getWorldWidth() * zoom) * 0.5f;
  SDL_Surface *background = loadImage(screen->w <= 640 ? "assets/background.png" : "assets/hi_background.jpg");
  NextPlacement next = { .x = 0.0f, .xv = 0.0f };
  next.reset(sim, seed);

  ControlState controls;
  memset(&controls, 0, sizeof(controls));
  bool placed = false;

  while (running) {
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
            c = Control::SOUTH;
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
    } else if (!(controls[Control::EAST] || controls[Control::SOUTH])) {
      placed = false;
    }

    next.step(sim);

    Fruit *fruits = sim.simulate(++seed);
    int count = sim.getNumFruits();

    next.setupPreview(sim);

    if (background->w < screen->w || background->h < screen->h)
      SDL_FillRect(screen, nullptr, 0);
    SDL_Rect bgPos = {
      .x = static_cast<Sint16>((screen->w - background->w) / 2),
      .y = static_cast<Sint16>((screen->h - background->h) / 2),
    };
    SDL_BlitSurface(background, nullptr, screen, &bgPos);

    renderer.renderFruits(fruits, count + 1, zoom, offsetX);

    // Update the screen
    SDL_Flip(screen);

    // Cap the frame rate to ~100 FPS
    int millisToWait = 10 - frame.elapsedSeconds()*1000.0f;
    if (millisToWait > 0) SDL_Delay(millisToWait);
  }

  SDL_Quit();
}
