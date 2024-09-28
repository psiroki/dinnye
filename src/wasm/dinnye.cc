#include <stdint.h>
#include <stddef.h>

#include "../common/sim.cc"

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
  static const uint32_t allocBy = 16*1024*1024;
  static const uint32_t *zeroAddress = nullptr;
  static const uint32_t overheadSize = 16384;

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
      uint32_t *withOverhead = allocTop + overheadSize;
      if (withOverhead > lastKnownMemoryEnd) {
        uint32_t memorySize = reinterpret_cast<uint32_t>(lastKnownMemoryEnd);
        uint32_t additional = (numWords << 2) + overheadSize;
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

FruitSim *sim = nullptr;

extern "C" float getWorldSizeX() {
  return worldSizeX;
}

extern "C" float getWorldSizeY() {
  return worldSizeY;
}

extern "C" float getNumFruits() {
  return sim->getNumFruits();
}

void* operator new(size_t size) {
    return allocator.allocateBytes(size);
}

extern "C" Fruit* init(int worldSeed) {
  if (!sim) {
    sim = new FruitSim();
  }
  return sim->init(worldSeed);
}

extern "C" Fruit* simulate(int frameSeed) {
  return sim->simulate(frameSeed);
}

extern "C" bool addFruit(float x, float y, unsigned radiusIndex, int seed) {
  return sim->addFruit(x, y, radiusIndex, seed);
}

extern "C" Fruit* previewFruit(float x, float y, unsigned radiusIndex, int seed) {
  return sim->previewFruit(x, y, radiusIndex, seed);
}
