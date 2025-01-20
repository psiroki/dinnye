#include "util.hh"

#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir(path, mode) _mkdir(path) // Windows does not support `mode`, so we ignore it
#else
#include <errno.h>
#endif

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

uint64_t Timestamp::microsDiff(const timespec &then, const timespec &now) {
  int32_t secDiff = now.tv_sec - then.tv_sec;
  if (now.tv_nsec < then.tv_nsec) {
    // like 1.8 to 2.4
    --secDiff;  // secDiff becomes 0
    return secDiff * static_cast<uint64_t>(1000000) + (1000000000L - then.tv_nsec + now.tv_nsec) / 1000;
  } else {
    return secDiff * 1000000 + (now.tv_nsec - then.tv_nsec) / 1000;
  }
}

uint64_t Timestamp::nanosDiff(const timespec &then, const timespec &now) {
  int32_t secDiff = now.tv_sec - then.tv_sec;
  if (now.tv_nsec < then.tv_nsec) {
    // like 1.8 to 2.4
    --secDiff;  // secDiff becomes 0
    return secDiff * 1000000000ULL + 1000000000ULL - then.tv_nsec + now.tv_nsec;
  } else {
    return secDiff * 1000000000ULL + now.tv_nsec - then.tv_nsec;
  }
}

float Timestamp::elapsedSeconds(bool reset) {
  timespec then(time), now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (reset) time = now;
  return secondsDiff(then, now);
}

uint64_t Timestamp::elapsedMicros(bool reset) {
  timespec then(time), now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (reset) time = now;
  return microsDiff(then, now);
}

uint64_t Timestamp::elapsedNanos(bool reset) {
  timespec then(time), now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  if (reset) time = now;
  return nanosDiff(then, now);
}

float Timestamp::secondsTo(const Timestamp &other) {
  return secondsDiff(time, other.time);
}

Condition::Condition() {
  pthread_mutex_init(&mutex, nullptr);
  pthread_cond_init(&condition, nullptr);
}

Condition::~Condition() {
  pthread_mutex_destroy(&mutex);
  pthread_cond_destroy(&condition);
}

void Condition::wait() {
  pthread_mutex_lock(&mutex);
  pthread_cond_wait(&condition, &mutex);
  pthread_mutex_unlock(&mutex);
}

void Condition::notify() {
  pthread_mutex_lock(&mutex);
  pthread_cond_signal(&condition);
  pthread_mutex_unlock(&mutex);
}

int createDirectoryForFile(const char *path) {
  size_t len = strnlen(path, 65536);
  char temp[len + 1];
  char *p = nullptr;

  // Copy path to a temporary buffer
  strncpy(temp, path, len + 1);
  len = strlen(temp);

  // Remove trailing slash, if there is one
  if (temp[len - 1] == '/') {
    temp[len - 1] = '\0';
  }

  // Iterate through the path, creating directories as needed
  for (p = temp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0'; // Temporarily end the string to create intermediate directories

      // Attempt to create the directory
      #if defined(_WIN32) && !defined(__MINGW32__)
      if (mkdir(temp) != 0 && errno != EEXIST) {
      #else
      if (mkdir(temp, 0755) != 0 && errno != EEXIST) {
      #endif
        return -1; // Failed to create directory
      }

      *p = '/'; // Restore slash
    }
  }

  // The last section is intentionally ignored, it's supposed to be a file

  return 0;
}

