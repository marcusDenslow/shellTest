/**
 * countdown_timer.c
 * Implementation of a focus timer that integrates with the shell status bar
 */

#include "countdown_timer.h"
#include "common.h"
#include "shell.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>

// Define FOREGROUND_CYAN (not defined in Windows API)
#define FOREGROUND_CYAN (FOREGROUND_BLUE | FOREGROUND_GREEN)

// Global timer state
static struct {
  BOOL is_active;         // Whether timer is currently running
  time_t end_time;        // When timer will expire (in seconds since epoch)
  char display_text[64];  // Current timer display text
  char session_name[128]; // Optional session name
  HANDLE timer_thread;    // Handle to the timer thread
  DWORD thread_id;        // ID of the timer thread
  BOOL should_exit;       // Flag to signal thread exit
} timer_state = {0};

// Forward declarations for internal functions
static DWORD WINAPI timer_thread_func(LPVOID lpParam);
static void update_timer_display_text();
static void update_status_bar_minimal(HANDLE hConsole);
static void show_timer_notification();
static int parse_time_string(char **args, int *seconds);

/**
 * Start a new countdown timer
 *
 * @param seconds Total duration in seconds
 * @param name Optional name for the timer session
 * @return 1 if successful, 0 on failure
 */
int start_countdown_timer(int seconds, const char *name) {
  // Stop any existing timer
  if (timer_state.is_active) {
    stop_countdown_timer();
  }

  // Set up the new timer
  timer_state.is_active = TRUE;
  timer_state.end_time = time(NULL) + seconds;

  // Set the session name if provided
  if (name && *name) {
    strncpy(timer_state.session_name, name,
            sizeof(timer_state.session_name) - 1);
    timer_state.session_name[sizeof(timer_state.session_name) - 1] = '\0';
  } else {
    strcpy(timer_state.session_name, "Focus Session");
  }

  // Initialize thread exit flag
  timer_state.should_exit = FALSE;

  // Create a thread to update the timer display
  timer_state.timer_thread =
      CreateThread(NULL,                  // Default security attributes
                   0,                     // Default stack size
                   timer_thread_func,     // Thread function
                   NULL,                  // No thread function arguments
                   0,                     // Default creation flags
                   &timer_state.thread_id // Receives thread identifier
      );

  if (timer_state.timer_thread == NULL) {
    fprintf(stderr, "Failed to create timer thread\n");
    timer_state.is_active = FALSE;
    return 0;
  }

  // Initial display update
  update_timer_display_text();

  return 1;
}

/**
 * Stop the current countdown timer
 */
void stop_countdown_timer() {
  if (!timer_state.is_active) {
    return;
  }

  // Signal thread to exit
  timer_state.should_exit = TRUE;

  // Wait for thread to exit (with timeout)
  if (timer_state.timer_thread != NULL) {
    WaitForSingleObject(timer_state.timer_thread, 1000);
    CloseHandle(timer_state.timer_thread);
    timer_state.timer_thread = NULL;
  }

  // Clear timer state
  timer_state.is_active = FALSE;
  memset(timer_state.display_text, 0, sizeof(timer_state.display_text));
}

/**
 * Check if a timer is currently active
 *
 * @return TRUE if timer is active, FALSE otherwise
 */
BOOL is_timer_active() { return timer_state.is_active; }

/**
 * Get the current timer display text
 *
 * @return Pointer to the display text string
 */
const char *get_timer_display() {
  return timer_state.is_active ? timer_state.display_text : "";
}

/**
 * Timer thread function - updates display and checks for completion
 */
