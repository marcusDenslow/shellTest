/**
 * shell.c
 * Implementation of core shell functionality
 */

#include "shell.h"
#include "aliases.h" // Added for alias support
#include "autocorrect.h"
#include "bookmarks.h" // Added for bookmark support
#include "builtins.h"
#include "countdown_timer.h"
#include "filters.h"
#include "git_integration.h" // Added for Git repository detection
#include "line_reader.h"
#include "structured_data.h"
#include "tab_complete.h" // Added for tab completion support
#include <stdio.h>
#include <time.h> // Added for time functions
//

// Global variables for status bar
static int g_console_width = 80;
static int g_status_line = 0;
static WORD g_normal_attributes = 0;
static WORD g_status_attributes = 0;
static BOOL g_status_bar_enabled =
    FALSE; // Flag to track if status bar is enabled

/**
 * Temporarily hide the status bar before command execution
 */
void hide_status_bar(HANDLE hConsole) {
  // Skip if status bar is not enabled
  if (!g_status_bar_enabled)
    return;

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return;
  }

  // Save current cursor position
  COORD cursorPos = csbi.dwCursorPosition;

  // Clear the status bar line
  COORD statusPos = {0, csbi.srWindow.Bottom};
  DWORD written;
  FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X, statusPos, &written);
  FillConsoleOutputAttribute(hConsole, g_normal_attributes, csbi.dwSize.X,
                             statusPos, &written);

  // Restore cursor position
  SetConsoleCursorPosition(hConsole, cursorPos);
}

/**
 * This function scrolls the console buffer up one line to make room for the
 * status bar when we're at the bottom of the screen.
 */
void ensure_status_bar_space(HANDLE hConsole) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return;
  }

  // If cursor is on the bottom line (or second-to-last line), we need to scroll
  if (csbi.dwCursorPosition.Y >= csbi.srWindow.Bottom - 1) {
    // First clear the status bar if it exists
    COORD statusPos = {0, csbi.srWindow.Bottom};
    DWORD written;
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X, statusPos,
                               &written);
    FillConsoleOutputAttribute(hConsole, g_normal_attributes, csbi.dwSize.X,
                               statusPos, &written);

    // Create a small scroll rectangle - everything except the status bar line
    SMALL_RECT scrollRect;
    scrollRect.Left = 0;
    scrollRect.Top = csbi.srWindow.Top;
    scrollRect.Right = csbi.dwSize.X - 1;
    scrollRect.Bottom = csbi.srWindow.Bottom - 1; // Exclude status bar line

    // The coordinate to move the rectangle to
    COORD destOrigin;
    destOrigin.X = 0;
    destOrigin.Y = csbi.srWindow.Top - 1; // Move up one line

    // Fill character for the vacated lines
    CHAR_INFO fill;
    fill.Char.AsciiChar = ' ';
    fill.Attributes = g_normal_attributes;

    // Scroll the window contents up
    ScrollConsoleScreenBuffer(hConsole, &scrollRect, NULL, destOrigin, &fill);

    // Update cursor position
    COORD newCursorPos;
    newCursorPos.X = csbi.dwCursorPosition.X;
    newCursorPos.Y = csbi.dwCursorPosition.Y - 1;
    SetConsoleCursorPosition(hConsole, newCursorPos);
  }
}

/**
 * Check for console window resize and update status bar position
 */
void check_console_resize(HANDLE hConsole) {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    COORD oldStatusLine = {0, g_status_line};
    int oldWidth = g_console_width;

    // Update dimensions
    g_console_width = csbi.dwSize.X;
    g_status_line = csbi.srWindow.Bottom;

    // If dimensions changed, we need to redraw status bar
    if (oldStatusLine.Y != g_status_line || oldWidth != g_console_width) {
      // Clear old status bar position if it's still visible
      if (oldStatusLine.Y <= csbi.srWindow.Bottom &&
          oldStatusLine.Y >= csbi.srWindow.Top) {
        COORD cursorPos = csbi.dwCursorPosition;

        // Clear the old status line
        SetConsoleCursorPosition(hConsole, oldStatusLine);
        DWORD written;
        FillConsoleOutputCharacter(hConsole, ' ', oldWidth, oldStatusLine,
                                   &written);
        FillConsoleOutputAttribute(hConsole, g_normal_attributes, oldWidth,
                                   oldStatusLine, &written);

        // Restore cursor
        SetConsoleCursorPosition(hConsole, cursorPos);
      }
    }
  }
}

