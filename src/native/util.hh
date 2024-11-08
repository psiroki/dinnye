#pragma once

#include <time.h>
#include <pthread.h>
#include <stdint.h>

template<typename T> class AutoDeleteArray {
  T* ptr;
  
  void release() {
    if (ptr) delete[] ptr;
    ptr = nullptr;
  }
public:
  AutoDeleteArray(T* ptr = nullptr): ptr(ptr) { }
  ~AutoDeleteArray() {
    release();
  }

  T* asPointer() {
    return ptr;
  }

  operator T*() {
    return ptr;
  }

  T* operator->() {
    return ptr;
  }

  T* operator=(T* newVal) {
    if (ptr == newVal) return newVal;
    release();
    return ptr = newVal;
  }
};

template<typename T> class AutoDelete {
  T* ptr;
  
  void release() {
    if (ptr) delete ptr;
    ptr = nullptr;
  }
public:
  AutoDelete(T* ptr = nullptr): ptr(ptr) { }
  ~AutoDelete() {
    release();
  }

  T* asPointer() {
    return ptr;
  }

  operator T*() {
    return ptr;
  }

  T* operator->() {
    return ptr;
  }

  T* operator=(T* newVal) {
    if (ptr == newVal) return newVal;
    release();
    return ptr = newVal;
  }
};

class Timestamp {
  timespec time;
  static float secondsDiff(const timespec &a, const timespec &b);
  static uint64_t microsDiff(const timespec &a, const timespec &b);
public:
  Timestamp();
  void reset();
  void resetWithDelta(float deltaSeconds);
  uint64_t elapsedMicros(bool reset = false);
  float elapsedSeconds(bool reset = false);
  float secondsTo(const Timestamp &other);
  inline const timespec& getTime() const {
    return time;
  }
};

struct BufferView {
  void *buffer;
  uint32_t sizeInBytes;
};

class Mutex {
private:
  pthread_mutex_t mutex;

public:
  inline Mutex() {
    pthread_mutex_init(&mutex, nullptr);
  }

  inline ~Mutex() {
    pthread_mutex_destroy(&mutex);
  }

  inline void lock() {
    pthread_mutex_lock(&mutex);
  }

  inline void unlock() {
    pthread_mutex_unlock(&mutex);
  }
};

class Condition {
private:
  pthread_mutex_t mutex;
  pthread_cond_t condition;
public:
  Condition();
  ~Condition();

  void wait();
  void notify();
};

int createDirectoryForFile(const char *path);
