#pragma once

#include <SDL/SDL.h>
#include <stdint.h>

#include "../common/sim.hh"

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
    width = s->w;
    height = s->h;
    pitch = s->pitch >> 2;
    pixels = reinterpret_cast<uint32_t*>(s->pixels);
  }
};

struct ShadedSphere {
  PixelBuffer albedo;
  uint32_t *shading;

  void render(PixelBuffer &target, int cx, int cy, int radius, int angle);
};

class SphereCache {
  ShadedSphere *s;
  SDL_Surface *cache;
  int radius;
  int angle;
  bool dirty;

public:
  inline SphereCache(): s(nullptr), cache(nullptr), radius(0), angle(0), dirty(false) { }
  
  inline ~SphereCache() {
    if (cache) {
      SDL_FreeSurface(cache);
      cache = nullptr;
    }
  }

  void reassign(ShadedSphere *newSphere, int newRadius);
  void release();
  SDL_Surface* withAngle(int newAngle);
};

class FruitRenderer {
  SDL_Surface **textures;
  int numTextures;
  uint32_t *shading;
  SphereCache spheres[fruitCap + numRadii];
  int numSpheres;
  ShadedSphere *sphereDefs;
  SDL_Surface *target;
public:
  FruitRenderer(SDL_Surface *target);
  ~FruitRenderer();

  void renderFruits(Fruit *fruits, int count, float zoom, float offsetX);
};
