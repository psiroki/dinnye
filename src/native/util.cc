#include "util.hh"

#include <stdint.h>

namespace {
  const float nanoToSecond = 1.0e-9f;
}

Timestamp::Timestamp() {
  clock_gettime(CLOCK_MONOTONIC, &time);
}

void Timestamp::reset() {
  clock_gettime(CLOCK_MONOTONIC, &time);
}

void Timestamp::resetWithDelta(float deltaSeconds) {
  reset();
  long secs = static_cast<long>(deltaSeconds);
  long rest = static_cast<long>((deltaSeconds - secs) * 1000000000L);
  time.tv_sec += secs;
  // we want to subtract more than what is in the tv_nsec field
  if (time.tv_nsec < -rest) {
    --time.tv_sec;
    rest += 1000000000L;
  }
  // the amount required for a full second is
  // less than what we're about to add to it
  if (1000000000L - time.tv_nsec < rest) {
    rest -= 1000000000L - time.tv_nsec;
    ++time.tv_sec;
  }
  time.tv_nsec += rest;
}

float Timestamp::secondsDiff(const timespec &then, const timespec &now) {
  int32_t secDiff = now.tv_sec - then.tv_sec;
  if (now.tv_nsec < then.tv_nsec) {
    // like 1.8 to 2.4
    --secDiff;  // secDiff becomes 0
    return secDiff + nanoToSecond * (1000000000L - then.tv_nsec + now.tv_nsec);
  } else {
    return secDiff + nanoToSecond * (now.tv_nsec - then.tv_nsec);
  }
}

float Timestamp::elapsedSeconds(bool reset) {
  timespec then(time), now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (reset) time = now;
  return secondsDiff(then, now);
}

float Timestamp::secondsTo(const Timestamp &other) {
  return secondsDiff(time, other.time);
}
