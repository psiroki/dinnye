#pragma once

#include "../common/sim.hh"

struct NextDrop {
  float x;
  float xv;
  int32_t radIndex;
  int32_t seed;
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
  uint32_t audioFlagsMuted;
  NextDrop next;
  int32_t outlierIndex;
  uint32_t simulationFrame;
  int32_t score;
  uint32_t numFruits;

  void write(Fruit *fruits, Writer &writer);
  void read(Fruit *fruits, Reader &writer);
};


