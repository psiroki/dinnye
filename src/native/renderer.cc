#include "renderer.hh"

#include <math.h>
#include <iostream>

#include "image.hh"
#include "font.hh"

#if defined(BITTBOY) || defined(LOREZ)
#define USE_QUICKBLIT
#endif

const Scalar pi = Scalar(float(M_PI));

// Uncomment this if the red and blue seems to be swapped
//#define RED_BLUE_SWAP

int SphereCache::numCacheHits = 0;
int SphereCache::numCacheMisses = 0;
int SphereCache::numCacheAngleMisses = 0;
int SphereCache::numCacheReassignMisses = 0;

#if defined(BITTBOY) || defined(LOREZ)
const unsigned TEXTURE_COORD_BITS = 7;
#else
const unsigned TEXTURE_COORD_BITS = 9;
#endif

const unsigned TEXTURE_SIZE = 1 << TEXTURE_COORD_BITS;
const unsigned TEXTURE_COORD_MASK = TEXTURE_SIZE - 1;

inline uint64_t unpackColor(uint32_t col) {
  return (((col & 0xff000000ULL) << 24) |
      ((col & 0xff0000ULL) << 16) |
      ((col & 0xff00ULL) << 8) |
      col & 0xffULL);
}

inline uint32_t packColor(uint64_t v) {
	return ((v >> 24) & 0xff000000u) |
    ((v >> 16) & 0xff0000u) |
		((v >> 8) & 0xff00u) |
		(v & 0xffu);
}

inline uint32_t ablend(uint32_t col, uint8_t alpha) {
	uint64_t v = unpackColor(col) * alpha;
	return ((v >> 32) & 0xff000000u) |
    ((v >> 24) & 0xff0000u) |
		((v >> 16) & 0xff00u) |
		((v >> 8) & 0xffu);
}

void renderSphereLightmap(PixelBuffer &pb) {
  int cx = pb.width >> 1;
  int cy = pb.height >> 1;
  int minDim = min(pb.width, pb.height);
  int r = (minDim >> 1) - 1;
  float sr = 1.0f / r;
  float r2 = r * r;
  // The light vector is (1, 1, sqrt(7)) normalized
  // The length of that vector is 3
  const float oneOverSqrt3 = 0.577350269f;

  float maskRadius = 0.5f * minDim - 3.0f; // for the sharp outline
  uint32_t *dst = pb.pixels;

  // Iterate over each pixel
  for (int y = 0; y < pb.height; ++y) {
    uint32_t *line = dst;
    for (int x = 0; x < pb.width; ++x) {
      // Calculate sphere coordinates
      float sx = -(x - cx) * sr;
      float sy = -(y - cy) * sr;
      float sz2 = 1.0f - (sx * sx + sy * sy);
      float lambert;

      if (sz2 >= 0.0f) {
        float sz = sqrtf(sz2);
        float dot = (sx + sy + 2.6457513110645906f * sz) * (1.0f / 3.0f);
        dot *= dot * dot;
        lambert = clamp(0.1f, 1.0f, dot * 0.9f + 0.1f);
      } else {
        lambert = 0.0f;
      }

      // Calculate distance from the center
      float dx = x - cx;
      float dy = y - cy;
      float distance = sqrtf(dx * dx + dy * dy);

      // Calculate the color based on the distances
      float mask = clamp(0.0f, 1.0f, 1.0f - (distance - maskRadius));

      // Combine the intensities to create the effect
      float combinedIntensity = lambert * 1.5f;

      // Reinhard tone mapping
      combinedIntensity = combinedIntensity + 14 / 241;
      combinedIntensity = combinedIntensity / (combinedIntensity + 1.0f) * 1.41f;

      // Calculate the grayscale color value
      uint32_t gray = min(1.0f, combinedIntensity) * 255;
      uint32_t alpha = min(255, static_cast<int>(mask * 255));
      if (alpha == 0) gray = 0;

      // Set the pixel color
      line[x] = (alpha << 24) | (gray << 16) | (gray << 8) | gray;
    }
    dst += pb.pitch;
  }
}

