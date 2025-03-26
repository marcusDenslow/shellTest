/**
 * timer.c
 * Implements a command to measure execution time of other commands
 */

#include "builtins.h"
#include "common.h"
#include "shell.h"
#include <time.h>

/**
 * Format a time interval in milliseconds to a readable string
 *
 * @param ms Time in milliseconds
 * @param buffer Buffer to store formatted time
 * @param buffer_size Size of the buffer
 */
void format_time(double ms, char *buffer, size_t buffer_size) {
  if (ms < 1.0) {
    // Microseconds
    snprintf(buffer, buffer_size, "%.2f μs", ms * 1000);
  } else if (ms < 1000.0) {
    // Milliseconds
    snprintf(buffer, buffer_size, "%.2f ms", ms);
  } else if (ms < 60000.0) {
    // Seconds
    snprintf(buffer, buffer_size, "%.2f s", ms / 1000);
  } else {
    // Minutes and seconds
    int minutes = (int)(ms / 60000);
    double seconds = (ms - minutes * 60000) / 1000;
    snprintf(buffer, buffer_size, "%d min %.2f s", minutes, seconds);
  }
}

/**
 * Implementation of the timer command
 * Usage: timer COMMAND [ARGS...]
 *
 * @param args Command arguments
 * @return 1 to continue, 0 to exit
 */
int lsh_timer(char **args) {
  if (!args[1]) {
    printf("timer: usage: timer COMMAND [ARGS...]\n");
    printf("Measures execution time of a command\n");
    return 1;
  }

  // Get handle to console for colored output
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD originalAttributes;
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    originalAttributes = csbi.wAttributes;
  } else {
    originalAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
  }

  // Create a new array for the command to execute (without "timer" prefix)
  char **cmd_args = &args[1];

  // Start timing
  clock_t start_time = clock();

  // Execute the command
  int result;
  if (strcmp(cmd_args[0], "cd") == 0 || strcmp(cmd_args[0], "exit") == 0 ||
      strcmp(cmd_args[0], "timer") == 0) {
    // Handle built-in commands that might affect shell state
    printf("timer: can't time built-in command: %s\n", cmd_args[0]);
    result = 1;
  } else {
    // Execute the command normally
    result = lsh_execute(cmd_args);
  }

  // End timing
  clock_t end_time = clock();

  // Calculate execution time in milliseconds
  double execution_time_ms =
      ((double)(end_time - start_time) / CLOCKS_PER_SEC) * 1000;

  // Format the execution time
  char time_str[64];
  format_time(execution_time_ms, time_str, sizeof(time_str));

  // Print the execution time with color
  printf("\n");
  SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
  printf("╭───────────────────────────────╮\n");
  printf("│ Execution time: %-14s │\n", time_str);
  printf("╰───────────────────────────────╯\n");
  SetConsoleTextAttribute(hConsole, originalAttributes);

  return result;
}

/**
 * Implementation of the time command (alias for timer)
 * Usage: time COMMAND [ARGS...]
 *
 * @param args Command arguments
 * @return 1 to continue, 0 to exit
 */
int lsh_time(char **args) { return lsh_timer(args); }
