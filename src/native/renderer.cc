#include <math.h>

#include "renderer.hh"
#include "image.hh"

const float pi = M_PI;

//#define RED_BLUE_SWAP

#ifdef BITTBOY
const unsigned TEXTURE_COORD_BITS = 7;
#else
const unsigned TEXTURE_COORD_BITS = 9;
#endif

const unsigned TEXTURE_SIZE = 1 << TEXTURE_COORD_BITS;
const unsigned TEXTURE_COORD_MASK = TEXTURE_SIZE - 1;

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
      uint32_t gray = min(1.0f, combinedIntensity*mask) * 255;

      // Set the pixel color
      line[x] = 0xff000000u | (gray << 24) | (gray << 16) << (gray << 8) | gray;
    }
    dst += pb.pitch;
  }
}

void ShadedSphere::render(PixelBuffer &target, int cx, int cy, int radius, int angle) {
  float zoom = (TEXTURE_SIZE * 0.5f) / radius;
  float rad = angle / 32768.0f * pi;
  int w = 2*radius;
  int h = w;
  int p = target.pitch;
  int zv = zoom * 65536.0f;
  int cv = cosf(rad) * 65536.0f * zoom;
  int sv = sinf(rad) * 65536.0f * zoom;
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
      int m = lm[rs + (rt << TEXTURE_COORD_BITS)] & 0xff;
      d[x] = m ? ablend(a[ru + (rv << TEXTURE_COORD_BITS)], m) | 0xff000000u : 0;
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

void SphereCache::reassign(ShadedSphere *newSphere, int newRadius) {
  if (s == newSphere && radius == newRadius) return;
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
  }
  angle = 0;
  dirty = true;
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
  }
  if (dirty) {
    angle = newAngle;

    if (SDL_MUSTLOCK(cache)) {
      SDL_LockSurface(cache);
    }

    PixelBuffer pb(cache);
    s->render(pb, radius, radius, radius, angle & 0xffff);

    if (SDL_MUSTLOCK(cache)) {
      SDL_UnlockSurface(cache);
    }
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
};

FruitRenderer::FruitRenderer(SDL_Surface *target): target(target), numSpheres(0) {
  numTextures = sizeof(imageNames) / sizeof(*imageNames);
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

void FruitRenderer::renderFruits(Fruit *fruits, int count, float zoom, float offsetX) {
  // Render gallery
  for (int i = 0; i < numRadii; ++i) {
    SphereCache &sc(spheres[i + numRadii]);
    int scale = zoom;
    int radius = scale * 7 / 12;
    int availableHeight = target->h - scale;
    int step = (availableHeight - 2*radius) / (numRadii - 1);
    sc.reassign(sphereDefs + i, scale * 2 / 3);
    SDL_Surface *s = sc.withAngle(0);
    SDL_Rect dst;
    dst.x = static_cast<Sint16>(offsetX - (radius * 9 / 2));
    dst.y = static_cast<Sint16>(i * step + scale / 2);
    SDL_BlitSurface(s, nullptr, target, &dst);
  }
  // Render playfield
  for (int i = 0; i < count; ++i) {
    Fruit &f(fruits[i]);
    SphereCache &sc(spheres[i + numRadii]);
    int radius = f.r * zoom;
    sc.reassign(sphereDefs + f.rIndex, radius);
    SDL_Surface *s = sc.withAngle((-f.rotation) & 0xffff);
    SDL_Rect dst;
    dst.x = static_cast<Sint16>(f.pos.x * zoom - radius + offsetX);
    dst.y = static_cast<Sint16>(f.pos.y * zoom - radius);
    SDL_BlitSurface(s, nullptr, target, &dst);
  }
  for (int i = count; i < numSpheres; ++i) {
    spheres[i + numRadii].release();
  }
  numSpheres = count;
}