namespace {
#ifdef FIXED
  Fixed sinLookup[65536];
#endif
  const SDL_Rect hiresTitleSprites[] = {
    { .x = 0, .y = 0, .w = 640, .h = 123, },
    { .x = 95, .y = 128, .w = 451, .h = 21, },
    { .x = 110, .y = 157, .w = 421, .h = 20, },
    { .x = 92, .y = 185, .w = 456, .h = 15, },
    { .x = 154, .y = 212, .w = 334, .h = 20, },
    { .x = 99, .y = 240, .w = 443, .h = 20, },
    { .x = 124, .y = 268, .w = 394, .h = 20, },
    { .x = 91, .y = 295, .w = 457, .h = 21, },
    { .x = 155, .y = 323, .w = 330, .h = 21, },
    { .x = 108, .y = 352, .w = 426, .h = 20, },
  };
  const SDL_Rect titleSprites[] = {
    { .x = 0, .y = 0, .w = 320, .h = 61, },
    { .x = 47, .y = 64, .w = 227, .h = 11, },
    { .x = 54, .y = 78, .w = 212, .h = 11, },
    { .x = 46, .y = 92, .w = 229, .h = 9, },
    { .x = 76, .y = 106, .w = 168, .h = 11, },
    { .x = 49, .y = 120, .w = 222, .h = 11, },
    { .x = 61, .y = 134, .w = 198, .h = 11, },
    { .x = 45, .y = 147, .w = 230, .h = 11, },
    { .x = 77, .y = 161, .w = 166, .h = 11, },
    { .x = 54, .y = 175, .w = 214, .h = 11, },
  };
  const int numTitleSprites = sizeof(titleSprites) / sizeof(*titleSprites);

  inline int smoothstep(int x, int bits) {
    int y = x * x * ((3 << bits) - 2*x);  // 4096 * 64
    int yb = bits * 3;
    if (yb > 16) y >>= yb - 16;
    if (yb < 16) y <<= 16 - yb;
    return y;
  }
}

void ShadedSphere::initTables() {
#ifdef FIXED  
  for (int i = 0; i < 65536; ++i) {
    sinLookup[i] = sinf(i / 32768.0f * pi);
  }
#endif
}

void ShadedSphere::render(PixelBuffer &target, int cx, int cy, int radius, int angle) {
#ifdef FIXED
  Fixed zoom = Fixed(static_cast<int>(TEXTURE_SIZE >> 1)) / radius;
  int zv = zoom.f;
  int cv = (sinLookup[(angle + 16384) & 0xFFFF] * zoom).f;
  int sv = (sinLookup[angle & 0xFFFF] * zoom).f;
#else
  float zoom = (TEXTURE_SIZE * 0.5f) / radius;
  float rad = angle / 32768.0f * pi;
  int zv = zoom * 65536.0f;
  int cv = cosf(rad) * 65536.0f * zoom;
  int sv = sinf(rad) * 65536.0f * zoom;
#endif
  int w = 2*radius;
  int h = w;
  int p = target.pitch;
  // The matrix is:
  // cv -sv
  // sv  cv
  int u = -w * (cv >> 1) - -h * (sv >> 1) + (TEXTURE_SIZE << 15);
  int v = -w * (sv >> 1) + -h * (cv >> 1) + (TEXTURE_SIZE << 15);
  int s = -w * (zv >> 1) + (TEXTURE_SIZE << 15);
  int t = -h * (zv >> 1) + (TEXTURE_SIZE << 15);
  uint32_t *d = target.pixels +
      (cx - radius) + p*(cy - radius);
  uint32_t *a = albedo.pixels;
  uint32_t *lm = shading;
  for (int y = 0; y <= h; ++y) {
    int lu = u;
    int lv = v;
    int ls = s;
    int rt = (t >> 16) & TEXTURE_COORD_MASK;
    for (int x = 0; x <= w; ++x) {
      int ru = (lu >> 16) & TEXTURE_COORD_MASK;
      int rv = (lv >> 16) & TEXTURE_COORD_MASK;
      int rs = (ls >> 16) & TEXTURE_COORD_MASK;
      int m = lm[rs + (rt << TEXTURE_COORD_BITS)];
      d[x] = ablend(a[ru + (rv << TEXTURE_COORD_BITS)], m & 0xff) | (m & 0xff000000u);
      lu += cv;
      lv += sv;
      ls += zv;
    }
    d += p;
    u += -sv;
    v += cv;
    t += zv;
  }
}

int SphereCache::reassign(ShadedSphere *newSphere, int newRadius, bool newOutlier) {
  if (s == newSphere && radius == newRadius && outlier == newOutlier) return 0;
  int result = 1;
  s = newSphere;
  if (radius != newRadius || outlier != newOutlier) {
    outlier = newOutlier;
    radius = newRadius;
    if (cache) SDL_FreeSurface(cache);
    int extra = outlier ? 2 : 0;
    cache = SDL_CreateRGBSurface(
      SDL_SWSURFACE,
      newRadius*2+1+extra, // Width of the image
      newRadius*2+1+extra, // Height of the image
      32, // Bits per pixel (8 bits per channel * 4 channels = 32 bits)
      0x00ff0000, // Red mask
      0x0000ff00, // Green mask
      0x000000ff, // Blue mask
      0xff000000  // Alpha mask
    );
#ifdef BITTBOY
    SDL_SetColorKey(cache, SDL_SRCCOLORKEY, 0);
#endif
    result = 2;
  }
  dirty = true;
  ++numCacheReassignMisses;
  return result;
}

