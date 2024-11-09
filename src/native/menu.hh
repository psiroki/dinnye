#pragma once

#include "renderer.hh"

class Submenu;

enum class Command { nop, resume, reset, quit, };

class GameSettings {
public:
  virtual bool isMusicEnabled()=0;
  virtual void setMusicEnabled(bool val)=0;
  virtual bool isSoundEnabled()=0;
  virtual void setSoundEnabled(bool val)=0;
};

class Menu {
  Submenu *main;
  Submenu *current;
  FruitRenderer &renderer;
  GameSettings &settings;
  uint32_t appearanceSeed;
public:
  Menu(FruitRenderer &renderer, GameSettings &settings);
  ~Menu();
  void reset();
  void moveVertical(int delta);
  void moveHorizontal(int delta);
  Command execute();
  void render(SDL_Surface *target);
  inline void setAppearanceSeed(uint32_t newSeed) {
    appearanceSeed = newSeed;
  }
};
