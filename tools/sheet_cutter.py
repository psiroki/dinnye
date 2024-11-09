#!/usr/bin/env python3

import sys
from PIL import Image

def find_sprites(image_path):
    img = Image.open(image_path)
    pixels = img.load()
    width, height = img.size

    current_sprite = None
    for y in range(height):
        empty_row = True
        for x in range(width):
            if pixels[x, y][3] != 0:  # Alpha channel is not 0 (not transparent)
                empty_row = False
                if current_sprite is None:
                    current_sprite = (x, y, x+1, y+1)  # Initialize bounding box
                else:
                    # Update bounding box
                    current_sprite = (
                        min(current_sprite[0], x),
                        min(current_sprite[1], y),
                        max(current_sprite[2], x+1),
                        max(current_sprite[3], y+1)
                    )
        if empty_row and current_sprite is not None:
            # Print the bounding box in C++ SDL_Rect format
            print(f"{{ .x = {current_sprite[0]}, .y = {current_sprite[1]}, .w = {current_sprite[2] - current_sprite[0]}, .h = {current_sprite[3] - current_sprite[1]}, }},")
            current_sprite = None

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 sheet_cutter.py image_file.png")
        sys.exit(1)

    image_path = sys.argv[1]
    find_sprites(image_path)