void SphereCache::release() {
  if (cache) SDL_FreeSurface(cache);
  cache = nullptr;
  radius = 0;
  angle = 0;
  dirty = true;
}

SDL_Surface* SphereCache::withAngle(int newAngle) {
  if (!dirty) {
    int diff = abs(angle - newAngle);
    if (diff >= 32768) diff = 65535 - diff;
    if (diff > 16) dirty = true;
    if (dirty) {
      ++numCacheAngleMisses;
#ifdef DEBUG_VISUALIZATION
      invalidationReason = 2;
#endif
    }
  } else {
#ifdef DEBUG_VISUALIZATION
    invalidationReason = 1;
#endif
  }
  if (dirty) {
    ++numCacheMisses;
    angle = newAngle;

    SurfaceLocker lock(cache);

    PixelBuffer &pb(lock.pb);
    int offset = outlier ? 1 : 0;
    s->render(pb, radius+offset, radius+offset, radius, angle & 0xffff);
    if (outlier) {
      uint32_t *line  = pb.pixels + pb.pitch + 1;
      int h = pb.height - 2;
      int w = pb.width - 2;
      for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
          uint32_t col = line[x];
          uint32_t a = line[x - pb.pitch];
          uint32_t b = line[x + pb.pitch];
          uint32_t c = line[x - 1];
          uint32_t d = line[x + 1];
          if ((a & 0xFFFFFFu) != 0xFFFFFFu && (col & 0xFF000000u) < (a & 0xFF000000u)) {
            line[x] = col + a | 0xFFFFFFu;
          } else if ((b & 0xFFFFFFu) != 0xFFFFFFu && (col & 0xFF000000u) < (b & 0xFF000000u)) {
            line[x] = col + b | 0xFFFFFFu;
          } else if ((c & 0xFFFFFFu) != 0xFFFFFFu && (col & 0xFF000000u) < (c & 0xFF000000u)) {
            line[x] = col + c | 0xFFFFFFu;
          } else if ((d & 0xFFFFFFu) != 0xFFFFFFu && (col & 0xFF000000u) < (d & 0xFF000000u)) {
            line[x] = col + d | 0xFFFFFFu;
          }
        }
        line += pb.pitch;
      }
    }

    lock.unlock();
    dirty = false;
  } else {
#ifdef DEBUG_VISUALIZATION
    invalidationReason = 0;
#endif
    ++numCacheHits;
  }
  return cache;
}

static const char * const imageNames[] = {
  "assets/pluto.png",
  "assets/moon.png",
  "assets/mercury.png",
  "assets/ganymede.png",
  "assets/mars.png",
  "assets/venus.png",
  "assets/earth.png",
  "assets/neptune.png",
  "assets/uranus.png",
  "assets/saturn.png",
  "assets/jupiter.png",
  nullptr,
};

void drawProgressbar(SDL_Surface *target, int position, int numSteps) {
  int width = target->w >> 1;
  int height = target->w >> 5;
  SDL_Rect r;
  r.x = static_cast<Sint16>((target->w - width - 4) >> 1);
  r.y = static_cast<Sint16>((target->h - height - 4) >> 1);
  r.w = static_cast<Uint16>(width + 4);
  r.h = static_cast<Uint16>(height + 4);
  SDL_FillRect(target, &r, 0xFFFFFFFFu);
  r.w = static_cast<Uint16>(width * (numSteps - position) / numSteps);
  if (r.w) {
    r.x += static_cast<Sint16>(2 + width - r.w);
    r.y += 2;
    r.h -= 4;
    SDL_FillRect(target, &r, 0xFF000000u);
  }
  SDL_Flip(target);
}

ScoreCache::~ScoreCache() {
  freeSurface();
}

void ScoreCache::freeSurface() {
  if (rendered) {
    SDL_FreeSurface(rendered);
    rendered = nullptr;
  }
}

SDL_Surface* ScoreCache::render(int newScore) {
  if (!font) return nullptr;
  if (dirty || newScore != score) {
    dirty = false;
    score = newScore;
    char s[256];
    snprintf(s, sizeof(s), "%s: %d", title, score);
    s[255] = 0;
    freeSurface();
    rendered = TTF_RenderText_Blended(font, s, SDL_Color{255, 255, 255});
  }
  return rendered;
}

