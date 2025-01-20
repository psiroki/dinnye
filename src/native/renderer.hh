#pragma once

#include <stdint.h>

#include "platform.hh"
#include "util.hh"
#include "../common/sim.hh"

template <typename T> T min(T a, T b) {
  return a < b ? a : b;
}

template <typename T> T max(T a, T b) {
  return a > b ? a : b;
}

template <typename T> T clamp(T min, T max, T val) {
  if (val < min) return min;
  if (val > max) return max;
  return val;
}

struct ShadedSphere {
  PixelBuffer albedo;
  uint32_t *shading;

  static void initTables();
  void render(PixelBuffer &target, int cx, int cy, int radius, int angle);
};

class SphereCache {
  ShadedSphere *s;
  SDL_Surface *cache;
  int radius;
  int angle;
  bool outlier;
  bool dirty;
#ifdef DEBUG_VISUALIZATION
  int invalidationReason;
#endif
  friend class FruitRenderer;

public:
  static int numCacheHits;
  static int numCacheMisses;
  static int numCacheAngleMisses;
  static int numCacheReassignMisses;

  inline SphereCache(): s(nullptr), cache(nullptr), radius(0), angle(0), dirty(false), outlier(false) { }

  void release();

  inline ~SphereCache() {
    release();
  }

  int reassign(ShadedSphere *newSphere, int newRadius, bool outlier = false);
  SDL_Surface* withAngle(int newAngle);

#ifdef DEBUG_VISUALIZATION
  int getInvalidationReason() {
    return invalidationReason;
  }
#endif
};

class ScoreCache {
  const char *title;
  SDL_Surface *rendered;
  int score;
  TTF_Font *font;
  bool dirty;

  void freeSurface();
public:
  inline ScoreCache(const char *title = "Score"):
      title(title),
      rendered(0),
      score(-1),
      font(0),
      dirty(false) {
    }
  ~ScoreCache();

  inline void setFont(TTF_Font *newFont) {
    font = newFont;
    dirty = true;
  }

  SDL_Surface* render(int newScore);
};

struct Placement {
  int x, y;
  int w, h;

  bool contains(int xc, int yc) const {
    return xc >= x && xc < x + w && yc >= y && yc < y + h;
  }
};

struct PlanetDefinition {
  char name[16];
  SDL_Surface *nameText;
  // Preview placement
  Placement placement;
};

void blur(SDL_Surface *s, int frame);

class FruitRenderer {
  SDL_Surface **textures;
  PlanetDefinition planetDefs[numRadii];
  int numTextures;
  uint32_t *shading;
  SphereCache spheres[fruitCap + numRadii];
  int numSpheres;
  ShadedSphere *sphereDefs;
  SDL_Surface *target;
  Scalar zoom;
  Scalar offsetX;
  Scalar sizeX, sizeY;
  int fontSize;
  TTF_Font *font;
  ScoreCache scoreCache;
  ScoreCache highscoreCache;
  SDL_Surface *title;
  int fps;
  uint32_t menuButtonAlpha;
  uint32_t menuButtonHover;
  Placement menuButtonPlacement;
  Timestamp performance;
  uint64_t performanceCounts[16];
  int performanceSnapshots[16];
  int perfIndex;

  /// Renders the topmost layer for the game and lost state
  void renderCommonOverlay(PixelBuffer pb);
  void layoutCommonOverlay();
  void addTime();
public:
  FruitRenderer(SDL_Surface *target);
  ~FruitRenderer();

  void dumpTimes();

  inline const Placement& getMenuButtonPlacement() const {
    return menuButtonPlacement;
  }

  inline void setFps(int newFps) {
    fps = newFps;
  }

  inline void setMenuButtonAlpha(int alpha) {
    menuButtonAlpha = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
  }

  inline void setMenuButtonHover(int alpha) {
    menuButtonHover = alpha < 0 ? 0 : alpha > 255 ? 255 : alpha;
  }

  inline void setLayout(Scalar newZoom, Scalar newOffsetX, const FruitSim &sim) {
    zoom = newZoom;
    offsetX = newOffsetX;
    sizeX = sim.getWorldWidth();
    sizeY = sim.getWorldHeight();
    layoutCommonOverlay();
  }
  SDL_Surface* renderText(const char *str, uint32_t color);
  void renderTitle(int taglineSelection, int fade);
  void renderLostScreen(int score, int highscore, SDL_Surface *background, int animationFrame);
  void renderMenuScores(int score, int highscore);
  void renderBackground(SDL_Surface *background);
  void renderSelection(PixelBuffer pb, int left, int top, int right, int bottom, int shift, bool hollow = false);
  void renderFruits(FruitSim &sim, int count, int selection, int outlierIndex, uint32_t frameIndex, Scalar frameFraction, bool skipScore);
};
