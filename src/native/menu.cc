#include "menu.hh"

#include <stdint.h>
#include <iostream>

namespace {

  namespace Meaning {
    enum {
      passive,
      resume,
      credits,
      quit,
      mainMenu,
      newGame,
      sound,
      music,
    };
  }

  struct MenuItem {
    const char *caption;
    uint32_t meaning;
  };

#define M(c, f) { .caption = c, .meaning = f, }
#define P(c) M(c, Meaning::passive)
#define EOM M(nullptr, 0)

  const MenuItem mainItems[] = {
    M("Resume", Meaning::resume),
    M("New Game", Meaning::newGame),
    M("Music", Meaning::music),
    M("Sound", Meaning::sound),
#ifndef BITTBOY
    M("Credits", Meaning::credits),
#endif
    M("Quit", Meaning::quit),
    EOM,
  };

  const MenuItem creditsItems[] = {
    P("Developed by Peter Siroki"),
    P(""),
    P("Music:"),
    P("Wiggle Until You Giggle"),
    P("By GoldenSoundLabs at Pixabay"),
    M("OK", Meaning::mainMenu),
    EOM,
  };
}

struct RenderedMenuItem {
  SDL_Surface *caption;
  int x, y;
};

class Submenu {
  const MenuItem * const items;
  int selection;
  int numMenuItems;
  int maxWidth;
  int sumHeight;
  RenderedMenuItem *rendered;
  FruitRenderer &renderer;
  bool resumeWasPossible;

  void adjustSelection(int movement, bool resumePossible = true);
public:
  Submenu(const MenuItem *items, FruitRenderer &renderer);
  ~Submenu();
  void reset();
  void hover(int x, int y);
  void moveVertical(int delta);
  inline const MenuItem* getSelection() {
    return items + selection;
  }
  void render(SDL_Surface *target, GameSettings &settings, bool resumePossible);
};

Submenu::Submenu(const MenuItem *items, FruitRenderer &renderer):
    items(items), renderer(renderer), rendered(nullptr), selection(0), resumeWasPossible(true) {
  for (numMenuItems = 0; items[numMenuItems].caption; ++numMenuItems);
  rendered = new RenderedMenuItem[numMenuItems];
  maxWidth = 0;
  sumHeight = 0;
  int height = 0;
  for (int i = 0; i < numMenuItems; ++i) {
    if (!*items[i].caption) {
      rendered[i].caption = nullptr;
      rendered[i].x = rendered[i].y = 0;
      sumHeight += height;
      continue;
    }
    SDL_Surface *s = renderer.renderText(items[i].caption, 0xFFFFFFu);
    rendered[i].caption = s;
    if (s->w > maxWidth) maxWidth = s->w;
    height = s->h;
    sumHeight += height;
  }
  adjustSelection(0);
}

Submenu::~Submenu() {
  for (int i = 0; i < numMenuItems; ++i) {
    SDL_FreeSurface(rendered[i].caption);
    rendered[i].caption = nullptr;
  }
  delete[] rendered;
}


void Submenu::adjustSelection(int movement, bool resumePossible) {
  if (!movement) movement = 1;
  while (items[selection].meaning == Meaning::passive || !resumePossible && items[selection].meaning == Meaning::resume) {
    selection += movement;
    if (selection >= numMenuItems) selection = 0;
    if (selection < 0) selection += numMenuItems;
  }
}

void Submenu::reset() {
  selection = 0;
  resumeWasPossible = true;
  adjustSelection(0);
}

void Submenu::hover(int x, int y) {
  int found = -1;
  for (int i = 0; i < numMenuItems; ++i) {
    const RenderedMenuItem &r(rendered[i]);
    if (r.caption && y >= r.y && y - r.y < r.caption->h && x >= r.x && x - r.y < r.caption->w) {
      found = i;
      break;
    }
  }
  if (found >= 0) {
    selection = found;
    adjustSelection(0, resumeWasPossible);
  }
}