namespace {
  inline void blurLine(PixelBuffer &pb, int y, int xs, int xe, int xd, int yd) {
    uint32_t *line = pb.pixels + pb.pitch * y + xs;
    if (yd > 0 ? y < pb.height - 1 : y > 0) {
      for (int x = xs; xd < 0 ? x >= xe : x < xe; x += xd) {
        uint64_t c = unpackColor(*line);
        // abyss policy: black
        if (xd < 0 ? x > 0 : x < xe-1) {
          c += unpackColor(line[xd]);
          c += unpackColor(line[xd+yd]);
        }
        c += unpackColor(line[yd]);
        *line = packColor(c >> 2);
        line += xd;
      }
    } else {
      for (int x = xs; xd < 0 ? x >= xe : x < xe; x += xd) {
        uint64_t c = unpackColor(*line);
        uint64_t n = unpackColor((xd < 0 ? x > 0 : x < xe-1) ? line[xd] : *line);
        // abyss policy: black
        c += unpackColor(*line);
        *line = packColor(c >> 2);
        line += xd;
      }
    }
  }

  inline void blur(PixelBuffer pb, bool right, bool down) {
    int xd = right ? 1 : -1;
    int yd = down ? pb.pitch : -pb.pitch;
    int xs = xd < 0 ? pb.width - 1 : 0;
    int xe = xd < 0 ? 0 : pb.width;
    if (yd < 0) {
      for (int y = pb.height - 1; y >= 0; --y) {
        blurLine(pb, y, xs, xe, xd, yd);
      }
    } else {
      for (int y = 0; y < pb.height; ++y) {
        blurLine(pb, y, xs, xe, xd, yd);
      }
    }
  }
}

void blur(SDL_Surface *s, int frame) {
  SurfaceLocker lock(s);
  PixelBuffer &pb(lock.pb);
  bool right = !!(frame & 1);
  bool down = !!((frame+3) & 2);
  blur(pb, right, down);
}

FruitRenderer::FruitRenderer(SDL_Surface *target): target(target), numSpheres(0), highscoreCache("High score") {
  ShadedSphere::initTables();

  numTextures = (sizeof(imageNames) / sizeof(*imageNames)) - 1;
  textures = new SDL_Surface*[numTextures];

  int numSteps = numTextures + 3;
  int currentStep = 0;

  for (int i = 0; i < numTextures; ++i)
    textures[i] = nullptr;
  
  for (int i = 0; i < numTextures; ++i) {
    drawProgressbar(target, currentStep++, numSteps);
    textures[i] = loadImage(imageNames[i]);
#ifdef RED_BLUE_SWAP
    SDL_LockSurface(textures[i]);
    PixelBuffer t(textures[i]);
    int numPixels = t.height * t.pitch;
    for (int j = 0; j < numPixels; ++j) {
      uint32_t c = t.pixels[j];
      uint32_t r = (c & 0xffu) << 16;
      uint32_t b = (c & 0xff0000u) >> 16;
      t.pixels[j] = c & 0xff00ff00u | r | b;
    }
    SDL_UnlockSurface(textures[i]);
#endif
    if (textures[i]->w > TEXTURE_SIZE) {
      SDL_LockSurface(textures[i]);
      PixelBuffer t(textures[i]);
      SDL_Surface *surface = SDL_CreateRGBSurface(
            SDL_SWSURFACE,
            TEXTURE_SIZE, // Width of the image
            TEXTURE_SIZE, // Height of the image
            32, // Bits per pixel (8 bits per channel * 4 channels = 32 bits)
            0x000000ff, // Red mask
            0x0000ff00, // Green mask
            0x00ff0000, // Blue mask
            0xff000000  // Alpha mask
          );
      SDL_LockSurface(surface);
      PixelBuffer target(surface);
      for (int y = 0; y < TEXTURE_SIZE; ++y) {
        int sy = y * t.height / TEXTURE_SIZE;

        const uint32_t *src = t.pixels + t.pitch * sy;
        uint32_t *dst = target.pixels + target.pitch * y;
        int linesToSum = (y+1) * t.height / TEXTURE_SIZE - sy;

        int sxn = 0;
        for (int x = 0; x < TEXTURE_SIZE; ++x) {
          uint64_t sum = 0;

          int sx = sxn / TEXTURE_SIZE;
          sxn += t.width;
          int colsToSum = sxn / TEXTURE_SIZE - sx;

          const uint32_t *srcLine = src + sx;

          for (int v = 0; v < linesToSum; ++v) {
            for (int u = 0; u < colsToSum; ++u) {
              sum += unpackColor(srcLine[u]);
            }
            srcLine += t.pitch;
          }

          uint32_t col = 0;
          uint32_t count = linesToSum * colsToSum;
          for (int i = 0; i < 4; ++i) {
            int ch = sum & 0xffff;
            col |= (ch / count) << (i * 8);
            sum >>= 16;
          }
          dst[x] = col;
        }
      }
      SDL_UnlockSurface(surface);
      SDL_UnlockSurface(textures[i]);
      SDL_FreeSurface(textures[i]);
      textures[i] = surface;
    }
  }

  fontSize = target->h / 25;
  SDL_RWops *rwops = createFontOps();
  font = TTF_OpenFontRW(rwops, 1, fontSize);
  scoreCache.setFont(font);
  highscoreCache.setFont(font);
  if (font) {
    drawProgressbar(target, currentStep++, numSteps);
    for (int i = 0; i < numRadii; ++i) {
      const char *name = imageNames[i];
      PlanetDefinition &def(planetDefs[i]);
      if (!name) {
        def.nameText = nullptr;
      } else {
        const char *start = strrchr(name, '/') + 1;
        const char *end = strchr(start, '.');
        int length = end - start;
        if (length >= sizeof(def.name)) length = sizeof(def.name) - 1;
        strncpy(def.name, start, length);
        def.name[length] = 0;
        def.name[0] &= ~0x20;
        def.nameText = TTF_RenderText_Blended(font, def.name, SDL_Color{255, 255, 255});
      }
    }
  } else {
    for (int i = 0; i < numRadii; ++i) {
      planetDefs[i].nameText = nullptr;
    }
  }

  drawProgressbar(target, currentStep++, numSteps);
  shading = new uint32_t[TEXTURE_SIZE*TEXTURE_SIZE];
  PixelBuffer pb(TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE, shading);
  renderSphereLightmap(pb);

  drawProgressbar(target, currentStep++, numSteps);
  sphereDefs = new ShadedSphere[numTextures];
  for (int i = 0; i < numTextures; ++i) {
    ShadedSphere &s(sphereDefs[i]);
    s.albedo = textures[i];
    s.shading = shading;
  }

  drawProgressbar(target, currentStep++, numSteps);
  title = loadImage("assets/title.png");
}

