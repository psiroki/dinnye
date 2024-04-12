#include <stdint.h>

static const float worldSizeX = 12;
static const float worldSizeY = 16;

extern "C" unsigned char __heap_base;
extern "C" uint32_t setMemorySize(uint32_t newSize);
extern "C" void dumpInt(int32_t val);
extern "C" void dumpQuadInt(int64_t a, int64_t b, int64_t c, int64_t d);

static void dumpPointer(const void *p) {
  dumpInt(reinterpret_cast<uint32_t>(p));
}

static void dumpPointerDelta(const void *a, const void *b) {
  dumpInt(reinterpret_cast<uint32_t>(b)-reinterpret_cast<uint32_t>(a));
}

static inline void *getHeapBase() {
  return &__heap_base;
}

namespace {
  static const uint32_t allocBy = 256*1024*1024;
  static const uint32_t *zeroAddress = nullptr;

  struct LinearAllocator;

  class AllocatorGuard {
    LinearAllocator &allocator;
    uint32_t *ptr;
  public:
    AllocatorGuard(LinearAllocator &allocator, uint32_t *ptr): allocator(allocator), ptr(ptr) {}
    AllocatorGuard(AllocatorGuard &&other): allocator(other.allocator), ptr(other.ptr) {
      other.ptr = nullptr;
    }
    ~AllocatorGuard();
    void release();
  };

  struct LinearAllocator {
    uint32_t *allocTop;
    uint32_t *lastKnownMemoryEnd;
  private:
    void refreshLastKnownMemoryEnd(uint32_t newSize=0) {
      lastKnownMemoryEnd = reinterpret_cast<uint32_t*>(setMemorySize(newSize));
    }
  public:
    const uint32_t* top() const {
      return allocTop;
    }

    void reset() {
      allocTop = static_cast<uint32_t*>(getHeapBase());
    }

    uint32_t *allocateBytes(uint32_t numBytes) {
      return allocateWords((numBytes + 3) >> 2);
    }

    uint32_t *allocateWords(uint32_t numWords) {
      if (!allocTop) allocTop = static_cast<uint32_t*>(getHeapBase());
      if (!lastKnownMemoryEnd) refreshLastKnownMemoryEnd();
      uint32_t *ptr = allocTop;
      allocTop += numWords;
      if (allocTop > lastKnownMemoryEnd) {
        uint32_t memorySize = reinterpret_cast<uint32_t>(lastKnownMemoryEnd);
        uint32_t additional = numWords << 2;
        // we always grow by at least allocBy
        if (additional < allocBy) additional = allocBy;
        setMemorySize(memorySize + additional);
        refreshLastKnownMemoryEnd();
      }
      return ptr;
    }

    AllocatorGuard guard() {
      return AllocatorGuard(*this, mark());
    }

    uint32_t *mark() {
      return allocTop;
    }

    void release(uint32_t *marked) {
      allocTop = marked;
    }
  } allocator = { nullptr, nullptr };

  AllocatorGuard::~AllocatorGuard() {
    release();
  }

  void AllocatorGuard::release() {
    if (ptr) allocator.release(ptr);
    ptr = nullptr;
  }
}

extern "C" void reset() {
  allocator.reset();
}

static float rsqrt(float number)
{
  long i;
  float x2, y;
  const float threehalfs = 1.5F;

  x2 = number * 0.5F;
  y  = number;
  i  = * ( long * ) &y;                       // evil floating point bit level hacking
  i  = 0x5f3759df - ( i >> 1 );               // what in the world?
  y  = * ( float * ) &i;
  y  = y * ( threehalfs - ( x2 * y * y ) );   // 1st iteration
  // y  = y * ( threehalfs - ( x2 * y * y ) );   // 2nd iteration, this can be removed

  return y;
}

static uint64_t nextSeed(uint64_t seed) {
  return (seed * 0x5DEECE66DLL + 0xBLL) & ((1LL << 48) - 1);
}

static float seedToFloat(uint64_t seed) {
  return (seed & 0xffffff) / static_cast<float>(0xffffff);
}

namespace {
  class Random {
    uint64_t seed;
  public:
    Random(uint64_t seed): seed(nextSeed(seed)) {}

    uint64_t operator()() {
      return seed = nextSeed(seed);
    }

    uint64_t operator()(int n) {
      return (seed = nextSeed(seed)) % n;
    }

    float fraction() {
      return seedToFloat(seed = nextSeed(seed));
    }
  };
}

float radii[11];
const int numRadii = sizeof(radii) / sizeof(*radii);
const int maxRandomRadii = numRadii / 2;
const float angleScale = 32768.0f / 3.141592653589793f;

