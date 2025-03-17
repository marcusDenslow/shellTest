/**
 * image_display.h
 * Functions for displaying images in the terminal
 */

#ifndef IMAGE_DISPLAY_H
#define IMAGE_DISPLAY_H

#include "common.h"

/**
 * Display an image in the terminal using escape sequences
 * 
 * @param args Command arguments (args[1] is the image path)
 * @return Always returns 1 (continue the shell)
 */
int lsh_img(char **args);

/**
 * Check if the terminal supports images
 */
BOOL is_terminal_graphics_capable();

#endif // IMAGE_DISPLAY_H
