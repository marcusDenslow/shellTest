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
#include "favorite_cities.h"
#include "filters.h"
#include "git_integration.h" // Added for Git repository detection
#include "line_reader.h"
#include "persistent_history.h"
#include "structured_data.h"
#include "tab_complete.h" // Added for tab completion support
#include "themes.h"
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

  // We want at least 1 blank line between text and status bar
  // If cursor is on the bottom line or second-to-last line, we need to scroll
  if (csbi.dwCursorPosition.Y >= csbi.srWindow.Bottom - 2) {
    // First clear the status bar if it exists
    COORD statusPos = {0, csbi.srWindow.Bottom};
    DWORD written;
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X, statusPos,
                               &written);
    FillConsoleOutputAttribute(hConsole, g_normal_attributes, csbi.dwSize.X,
                               statusPos, &written);

    // Add a blank line above status bar for spacing
    COORD blankLinePos = {0, csbi.srWindow.Bottom - 1};
    FillConsoleOutputCharacter(hConsole, ' ', csbi.dwSize.X, blankLinePos,
                               &written);
    FillConsoleOutputAttribute(hConsole, g_normal_attributes, csbi.dwSize.X,
                               blankLinePos, &written);

    // Create a small scroll rectangle - everything except the status bar and
    // spacing lines
    SMALL_RECT scrollRect;
    scrollRect.Left = 0;
    scrollRect.Top = csbi.srWindow.Top;
    scrollRect.Right = csbi.dwSize.X - 1;
    scrollRect.Bottom =
        csbi.srWindow.Bottom - 2; // Exclude status bar and spacing line

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
  if (!g_status_bar_enabled)
    return;

  check_console_resize(hConsole);

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return;
  }

  COORD cursorPos = csbi.dwCursorPosition;

  g_console_width = csbi.dwSize.X;

  g_status_line = csbi.srWindow.Bottom;

  CONSOLE_CURSOR_INFO cursorinfo;
  GetConsoleCursorInfo(hConsole, &cursorinfo);
  BOOL originalCursorVisable = cursorinfo.bVisible;
  cursorinfo.bVisible = FALSE;
  SetConsoleCursorInfo(hConsole, &cursorinfo);

  COORD statusPos = {0, g_status_line};

  DWORD charsWritten;
  FillConsoleOutputCharacter(hConsole, ' ', g_console_width, statusPos,
                             &charsWritten);
  FillConsoleOutputAttribute(hConsole, g_status_attributes, g_console_width,
                             statusPos, &charsWritten);

  const char *timer_info = get_timer_display();
  BOOL has_timer = is_timer_active() && timer_info && timer_info[0];

  if (has_timer) {
    int timer_info_len = strlen(timer_info);
    COORD timerPos = {g_console_width - timer_info_len - 2, g_status_line};

    WORD timerInfoColor = g_status_attributes | current_theme.WARNING_COLOR;

    WriteConsoleOutputCharacter(hConsole, timer_info, timer_info_len, timerPos,
                                &charsWritten);
    FillConsoleOutputAttribute(hConsole, timerInfoColor, timer_info_len,
                               timerPos, &charsWritten);
  } else {
  }

  SetConsoleCursorPosition(hConsole, cursorPos);

  cursorinfo.bVisible = originalCursorVisable;
  SetConsoleCursorInfo(hConsole, &cursorinfo);
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
  g_normal_attributes = current_theme.PRIMARY_COLOR;

  // Set status bar attributes using theme colors
  g_status_attributes = current_theme.STATUS_BAR_COLOR;

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

  // Reset text attributes to theme primary color
  SetConsoleTextAttribute(hConsole, g_normal_attributes);

  // Restore cursor position
  SetConsoleCursorPosition(hConsole, cursorPos);

  // Mark status bar as enabled
  g_status_bar_enabled = TRUE;

  return 1;
}
/**
 * Add padding above the status bar to avoid a cramped UI
 * This ensures there are at least two blank lines between output and prompt
 */