struct Point {
  float x, y;

  Point() { }
  Point(float x, float y): x(x), y(y) { }

  Point operator -(const Point &other) const {
    return Point(x - other.x, y - other.y);
  }

  Point operator +(const Point &other) const {
    return Point(x + other.x, y + other.y);
  }

  Point& operator +=(const Point &other) {
    x += other.x;
    y += other.y;
    return *this;
  }

  Point& operator -=(const Point &other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  Point& operator *=(float scale) {
    x *= scale;
    y *= scale;
    return *this;
  }

  float operator ^(const Point &other) const {
    return x * other.y - y * other.x;
  }

  float lengthSquared() const {
    return x * x + y * y;
  }
};

struct CustomFruit {
  Point pos;
  Point lastPos;
  float r, r2;
  uint32_t rotation, rIndex;

  void move() {
    Point diff = pos - lastPos;
    lastPos = pos;
    pos.y += 0.0078125f;
    diff *= 0.95f;
    pos += diff;
  }

  void roll(const Point &rel, float scale = 1.0f) {
    Point vel = pos - lastPos;
    float rs = rsqrt(rel.lengthSquared());
    float angleVel = (rel ^ vel) * rs / r;
    rotation += angleVel * angleScale;
  }

  bool keepDistance(CustomFruit &other) {
    Point diff = other.pos - pos;
    float d2 = diff.x * diff.x + diff.y * diff.y;
    float rsum = r + other.r;
    float rs = rsum * rsum;
    if (d2 < rs) {
      // they overlap: let's just nudge them
      if (rIndex == other.rIndex && rIndex < numRadii) {
        ++rIndex;
        r = radii[rIndex];
        r2 = r*r;
        pos = pos + other.pos;
        pos *= 0.5f;
        lastPos = pos;
        return true;
      } else {
        float dr = rsqrt(d2);
        float factor = (r + other.r - d2 * dr) * (1.0f / 32.0f);
        diff *= dr * factor;
        other.pos += diff;
        pos -= diff;
        roll(diff);
        other.roll(diff, -1.0f);
      }
    }
    return false;
  }

  void constrainInside() {
    float angleOffset = 0.0f;
    Point diff = pos - lastPos;
    if (pos.x < r) {
      pos.x = r;
      angleOffset += diff.y / r;
    }
    if (pos.x > worldSizeX - r) {
      pos.x = worldSizeX - r;
      angleOffset -= diff.y / r;
    }
    // there is no top
    // if (pos.y < r) pos.y = r;
    if (pos.y > worldSizeY - r) {
      pos.y = worldSizeY - r;
      angleOffset += diff.x / r;
    }
    rotation += angleOffset * angleScale;
  }
};

CustomFruit fruits[1024];
const int fruitCap = sizeof(fruits) / sizeof(*fruits);
int numFruits = 0;

extern "C" float getWorldSizeX() {
  return worldSizeX;
}

extern "C" float getWorldSizeY() {
  return worldSizeY;
}

extern "C" float getNumFruits() {
  return numFruits;
}

extern "C" CustomFruit* init(int worldSeed) {
  Random rand(worldSeed);
  radii[0] = 1.0f / 8.0f;
  for (int i = 1; i < numRadii; ++i) {
    radii[i] = radii[i - 1] * 1.4142135623730951f;
  }
  numFruits = 192;
  if (numFruits > fruitCap) numFruits = fruitCap;
  for (int i = 0; i < numFruits; ++i) {
    CustomFruit &f(fruits[i]);
    f.rIndex = rand(maxRandomRadii);
    f.r = radii[f.rIndex];
    f.r2 = f.r * f.r;
    f.rotation = rand() & 65535;

    float d = f.r * 2.0f;

    f.pos.x = rand.fraction() * (worldSizeX - d) + f.r;
    f.pos.y = rand.fraction() * (worldSizeY - d) + f.r;
    f.lastPos = f.pos;
  }
  return fruits;
}

extern "C" CustomFruit* simulate(int frameSeed) {
  // apply gravity and movement
  for (int i = 0; i < numFruits; ++i) {
    fruits[i].move();
  }
  for (int iter = 0; iter < 8; ++iter) {
    // apply constraints
    for (int i = 1; i < numFruits; ++i) {
      for (int j = 0; j < i; ++j) {
        if (fruits[j].keepDistance(fruits[i])) {
          if (i < numFruits - 1) {
            fruits[i] = fruits[numFruits - 1];
          }
          --numFruits;
          --i;
        }
      }
    }
    for (int i = 0; i < numFruits; ++i) {
      fruits[i].constrainInside();
    }
  }
  return fruits;
}
