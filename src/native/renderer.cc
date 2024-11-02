#include "renderer.hh"

#include <math.h>
#include <iostream>

#include "image.hh"
#include "roboto.hh"

#ifdef BITTBOY
#define USE_QUICKBLIT
#endif

const Scalar pi = Scalar(float(M_PI));

// Uncomment this if the red and blue seems to be swapped
//#define RED_BLUE_SWAP

int SphereCache::numCacheHits = 0;
int SphereCache::numCacheMisses = 0;
int SphereCache::numCacheAngleMisses = 0;
int SphereCache::numCacheReassignMisses = 0;

#ifdef BITTBOY
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
      line[x] = (alpha << 24) | (gray << 16) << (gray << 8) | gray;
    }
    dst += pb.pitch;
  }
}

namespace {
#ifdef FIXED
  Fixed sinLookup[65536];
#endif

  struct SurfaceLocker {
    SDL_Surface *surface;
    PixelBuffer pb;

    inline SurfaceLocker(SDL_Surface *surface=nullptr): surface(surface), pb(nullptr) {
      if (surface && SDL_MUSTLOCK(surface)) {
        SDL_LockSurface(surface);
      }
      pb = surface;
    }

    inline void unlock() {
      if (surface) {
        if (SDL_MUSTLOCK(surface)) {
          SDL_LockSurface(surface);
        }
        pb = surface = nullptr;
      }
    }

    inline SDL_Surface* operator=(SDL_Surface *s) {
      unlock();
      surface = s;
      if (surface && SDL_MUSTLOCK(surface)) {
        SDL_LockSurface(surface);
      }
      pb = surface;
      return surface;
    }

    inline ~SurfaceLocker() {
      unlock();
    }
  };
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
  s = nullptr;
  cache = nullptr;
  radius = 0;
  angle = 0;
  dirty = false;
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
    snprintf(s, sizeof(s), "Score: %d", score);
    freeSurface();
    rendered = TTF_RenderText_Blended(font, s, SDL_Color{255, 255, 255});
  }
  return rendered;
}

FruitRenderer::FruitRenderer(SDL_Surface *target): target(target), numSpheres(0) {
  ShadedSphere::initTables();

  numTextures = (sizeof(imageNames) / sizeof(*imageNames)) - 1;
  textures = new SDL_Surface*[numTextures];

  for (int i = 0; i < numTextures; ++i)
    textures[i] = nullptr;
  
  for (int i = 0; i < numTextures; ++i) {
    drawProgressbar(target, i, numTextures + 2);
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
  SDL_RWops *rwops = createRobotoOps();
  font = TTF_OpenFontRW(rwops, 1, fontSize);
  scoreCache.setFont(font);
  if (font) {
    drawProgressbar(target, numTextures, numTextures + 2);
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

  drawProgressbar(target, numTextures + 1, numTextures + 2);
  shading = new uint32_t[TEXTURE_SIZE*TEXTURE_SIZE];
  PixelBuffer pb(TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE, shading);
  renderSphereLightmap(pb);

  drawProgressbar(target, numTextures + 2, numTextures + 2);
  sphereDefs = new ShadedSphere[numTextures];
  for (int i = 0; i < numTextures; ++i) {
    ShadedSphere &s(sphereDefs[i]);
    s.albedo = textures[i];
    s.shading = shading;
  }
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
  delete[] textures;
  textures = nullptr;
  numTextures = 0;
  delete[] shading;
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
      dst.x = static_cast<Sint16>(planetLeft - text->w - 8);
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
  for (int y = 0; y < pb.height; ++y) {
    uint32_t *line = pb.pixels + y * pb.pitch;
    for (int x = 0; x < pb.width; ++x) {
      line[x] = line[x] & 0xFFFFFFu;
    }
  }
  lock.unlock();
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

void FruitRenderer::renderFruits(FruitSim &sim, int count, int selection, int outlierIndex, uint32_t frameIndex) {
  Fruit *fruits = sim.getFruits();
  int score = sim.getScore();
  SDL_Surface *scoreText = scoreCache.render(score);
  if (scoreText) {
    SDL_Rect scorePos {
      .x = static_cast<Sint16>(static_cast<int>(offsetX-scoreText->w) >> 1),
      .y = static_cast<Sint16>((planetDefs[0].y * 7 / 8 - scoreText->h) >> 1),
    };
    SDL_BlitSurface(scoreText, nullptr, target, &scorePos);
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
    SurfaceLocker targetLock(target);
    PixelBuffer &pb(targetLock.pb);
    int bottom = rect.y + rect.h;
    int right = def.x << 2;
    for (int y = rect.y; y < bottom; ++y) {
      uint32_t *line = pb.pixels + pb.pitch * y;
      int r = right-- >> 2;
      for (int x = 0; x < r; ++x) {
        uint32_t col = line[x+2];
        int red = (col >> 16) & 0xFF;
        line[x] = 0xFFFFFFFF - ablend(col, red);
      }
    }
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
      above[numAbove++] = screenY << 16 | (screenX & 0xFFFF);
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
