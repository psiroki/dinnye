#include <stdint.h>

static const float worldSize = 16;

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
};

struct CustomFruit {
  Point pos;
  Point lastPos;
  float r, r2;

  void move() {
    Point diff = pos - lastPos;
    lastPos = pos;
    pos.y += 0.0078125f;
    diff *= 0.99f;
    pos += diff;
  }

  void keepDistance(CustomFruit &other) {
    Point diff = other.pos - pos;
    float d2 = diff.x * diff.x + diff.y * diff.y;
    float rs = r2 + r * other.r + other.r2;
    if (d2 < rs) {
      // they overlap: let's just nudge them
      float dr = rsqrt(d2);
      float factor = (r + other.r - d2 * dr) * (1.0f / 32.0f);
      diff *= dr * factor;
      other.pos += diff;
      pos -= diff;
    }
  }

  void constrainInside() {
    if (pos.x < r) pos.x = r;
    if (pos.x > worldSize - r) pos.x = worldSize - r;
    if (pos.y < r) pos.y = r;
    if (pos.y > worldSize - r) pos.y = worldSize - r;
  }
};

CustomFruit fruits[64];
const int numFruits = sizeof(fruits) / sizeof(*fruits);

extern "C" float getWorldSize() {
  return worldSize;
}

extern "C" float getNumFruits() {
  return numFruits;
}

extern "C" CustomFruit* init(int worldSeed) {
  uint64_t seed = nextSeed(worldSeed);
  for (int i = 0; i < numFruits; ++i) {
    CustomFruit &f(fruits[i]);
    f.pos.x = seedToFloat(seed = nextSeed(seed)) * worldSize;
    f.pos.y = seedToFloat(seed = nextSeed(seed)) * worldSize;
    f.lastPos = f.pos;
    f.r = (((seed = nextSeed(seed)) % 11) + 1) / 8.0f;
    f.r2 = f.r * f.r;
  }
  return fruits;
}

extern "C" CustomFruit* simulate(int frameSeed) {
  // apply gravity and movement
  for (int i = 0; i < numFruits; ++i) {
    fruits[i].move();
  }
  for (int iter = 0; iter < 32; ++iter) {
    // apply constraints
    for (int i = 1; i < numFruits; ++i) {
      for (int j = 0; j < i; ++j) {
        fruits[j].keepDistance(fruits[i]);
      }
    }
    for (int i = 0; i < numFruits; ++i) {
      fruits[i].constrainInside();
    }
  }
  return fruits;
}
