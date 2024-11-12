#include "platform.hh"

#include <iostream>

Platform::Platform():
#ifdef USE_SDL2
  window(nullptr),
  renderer(nullptr),
  texture(nullptr),
#endif
  screen(nullptr) { }

// Initialize SDL and create a window with the specified dimensions
SDL_Surface* Platform::initSDL(int w, int h, int o) {
  width = w;
  height = h;
  orientation = o;
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
    std::cerr << "Failed to initialize SDL: " << SDL_GetError() << std::endl;
    return nullptr;
  }

  if (TTF_Init() < 0) {
    std::cerr << "Failed to initialize SDL_ttf: " << TTF_GetError() << std::endl;
    SDL_Quit();
    return nullptr;
  }

#ifdef USE_SDL2
  bool fullscreen = (width == 0 || height == 0);
  if (fullscreen) {
    SDL_DisplayMode displayMode;
    if (SDL_GetCurrentDisplayMode(0, &displayMode) != 0) {
      std::cerr << "Failed to get display mode: " << SDL_GetError() << std::endl;
      TTF_Quit();
      SDL_Quit();
      return nullptr;
    }
    width = displayMode.w;
    height = displayMode.h;
  }

  Uint32 windowFlags = SDL_WINDOW_SHOWN | (fullscreen ? SDL_WINDOW_FULLSCREEN : 0);
  window = SDL_CreateWindow("Planet Merge", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, windowFlags);
  if (!window) {
      std::cerr << "Failed to create window: " << SDL_GetError() << std::endl;
      TTF_Quit();
      SDL_Quit();
      return nullptr;
  }

  // Create a renderer with vsync enabled, compatible with SDL2's double-buffering
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
      std::cerr << "Failed to create renderer: " << SDL_GetError() << std::endl;
      SDL_DestroyWindow(window);
      TTF_Quit();
      SDL_Quit();
      return nullptr;
  }

  // Optional: get the window surface if needed (e.g., for software rendering)
  if (!orientation) {
    screen = SDL_GetWindowSurface(window);
  } else {
    int sw = orientation & 1 ? height : width;
    int sh = orientation & 1 ? width : height;
    // Create the screen surface for software rendering (always use the unrotated dimensions)
    screen = SDL_CreateRGBSurface(0, sw, sh, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000);
    if (!screen) {
      std::cerr << "Failed to create screen surface: " << SDL_GetError() << std::endl;
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      TTF_Quit();
      SDL_Quit();
      return nullptr;
    }

    // Create a texture to present the final image on screen
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, sw, sh);
    if (!texture) {
      std::cerr << "Failed to create texture: " << SDL_GetError() << std::endl;
      SDL_FreeSurface(screen);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      TTF_Quit();
      SDL_Quit();
      return nullptr;
    }

    if (SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE) < 0) {
      std::cerr << "Failed to set blend mode on texture: " << SDL_GetError() << std::endl;
      SDL_FreeSurface(screen);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      TTF_Quit();
      SDL_Quit();
      return nullptr;
    }
  }

  if (SDL_SetSurfaceBlendMode(screen, SDL_BLENDMODE_NONE) < 0) {
    std::cerr << "Failed to set blend mode on surface: " << SDL_GetError() << std::endl;
    SDL_FreeSurface(screen);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    return nullptr;
  }
#else
  bool fullscreen = width == 0 || height == 0;
  if (fullscreen) {
    const SDL_VideoInfo *videoInfo = SDL_GetVideoInfo();
    width = videoInfo->current_w;
    height = videoInfo->current_h;
  }

  screen = SDL_SetVideoMode(width, height, 32, SDL_HWSURFACE |
      SDL_DOUBLEBUF |
      (fullscreen ? SDL_FULLSCREEN : 0));
  if (screen == nullptr) {
    std::cerr << "Failed to set video mode: " << SDL_GetError() << std::endl;
    return nullptr;
  }
#endif

  return screen;
}

SDL_Surface* Platform::displayFormat(SDL_Surface *src) {
#ifdef USE_SDL2
  SDL_Surface *result = SDL_CreateRGBSurfaceWithFormat(
      0,                               // Flags (0 for no specific flags)
      src->w, src->h,
      screen->format->BitsPerPixel,   // Bit depth
      screen->format->format          // Pixel format from the screen surface
  );
  SDL_BlitSurface(src, nullptr, result, nullptr);
  return result;
#else
  return SDL_DisplayFormatAlpha(src);
#endif
}

SDL_Surface* Platform::createSurface(int width, int height) {
  return SDL_CreateRGBSurface(
#ifdef USE_SDL2
    0,
#else
    SDL_SWSURFACE,
#endif
    width, // Width of the image
    height, // Height of the image
    32, // Bits per pixel (8 bits per channel * 4 channels = 32 bits)
    0x00ff0000, // Red mask
    0x0000ff00, // Green mask
    0x000000ff, // Blue mask
    0xff000000  // Alpha mask
  );
}

void Platform::makeOpaque(SDL_Surface *s, bool opaque) {
#ifdef USE_SDL2
  SDL_SetSurfaceBlendMode(s, opaque ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
#else
  SDL_SetAlpha(s, opaque ? 0 : SDL_SRCALPHA, 255);
#endif
}

void Platform::present() {
#ifdef USE_SDL2
  if (!orientation) {
    //SDL_RenderPresent(renderer);
    SDL_UpdateWindowSurface(window);
  } else {
    SDL_UpdateTexture(texture, nullptr, screen->pixels, screen->pitch);
    SDL_Rect dst {
      .x = (width - screen->w) >> 1,
      .y = (height - screen->h) >> 1,
      .w = screen->w,
      .h = screen->h,
    };
    SDL_RenderCopyEx(renderer, texture, nullptr, &dst, orientation * 90.0, nullptr, SDL_FLIP_NONE);
    SDL_RenderPresent(renderer);
  }
#else
  SDL_Flip(screen);
#endif
}