FruitRenderer::~FruitRenderer() {
  if (font) {
    TTF_CloseFont(font);
    font = nullptr;
  }
  for (int i = 0; i < numTextures; ++i) {
    if (textures[i]) {
      SDL_FreeSurface(textures[i]);
      textures[i] = nullptr;
    }
  }
  if (title) {
    SDL_FreeSurface(title);
    title = nullptr;
  }
  for (int i = 0; i < numRadii; ++i) {
    SDL_Surface *s = planetDefs[i].nameText;
    if (s) SDL_FreeSurface(s);
  }
  delete[] textures;
  textures = nullptr;
  numTextures = 0;
  delete[] shading;
  delete[] sphereDefs;
  sphereDefs = nullptr;
}

SDL_Surface* FruitRenderer::renderText(const char *str, uint32_t color) {
  SDL_Color col;
  col.r = (color >> 16) & 0xFF;
  col.g = (color >> 8) & 0xFF;
  col.b = color & 0xFF;
  return TTF_RenderText_Blended(font, str, col);
}

void FruitRenderer::renderTitle(int taglineSelection, int fade) {
  const SDL_Rect *sprites = target->w < 640 ? titleSprites : hiresTitleSprites;
  SDL_Rect caption = sprites[0];
  int numTaglines = numTitleSprites - 1;
  SDL_Rect tagline = sprites[taglineSelection % numTaglines + 1];
  SDL_Rect captionTarget {
    .x = static_cast<Sint16>(target->w - caption.w >> 1),
    .y = static_cast<Sint16>(target->h/3 - (caption.h + tagline.h) >> 1),
  };
  SDL_Rect taglineTarget {
    .x = static_cast<Sint16>(target->w - tagline.w >> 1),
    .y = static_cast<Sint16>(captionTarget.y + caption.h),
  };
  int bottom = (taglineTarget.y + tagline.h)*5 >> 2;
  int increment = 256*256 / bottom;
  int alpha = 0;
  SurfaceLocker lock(target);
  uint32_t *p = lock.pb.pixels;
  for (int y = 0; y < bottom; ++y) {
    int realAlpha = alpha >> 8;
    if (realAlpha > 255) break;
    for (int x = 0; x < target->w; ++x) {
      p[x] = ablend(p[x], realAlpha);
    }
    p += lock.pb.pitch;
    alpha += increment;
  }
  lock.unlock();
  SDL_BlitSurface(title, &caption, target, &captionTarget);
  SDL_BlitSurface(title, &tagline, target, &taglineTarget);
}

