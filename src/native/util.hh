#pragma once

#include <time.h>

class Timestamp {
  timespec time;
  static float secondsDiff(const timespec &a, const timespec &b);
public:
  Timestamp();
  void reset();
  void resetWithDelta(float deltaSeconds);
  float elapsedSeconds(bool reset = false);
  float secondsTo(const Timestamp &other);
  inline const timespec& getTime() {
    return time;
  }
};
