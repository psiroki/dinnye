#include "sim.hh"

namespace {

  const Scalar worldSizeX = 12;
  const Scalar worldSizeY = 16;

#ifdef FIXED
  Fixed rsqrt(Fixed f) {
    uint32_t n = f.f;
    if (!n) return f;
    int bits = 32 - __builtin_clz(n);
    int newBits = 16-((bits-16) >> 1);
    uint32_t y = ((n-(1 << bits-1))>>1)+((bits&1^1) << bits-1);
    y ^= (1 << (bits - 1)) - 1;
    if (newBits > bits) {
      y <<= newBits - bits;
    } else if (newBits < bits) {
      y >>= bits - newBits;
    }
    y += (1 << newBits);
    y -= 0x4dbfab13 >> (31-newBits);
    Fixed result = Fixed::fromRaw(y);
    return result * (3 - f*result*result) >> 1;
  }
#else
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
#endif
}

float radii[numRadii];
const int numRandomRadii = numRadii / 2;
const float angleScale = 32768.0f / 3.141592653589793f;

void Fruit::move(Scalar gravity) {
  Point diff = pos - lastPos;
  lastPos = pos;
  pos.y += gravity;
  diff *= Scalar(0.999f);
  pos += diff;
  relSum.x = relSum.y = 0.0f;
  flags &= ~touched;
}

void Fruit::roll() {
  if (flags & touched) {
    Point vel = pos - lastPos;
    if (vel.lengthSquared() > Scalar(1.0e-3f)) {
      Point rel = relSum;
      rel.rotate90();

      rel *= rsqrt(rel.lengthSquared());
      Scalar angleVel = (rel * vel) * (1.0e-1f / 3.141592654f);
      rotation += angleVel * angleScale;
    }
  }
}

bool Fruit::touches(const Fruit &other) const {
  Point diff = other.pos - pos;
  Scalar d2 = diff.x * diff.x + diff.y * diff.y;
  Scalar rsum = r + other.r;
  Scalar rs = rsum * rsum;
  return d2 < rs;
}

int Fruit::keepDistance(Fruit &other, uint32_t frameIndex) {
  Point diff = other.pos - pos;
  Scalar d2 = diff.x * diff.x + diff.y * diff.y;
  Scalar rsum = r + other.r;
  Scalar rs = rsum * rsum;
  if (d2 < rs) {
    // overlap
    if (rIndex == other.rIndex) {
      int score = (rIndex + 1)*(rIndex + 2) >> 1;
      other.flags |= deletable;
      if (rIndex >= numRadii - 1) {
        // both of them should disappear
        flags |= deletable;
        return score;
      }
      // merge them
      ++rIndex;
      r = radii[rIndex];
      r2 = r*r;
      pos = pos + other.pos;
      pos *= Scalar(0.5f);
      lastPos = pos;
      bottomTouchFrame = 0;
      return score;
    } else {
      // nudge them
      Scalar dr = rsqrt(d2);
      // d2 = d^2 (distance squared)
      // dr = 1/sqrt(d2)
      // d = d2*dr = (d2 / sqrt(d2) = sqrt(d2))
      Scalar factor = (r + other.r - d2 * dr) * Scalar(1.0f / 16.0f) / rsum;
      diff *= factor;
      other.pos += diff * r;
      pos -= diff * other.r;

      // we aren't using diff for anything else, so adjust it
      // to alter the rotation vector
      diff *= 4.0f;
      relSum += diff;
      flags |= touched;
      other.relSum -= diff;
      other.flags |= touched;
      if (bottomTouchFrame == frameIndex && diff.y < -scalarAbs(diff.x)/2) {
        other.bottomTouchFrame = frameIndex;
      } else if (other.bottomTouchFrame == frameIndex) {
        bottomTouchFrame = frameIndex;
      }
    }
  }
  return 0;
}

void Fruit::constrainInside(uint32_t frameIndex) {
  Point diff = pos - lastPos;
  if (pos.x < r) {
    pos.x = r;
    relSum += Point(r, 0.0f);
    flags |= touched;
  }
  if (pos.x > worldSizeX - r) {
    pos.x = worldSizeX - r;
    relSum += Point(-r, 0.0f);
    flags |= touched;
  }
  // there is no top, but to keep things sane, we don't
  // let objects past -4096
  if (pos.y < Scalar(-4096)) pos.y = Scalar(-4096);
  if (pos.y > worldSizeY - r) {
    pos.y = worldSizeY - r;
    relSum += Point(0.0f, r);
    flags |= touched;
    bottomTouchFrame = frameIndex;
  }
}

