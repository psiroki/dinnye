#pragma once
#include <SDL/SDL.h>

SDL_Surface* loadImage(const char* filename);
SDL_Surface* loadImageFromMemory(const void* contents, int size);
