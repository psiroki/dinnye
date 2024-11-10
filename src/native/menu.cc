#include "menu.hh"

#include <stdint.h>

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
    M("Quit", Meaning::quit),
    EOM,
  };
}


class Submenu {
  const MenuItem * const items;
  int selection;
  int numMenuItems;
  int maxWidth;
  int sumHeight;
  SDL_Surface **captions;
  FruitRenderer &renderer;
  bool resumeWasPossible;

  void adjustSelection(int movement, bool resumePossible = true);
public:
  Submenu(const MenuItem *items, FruitRenderer &renderer);
  ~Submenu();
  void reset();
  void moveVertical(int delta);
  inline const MenuItem* getSelection() {
    return items + selection;
  }
  void render(SDL_Surface *target, GameSettings &settings, bool resumePossible);
};

Submenu::Submenu(const MenuItem *items, FruitRenderer &renderer):
    items(items), renderer(renderer), captions(nullptr), selection(0), resumeWasPossible(true) {
  for (numMenuItems = 0; items[numMenuItems].caption; ++numMenuItems);
  captions = new SDL_Surface*[numMenuItems];
  maxWidth = 0;
  sumHeight = 0;
  for (int i = 0; i < numMenuItems; ++i) {
    SDL_Surface *s = renderer.renderText(items[i].caption, 0xFFFFFFu);
    captions[i] = s;
    if (s->w > maxWidth) maxWidth = s->w;
    sumHeight += s->h;
  }
  adjustSelection(0);
}

Submenu::~Submenu() {
  for (int i = 0; i < numMenuItems; ++i) {
    SDL_FreeSurface(captions[i]);
    captions[i] = nullptr;
  }
  delete[] captions;
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
  for (int i = 0; i < numMenuItems; ++i) {
    if (!resumePossible && items[i].meaning == Meaning::resume) continue;
    SDL_Rect rect { .x = static_cast<Sint16>(x), .y = static_cast<Sint16>(y) };
    SDL_BlitSurface(captions[i], nullptr, target, &rect);
    int height = captions[i]->h;
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
    int height = captions[i]->h;
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
  current = main;
}

Menu::~Menu() {
  delete main;
  main = current = nullptr;
}

void Menu::reset() {
  current = main;
  current->reset();
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
    case Meaning::quit:
      return Command::quit;
  }
  return Command::nop;
}

void Menu::render(SDL_Surface *target, bool resumePossible) {
  renderer.renderTitle(appearanceSeed, appearanceFrame++);
  current->render(target, settings, resumePossible);
}
