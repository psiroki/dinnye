#include "serialization.hh"

namespace s {

  struct Point {
    float x, y;

    Point() { }
    Point(const ::Point &p): x(p.x), y(p.y) { }

    operator ::Point() const {
      return ::Point(x, y);
    }
  };

  struct Fruit {
    Point pos;
    Point lastPos;
    float r, r2;
    uint32_t rotation, rIndex;
    Point relSum;
    uint32_t flags;
    uint32_t bottomTouchFrame;

    Fruit() { }

    Fruit(const ::Fruit &f):
      pos(f.pos),
      lastPos(f.lastPos),
      r(f.r),
      r2(f.r2),
      rotation(f.rotation),
      rIndex(f.rIndex),
      relSum(f.relSum),
      flags(f.flags),
      bottomTouchFrame(f.bottomTouchFrame) { }
    
    void setup(::Fruit &f) {
      f.pos = pos;
      f.lastPos = lastPos;
      f.r = r;
      f.r2 = r2;
      f.rotation = rotation;
      f.rIndex = rIndex;
      f.relSum = relSum;
      f.flags = flags;
      f.bottomTouchFrame = bottomTouchFrame;
    }
  };

}

void SaveState::write(Fruit *fruits, Writer &writer) {
  writer.write(reinterpret_cast<const uint32_t*>(this), sizeof(*this)+3 >> 2);
  for (int i = 0; i < numFruits; ++i) {
    s::Fruit f(fruits[i]);
    writer.write(reinterpret_cast<const uint32_t*>(&f), sizeof(f)+3 >> 2);
  }
}

void SaveState::read(Fruit *fruits, Reader &reader) {
  reader.read(reinterpret_cast<uint32_t*>(this), sizeof(*this)+3 >> 2);
  for (int i = 0; i < numFruits; ++i) {
    s::Fruit f;
    reader.read(reinterpret_cast<uint32_t*>(&f), sizeof(f)+3 >> 2);
    f.setup(fruits[i]);
  }
}