Fruit* FruitSim::init(int worldSeed) {
#ifdef SPEEDTESTING
  numFruits = 128;
  worldSeed = 7;
#else
  numFruits = 0;
#endif
  Random rand(worldSeed);
  gravity = Scalar(0.0078125f);
  radii[0] = 1.0f / 3.0f;
  for (int i = 1; i < numRadii; ++i) {
    float fac = i / static_cast<float>(numRadii - 1);
    float lin = i + 1;
    float exp = radii[i - 1] * 1.2968395546510096f;
    radii[i] = exp;
  }
  if (numFruits > fruitCap) numFruits = fruitCap;
  for (int i = 0; i < numFruits; ++i) {
    Fruit &f(fruits[i]);
    f.rIndex = rand(numRandomRadii);
    f.r = radii[f.rIndex];
    f.r2 = f.r * f.r;
    f.rotation = rand() & 65535;
    f.flags = 0;

    float d = f.r * 2.0f;

    f.pos.x = rand.fraction() * (worldSizeX - d) + f.r;
    f.pos.y = rand.fraction() * (worldSizeY - d) + f.r;

    f.lastPos = f.pos;
  }
  return fruits;
}

Fruit* FruitSim::simulate(int frameSeed, uint32_t frameIndex) {
  lastPopCount = 0;
  // apply gravity and movement
  for (int i = 0; i < numFruits; ++i) {
    fruits[i].move(gravity);
  }
  const int numIter = 16;
  for (int iter = 0; iter < numIter; ++iter) {
    // apply constraints
    for (int i = 1; i < numFruits; ++i) {
      for (int j = 0; j < i; ++j) {
        int scoreIncrement = fruits[j].keepDistance(fruits[i], frameIndex);
        if (scoreIncrement) {
          score += scoreIncrement;
          ++popCount;
          ++lastPopCount;
          if (fruits[j].flags & Fruit::deletable) {
            if (j < numFruits - 1) {
              fruits[j] = fruits[numFruits - 1];
            }
            --numFruits;
            --j;
          }
          if (fruits[i].flags & Fruit::deletable) {
            if (i < numFruits - 1) {
              fruits[i] = fruits[numFruits - 1];
            }
            --numFruits;
            --i;
          }
        }
      }
    }
    for (int i = 0; i < numFruits; ++i) {
      fruits[i].constrainInside(frameIndex);
    }
    for (int i = 0; i < numFruits; ++i) {
      fruits[i].roll();
    }
  }
  return fruits;
}

int FruitSim::findGroundedOutside(uint32_t frameIndex) {
  if (lastPopCount > 0) return -1;
  Scalar maxY = -worldSizeY;
  int found = -1;
  for (int i = 0; i < numFruits; ++i) {
    Fruit &f(fruits[i]);
    if (f.bottomTouchFrame == frameIndex && f.pos.y < f.r && maxY < f.pos.y) {
      found = i;
      maxY = f.pos.y;
    }
  }
  return found;
}

bool FruitSim::addFruit(Scalar x, Scalar y, unsigned radiusIndex, int seed) {
  Random rand(seed);
  if (numFruits >= fruitCap) return false;
  if (radiusIndex >= numRadii) radiusIndex = numRadii - 1;
  int index = numFruits++;
  Fruit &f(fruits[index]);
  f.rIndex = radiusIndex;
  f.r = radii[f.rIndex];
  f.r2 = f.r * f.r;
  f.rotation = rand() & 65535;
  f.flags = 0;

  if (x < f.r) x = f.r;
  if (x > worldSizeX - f.r) x = worldSizeX - f.r;
  if (y > worldSizeY - f.r) y = worldSizeY - f.r;

  f.pos.x = x;
  f.pos.y = y;

  f.lastPos = f.pos;
  return true;
}

Fruit* FruitSim::previewFruit(Scalar x, Scalar y, unsigned radiusIndex, int seed) {
  int numFruitsBefore = numFruits;
  Fruit *result = nullptr;
  if (addFruit(x, y, radiusIndex, seed)) {
    result = fruits + (numFruits - 1);
    result->flags |= Fruit::sensor;
  }
  numFruits = numFruitsBefore;
  return result;
}

bool FruitSim::touchesAny(const Fruit &f) const {
  for (int i = 0; i < numFruits; ++i) {
    if (fruits[i].touches(f))
      return true;
  }
  return false;
}

Scalar FruitSim::getWorldWidth() const {
  return worldSizeX;
}

Scalar FruitSim::getWorldHeight() const {
  return worldSizeY;
}

int FruitSim::getNumRadii() {
  return numRadii;
}

int FruitSim::getNumRandomRadii() {
  return numRandomRadii;
}

Scalar FruitSim::getRadius(int index) {
  return radii[index];
}

int FruitSim::getPopCount() const {
  return popCount;
}
