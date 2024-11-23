#pragma once

#include <stdint.h>

#include "util.hh"
#include "platform.hh"

enum class Control {
  UNMAPPED, UP, DOWN, LEFT, RIGHT, NORTH, SOUTH, WEST, EAST, R1, L1, R2, L2, START, SELECT, MENU, LAST_ITEM,
};

const int TYPE_KEY = 0 << 16;
const int TYPE_BUTTON = 1 << 16;
const int TYPE_HAT = 2 << 16;

struct KeyMapTable {
  int32_t numEntries;
  int32_t maxProbes;
  KeyHasher hasher;
  int32_t mappings[0];
};

class InputMapping {
  static const KeyMapTable* const defaultLayout;

  const KeyMapTable *table;
#ifdef USE_GAME_CONTROLLER
  Control controllerButtonMapping[static_cast<int32_t>(SDL_CONTROLLER_BUTTON_MAX)];
  void initControllerMapping();
#endif
  Control mapRaw(int32_t val) const;
public:
  inline InputMapping(): table(defaultLayout) {
#ifdef USE_GAME_CONTROLLER
    initControllerMapping();
#endif
  }
  inline InputMapping(const KeyMapTable *t): table(t) {
#ifdef USE_GAME_CONTROLLER
    initControllerMapping();
#endif
  }

  void dumpTable();

  inline void setTable(void *tableBuffer) {
    table = reinterpret_cast<const KeyMapTable*>(tableBuffer);
  }

  inline Control mapKey(int32_t key) const {
    return mapRaw(key | TYPE_KEY);
  }

  inline Control mapButton(int button) const {
    return mapRaw(button | TYPE_BUTTON);
  }

  inline Control mapHatDirection(int directionMask) const {
    return mapRaw(directionMask | TYPE_HAT);
  }

#ifdef USE_GAME_CONTROLLER
  Control mapGameControllerButton(SDL_GameControllerButton button) const;
  
  inline Control mapGameControllerButtonIndex(int button) const {
    return mapGameControllerButton(static_cast<SDL_GameControllerButton>(button));
  }
#endif
};
