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
    };
  }

  struct MenuItem {
    const char *caption;
    uint32_t flags;
  };

#define M(c, f) { .caption = c, .flags = f, }
#define P(c) M(c, Meaning::passive)
#define EOM M(nullptr, 0)

  const MenuItem mainItems[] = {
    M("Resume", Meaning::resume),
    M("New Game", Meaning::newGame),
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

  void adjustSelection();
public:
  Submenu(const MenuItem *items, FruitRenderer &renderer);
  ~Submenu();
  void reset();
  void moveVertical(int delta);
  inline const MenuItem* getSelection() {
    return items + selection;
  }
  void render(SDL_Surface *target);
};

Submenu::Submenu(const MenuItem *items, FruitRenderer &renderer):
    items(items), renderer(renderer), captions(nullptr), selection(0) {
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
  adjustSelection();
}

Submenu::~Submenu() {
  for (int i = 0; i < numMenuItems; ++i) {
    SDL_FreeSurface(captions[i]);
    captions[i] = nullptr;
  }
  delete[] captions;
}


void Submenu::adjustSelection() {
  while (items[selection].flags == Meaning::passive) {
    ++selection;
    if (selection >= numMenuItems) selection = 0;
  }
}

void Submenu::reset() {
  selection = 0;
  adjustSelection();
}

void Submenu::moveVertical(int delta) {
  selection += delta;
  selection %= numMenuItems;
  if (selection < 0) selection += numMenuItems;
  adjustSelection();
}

void Submenu::render(SDL_Surface *target) {
  int x = (target->w - maxWidth) >> 1;
  int y = (target->h - sumHeight - numMenuItems) >> 1;
  int top = y;
  int bottom = y;
  for (int i = 0; i < numMenuItems; ++i) {
    SDL_Rect rect { .x = static_cast<Sint16>(x), .y = static_cast<Sint16>(y) };
    SDL_BlitSurface(captions[i], nullptr, target, &rect);
    if (i == selection) top = y;
    y += captions[i]->h + 1;
    if (i == selection) bottom = y;
  }
  SurfaceLocker locker(target);
  int margin = (bottom - top) >> 1;
  renderer.renderSelection(locker.pb, x - margin, top, x + maxWidth + margin * 2, bottom);
}

Menu::Menu(FruitRenderer &renderer): renderer(renderer) {
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
  switch (sel->flags) {
    case Meaning::mainMenu:
      current = main;
      break;
    case Meaning::newGame:
      return Command::reset;
    case Meaning::resume:
      return Command::resume;
    case Meaning::quit:
      return Command::quit;
  }
  return Command::nop;
}

void Menu::render(SDL_Surface *target) {
  current->render(target);
}
