/**
 * themes.c
 * Implementation of theme system for shell appearance
 */

#include "themes.h"
#include <stdio.h>
#include <string.h>

#define FOREGROUND_CYAN (FOREGROUND_GREEN | FOREGROUND_BLUE)
#define FOREGROUND_MAGENTA (FOREGROUND_RED | FOREGROUND_BLUE)

// Current active theme
ShellTheme current_theme;

// Define built-in themes
static ShellTheme default_theme = {
    .PRIMARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White
    .SECONDARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,  // White (dimmer)
    .ACCENT_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY,  // Bright Green
    .SUCCESS_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY,     // Bright Red
    .WARNING_COLOR = FOREGROUND_RED | FOREGROUND_GREEN |
                     FOREGROUND_INTENSITY, // Bright Yellow

    .HEADER_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .STATUS_BAR_COLOR = 0,                                   // Black
    .STATUS_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                         FOREGROUND_INTENSITY,              // Bright White
    .PROMPT_COLOR = FOREGROUND_CYAN | FOREGROUND_INTENSITY, // Bright Cyan

    .DIRECTORY_COLOR = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .EXECUTABLE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN |
                        FOREGROUND_INTENSITY, // Bright Yellow
    .TEXT_FILE_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White
    .IMAGE_FILE_COLOR = FOREGROUND_RED | FOREGROUND_BLUE |
                        FOREGROUND_INTENSITY,                  // Bright Magenta
    .CODE_FILE_COLOR = FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Bright Blue
    .ARCHIVE_FILE_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY, // Bright Red

    .SYNTAX_KEYWORD = FOREGROUND_CYAN | FOREGROUND_INTENSITY, // Bright Cyan
    .SYNTAX_STRING = FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Bright Green
    .SYNTAX_COMMENT = FOREGROUND_INTENSITY,                   // Gray
    .SYNTAX_NUMBER =
        FOREGROUND_MAGENTA | FOREGROUND_INTENSITY, // Bright Magenta
    .SYNTAX_PREPROCESSOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY, // Yellow

    .name = "default"};

static ShellTheme rose_pine_theme = {
    .PRIMARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White (base)
    .SECONDARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // Muted
    .ACCENT_COLOR =
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Rose
    .SUCCESS_COLOR = FOREGROUND_GREEN,                           // Pine
    .ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY,        // Love
    .WARNING_COLOR = FOREGROUND_RED | FOREGROUND_GREEN,          // Gold

    .HEADER_COLOR =
        FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Rose
    .STATUS_BAR_COLOR = 0,                                       // Base
    .STATUS_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                         FOREGROUND_INTENSITY,        // Text
    .PROMPT_COLOR = FOREGROUND_RED | FOREGROUND_BLUE, // Iris

    .DIRECTORY_COLOR = FOREGROUND_GREEN,                   // Pine
    .EXECUTABLE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN, // Gold
    .TEXT_FILE_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,    // Text
    .IMAGE_FILE_COLOR = FOREGROUND_BLUE | FOREGROUND_INTENSITY, // Foam
    .CODE_FILE_COLOR = FOREGROUND_BLUE,                         // Iris (muted)
    .ARCHIVE_FILE_COLOR = FOREGROUND_RED,                       // Love (muted)

    .SYNTAX_KEYWORD = FOREGROUND_RED | FOREGROUND_BLUE,        // Iris
    .SYNTAX_STRING = FOREGROUND_GREEN,                         // Pine
    .SYNTAX_COMMENT = FOREGROUND_RED | FOREGROUND_GREEN | 0x8, // Muted
    .SYNTAX_NUMBER = FOREGROUND_RED | FOREGROUND_INTENSITY,    // Love
    .SYNTAX_PREPROCESSOR = FOREGROUND_RED | FOREGROUND_GREEN,  // Gold

    .name = "rose-pine"};

// Path to the theme configuration file
char theme_config_path[MAX_PATH];

/**
 * Initialize the theme system
 */