/**
 * Updates to shell.c to integrate the focus timer
 * with the status bar
 */

// Modify the update_status_bar function to include timer information
void update_status_bar(HANDLE hConsole, const char *git_info) {
  // Skip if status bar is not enabled yet
  if (!g_status_bar_enabled)
    return;

  // Check for resize
  check_console_resize(hConsole);

  // Get current console information
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return; // Can't proceed without console info
  }

  // Save current cursor position
  COORD cursorPos = csbi.dwCursorPosition;

  // Update console dimensions in case of resize
  g_console_width = csbi.dwSize.X;

  // Always use the bottom line of the current console window
  g_status_line = csbi.srWindow.Bottom;

  // Hide cursor during the update
  CONSOLE_CURSOR_INFO cursorInfo;
  GetConsoleCursorInfo(hConsole, &cursorInfo);
  BOOL originalCursorVisible = cursorInfo.bVisible;
  cursorInfo.bVisible = FALSE;
  SetConsoleCursorInfo(hConsole, &cursorInfo);

  // Position cursor at beginning of status line (bottom line)
  COORD statusPos = {0, g_status_line};

  // Clear the status bar in one operation
  DWORD charsWritten;
  FillConsoleOutputCharacter(hConsole, ' ', g_console_width, statusPos,
                             &charsWritten);
  FillConsoleOutputAttribute(hConsole, g_status_attributes, g_console_width,
                             statusPos, &charsWritten);

  // Check if we have a timer running
  const char *timer_info = get_timer_display();
  BOOL has_timer = is_timer_active() && timer_info && timer_info[0];

  // If we have Git info to display
  if (git_info && git_info[0]) {
    // Prepare clean git info (strip ANSI color codes if any)
    char clean_git_info[256] = "";
    int c_index = 0;
    int in_ansi = 0;

    for (const char *p = git_info; *p; p++) {
      if (*p == '\033') {
        in_ansi = 1;
        continue;
      }

      if (in_ansi) {
        if (*p == 'm') {
          in_ansi = 0;
        }
        continue;
      }

      // Not in ANSI sequence, copy character
      if (c_index < sizeof(clean_git_info) - 1) {
        clean_git_info[c_index++] = *p;
      }
    }
    clean_git_info[c_index] = '\0';

    // Set text color for Git info (purple)
    WORD gitInfoColor = g_status_attributes | FOREGROUND_RED | FOREGROUND_BLUE |
                        FOREGROUND_INTENSITY;

    // Calculate positions for both Git info and timer (if needed)
    int git_info_len = strlen(clean_git_info);

    if (has_timer) {
      // Display Git info on the left
      WriteConsoleOutputCharacter(hConsole, clean_git_info, git_info_len,
                                  statusPos, &charsWritten);

      // Set the attributes for the written characters
      COORD attrPos = statusPos;
      FillConsoleOutputAttribute(hConsole, gitInfoColor, git_info_len, attrPos,
                                 &charsWritten);

      // Display timer info on the right
      int timer_info_len = strlen(timer_info);
      COORD timerPos = {g_console_width - timer_info_len - 2, g_status_line};

      // Set text color for timer info (cyan)
      WORD timerInfoColor =
          g_status_attributes | FOREGROUND_RED | FOREGROUND_INTENSITY;

      WriteConsoleOutputCharacter(hConsole, timer_info, timer_info_len,
                                  timerPos, &charsWritten);

      FillConsoleOutputAttribute(hConsole, timerInfoColor, timer_info_len,
                                 timerPos, &charsWritten);
    } else {
      // No timer, just display Git info
      WriteConsoleOutputCharacter(hConsole, clean_git_info,
                                  strlen(clean_git_info), statusPos,
                                  &charsWritten);

      // Set the attributes for the written characters
      DWORD length = strlen(clean_git_info);
      COORD attrPos = statusPos;
      FillConsoleOutputAttribute(hConsole, gitInfoColor, length, attrPos,
                                 &charsWritten);
    }
  } else {
    if (has_timer) {
      // No Git info but we have a timer
      // Display timer on the right
      int timer_info_len = strlen(timer_info);
      COORD timerPos = {g_console_width - timer_info_len - 2, g_status_line};

      // Set text color for timer info (cyan)
      WORD timerInfoColor =
          g_status_attributes | FOREGROUND_RED | FOREGROUND_INTENSITY;

      WriteConsoleOutputCharacter(hConsole, timer_info, timer_info_len,
                                  timerPos, &charsWritten);

      FillConsoleOutputAttribute(hConsole, timerInfoColor, timer_info_len,
                                 timerPos, &charsWritten);

      // Also show default message on left
      const char *defaultMsg = " Shell Status";
      WriteConsoleOutputCharacter(hConsole, defaultMsg, strlen(defaultMsg),
                                  statusPos, &charsWritten);
    } else {
      // Default message when no Git info or timer is available
      const char *defaultMsg = " Shell Status";
      WriteConsoleOutputCharacter(hConsole, defaultMsg, strlen(defaultMsg),
                                  statusPos, &charsWritten);
    }
  }

  // Restore original cursor position
  SetConsoleCursorPosition(hConsole, cursorPos);

  // Restore cursor visibility
  cursorInfo.bVisible = originalCursorVisible;
  SetConsoleCursorInfo(hConsole, &cursorInfo);
}