void add_padding_before_prompt(HANDLE hConsole) {
  // Get current console information
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return;
  }

  // We want 2 lines of padding, so check if cursor is within 2 lines of status
  // bar The status bar is at srWindow.Bottom, so check for Bottom-1 and
  // Bottom-2
  const int DESIRED_PADDING = 2; // Change this to increase/decrease padding
  int current_padding = csbi.srWindow.Bottom - csbi.dwCursorPosition.Y;

  if (current_padding < DESIRED_PADDING) {
    // Calculate how many newlines we need to add to get desired padding
    int newlines_needed = DESIRED_PADDING - current_padding;

    // Add the required number of newlines
    for (int i = 0; i < newlines_needed; i++) {
      printf("\n");

      // After each newline, ensure status bar space to prevent pushing it off
      // screen
      ensure_status_bar_space(hConsole);
    }

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
 * Main shell loop (updated with persistent history support)
 */
void lsh_loop(void) {
  char *line;
  char ***commands;
  int status;
  char cwd[1024];
  char prompt_path[1024];
  char git_info[128];
  char git_url[1024] = "";
  char username[256];

  // Static variables to cache Git information
  static char last_directory[1024];
  static char cached_git_info[128];
  static int cached_in_git_repo = 0;

  // Define color reset code
  const char *COLOR_RESET = "\033[0m";

  // Initialize static strings
  last_directory[0] = '\0';
  cached_git_info[0] = '\0';
  git_info[0] = '\0';
  strcpy(username, "Elden Lord");

  // Get handle to console
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

  // Enable VT processing for ANSI escape sequences
  DWORD dwMode = 0;
  GetConsoleMode(hConsole, &dwMode);
  dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
  SetConsoleMode(hConsole, dwMode);

  // Initialize aliases
  init_aliases();

  // Initialize bookmarks
  init_bookmarks();

  // Initialize favorite cities
  init_favorite_cities();

  // Initialize theme system
  init_theme_system();
  apply_current_theme();

  // Initialize persistent history
  init_persistent_history();

  // Display the welcome banner at startup
  display_welcome_banner();

  // Initialize the status bar
  init_status_bar(hConsole);

  do {
    // Clear git_info for this iteration
    git_info[0] = '\0';
    git_url[0] = '\0';

    // Get current console info
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      g_status_line = csbi.srWindow.Bottom;
      g_console_width = csbi.dwSize.X;
    }

    // CRUCIAL: Make sure there's at least one line between prompt and status
    // bar
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
      // If cursor is too close to bottom, add newlines
      if (csbi.srWindow.Bottom - csbi.dwCursorPosition.Y < 2) {
        printf("\n");
      }
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

      // Check for Git info
      char git_branch[64] = "";
      char git_repo[64] = "";
      int is_dirty = 0;
      int in_git_repo = 0;

      if (directory_changed) {
        // Update last directory
        strcpy(last_directory, cwd);

        // Clear cached Git info
        cached_git_info[0] = '\0';
        cached_in_git_repo = 0;

        // Check if we're in a Git repository
        cached_in_git_repo =
            get_git_branch(git_branch, sizeof(git_branch), &is_dirty);

        if (cached_in_git_repo) {
          int has_repo_name = get_git_repo_name(git_repo, sizeof(git_repo));

          if (has_repo_name) {
            snprintf(cached_git_info, sizeof(cached_git_info),
                     " git:(%s%s%s%s)", git_repo,
                     strlen(git_branch) > 0 ? " " : "", git_branch,
                     is_dirty ? "*" : "");
          } else {
            snprintf(cached_git_info, sizeof(cached_git_info), " git:(%s%s)",
                     git_branch, is_dirty ? "*" : "");
          }
        }
      } else if (cached_in_git_repo) {
        // Use cached info for branch/repo names, but still populate git_branch
        get_git_branch(git_branch, sizeof(git_branch), &is_dirty);
        get_git_repo_name(git_repo, sizeof(git_repo));
      }

      // Set in_git_repo from cached value
      in_git_repo = cached_in_git_repo;

      // Always get Git remote URL if we're in a repository
      // This ensures the URL is available for every prompt, not just when
      // directory changes
      if (in_git_repo) {
        char cmd[1024] = "";
        FILE *fp;
        snprintf(cmd, sizeof(cmd), "git config --get remote.origin.url 2>nul");
        fp = _popen(cmd, "r");
        if (fp) {
          char origin_url[1024] = "";
          if (fgets(origin_url, sizeof(origin_url), fp)) {
            // Remove newline
            size_t len = strlen(origin_url);
            if (len > 0 && origin_url[len - 1] == '\n') {
              origin_url[len - 1] = '\0';
            }

            // Convert SSH URLs to HTTPS URLs if needed
            if (strncmp(origin_url, "git@", 4) == 0) {
              // SSH format: git@github.com:username/repo.git
              char *domain_start = origin_url + 4;
              char *repo_path = strchr(domain_start, ':');

              if (repo_path) {
                *repo_path = '\0'; // Terminate the domain part
                repo_path++;       // Move past the colon

                // Remove .git suffix if present
                char *git_suffix = strstr(repo_path, ".git");
                if (git_suffix) {
                  *git_suffix = '\0';
                }

                // Construct HTTPS URL
                snprintf(git_url, sizeof(git_url), "https://%s/%s",
                         domain_start, repo_path);
              } else {
                // Fallback - just use the original
                strcpy(git_url, origin_url);
              }
            } else if (strncmp(origin_url, "https://", 8) == 0) {
              // Already HTTPS URL, just remove .git suffix if present
              char *git_suffix = strstr(origin_url, ".git");
              if (git_suffix) {
                *git_suffix = '\0';
              }
              strcpy(git_url, origin_url);
            } else {
              // Unknown format, use as-is
              strcpy(git_url, origin_url);
            }
          }
          _pclose(fp);
        }
      }

      // Ensure we still have room for status bar after prompt
      ensure_status_bar_space(hConsole);

      // Update the status bar initially without Git info
      update_status_bar(hConsole, "");

      // Use cached Git info for status bar
      if (cached_in_git_repo) {
        strcpy(git_info, cached_git_info);
      }

      // Update status bar with Git info (if any)
      if (git_info[0] != '\0') {
        update_status_bar(hConsole, git_info);
      }

      // Check if we should use ANSI colors based on the theme setting
      if (current_theme.use_ansi_colors) {
        // Print directory in Rose color (soft pink/peach)
        printf("%s%s", current_theme.ANSI_ROSE, current_dir);

        // Git info with exact colors
        if (cached_in_git_repo) {
          // Git syntax in Pine color (soft blue)
          printf("%s git:(", current_theme.ANSI_PINE);

          // Repository name in Love color (soft red) with clickable link if URL
          // is available
          if (git_url[0] != '\0') {
            // Extract repo and branch info for the link text
            char repo_branch[128] = "";
            char *start = strstr(cached_git_info, "(");
            char *end = strstr(cached_git_info, ")");

            if (start && end && end > start) {
              // Copy the content between parentheses for link text
              strncpy(repo_branch, start + 1, end - start - 1);
              repo_branch[end - start - 1] = '\0';

              // Create a clickable link using OSC 8 escape sequence - broken
              // into separate printf calls
              printf("%s", current_theme.ANSI_LOVE);
              printf("\033]8;;%s\033\\", git_url); // Start hyperlink
              printf("%s", repo_branch);           // Link text
              printf("\033]8;;\033\\");            // End hyperlink
              printf("%s", current_theme.ANSI_PINE);
            } else {
              // Fallback to non-link display if parsing fails
              printf("%s", current_theme.ANSI_LOVE);

              // Print the repo/branch info
              if (start && end && end > start) {
                char repo_branch[128] = "";
                strncpy(repo_branch, start + 1, end - start - 1);
                repo_branch[end - start - 1] = '\0';
                printf("%s", repo_branch);
              }
            }
          } else {
            // No URL available, print without link
            printf("%s", current_theme.ANSI_LOVE);

            // Print the repo/branch info
            char *start = strstr(cached_git_info, "(");
            char *end = strstr(cached_git_info, ")");

            if (start && end && end > start) {
              char repo_branch[128] = "";
              strncpy(repo_branch, start + 1, end - start - 1);
              repo_branch[end - start - 1] = '\0';
              printf("%s", repo_branch);
            }
          }

          // Closing parenthesis in Pine color (soft blue)
          printf("%s)", current_theme.ANSI_PINE);
        }

        // Gold X character at end
        printf("%s ✘ ", current_theme.ANSI_GOLD);

        // Reset to default color for user input
        printf("%s", COLOR_RESET);
      } else {
        // Fallback to standard console colors if ANSI not supported
        SetConsoleTextAttribute(hConsole, current_theme.DIRECTORY_COLOR);
        printf("%s", current_dir);

        if (cached_in_git_repo) {
          SetConsoleTextAttribute(hConsole, current_theme.ACCENT_COLOR);
          printf(" git:(");

          SetConsoleTextAttribute(hConsole, current_theme.PROMPT_COLOR);

          // Extract repository and branch name
          char repo_branch[128] = "";
          char *start = strstr(cached_git_info, "(");
          char *end = strstr(cached_git_info, ")");

          if (start && end && end > start) {
            strncpy(repo_branch, start + 1, end - start - 1);
            repo_branch[end - start - 1] = '\0';

            // If we have a git URL, make the repo name clickable
            if (git_url[0] != '\0') {
              // Create a clickable link using OSC 8 escape sequence
              printf("\033]8;;%s\033\\", git_url); // Start hyperlink
              printf("%s", repo_branch);           // Display link text
              printf("\033]8;;\033\\");            // End hyperlink
            } else {
              printf("%s", repo_branch);
            }
          }

          SetConsoleTextAttribute(hConsole, current_theme.ACCENT_COLOR);
          printf(")");
        }

        SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN |
                                              FOREGROUND_INTENSITY);
        printf(" ✘ ");

        SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);
      }

      fflush(stdout);
    }

    // Read user input
    line = lsh_read_line();

    // Split commands
    commands = lsh_split_commands(line);

    // Check if there are any pipes (more than one command)
    int pipe_count = 0;
    while (commands[pipe_count] != NULL)
      pipe_count++;

    // Record the final command to history
    if (pipe_count >= 1 && line[0] != '\0') {
      // Reconstruct the final command for history (for accurate frequency
      // tracking)
      char final_command[LSH_RL_BUFSIZE] = "";

      for (int cmd_idx = 0; cmd_idx < pipe_count; cmd_idx++) {
        char **args = commands[cmd_idx];

        // Add pipe symbol between commands
        if (cmd_idx > 0) {
          strcat(final_command, " | ");
        }

        // Add each argument for this command
        for (int arg_idx = 0; args[arg_idx] != NULL; arg_idx++) {
          if (arg_idx > 0) {
            strcat(final_command, " ");
          }
          strcat(final_command, args[arg_idx]);
        }
      }

      // Now add the fully constructed command to history
      lsh_add_to_history(final_command);
    }

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

  // Clean up
  cleanup_aliases();
  cleanup_bookmarks();
  cleanup_favorite_cities();
  cleanup_persistent_history();
}