void Submenu::moveVertical(int delta) {
  selection += delta;
  selection %= numMenuItems;
  if (selection < 0) selection += numMenuItems;
  adjustSelection(delta, resumeWasPossible);
}

void Submenu::render(SDL_Surface *target, GameSettings &settings, bool resumePossible) {
  resumeWasPossible = resumePossible;
  if (!resumePossible) adjustSelection(0, resumePossible);
  int x = (target->w - maxWidth) >> 1;
  int shownMenuItems = numMenuItems - (resumePossible ? 0 : 1);
  int startY = (target->h - sumHeight - shownMenuItems) * 2 / 3;
  int y = startY;
  int top = y;
  int bottom = y;
  int height = 0;
  for (int i = 0; i < numMenuItems; ++i) {
    if (!resumePossible && items[i].meaning == Meaning::resume) continue;
    SDL_Rect rect = makeRect(x, y);
    rendered[i].x = x;
    rendered[i].y = y;
    if (rendered[i].caption) {
      SDL_BlitSurface(rendered[i].caption, nullptr, target, &rect);
      height = rendered[i].caption->h;
    }
    uint32_t meaning = items[i].meaning;
    if (meaning == Meaning::music || meaning == Meaning::sound) {
      bool enabled = meaning == Meaning::music ? settings.isMusicEnabled() : settings.isSoundEnabled();
    }
    if (i == selection) top = y;
    y += height + 1;
    if (i == selection) bottom = y;
  }
  SurfaceLocker locker(target);
  y = startY;
  for (int i = 0; i < numMenuItems; ++i) {
    if (!resumePossible && items[i].meaning == Meaning::resume) continue;
    if (rendered[i].caption) height = rendered[i].caption->h;
    uint32_t meaning = items[i].meaning;
    if (meaning == Meaning::music || meaning == Meaning::sound) {
      bool enabled = meaning == Meaning::music ? settings.isMusicEnabled() : settings.isSoundEnabled();
      int left = x - height * 3 / 5;
      int width = height * 2 / 5;
      renderer.renderSelection(locker.pb, left, y + height / 8, left + width, y + height * 7 / 8 + 1, 0, !enabled);
    }
    y += height + 1;
  }
  int marginLeft = (bottom - top)*3 >> 2;
  int marginRight = (bottom - top) >> 1;
  renderer.renderSelection(locker.pb, x - marginLeft, top, x + maxWidth + marginRight * 2, bottom, 0);
}

Menu::Menu(FruitRenderer &renderer, GameSettings &settings): renderer(renderer), settings(settings) {
  main = new Submenu(mainItems, renderer);
#ifndef BITTBOY
  credits = new Submenu(creditsItems, renderer);
#else
  credits = nullptr;
#endif
  current = main;
}

Menu::~Menu() {
  delete main;
  if (credits) delete credits;
  credits = main = current = nullptr;
}

void Menu::reset() {
  current = main;
  current->reset();
}

void Menu::hover(int x, int y) {
  current->hover(x, y);
}

void Menu::moveVertical(int delta) {
  current->moveVertical(delta);
}

void Menu::moveHorizontal(int delta) {
}

Command Menu::execute() {
  const MenuItem *sel = current->getSelection();
  switch (sel->meaning) {
    case Meaning::mainMenu:
      current = main;
      break;
    case Meaning::newGame:
      return Command::reset;
    case Meaning::resume:
      return Command::resume;
    case Meaning::music:
      settings.setMusicEnabled(!settings.isMusicEnabled());
      return Command::nop;
    case Meaning::sound:
      settings.setSoundEnabled(!settings.isSoundEnabled());
      return Command::nop;
    case Meaning::credits:
      current = credits;
      break;
    case Meaning::quit:
      return Command::quit;
  }
  return Command::nop;
}

void Menu::render(SDL_Surface *target, bool resumePossible) {
  renderer.renderTitle(appearanceSeed, appearanceFrame++);
  current->render(target, settings, resumePossible);
}
