#pragma once

#include <stdint.h>


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

struct Fruit {
  Point pos;
  Point lastPos;
  float r, r2;
  uint32_t rotation, rIndex;
  Point relSum;
  uint32_t relCount;

  void move();
  void roll();
  bool keepDistance(Fruit &other);
  void constrainInside();
};


const int fruitCap = 1024;

class FruitSim {
  Fruit fruits[fruitCap];
  int numFruits;
public:
  inline FruitSim() { }

  inline int getNumFruits() {
    return numFruits;
  }

  Fruit* init(int worldSeed);
  Fruit* simulate(int frameSeed);
  bool addFruit(float x, float y, unsigned radiusIndex, int seed);
  Fruit* previewFruit(float x, float y, unsigned radiusIndex, int seed);
  float getWorldWidth();
  float getWorldHeight();
};

#ifdef IMPLEMENT_SIM
#include "sim.cc"
#endif