/**
 * Initialize the status bar at the bottom of the screen
 */
int init_status_bar(HANDLE hConsole) {
  // Get console information
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return 0;
  }

  // Save normal attributes for later
  g_normal_attributes = csbi.wAttributes;

  // Set status bar attributes (true black background)
  g_status_attributes = 0; // Pure black background (no intensity)

  // Get console dimensions
  g_console_width = csbi.dwSize.X;
  g_status_line = csbi.srWindow.Bottom;

  // Save current cursor position
  COORD cursorPos = csbi.dwCursorPosition;

  // Draw initial empty status bar
  COORD statusPos = {0, g_status_line};
  SetConsoleCursorPosition(hConsole, statusPos);

  // Set status bar color
  SetConsoleTextAttribute(hConsole, g_status_attributes);

  // Fill the entire line with spaces for the status bar background
  for (int i = 0; i < g_console_width; i++) {
    putchar(' ');
  }

  // Reset text attributes
  SetConsoleTextAttribute(hConsole, g_normal_attributes);

  // Restore cursor position
  SetConsoleCursorPosition(hConsole, cursorPos);

  // Mark status bar as enabled
  g_status_bar_enabled = TRUE;

  return 1;
}

/**
 * Add padding above the status bar to avoid a cramped UI
 * This ensures there's at least one blank line between output and prompt
 */
void add_padding_before_prompt(HANDLE hConsole) {
  // Get current console information
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return;
  }

  // Check if we need padding (if cursor is directly above status bar)
  if (csbi.dwCursorPosition.Y == csbi.srWindow.Bottom - 1) {
    // Add a newline to create space
    printf("\n");

    // After adding newline, ensure status bar space again
    // This will make sure we don't push the status bar off screen
    ensure_status_bar_space(hConsole);

    // Update status bar position
    check_console_resize(hConsole);
  }
}

/**
 * Display a welcome banner with BBQ sauce invention time
 */
