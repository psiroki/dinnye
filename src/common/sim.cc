#include <stdint.h>

namespace {

  const float worldSizeX = 12;
  const float worldSizeY = 16;

  float rsqrt(float number)
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
const int numRandomRadii = numRadii / 2;
const float angleScale = 32768.0f / 3.141592653589793f;

struct Point {
  float x, y;

  inline Point() { }
  inline Point(float x, float y): x(x), y(y) { }

  inline void rotate90() {
    float save = x;
    x = y;
    y = -save;
  }

  inline Point operator -(const Point &other) const {
    return Point(x - other.x, y - other.y);
  }

  inline Point operator +(const Point &other) const {
    return Point(x + other.x, y + other.y);
  }

  inline Point& operator +=(const Point &other) {
    x += other.x;
    y += other.y;
    return *this;
  }

  inline Point& operator -=(const Point &other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  inline Point& operator *=(float scale) {
    x *= scale;
    y *= scale;
    return *this;
  }

  inline float operator ^(const Point &other) const {
    return x * other.y - y * other.x;
  }

  inline float operator *(const Point &other) const {
    return x * other.x + y * other.y;
  }

  inline float lengthSquared() const {
    return x * x + y * y;
  }
};

struct CustomFruit {
  Point pos;
  Point lastPos;
  float r, r2;
  uint32_t rotation, rIndex;
  Point relSum;
  uint32_t relCount;

  void move();
  void roll();
  bool keepDistance(CustomFruit &other);
  void constrainInside();
};

void CustomFruit::move() {
  Point diff = pos - lastPos;
  lastPos = pos;
  pos.y += 0.0078125f;
  diff *= 0.999f;
  pos += diff;
  relSum.x = relSum.y = 0.0f;
  relCount = 0;
}

void CustomFruit::roll() {
  if (relCount) {
    Point vel = pos - lastPos;
    if (vel.lengthSquared() > 1.0e-3f) {
      Point rel = relSum;
      rel.rotate90();

      rel *= rsqrt(rel.lengthSquared());
      float angleVel = (rel * vel) * (1.0f / 3.141592654f);
      rotation += angleVel * angleScale;
    }
  }
}

bool CustomFruit::keepDistance(CustomFruit &other) {
  Point diff = other.pos - pos;
  float d2 = diff.x * diff.x + diff.y * diff.y;
  float rsum = r + other.r;
  float rs = rsum * rsum;
  if (d2 < rs) {
    // overlap
    if (rIndex == other.rIndex && rIndex < numRadii - 1) {
      // merge them
      ++rIndex;
      r = radii[rIndex];
      r2 = r*r;
      pos = pos + other.pos;
      pos *= 0.5f;
      lastPos = pos;
      return true;
    } else {
      // nudge them
      float dr = rsqrt(d2);
      // d2 = d^2 (distance squared)
      // dr = 1/sqrt(d2)
      // d = d2*dr = (d2 / sqrt(d2) = sqrt(d2))
      float factor = (r + other.r - d2 * dr) * (1.0f / 16.0f);
      diff *= dr * factor;
      other.pos += diff;
      pos -= diff;

      // we aren't using diff for anything else, so adjust it
      // to alter the rotation vector
      diff *= 4.0f;
      relSum += diff;
      ++relCount;
      other.relSum -= diff;
      ++other.relCount;
    }
  }
  return false;
}

void CustomFruit::constrainInside() {
  Point diff = pos - lastPos;
  if (pos.x < r) {
    pos.x = r;
    relSum += Point(r, 0.0f);
    ++relCount;
  }
  if (pos.x > worldSizeX - r) {
    pos.x = worldSizeX - r;
    relSum += Point(-r, 0.0f);
    ++relCount;
  }
  // there is no top
  // if (pos.y < r) pos.y = r;
  if (pos.y > worldSizeY - r) {
    pos.y = worldSizeY - r;
    relSum += Point(0.0f, r);
    ++relCount;
  }
}

const int fruitCap = 1024;

class FruitSim {
  CustomFruit fruits[fruitCap];
  int numFruits;
public:
  inline FruitSim() { }

  inline int getNumFruits() {
    return numFruits;
  }

  CustomFruit* init(int worldSeed);
  CustomFruit* simulate(int frameSeed);
  bool addFruit(float x, float y, unsigned radiusIndex, int seed);
  CustomFruit* previewFruit(float x, float y, unsigned radiusIndex, int seed);
  inline float getWorldWidth() {
    return worldSizeX;
  }
  inline float getWorldHeight() {
    return worldSizeY;
  }
};

CustomFruit* FruitSim::init(int worldSeed) {
  Random rand(worldSeed);
  radii[0] = 1.0f / 3.0f;
  for (int i = 1; i < numRadii; ++i) {
    float fac = i / static_cast<float>(numRadii - 1);
    float lin = i + 1;
    float exp = radii[i - 1] * 1.2968395546510096f;
    radii[i] = exp;
  }
  numFruits = 128;
  if (numFruits > fruitCap) numFruits = fruitCap;
  for (int i = 0; i < numFruits; ++i) {
    CustomFruit &f(fruits[i]);
    f.rIndex = rand(numRandomRadii);
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

CustomFruit* FruitSim::simulate(int frameSeed) {
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
    for (int i = 0; i < numFruits; ++i) {
      fruits[i].roll();
    }
  }
  return fruits;
}

bool FruitSim::addFruit(float x, float y, unsigned radiusIndex, int seed) {
  Random rand(seed);
  if (numFruits >= fruitCap) return false;
  if (radiusIndex >= numRadii) radiusIndex = numRadii - 1;
  int index = numFruits++;
  CustomFruit &f(fruits[index]);
  f.rIndex = radiusIndex;
  f.r = radii[f.rIndex];
  f.r2 = f.r * f.r;
  f.rotation = rand() & 65535;

  if (x < f.r) x = f.r;
  if (x > worldSizeX - f.r) x = worldSizeX - f.r;
  if (y < f.r) y = f.r;
  if (y > worldSizeY - f.r) y = worldSizeY - f.r;

  f.pos.x = x;
  f.pos.y = y;

  f.lastPos = f.pos;
  return true;
}

CustomFruit* FruitSim::previewFruit(float x, float y, unsigned radiusIndex, int seed) {
  int numFruitsBefore = numFruits;
  CustomFruit *result = nullptr;
  if (addFruit(x, y, radiusIndex, seed))
    result = fruits + (numFruits - 1);
  numFruits = numFruitsBefore;
  return result;
}
