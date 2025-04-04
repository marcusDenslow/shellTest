/**
 * main.c
 * Entry point for the LSH shell program
 */

#include "common.h"
#include "shell.h"

/**
 * Main entry point
 */

int main(int argc, char **argv) {
  // Set console output code page to UTF-8 (65001)
  // This enables proper display of UTF-8 box characters
  UINT oldCP = GetConsoleOutputCP();
  SetConsoleOutputCP(65001);

  // Add theme to builtins arrays
  // This is usually done in builtins.c before compilation
  // But shown here for clarity of what needs to be added
  /*
  // In builtins.c:
  char *builtin_str[] = {
    // ... existing commands ...
    "theme",
  };

  int (*builtin_func[])(char **) = {
    // ... existing functions ...
    &lsh_theme,
  };
  */

  // Start the shell loop
  lsh_loop();

  // Restore the original code page (optional cleanup)
  SetConsoleOutputCP(oldCP);

  return EXIT_SUCCESS;
}