void display_welcome_banner(void) {
  // Calculate time since BBQ sauce invention (January 1, 1650)
  // Current time
  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);

  // January 1, 1650
  int bbq_year = 1650;
  int current_year = now_tm->tm_year + 1900;

  // Calculate difference in years
  int years = current_year - bbq_year;

  // Calculate remaining months, days, etc.
  int months = now_tm->tm_mon;    // 0-11 for Jan-Dec
  int days = now_tm->tm_mday - 1; // Assuming invention was on the 1st
  int hours = now_tm->tm_hour;
  int minutes = now_tm->tm_min;
  int seconds = now_tm->tm_sec;

  // Format the time string
  char time_str[256];
  snprintf(time_str, sizeof(time_str),
           "It's been %d years, %d months, %d days, %d hours, %d minutes, %d "
           "seconds since BBQ sauce was invented",
           years, months, days, hours, minutes, seconds);

  // Get console width to center the box
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  int consoleWidth = 80; // Default width if we can't get actual console info

  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  }

  // Calculate box width based on content
  const char *title = "Welcome to shell!";

  // Calculate the minimum box width needed - ensure it fits the time string
  int min_width = strlen(time_str) + 4; // Add padding
  int box_width = min_width;

  // Calculate left padding to center the box
  int left_padding =
      (consoleWidth - box_width - 2) / 2; // -2 for the border chars
  if (left_padding < 0)
    left_padding = 0;

  // Define colors
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD boxColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
  WORD textColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                   FOREGROUND_INTENSITY;
  WORD originalAttrs;

  // Get original console attributes
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
  originalAttrs = consoleInfo.wAttributes;

  // Clear any previous content (important to remove the game trivia box)
  system("cls");

  // Set color for box drawing
  SetConsoleTextAttribute(hConsole, boxColor);

  // Top border
  printf("%*s\u250C", left_padding, "");
  for (int i = 0; i < box_width; i++)
    printf("\u2500");
  printf("\u2510\n");

  // Title row
  printf("%*s\u2502", left_padding, "");
  int title_padding = (box_width - strlen(title)) / 2;
  SetConsoleTextAttribute(hConsole, textColor);
  printf("%*s%s%*s", title_padding, "", title,
         box_width - title_padding - strlen(title), "");
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("\u2502\n");

  // Separator
  printf("%*s\u251C", left_padding, "");
  for (int i = 0; i < box_width; i++)
    printf("\u2500");
  printf("\u2524\n");

  // Time since invention row
  printf("%*s\u2502", left_padding, "");
  SetConsoleTextAttribute(hConsole, textColor);
  printf(" %-*s ", box_width - 2, time_str);
  SetConsoleTextAttribute(hConsole, boxColor);
  printf("\u2502\n");

  // Bottom border
  printf("%*s\u2514", left_padding, "");
  for (int i = 0; i < box_width; i++)
    printf("\u2500");
  printf("\u2518\n\n");

  // Reset console attributes
  SetConsoleTextAttribute(hConsole, originalAttrs);
}

/**
 * Launch an external program with auto-correction support
 */
int lsh_launch(char **args) {
  // Construct command line string for CreateProcess
  char command[1024] = "";
  for (int i = 0; args[i] != NULL; i++) {
    strcat(command, args[i]);
    strcat(command, " ");
  }

  // Hide the timer before launching external program
  hide_timer_display();

  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));

  // Create a new process
  if (!CreateProcess(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si,
                     &pi)) {

    DWORD error = GetLastError();

    // Check if this is a "command not found" type error
    if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
      // Try auto-correction
      if (attempt_command_correction(args)) {
        // Command was corrected and executed successfully
        show_timer_display();
        return 1;
      }
    }

    fprintf(stderr, "lsh: failed to execute %s\n", args[0]);
    // Restore timer even if process creation failed
    show_timer_display();
    return 1;
  }

  // Wait for the process to finish
  WaitForSingleObject(pi.hProcess, INFINITE);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  // Restore timer display after external program finishes
  show_timer_display();

  return 1;
}

/**
 * Execute a command (updated with auto-correction support)
 */
int lsh_execute(char **args) {
  int i;

  if (args[0] == NULL) {
    return 1;
  }

  // Check if the command is an alias
  AliasEntry *alias = find_alias(args[0]);
  if (alias) {
    // Create expanded command with the alias
    char expanded_cmd[1024] = "";
    strcpy(expanded_cmd, alias->command);

    // Add any arguments
    for (i = 1; args[i] != NULL; i++) {
      strcat(expanded_cmd, " ");
      strcat(expanded_cmd, args[i]);
    }

    // Parse the expanded command
    char *expanded_copy = _strdup(expanded_cmd);
    char **expanded_args = lsh_split_line(expanded_copy);

    // Execute the expanded command
    int status = lsh_execute(expanded_args);

    // Clean up
    free(expanded_copy);
    free(expanded_args);

    return status;
  }

  // Check for builtin commands
  for (i = 0; i < lsh_num_builtins(); i++) {
    if (strcmp(args[0], builtin_str[i]) == 0) {
      return (*builtin_func[i])(args);
    }
  }

  // If not a builtin, launch external program
  return lsh_launch(args);
}

/**
 * Execute a pipeline of commands
 */
