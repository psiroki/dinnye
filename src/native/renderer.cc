#include "renderer.hh"

#include <math.h>
#include <SDL/SDL_ttf.h>

#include "image.hh"
#include "roboto.hh"
#include "../common/sim.hh"

const float pi = M_PI;

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

void renderSphere(PixelBuffer &pb) {
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

int SphereCache::reassign(ShadedSphere *newSphere, int newRadius) {
  if (s == newSphere && radius == newRadius) return 0;
  int result = 1;
  s = newSphere;
  if (radius != newRadius) {
    radius = newRadius;
    if (cache) SDL_FreeSurface(cache);
    cache = SDL_CreateRGBSurface(
      SDL_SWSURFACE,
      newRadius*2+1, // Width of the image
      newRadius*2+1, // Height of the image
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

    if (SDL_MUSTLOCK(cache)) {
      SDL_LockSurface(cache);
    }

    PixelBuffer pb(cache);
    s->render(pb, radius, radius, radius, angle & 0xffff);

    if (SDL_MUSTLOCK(cache)) {
      SDL_UnlockSurface(cache);
    }
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

FruitRenderer::FruitRenderer(SDL_Surface *target): target(target), numSpheres(0) {
  ShadedSphere::initTables();

  numTextures = (sizeof(imageNames) / sizeof(*imageNames)) - 1;
  textures = new SDL_Surface*[numTextures];

  for (int i = 0; i < numTextures; ++i)
    textures[i] = nullptr;
  
  for (int i = 0; i < numTextures; ++i) {
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
  TTF_Font *font = TTF_OpenFontRW(rwops, 1, fontSize);
  if (font) {
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
    TTF_CloseFont(font);
  } else {
    for (int i = 0; i < numRadii; ++i) {
      planetDefs[i].nameText = nullptr;
    }
  }

  shading = new uint32_t[TEXTURE_SIZE*TEXTURE_SIZE];
  PixelBuffer pb(TEXTURE_SIZE, TEXTURE_SIZE, TEXTURE_SIZE, shading);
  renderSphere(pb);

  sphereDefs = new ShadedSphere[numTextures];
  for (int i = 0; i < numTextures; ++i) {
    ShadedSphere &s(sphereDefs[i]);
    s.albedo = textures[i];
    s.shading = shading;
  }
}

FruitRenderer::~FruitRenderer() {
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
  for (int i = 0; i < numRadii; ++i) {
    SphereCache &sc(spheres[i]);
    int availableHeight = target->h - zoom;
    int step = (availableHeight - 2*radius) / (numRadii - 1);
    sc.reassign(sphereDefs + i, zoom * 2 / 3);
    SDL_Surface *s = sc.withAngle(0);
    int x = offsetX - (radius * 9 / 2);
    int y = i * step + zoom / 2;
    SDL_Rect dst;
    dst.x = static_cast<Sint16>(x);
    dst.y = static_cast<Sint16>(y);
    SDL_BlitSurface(s, nullptr, background, &dst);

    PlanetDefinition &def(planetDefs[i]);
    def.x = x;
    def.y = y;
    def.w = s->w;
    def.h = s->h;

    SDL_Surface *text = def.nameText;
    if (text) {
      dst.x = static_cast<Sint16>(x - text->w - 8);
      dst.y = static_cast<Sint16>(y + radius - text->h / 2 + fontSize / 8);
      SDL_BlitSurface(text, nullptr, background, &dst);
    }
  }
  if (SDL_MUSTLOCK(background)) {
    SDL_LockSurface(background);
  }
  PixelBuffer pb(background);
  int left = offsetX;
  int right = offsetX + sizeX * zoom;
  int top = 0;
  int bottom = sizeY * zoom; 
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
  if (SDL_MUSTLOCK(background)) {
    SDL_UnlockSurface(background);
  }
}

void FruitRenderer::renderFruits(Fruit *fruits, int count, int selection) {
  // Render selection
  if (selection >= 0 && selection < numRadii) {
    PlanetDefinition &def(planetDefs[selection]);
    SDL_Rect rect {
      .x = static_cast<Sint16>(def.x + def.w + 2),
      .y = static_cast<Sint16>(def.y + def.h / 8),
      .w = 4,
      .h = static_cast<Uint16>(def.h * 6 / 8),
    };
    if (SDL_MUSTLOCK(target)) {
      SDL_LockSurface(target);
    }
    PixelBuffer pb(target);
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
    if (SDL_MUSTLOCK(target)) {
      SDL_UnlockSurface(target);
    }
    //SDL_FillRect(target, &rect, 0xFFFFFFFFu);
  }
  // Render playfield
  for (int i = 0; i < count; ++i) {
    Fruit &f(fruits[i]);
    SphereCache &sc(spheres[i + numRadii]);
    int radius = f.r * zoom;
    int reassignResult = sc.reassign(sphereDefs + f.rIndex, radius);
    SDL_Surface *s = sc.withAngle((-f.rotation) & 0xffff);
    SDL_Rect dst;
#ifdef DEBUG_VISUALIZATION
    int invReason = sc.getInvalidationReason();
    if (invReason || reassignResult) {
      dst.x = static_cast<Sint16>(f.pos.x * zoom - radius + offsetX);
      dst.y = static_cast<Sint16>(f.pos.y * zoom - radius);
      dst.h = dst.w = radius << 1;
      uint32_t color = invReason * 0x7F | reassignResult * 0x7F00 | 0xFF000000u;
      SDL_FillRect(target, &dst, color);
      memset(&dst, 0, sizeof(dst));
    }
#endif
    dst.x = static_cast<Sint16>(f.pos.x * zoom - radius + offsetX);
    dst.y = static_cast<Sint16>(f.pos.y * zoom - radius);
    SDL_BlitSurface(s, nullptr, target, &dst);
  }
  for (int i = count; i < numSpheres; ++i) {
    spheres[i + numRadii].release();
  }
  numSpheres = count;
}