static DWORD WINAPI timer_thread_func(LPVOID lpParam) {
  // Console handle for status updates
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

  // Time tracking for smoother updates
  DWORD last_update_time = GetTickCount();

  while (!timer_state.should_exit) {
    // Check if timer has expired
    time_t now = time(NULL);
    if (now >= timer_state.end_time) {
      // Timer completed
      show_timer_notification();

      // Clear timer state
      timer_state.is_active = FALSE;
      timer_state.should_exit = TRUE;

      // Clear timer display from status bar
      timer_state.display_text[0] = '\0';
      update_status_bar(hConsole, "");
      break;
    }

    // Update timer display only when the display would change
    // (when seconds change) or every 1000ms at minimum
    DWORD current_time = GetTickCount();
    if (current_time - last_update_time >= 500) {
      // Update with minimal cursor disturbance
      CONSOLE_SCREEN_BUFFER_INFO csbi;
      CONSOLE_CURSOR_INFO cursorInfo;

      // Save cursor information if possible
      BOOL cursorInfoSaved = GetConsoleCursorInfo(hConsole, &cursorInfo);
      BOOL cursorPosSaved = GetConsoleScreenBufferInfo(hConsole, &csbi);

      // Hide cursor during update if we saved the info
      if (cursorInfoSaved && cursorInfo.bVisible) {
        CONSOLE_CURSOR_INFO hiddenCursor = {1, FALSE};
        SetConsoleCursorInfo(hConsole, &hiddenCursor);
      }

      // Update timer display text
      update_timer_display_text();

      // Update status bar directly (with minimal updates)
      update_status_bar_minimal(hConsole);

      // Restore cursor position if we saved it
      if (cursorPosSaved) {
        SetConsoleCursorPosition(hConsole, csbi.dwCursorPosition);
      }

      // Restore cursor visibility
      if (cursorInfoSaved && cursorInfo.bVisible) {
        SetConsoleCursorInfo(hConsole, &cursorInfo);
      }

      // Update time tracking
      last_update_time = current_time;
    }

    // Sleep briefly to avoid excessive CPU usage
    Sleep(100);
  }

  return 0;
}

/**
 * Update just the timer display text without updating the status bar
 */
static void update_timer_display_text() {
  if (!timer_state.is_active) {
    return;
  }

  // Calculate remaining time
  time_t now = time(NULL);
  time_t remaining =
      timer_state.end_time > now ? timer_state.end_time - now : 0;

  // Format the remaining time
  int hours = remaining / 3600;
  int minutes = (remaining % 3600) / 60;
  int seconds = remaining % 60;

  // Format the display string based on remaining time
  if (hours > 0) {
    sprintf(timer_state.display_text, "⏱️ %dh %dm %ds", hours, minutes, seconds);
  } else if (minutes > 0) {
    sprintf(timer_state.display_text, "⏱️ %dm %ds", minutes, seconds);
  } else {
    sprintf(timer_state.display_text, "⏱️ %ds", seconds);
  }

  // Add session name for longer display
  if (strlen(timer_state.session_name) > 0 && (hours > 0 || minutes > 5)) {
    char temp[64];
    strcpy(temp, timer_state.display_text);
    sprintf(timer_state.display_text, "%s - %s", temp,
            timer_state.session_name);
  }
}

/**
 * Update the status bar with minimal cursor disturbance
 * This is a simpler version of the main update_status_bar function
 * that only updates the timer portion
 */
static void update_status_bar_minimal(HANDLE hConsole) {
  if (!timer_state.is_active) {
    return;
  }

  // Get current console information
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return; // Can't proceed without console info
  }

  // Get console width
  int console_width = csbi.dwSize.X;

  // Only update the timer portion on the right side
  int timer_info_len = strlen(timer_state.display_text);
  COORD timerPos = {console_width - timer_info_len - 2, csbi.srWindow.Bottom};

  // Set text color for timer info (cyan)
  WORD timerInfoColor = FOREGROUND_CYAN | FOREGROUND_INTENSITY;

  // Write the timer text
  DWORD charsWritten;
  WriteConsoleOutputCharacter(hConsole, timer_state.display_text,
                              timer_info_len, timerPos, &charsWritten);

  // Set attributes for the timer text
  FillConsoleOutputAttribute(hConsole, timerInfoColor, timer_info_len, timerPos,
                             &charsWritten);
}

/**
 * Show notification when timer completes
 */