void init_theme_system(void) {
  // Determine theme config file location in user's home directory
  char *home_dir = getenv("USERPROFILE");
  if (home_dir) {
    snprintf(theme_config_path, MAX_PATH, "%s\\.lsh_theme", home_dir);
  } else {
    // Fallback to current directory if USERPROFILE not available
    strcpy(theme_config_path, ".lsh_theme");
  }

  // Set default theme initially
  memcpy(&current_theme, &default_theme, sizeof(ShellTheme));

  // Try to load theme from config file
  FILE *config_file = fopen(theme_config_path, "r");
  if (config_file) {
    char theme_name[32];
    if (fscanf(config_file, "theme=%31s", theme_name) == 1) {
      // We found a theme name, try to load it
      load_theme(theme_name);
    }
    fclose(config_file);
  } else {
    // Config file doesn't exist, create it with default theme
    config_file = fopen(theme_config_path, "w");
    if (config_file) {
      fprintf(config_file, "theme=default\n");
      fclose(config_file);
    }
  }
}

/**
 * Load a specific theme by name
 *
 * @param theme_name Name of the theme to load
 * @return 1 if successful, 0 if theme not found
 */
int load_theme(const char *theme_name) {
  if (strcmp(theme_name, "default") == 0) {
    memcpy(&current_theme, &default_theme, sizeof(ShellTheme));
    return 1;
  } else if (strcmp(theme_name, "rose-pine") == 0) {
    memcpy(&current_theme, &rose_pine_theme, sizeof(ShellTheme));
    return 1;
  }

  // Theme not found
  fprintf(stderr, "Theme '%s' not found\n", theme_name);
  return 0;
}

/**
 * Apply the current theme settings
 * Called whenever theme-dependent UI elements need to be updated
 */
void apply_current_theme(void) {
  // When called, the shell can update any on-screen elements that depend on
  // theme colors This function would be called when changing themes or at
  // startup

  // For now, just update the console's text color to the primary color
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);
}

/**
 * Get pointer to the current theme
 *
 * @return Pointer to the current theme structure
 */
const ShellTheme *get_current_theme(void) { return &current_theme; }

/**
 * List all available themes
 */
void list_available_themes(void) {
  printf("Available themes:\n");
  printf("  default    - Standard shell colors\n");
  printf("  rose-pine  - Soothing, warm color scheme\n");

  // Display which theme is currently active
  printf("\nCurrent theme: %s\n", current_theme.name);
}

/**
 * Command handler for the "theme" command
 *
 * @param args Command arguments
 * @return 1 to continue shell execution, 0 to exit shell
 */
int lsh_theme(char **args) {
  if (args[1] == NULL) {
    // No arguments, show usage info and current theme
    printf("Usage: theme <command> [arguments]\n");
    printf("Commands:\n");
    printf("  list      List available themes\n");
    printf("  set NAME  Set the current theme to NAME\n");
    printf("  show      Show current theme details\n");
    printf("\nCurrent theme: %s\n", current_theme.name);
    return 1;
  }

  // Handle subcommands
  if (strcmp(args[1], "list") == 0) {
    list_available_themes();
    return 1;
  } else if (strcmp(args[1], "set") == 0) {
    if (args[2] == NULL) {
      printf("Usage: theme set <theme_name>\n");
      printf("Try 'theme list' to see available themes\n");
      return 1;
    }

    // Try to load the requested theme
    if (load_theme(args[2])) {
      // Update theme config file
      FILE *config_file = fopen(theme_config_path, "w");
      if (config_file) {
        fprintf(config_file, "theme=%s\n", args[2]);
        fclose(config_file);

        // Apply the theme
        apply_current_theme();

        printf("Theme set to '%s'\n", args[2]);
      } else {
        fprintf(stderr, "Could not save theme setting\n");
      }
    }
    return 1;
  } else if (strcmp(args[1], "show") == 0) {
    printf("Current theme: %s\n", current_theme.name);
    // You could print color details here if desired
    return 1;
  } else {
    printf("Unknown theme command: %s\n", args[1]);
    printf("Try 'theme list' or 'theme set <name>'\n");
    return 1;
  }

  return 1;
}