void FruitRenderer::renderLostScreen(int score, int highscore, SDL_Surface *background, uint32_t animationFrame) {
  if (background) SDL_BlitSurface(background, nullptr, target, nullptr);
  SDL_Surface *scoreText = scoreCache.render(score);
  SDL_Surface *highscoreText = highscore > 0 ? highscoreCache.render(highscore) : nullptr;
  if (scoreText) {
    int hsw = highscoreText ? highscoreText->w : 0;
    int hsh = highscoreText ? highscoreText->h : 0;
    int x1 = static_cast<int>(offsetX-scoreText->w) >> 1;
    int y1 = (planetDefs[0].y * 7 / 8 - scoreText->h) >> 1;
    int x2 = target->w - max(scoreText->w, hsw) >> 1;
    int y2 = (target->h - scoreText->h - hsh)  / 3;
    int progress = animationFrame;
    if (progress > 64) {
      progress -= 64;
      x1 = x2;
    } else {
      y2 = y1;
    }
    progress = clamp(0, 64, progress);  // 64
    progress = smoothstep(progress, 6);
    // progress = progress * progress * (3*64 - 2*progress) >> 3;  // 4096 * 64
    // progress = progress * progress * (3*1024 - 2*progress) >> 15;  // 32768
    // progress = progress * (64 - progress);
    // progress = progress * (2048 - progress) >> 5;
    SDL_Rect scorePos {
      .x = static_cast<Sint16>(x1 + ((x2 - x1) * progress >> 16)),
      .y = static_cast<Sint16>(y1 + ((y2 - y1) * progress >> 16)),
    };

    progress = smoothstep(clamp(0, 64, static_cast<int>(animationFrame) - 112), 6);
    x1 = target->w;
    y1 = y2;
    SDL_Rect highscorePos {
      .x = static_cast<Sint16>(x1 + ((x2 - x1) * progress >> 16)),
      .y = static_cast<Sint16>(y1 + ((y2 - y1) * progress >> 16)),
    };
    highscorePos.y += scoreText->h;

    SDL_BlitSurface(scoreText, nullptr, target, &scorePos);
    if (highscoreText) SDL_BlitSurface(highscoreText, nullptr, target, &highscorePos);
  }
}

