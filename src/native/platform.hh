#pragma once 

#ifdef USE_SDL2
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#else
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#endif

class Platform {
  SDL_Surface *screen;
  int width, height;
  int orientation;
#ifdef USE_SDL2
  SDL_Window* window;
  SDL_Renderer* renderer;
  SDL_Texture* texture;        // Texture to display the final surface
#endif
public:
  Platform();
  SDL_Surface *initSDL(int width, int height, int orientation = 0);
  SDL_Surface* displayFormat(SDL_Surface *src);
  SDL_Surface* createSurface(int width, int height);
  void makeOpaque(SDL_Surface *s, bool opaque = true);
  void present();
};

inline SDL_Rect makeRect(int x, int y, int w = 0, int h = 0) {
  return {
    .x = static_cast<Sint16>(x),
    .y = static_cast<Sint16>(y),
    .w = static_cast<Uint16>(w),
    .h = static_cast<Uint16>(h),
  };
}
