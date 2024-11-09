#pragma once

#include "../common/sim.hh"

struct NextDrop {
  float x;
  float xv;
  int32_t radIndex;
  int32_t seed;
};

struct Highscore {
  uint32_t name;
  int32_t score;

  Highscore(): name(0), score(0) { }
};

template<typename T> struct RecordBuffer {
  T *items;
  uint32_t capacity;

  RecordBuffer() { }
  RecordBuffer(T *items, uint32_t capacity): items(items), capacity(capacity) { }
};

class Writer {
public:
  virtual void write(const uint32_t *buf, uint32_t numWords)=0;
};

class Reader {
public:
  virtual void read(uint32_t *buf, uint32_t numWords)=0;
};

struct SaveState {
  // Version 1
  static constexpr uint32_t magicExpected = 0xcafebee1;

  uint32_t audioFlagsMuted;
  NextDrop next;
  int32_t outlierIndex;
  uint32_t simulationFrame;
  int32_t score;
  uint32_t numHighscores;
  uint32_t numFruits;
  uint32_t magicBytes;

  void write(Fruit *fruits, Highscore *highscores, Writer &writer);
  bool read(RecordBuffer<Fruit> &fruits, RecordBuffer<Highscore> &highscores, Reader &writer);
};