static void show_timer_notification() {
  // Get handle to console
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  WORD originalAttributes;

  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    originalAttributes = csbi.wAttributes;
  } else {
    originalAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
  }

  // Make a sound
  Beep(750, 300);
  Sleep(150);
  Beep(750, 300);

  // Save cursor position
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  COORD savedPosition = csbi.dwCursorPosition;

  // Create a notification box
  SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN |
                                        FOREGROUND_INTENSITY);

  // Position the notification at the center of the console
  int boxWidth = 50;
  int left = (csbi.dwSize.X - boxWidth) / 2;
  int top = csbi.dwCursorPosition.Y - 5;
  if (top < 0)
    top = 0;

  COORD boxPos = {left, top};
  SetConsoleCursorPosition(hConsole, boxPos);

  // Draw notification box
  printf("╔══════════════════════════════════════════════╗\n");
  SetConsoleCursorPosition(hConsole, (COORD){left, top + 1});
  printf("║                TIMER FINISHED                ║\n");
  SetConsoleCursorPosition(hConsole, (COORD){left, top + 2});
  printf("║                                              ║\n");
  SetConsoleCursorPosition(hConsole, (COORD){left, top + 3});
  printf("║  %-44s  ║\n", timer_state.session_name);
  SetConsoleCursorPosition(hConsole, (COORD){left, top + 4});
  printf("║  Time's up! Take a break or start a new timer ║\n");
  SetConsoleCursorPosition(hConsole, (COORD){left, top + 5});
  printf("╚══════════════════════════════════════════════╝\n");

  // Reset attributes
  SetConsoleTextAttribute(hConsole, originalAttributes);

  // Restore cursor position
  SetConsoleCursorPosition(hConsole, savedPosition);
}

/**
 * Parse time units from command arguments
 * Supports formats like: 30m, 1h30m, 45s, 1 hour 30 mins, etc.
 *
 * @param args Command arguments array
 * @param seconds Pointer to store total seconds
 * @return 1 if successful, 0 on parsing error
 */
static int parse_time_string(char **args, int *seconds) {
  *seconds = 0;
  int arg_index = 1;
  int value = 0;
  int any_time_parsed = 0;

  while (args[arg_index] != NULL) {
    // Check if this argument is a number
    if (isdigit(args[arg_index][0])) {
      // Parse the number value
      value = atoi(args[arg_index]);
      arg_index++;

      // Check if next arg is a time unit
      if (args[arg_index] == NULL) {
        // Assume seconds if no unit specified
        *seconds += value;
        any_time_parsed = 1;
        break;
      }

      // Check time unit
      char *unit = args[arg_index];

      // Handle various time unit formats
      if (strcasecmp(unit, "s") == 0 || strcasecmp(unit, "sec") == 0 ||
          strcasecmp(unit, "secs") == 0 || strcasecmp(unit, "second") == 0 ||
          strcasecmp(unit, "seconds") == 0) {
        *seconds += value;
        any_time_parsed = 1;
      } else if (strcasecmp(unit, "m") == 0 || strcasecmp(unit, "min") == 0 ||
                 strcasecmp(unit, "mins") == 0 ||
                 strcasecmp(unit, "minute") == 0 ||
                 strcasecmp(unit, "minutes") == 0) {
        *seconds += value * 60;
        any_time_parsed = 1;
      } else if (strcasecmp(unit, "h") == 0 || strcasecmp(unit, "hr") == 0 ||
                 strcasecmp(unit, "hrs") == 0 ||
                 strcasecmp(unit, "hour") == 0 ||
                 strcasecmp(unit, "hours") == 0) {
        *seconds += value * 3600;
        any_time_parsed = 1;
      } else {
        // Invalid time unit - check if it might be the timer name
        arg_index--;
        break;
      }

      arg_index++;
    } else if (strchr(args[arg_index], 'h') || strchr(args[arg_index], 'm') ||
               strchr(args[arg_index], 's')) {
      // Handle compact format like "1h30m45s"
      char *p = args[arg_index];
      char *endptr;

      while (*p) {
        // Parse number
        value = strtol(p, &endptr, 10);
        if (p == endptr) {
          // No number found, move to next character
          p++;
          continue;
        }

        // Check unit
        if (*endptr == 'h') {
          *seconds += value * 3600;
          any_time_parsed = 1;
        } else if (*endptr == 'm') {
          *seconds += value * 60;
          any_time_parsed = 1;
        } else if (*endptr == 's') {
          *seconds += value;
          any_time_parsed = 1;
        }

        // Move past the unit
        p = endptr + 1;
      }

      arg_index++;
    } else {
      // Not a time specification, assume it's the session name
      break;
    }
  }

  // Return value indicates whether we successfully parsed any time units
  return any_time_parsed;
}

/**
 * Command handler for the "timer" command
 * Usage: timer [duration] [session name]
 * Examples:
 *   timer 30m
 *   timer 1h30m "Bug fixing session"
 *   timer 45s quick break
 *   timer stop
 *
 * @param args Command arguments
 * @return 1 to continue shell, 0 to exit
 */