int lsh_execute_piped(char ***commands) {
  int i;
  TableData *result = NULL;

  // Execute each command in the pipeline
  for (i = 0; commands[i] != NULL; i++) {
    char **args = commands[i];

    if (args[0] == NULL) {
      continue;
    }

    // First command in pipeline
    if (i == 0) {
      // Check for commands that can produce structured data
      if (strcmp(args[0], "ls") == 0 || strcmp(args[0], "dir") == 0) {
        result = lsh_dir_structured(args);
        if (!result) {
          fprintf(stderr, "lsh: error generating structured output for '%s'\n",
                  args[0]);
          return 1;
        }
      } else if (strcmp(args[0], "ps") == 0) {
        // Add support for ps command to produce structured data
        result = lsh_ps_structured(args);
        if (!result) {
          fprintf(stderr, "lsh: error generating structured output for '%s'\n",
                  args[0]);
          return 1;
        }
      } else {
        fprintf(stderr, "lsh: command '%s' does not support piping\n", args[0]);
        return 1;
      }
    } else {
      // Handle piped commands (filters)
      if (result == NULL) {
        fprintf(stderr, "lsh: no data to pipe\n");
        return 1;
      }

      // Search for matching filter
      int found = 0;
      for (int j = 0; j < filter_count; j++) {
        if (strcmp(args[0], filter_str[j]) == 0) {
          // Run the filter
          TableData *filtered = (*filter_func[j])(result, args + 1);

          // Clean up previous result
          free_table(result);

          // Use the filtered result for next stage or output
          result = filtered;
          found = 1;
          break;
        }
      }

      if (!found) {
        fprintf(stderr, "lsh: filter '%s' not supported\n", args[0]);
        free_table(result);
        result = NULL;
        return 1;
      }
    }
  }

  // Print the final result
  if (result != NULL) {
    print_table(result);
    free_table(result);
  }

  return 1;
}

/**
 * Free memory for a command array from lsh_split_commands
 */
void free_commands(char ***commands) {
  if (!commands)
    return;

  for (int i = 0; commands[i] != NULL; i++) {
    // Note: We don't free the token strings since they're
    // just pointers into the original command string
    free(commands[i]);
  }
  free(commands);
}

/**
 * Get the name of the parent and current directories from a path
 */
void get_path_display(const char *cwd, char *parent_dir_name,
                      char *current_dir_name, size_t buf_size) {
  // Initialize output buffers
  parent_dir_name[0] = '\0';
  current_dir_name[0] = '\0';

  // Find the last directory separator
  char *last_sep = strrchr(cwd, '\\');

  if (last_sep != NULL) {
    // Get current directory name
    strncpy(current_dir_name, last_sep + 1, buf_size - 1);
    current_dir_name[buf_size - 1] = '\0'; // Ensure null termination

    // Save the position and temporarily cut the string
    char temp = *last_sep;
    *last_sep = '\0';

    // Find the parent directory
    char *parent_sep = strrchr(cwd, '\\');

    if (parent_sep != NULL) {
      // Parent directory exists, get its name
      strncpy(parent_dir_name, parent_sep + 1, buf_size - 1);
      parent_dir_name[buf_size - 1] = '\0'; // Ensure null termination
    } else {
      // The parent is the root, use the entire path up to last_sep
      strncpy(parent_dir_name, cwd, buf_size - 1);
      parent_dir_name[buf_size - 1] = '\0'; // Ensure null termination
    }

    // Restore the path
    *last_sep = temp;
  } else {
    // No backslash found, use the entire path as current directory
    strncpy(current_dir_name, cwd, buf_size - 1);
    current_dir_name[buf_size - 1] = '\0'; // Ensure null termination
  }
}

/**
 * Main shell loop (updated to fix Git info updates)
 */
