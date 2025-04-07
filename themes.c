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
    // Standard console colors
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

    // ANSI colors for default theme (basic colors, not true full 24-bit colors)
    .ANSI_BASE = "\033[37m",      // White
    .ANSI_SURFACE = "\033[37m",   // White
    .ANSI_OVERLAY = "\033[37m",   // White
    .ANSI_MUTED = "\033[37m",     // White
    .ANSI_SUBTLE = "\033[37m",    // White
    .ANSI_TEXT = "\033[97m",      // Bright White
    .ANSI_LOVE = "\033[91m",      // Bright Red
    .ANSI_GOLD = "\033[93m",      // Bright Yellow
    .ANSI_ROSE = "\033[95m",      // Bright Magenta
    .ANSI_PINE = "\033[92m",      // Bright Green
    .ANSI_FOAM = "\033[96m",      // Bright Cyan
    .ANSI_IRIS = "\033[94m",      // Bright Blue
    .ANSI_HIGHLIGHT = "\033[37m", // White

    // Don't use ANSI colors for default theme
    .use_ansi_colors = FALSE,

    .name = "default"};

static ShellTheme rose_pine_theme = {
    // Legacy console colors (for backward compatibility)
    .PRIMARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // White (base)
    .SECONDARY_COLOR =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE, // Muted
    .ACCENT_COLOR = FOREGROUND_BLUE, // Blue for git syntax
    .SUCCESS_COLOR = FOREGROUND_GREEN,
    .ERROR_COLOR = FOREGROUND_RED | FOREGROUND_INTENSITY,
    .WARNING_COLOR = FOREGROUND_RED | FOREGROUND_GREEN,

    .HEADER_COLOR = FOREGROUND_RED | FOREGROUND_BLUE,
    .STATUS_BAR_COLOR = 0,
    .STATUS_TEXT_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                         FOREGROUND_INTENSITY,
    .PROMPT_COLOR =
        FOREGROUND_RED | FOREGROUND_INTENSITY, // Bright red for repo name

    .DIRECTORY_COLOR =
        FOREGROUND_RED | FOREGROUND_INTENSITY, // Bright pink for directory

    .EXECUTABLE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN,
    .TEXT_FILE_COLOR = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    .IMAGE_FILE_COLOR = FOREGROUND_BLUE,
    .CODE_FILE_COLOR = FOREGROUND_BLUE,
    .ARCHIVE_FILE_COLOR = FOREGROUND_RED,

    .SYNTAX_KEYWORD = FOREGROUND_RED | FOREGROUND_BLUE,
    .SYNTAX_STRING = FOREGROUND_GREEN,
    .SYNTAX_COMMENT = FOREGROUND_RED | FOREGROUND_GREEN | 0x8,
    .SYNTAX_NUMBER = FOREGROUND_RED,
    .SYNTAX_PREPROCESSOR = FOREGROUND_RED | FOREGROUND_GREEN,

    // ANSI true color definitions - exact Rose Pine colors
    .ANSI_BASE = "\033[38;2;25;23;36m",      // Base color
    .ANSI_SURFACE = "\033[38;2;31;29;46m",   // Surface color
    .ANSI_OVERLAY = "\033[38;2;38;35;58m",   // Overlay color
    .ANSI_MUTED = "\033[38;2;110;106;134m",  // Muted color
    .ANSI_SUBTLE = "\033[38;2;144;140;170m", // Subtle color
    .ANSI_TEXT = "\033[38;2;224;222;244m",   // Text color
    .ANSI_LOVE = "\033[38;2;235;111;146m",   // Love/Red color
    .ANSI_GOLD = "\033[38;2;246;193;119m",   // Gold color
    // Updated Rose/pink color to a soft peach
    .ANSI_ROSE = "\033[38;2;255;195;195m",   // Soft Peach
    .ANSI_PINE = "\033[38;2;49;116;143m",    // Pine color
    .ANSI_FOAM = "\033[38;2;156;207;216m",   // Foam color
    .ANSI_IRIS = "\033[38;2;196;167;231m",   // Iris color
    .ANSI_HIGHLIGHT = "\033[38;2;68;65;90m", // Highlight color

    // Enable ANSI colors for Rose Pine theme
    .use_ansi_colors = TRUE,

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

  // Enable VT processing for ANSI escape sequences if the theme uses them
  if (current_theme.use_ansi_colors) {
    DWORD dwMode = 0;
    GetConsoleMode(hConsole, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hConsole, dwMode);

    // Use ANSI text color
    printf("%s", current_theme.ANSI_TEXT);
  } else {
    // Use standard console color
    SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);
  }
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
  printf(
      "  rose-pine  - Soothing, warm color scheme with true color support\n");

  // Display which theme is currently active
  printf("\nCurrent theme: %s", current_theme.name);

  // Display ANSI color status for current theme
  if (current_theme.use_ansi_colors) {
    printf(" (using true color)\n");
  } else {
    printf(" (using standard console colors)\n");
  }
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
    printf("\nCurrent theme: %s", current_theme.name);

    // Display ANSI color status for current theme
    if (current_theme.use_ansi_colors) {
      printf(" (using true color)\n");
    } else {
      printf(" (using standard console colors)\n");
    }

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

        // Display ANSI color status for the selected theme
        if (current_theme.use_ansi_colors) {
          printf("This theme uses true color for better visual appearance.\n");
        }
      } else {
        fprintf(stderr, "Could not save theme setting\n");
      }
    }
    return 1;
  } else if (strcmp(args[1], "show") == 0) {
    printf("Current theme: %s\n", current_theme.name);

    // Display ANSI color status
    if (current_theme.use_ansi_colors) {
      printf("Using true color mode");

      // Show a color demo if using ANSI colors
      printf("\nColor sample:\n");
      printf("%sBase %sText %sLove %sGold %sRose %sPine %sFoam %sIris%s\n",
             current_theme.ANSI_BASE, current_theme.ANSI_TEXT,
             current_theme.ANSI_LOVE, current_theme.ANSI_GOLD,
             current_theme.ANSI_ROSE, current_theme.ANSI_PINE,
             current_theme.ANSI_FOAM, current_theme.ANSI_IRIS,
             "\033[0m"); // Reset
    } else {
      printf("Using standard console colors\n");
    }

    return 1;
  } else {
    printf("Unknown theme command: %s\n", args[1]);
    printf("Try 'theme list' or 'theme set <name>'\n");
    return 1;
  }

  return 1;
}
