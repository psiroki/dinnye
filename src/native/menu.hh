#pragma once

#include "renderer.hh"

class Submenu;

enum class Command { nop, resume, reset, quit, };

class Menu {
  Submenu *main;
  Submenu *current;
  FruitRenderer &renderer;
public:
  Menu(FruitRenderer &renderer);
  ~Menu();
  void reset();
  void moveVertical(int delta);
  void moveHorizontal(int delta);
  Command execute();
  void render(SDL_Surface *target);
};
