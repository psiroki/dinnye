#pragma once

#include <stdint.h>

#ifdef FIXED
struct Fixed {
  int32_t f;  // 16.16 fixed point number

  // Constants
  static constexpr int FRACTIONAL_BITS = 16;

  // Constructors
  Fixed() {}                        // Default constructor
  Fixed(int val) : f(val << FRACTIONAL_BITS) {}  // Constructor from int
  Fixed(float val) : f(static_cast<int32_t>(val * (1 << FRACTIONAL_BITS))) {}  // Constructor from float

  // Static helper to create Fixed from raw value
  static Fixed fromRaw(int32_t raw) {
    Fixed result;
    result.f = raw;
    return result;
  }

  // Conversion to float
  float toFloat() const {
    return static_cast<float>(f) / (1 << FRACTIONAL_BITS);
  }

  // Conversion to int
  int toInt() const {
    return f >> FRACTIONAL_BITS;
  }

  operator float() const {
    return toFloat();
  }

  // Assignment operators
  Fixed& operator=(int val) {
    f = val << FRACTIONAL_BITS;
    return *this;
  }

  Fixed& operator=(float val) {
    f = static_cast<int32_t>(val * (1 << FRACTIONAL_BITS));
    return *this;
  }

  // Arithmetic operators
  Fixed operator+(const Fixed& other) const {
    return Fixed::fromRaw(f + other.f);
  }

  Fixed operator-(const Fixed& other) const {
    return Fixed::fromRaw(f - other.f);
  }

  Fixed operator-() const {
    return Fixed::fromRaw(-f);
  }

  Fixed operator*(const Fixed& other) const {
    // Perform multiplication and adjust back to fixed-point
    return Fixed::fromRaw((int64_t(f) * other.f) >> FRACTIONAL_BITS);
  }

  Fixed operator/(const Fixed& other) const {
    // Perform division and adjust back to fixed-point
    return Fixed::fromRaw((int64_t(f) << FRACTIONAL_BITS) / other.f);
  }

  // Relational operators
  bool operator==(const Fixed& other) const {
    return f == other.f;
  }

  bool operator!=(const Fixed& other) const {
    return f != other.f;
  }

  bool operator<(const Fixed& other) const {
    return f < other.f;
  }

  bool operator<=(const Fixed& other) const {
    return f <= other.f;
  }

  bool operator>(const Fixed& other) const {
    return f > other.f;
  }

  bool operator>=(const Fixed& other) const {
    return f >= other.f;
  }

  // Compound assignment operators
  Fixed& operator+=(const Fixed& other) {
    f += other.f;
    return *this;
  }

  Fixed& operator-=(const Fixed& other) {
    f -= other.f;
    return *this;
  }

  Fixed& operator*=(const Fixed& other) {
    f = (int64_t(f) * other.f) >> FRACTIONAL_BITS;
    return *this;
  }

  // Operations with int
  Fixed operator+(int val) const {
    return *this + Fixed(val);
  }

  Fixed operator-(int val) const {
    return *this - Fixed(val);
  }

  Fixed operator*(int val) const {
    return fromRaw(f*val);
  }

  Fixed operator>>(int val) const {
    return fromRaw(f >> val);
  }

  Fixed operator<<(int val) const {
    return fromRaw(f << val);
  }

  // Operations with float
  Fixed operator+(float val) const {
    return *this + Fixed(val);
  }

  Fixed operator-(float val) const {
    return *this - Fixed(val);
  }

  Fixed operator*(float val) const {
    return *this * Fixed(val);
  }

  // Friend functions for operations with int or float on the left side
  friend Fixed operator+(int lhs, const Fixed& rhs) {
    return Fixed(lhs) + rhs;
  }

  friend Fixed operator-(int lhs, const Fixed& rhs) {
    return Fixed(lhs) - rhs;
  }

  friend Fixed operator*(int lhs, const Fixed& rhs) {
    return fromRaw(lhs * rhs.f);
  }

  friend Fixed operator+(float lhs, const Fixed& rhs) {
    return Fixed(lhs) + rhs;
  }

  friend Fixed operator-(float lhs, const Fixed& rhs) {
    return Fixed(lhs) - rhs;
  }

  friend Fixed operator*(float lhs, const Fixed& rhs) {
    return Fixed(lhs) * rhs;
  }
};

typedef Fixed Scalar;
#else
typedef float Scalar;
#endif

struct Point {
  Scalar x, y;

  inline Point() { }
  inline Point(Scalar x, Scalar y): x(x), y(y) { }

  inline void rotate90() {
    Scalar save = x;
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

  inline Point& operator *=(Scalar scale) {
    x *= scale;
    y *= scale;
    return *this;
  }

  inline Scalar operator ^(const Point &other) const {
    return x * other.y - y * other.x;
  }

  inline Scalar operator *(const Point &other) const {
    return x * other.x + y * other.y;
  }

  inline Point operator *(Scalar f) const {
    return Point(f * x, f * y);
  }

  inline Scalar lengthSquared() const {
    return x * x + y * y;
  }
};

struct Fruit {
  Point pos;
  Point lastPos;
  Scalar r, r2;
  uint32_t rotation, rIndex;
  Point relSum;
  uint32_t relCount;

  void move();
  void roll();
  bool keepDistance(Fruit &other);
  void constrainInside();
};

const int fruitCap = 1024;
const int numRadii = 11;

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
  bool addFruit(Scalar x, Scalar y, unsigned radiusIndex, int seed);
  Fruit* previewFruit(Scalar x, Scalar y, unsigned radiusIndex, int seed);
  Scalar getWorldWidth();
  Scalar getWorldHeight();
  int getNumRadii();
  int getNumRandomRadii();
  Scalar getRadius(int index);
};

#ifdef IMPLEMENT_SIM
#include "sim.cc"
#endif