int lsh_focus_timer(char **args) {
  // Check for arguments
  if (args[1] == NULL) {
    // No arguments - show current timer or usage info
    if (timer_state.is_active) {
      time_t now = time(NULL);
      time_t remaining =
          timer_state.end_time > now ? timer_state.end_time - now : 0;

      printf("Timer active: %s\n", timer_state.display_text);
      printf("Session: %s\n", timer_state.session_name);

      int hours = remaining / 3600;
      int minutes = (remaining % 3600) / 60;
      int seconds = remaining % 60;

      printf("Remaining time: %dh %dm %ds\n", hours, minutes, seconds);
    } else {
      printf("Usage: timer [duration] [session name]\n");
      printf("Examples:\n");
      printf("  timer 30m\n");
      printf("  timer 1h30m \"Bug fixing session\"\n");
      printf("  timer 45s quick break\n");
      printf("  timer stop     (stops any running timer)\n");
    }
    return 1;
  }

  // Check for "stop" command
  if (strcasecmp(args[1], "stop") == 0) {
    if (timer_state.is_active) {
      printf("Timer stopped\n");
      stop_countdown_timer();
    } else {
      printf("No timer is currently running\n");
    }
    return 1;
  }

  // Parse time duration
  int seconds = 0;
  if (!parse_time_string(args, &seconds)) {
    printf("Invalid time format. Examples: 30m, 1h30m, 45s\n");
    return 1;
  }

  // Find where the session name starts (if any)
  int name_index = 1;
  while (args[name_index] != NULL) {
    char *p = args[name_index];
    // Skip arguments that look like time units
    if (isdigit(p[0]) || strchr(p, 'h') || strchr(p, 'm') || strchr(p, 's')) {
      name_index++;
      // Skip the unit word if present
      if (args[name_index] != NULL &&
          (strcasecmp(args[name_index], "minutes") == 0 ||
           strcasecmp(args[name_index], "minute") == 0 ||
           strcasecmp(args[name_index], "mins") == 0 ||
           strcasecmp(args[name_index], "min") == 0 ||
           strcasecmp(args[name_index], "seconds") == 0 ||
           strcasecmp(args[name_index], "second") == 0 ||
           strcasecmp(args[name_index], "secs") == 0 ||
           strcasecmp(args[name_index], "sec") == 0 ||
           strcasecmp(args[name_index], "hours") == 0 ||
           strcasecmp(args[name_index], "hour") == 0 ||
           strcasecmp(args[name_index], "hrs") == 0 ||
           strcasecmp(args[name_index], "hr") == 0)) {
        name_index++;
      }
    } else {
      break;
    }
  }

  // Extract session name if present
  char session_name[128] = "";
  if (args[name_index] != NULL) {
    // Check if the name is in quotes
    if (args[name_index][0] == '"' || args[name_index][0] == '\'') {
      // Remove quotes and concat all remaining args
      char *p = args[name_index];
      int in_quotes = 1;
      while (*p) {
        if (*p == args[name_index][0]) {
          in_quotes = !in_quotes;
          p++;
          continue;
        }
        if (in_quotes && strlen(session_name) < sizeof(session_name) - 1) {
          strncat(session_name, p, 1);
        }
        p++;
      }
    } else {
      // Concatenate all remaining args as session name
      while (args[name_index] != NULL) {
        if (strlen(session_name) > 0) {
          strcat(session_name, " ");
        }
        strcat(session_name, args[name_index]);
        name_index++;
      }
    }
  }

  // Start the timer
  if (start_countdown_timer(seconds, session_name)) {
    int hours = seconds / 3600;
    int minutes = (seconds % 3600) / 60;
    int secs = seconds % 60;

    if (hours > 0) {
      printf("Timer started for %dh %dm %ds", hours, minutes, secs);
    } else if (minutes > 0) {
      printf("Timer started for %dm %ds", minutes, secs);
    } else {
      printf("Timer started for %ds", secs);
    }

    if (strlen(session_name) > 0) {
      printf(" - %s", session_name);
    }

    printf("\nTimer will be displayed in the status bar\n");
  } else {
    printf("Failed to start timer\n");
  }

  return 1;
}