void lsh_loop(void) {
  char *line;
  char ***commands;
  int status;
  char cwd[1024];
  char prompt_path[1024];
  char git_info[128] = "";
  char username[256] = "Elden Lord";

  // Static variables to cache Git information
  static char last_directory[1024] = "";
  static char cached_git_info[128] = "";
  static int cached_in_git_repo = 0;

  // ANSI color codes for styling
  const char *CYAN = "\033[36m";
  const char *GREEN = "\033[32m";
  const char *YELLOW = "\033[33m";
  const char *BLUE = "\033[34m";
  const char *PURPLE = "\033[35m";
  const char *BRIGHT_PURPLE = "\033[95m";
  const char *RESET = "\033[0m";

  // Get handle to console
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

  // Initialize aliases
  init_aliases();

  // Initialize bookmarks
  init_bookmarks();

  // Display the welcome banner at startup
  display_welcome_banner();

  // Initialize the status bar
  init_status_bar(hConsole);

  do {
    // Clear git_info for this iteration
    git_info[0] = '\0';

    // Always update the status bar with the current window dimensions
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      g_status_line = csbi.srWindow.Bottom;
      g_console_width = csbi.dwSize.X;

      // Check if we need to scroll to make room for the status bar
      ensure_status_bar_space(hConsole);
    }

    // Get current directory for the prompt
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
      perror("lsh");
      strcpy(prompt_path, "unknown_path"); // Fallback in case of error
      // Reset cached directory since we can't determine current directory
      last_directory[0] = '\0';
    } else {
      // Check if directory has changed since last prompt
      int directory_changed = (strcmp(cwd, last_directory) != 0);

      // Get parent and current directory names
      char parent_dir[256] = "";
      char current_dir[256] = "";
      get_path_display(cwd, parent_dir, current_dir, sizeof(parent_dir));

      // Format prompt with username, parent and current directory
      if (parent_dir[0] != '\0') {
        snprintf(prompt_path, sizeof(prompt_path), "%s%s%s in %s%s\\%s%s", CYAN,
                 username, RESET, BLUE, parent_dir, current_dir, RESET);
      } else {
        // If we don't have a parent directory, just show the current
        snprintf(prompt_path, sizeof(prompt_path), "%s%s%s in %s%s%s", CYAN,
                 username, RESET, BLUE, current_dir, RESET);
      }

      // Print prompt immediately
      printf("%s -> ", prompt_path);
      fflush(stdout);

      // Update the status bar initially without Git info
      update_status_bar(hConsole, "");

      // Check for Git info if directory has changed
      if (directory_changed) {
        // Update last directory
        strcpy(last_directory, cwd);

        // Clear cached Git info
        cached_git_info[0] = '\0';
        cached_in_git_repo = 0;

        // Check if we're in a Git repository
        char git_branch[64] = "";
        char git_repo[64] = "";
        int is_dirty = 0;
        cached_in_git_repo =
            get_git_branch(git_branch, sizeof(git_branch), &is_dirty);

        if (cached_in_git_repo) {
          int has_repo_name = get_git_repo_name(git_repo, sizeof(git_repo));

          if (has_repo_name) {
            snprintf(cached_git_info, sizeof(cached_git_info),
                     "\u2387 %s [%s%s]", git_repo, git_branch,
                     is_dirty ? "*" : "");
          } else {
            snprintf(cached_git_info, sizeof(cached_git_info), "\u2387 [%s%s]",
                     git_branch, is_dirty ? "*" : "");
          }
        }
      }

      // Use cached Git info (will be empty if not in a repo)
      if (cached_in_git_repo) {
        strcpy(git_info, cached_git_info);
      }

      // Update status bar with Git info (if any)
      if (git_info[0] != '\0') {
        update_status_bar(hConsole, git_info);
      }
    }

    // Read user input
    line = lsh_read_line();

    // Add command to history if not empty
    if (line[0] != '\0') {
      lsh_add_to_history(line);
    }

    // Split and execute commands
    commands = lsh_split_commands(line);

    // Check if there are any pipes (more than one command)
    int pipe_count = 0;
    while (commands[pipe_count] != NULL)
      pipe_count++;

    // Hide status bar before command execution to prevent ghost duplicates
    hide_status_bar(hConsole);

    if (pipe_count > 1) {
      // Execute piped commands
      status = lsh_execute_piped(commands);
    } else if (pipe_count == 1) {
      // Execute single command the normal way
      status = lsh_execute(commands[0]);
    } else {
      // No commands (empty line)
      status = 1;
    }

    // Always redraw the status bar after command execution
    update_status_bar(hConsole, git_info);

    // Clean up
    free(line);
    free_commands(commands);

  } while (status);

  // Clean up aliases on exit
  cleanup_aliases();

  // Clean up bookmarks on exit
  cleanup_bookmarks();
}
