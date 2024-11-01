#pragma once

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <stdint.h>

#include "../common/sim.hh"

template <typename T> T min(T a, T b) {
  return a < b ? a : b;
}

template <typename T> T max(T a, T b) {
  return a > b ? a : b;
}

template <typename T> T clamp(T min, T max, T val) {
  if (val < min) return min;
  if (val > max) return max;
  return val;
}

struct PixelBuffer {
  /// Pointer to the 32 bit pixels
  uint32_t *pixels;
  /// Width in pixels
  int width;
  /// Height in pixels
  int height;
  /// Word pitch
  int pitch;

  inline PixelBuffer(int width = 0, int height = 0, int pitch = 0, uint32_t *pixels = nullptr):
    width(width),
    height(height),
    pitch(pitch),
    pixels(pixels) {
  }

  inline PixelBuffer(SDL_Surface *s) {
    *this = s;
  }

  inline void operator=(SDL_Surface *s) {
    if (s) {
      width = s->w;
      height = s->h;
      pitch = s->pitch >> 2;
      pixels = reinterpret_cast<uint32_t*>(s->pixels);
    } else {
      width = height = pitch = 0;
      pixels = nullptr;
    }
  }
};

struct ShadedSphere {
  PixelBuffer albedo;
  uint32_t *shading;

  static void initTables();
  void render(PixelBuffer &target, int cx, int cy, int radius, int angle);
};

class SphereCache {
  ShadedSphere *s;
  SDL_Surface *cache;
  int radius;
  int angle;
  bool outlier;
  bool dirty;
#ifdef DEBUG_VISUALIZATION
  int invalidationReason;
#endif
  friend class FruitRenderer;

public:
  static int numCacheHits;
  static int numCacheMisses;
  static int numCacheAngleMisses;
  static int numCacheReassignMisses;

  inline SphereCache(): s(nullptr), cache(nullptr), radius(0), angle(0), dirty(false) { }
  
  inline ~SphereCache() {
    if (cache) {
      SDL_FreeSurface(cache);
      cache = nullptr;
    }
  }

  int reassign(ShadedSphere *newSphere, int newRadius, bool outlier = false);
  void release();
  SDL_Surface* withAngle(int newAngle);

#ifdef DEBUG_VISUALIZATION
  int getInvalidationReason() {
    return invalidationReason;
  }
#endif
};

class ScoreCache {
  SDL_Surface *rendered;
  int score;
  TTF_Font *font;
  bool dirty;

  void freeSurface();
public:
  inline ScoreCache(): rendered(0), score(-1), font(0), dirty(false) { }
  ~ScoreCache();

  inline void setFont(TTF_Font *newFont) {
    font = newFont;
    dirty = true;
  }

  SDL_Surface* render(int newScore);
};

struct PlanetDefinition {
  char name[16];
  SDL_Surface *nameText;
  // Preview placement
  int x, y;
  int w, h;
};

class FruitRenderer {
  SDL_Surface **textures;
  PlanetDefinition planetDefs[numRadii];
  int numTextures;
  uint32_t *shading;
  SphereCache spheres[fruitCap + numRadii];
  int numSpheres;
  ShadedSphere *sphereDefs;
  SDL_Surface *target;
  Scalar zoom;
  Scalar offsetX;
  Scalar sizeX, sizeY;
  int fontSize;
  TTF_Font *font;
  ScoreCache scoreCache;
public:
  FruitRenderer(SDL_Surface *target);
  ~FruitRenderer();

  inline void setLayout(Scalar newZoom, Scalar newOffsetX, const FruitSim &sim) {
    zoom = newZoom;
    offsetX = newOffsetX;
    sizeX = sim.getWorldWidth();
    sizeY = sim.getWorldHeight();
  }
  void renderBackground(SDL_Surface *background);
  void renderFruits(FruitSim &sim, int count, int selection, int outlierIndex, uint32_t frameIndex);
};