void FruitRenderer::renderBackground(SDL_Surface *background) {
  // Render gallery
  int radius = zoom * 7 / 12;
  int realRadius = zoom * 2 / 3;
  int availableHeight = target->h - zoom * 7/4;
  int step = (availableHeight - 2*radius) / (numRadii - 1);
  int galleryTop = background->h - (background->h >> 6) - availableHeight - step/2 + zoom / 2;
  int galleryBottom = galleryTop + (numRadii - 1) * step + realRadius * 2;
  int planetLeft = offsetX - (radius * 9 / 2);
  int galleryRight = planetLeft + radius * 11 / 4;
  SurfaceLocker b(background);
  PixelBuffer pb(b.pb);
  int shadeTop = galleryTop - zoom/4;
  int shadeBottom = galleryBottom + zoom/4 + 1 - step / 2;
  for (int y = shadeTop; y < shadeBottom; ++y) {
    uint32_t *line = pb.pixels + pb.pitch * y;
    int yp = (shadeBottom - y) * 256 / (shadeBottom - galleryTop);
    yp = 255 - ((255 - yp) * (255 - yp) >> 8);
    if (yp <= 1) continue;
    int lineEnd = galleryRight;
    if (y < shadeTop + 8) {
      float yv = shadeTop + 8 - y;
      int x = sqrtf(64 - yv * yv);
      lineEnd -= 8 - x;
    }
    for (int x = 0; x < lineEnd; ++x) {
      uint32_t col = line[x];
      int redInvert = 0xff - ((col >> 16) & 0xff);
      int alpha = (redInvert * 256 / (redInvert + 256));
      alpha = 255-((255 - alpha) * yp >> 8);
      uint32_t shaded = ablend(col, alpha);
      line[x] = shaded;
    }
  }
  b.unlock();
  for (int i = 0; i < numRadii; ++i) {
    SphereCache &sc(spheres[i]);
    sc.reassign(sphereDefs + i, realRadius);
    SDL_Surface *s = sc.withAngle(0);
    int y = galleryTop + i * step;
    SDL_Rect dst;
    dst.x = static_cast<Sint16>(planetLeft);
    dst.y = static_cast<Sint16>(y+1);
    SDL_BlitSurface(s, nullptr, background, &dst);

    PlanetDefinition &def(planetDefs[i]);
    def.x = planetLeft;
    def.y = y;
    def.w = s->w;
    def.h = s->h;

    SDL_Surface *text = def.nameText;
    if (text) {
      dst.x = static_cast<Sint16>(planetLeft - text->w - (radius + 1) / 2);
      dst.y = static_cast<Sint16>(y + radius - text->h * 9 / 16 + fontSize / 8);
      SDL_BlitSurface(text, nullptr, background, &dst);
    }
  }
  SurfaceLocker lock(background);
  pb = lock.pb;
  int left = offsetX;
  int right = offsetX + sizeX * zoom;
  int bottom = target->h;
  int top = bottom - sizeY * zoom;
  for (int y = top; y < bottom; ++y) {
    uint32_t *line = pb.pixels + pb.pitch * y;
    int shadowLeft = max(0, left - 8);
    int shadowRight = min(pb.width, right + 8);
    for (int x = shadowLeft; x < left; ++x) {
      line[x] = ablend(line[x], 0x80);
    }
    for (int x = left; x < right; ++x) {
      line[x] = ablend(line[x], 0xC0);
    }
    for (int x = right; x < shadowRight; ++x) {
      line[x] = ablend(line[x], 0x80);
    }
  }
#ifdef BITTBOY
  Random noise(1337);
#endif
  for (int y = 0; y < pb.height; ++y) {
    uint32_t *line = pb.pixels + y * pb.pitch;
    for (int x = 0; x < pb.width; ++x) {
      uint32_t col = line[x] & 0xFFFFFFu;
#ifdef BITTBOY
      uint32_t ncol = 0;
      for (int i = 0; i < 3; ++i) {
        int ch = (col >> (i * 8)) & 0xff;
        int n = noise()&7;
        switch (n) {
          case 0: n = 0; break;
          case 1: n = 1; break;
          case 2: n = 2; break;
          case 3: n = 3; break;
          case 4: n = 2; break; // Higher probability for 2
          case 5: n = 3; break; // Higher probability for 3
          case 6: n = 4; break;
          case 7: n = 4; break; // Higher probability for 4
          default: n = 0; break; // Safeguard
        }
        ch += (n) - 2;
        if (ch < 0) ch = 0;
        if (ch > 255) ch = 255;
        ncol |= ch << (i * 8);
      }
      col = ncol;
#endif
      line[x] = col;
    }
  }
  lock.unlock();
  // these caches won't be needed anymore
  for (int i = 0; i < numRadii; ++i) {
    spheres[i].release();
  }
}

void quickBlit(PixelBuffer src, PixelBuffer dst, int x, int y) {
  int w = src.width;
  int h = src.height;
  uint32_t *s = src.pixels;
  if (x < 0) {
    w += x;
    s -= x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    s -= y * src.pitch;
    y = 0;
  }
  if (x + w > dst.width) {
    w = dst.width - x;
  }
  if (y + h > dst.height) {
    h = dst.height - y;
  }
  if (w <= 0 || h <= 0) return;
  if (x < 0 || y < 0) {
    return;
  }
  uint32_t *d = dst.pixels + x + y * dst.pitch;
  for (int py = 0; py < h; ++py) {
    uint32_t *dl = d;
    uint32_t *sl = s;
    for (int px = 0; px < w; ++px) {
      uint32_t col = *sl++;
     if (col) *dl = col;
      ++dl;
    }
    d += dst.pitch;
    s += src.pitch;
  }
}

void FruitRenderer::renderSelection(PixelBuffer pb, int left, int top, int right, int bottom, int shift, bool hollow) {
  left = (left << 2) + 3;
  right = (right << 2) + 3;
  for (int y = top; y < bottom; ++y) {
    uint32_t *line = pb.pixels + pb.pitch * y;
    int r = right-- >> 2;
    int l = left-- >> 2;
    if (l < 0) l = 0;
    int xi = hollow && y > top && y < bottom - 1 ? r - l - 1 : 1;
    for (int x = l; x < r; x += xi) {
      uint32_t col = line[x+shift];
      int red = (col >> 16) & 0xFF;
      line[x] = 0xFFFFFFFF - ablend(col, red);
    }
  }
}

void FruitRenderer::renderFruits(FruitSim &sim, int count, int selection, int outlierIndex, uint32_t frameIndex, bool skipScore) {
  Fruit *fruits = sim.getFruits();
  if (!skipScore) {
    int score = sim.getScore();
    SDL_Surface *scoreText = scoreCache.render(score);
    if (scoreText) {
      SDL_Rect scorePos {
        .x = static_cast<Sint16>(static_cast<int>(offsetX-scoreText->w) >> 1),
        .y = static_cast<Sint16>((planetDefs[0].y * 7 / 8 - scoreText->h) >> 1),
      };
      SDL_BlitSurface(scoreText, nullptr, target, &scorePos);
    }
  }
  // Render selection
  if (selection >= 0 && selection < numRadii) {
    PlanetDefinition &def(planetDefs[selection]);
    SDL_Rect rect {
      .x = static_cast<Sint16>(def.x + def.w + 2),
      .y = static_cast<Sint16>(def.y + def.h / 8),
      .w = 4,
      .h = static_cast<Uint16>(def.h * 6 / 8),
    };
    int top = rect.y;
    int bottom = rect.y + rect.h;
    int left = 0;
    int right = def.x;

    SurfaceLocker targetLock(target);
    PixelBuffer &pb(targetLock.pb);
    renderSelection(pb, left, top, right, bottom, 2);
    targetLock.unlock();
  }

  int bottom = target->h;
  int top = bottom - sizeY * zoom;

  // Render drop line
  if (sim.getNumFruits() < count) {
    SurfaceLocker lock(target);
    const Fruit &f(fruits[count - 1]);
    int x = f.pos.x * zoom + offsetX;
    int startY = f.pos.y * zoom + top;
    uint32_t *p = lock.pb.pixels + x + startY * lock.pb.pitch;

    int alpha = 0x40;
    uint32_t premultiplied = alpha | (alpha << 8) | (alpha << 16);
    alpha = 0xFF - alpha;
    for (int y = startY; y < target->h; ++y) {
      *p = ablend(*p, alpha) + premultiplied;
      p += lock.pb.pitch;
    }
  }

#ifdef USE_QUICKBLIT
  SurfaceLocker sl(target);
#endif
  // Render playfield
  int32_t above[fruitCap];
  int numAbove = 0;
  for (int i = 0; i < count; ++i) {
    int index = i == 0 ? count - 1 : i - 1;
    Fruit &f(fruits[index]);
    SphereCache &sc(spheres[index + numRadii]);
    int radius = f.r * zoom;
    int reassignResult = sc.reassign(sphereDefs + f.rIndex, radius, index == outlierIndex);
    SDL_Surface *s = sc.withAngle((-f.rotation) & 0xffff);
    SDL_Rect dst;
#ifdef DEBUG_VISUALIZATION
    int invReason = sc.getInvalidationReason();
    bool grounded = f.bottomTouchFrame == frameIndex;
    if (invReason || reassignResult || grounded) {
      dst.x = static_cast<Sint16>(f.pos.x * zoom - radius + offsetX);
      dst.y = static_cast<Sint16>(f.pos.y * zoom - radius);
      dst.h = dst.w = radius << 1;
      uint32_t color = invReason * 0x7F | reassignResult * 0x7F00 | (grounded ? 0xFF0000 : 0) | 0xFF000000u;
      SDL_FillRect(target, &dst, color);
      memset(&dst, 0, sizeof(dst));
    }
#endif
    int screenX = f.pos.x * zoom - radius + offsetX;
    int screenY = f.pos.y * zoom - radius + top;
    if (screenY < -s->h) {
      if (screenY < -32768) screenY = -32768;
      above[numAbove++] = static_cast<uint32_t>(screenY) << 16 | (screenX & 0xFFFF);
    } else {
#ifdef USE_QUICKBLIT
      quickBlit(s, sl.pb, screenX, screenY);
#else
      dst.x = static_cast<Sint16>(screenX);
      dst.y = static_cast<Sint16>(screenY);
      SDL_BlitSurface(s, nullptr, target, &dst);
#endif
    }
  }
#ifdef USE_QUICKBLIT
  sl.unlock();
#endif

  if (numAbove) {
    SurfaceLocker lock(target);
    PixelBuffer pb(lock.pb);
    for (int i = 0; i < numAbove; ++i) {
      int32_t v = above[i];
      int fx = v & 0xFFFF;
      int fy = v >> 16;
      int iconSize = 3 + (-fy / 2 * zoom / target->h);
      if (iconSize > 16) iconSize = 16;
      for (int y = 0; y < iconSize; ++y) {
        uint32_t *line = pb.pixels + pb.pitch * y;
        int size = (y >> 1)*2 + 1;
        line += fx - (size >> 1);
        for (int x = 0; x < size; ++x) {
          line[x] = 0xFFE0E0E0u;
        }
      }
    }
  }

  for (int i = count; i < numSpheres; ++i) {
    spheres[i + numRadii].release();
  }
  numSpheres = count;
}
