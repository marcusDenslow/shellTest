/**
 * grep.c
 * Implementation of text searching in files with interactive navigation
 */

#include "grep.h"
#include "builtins.h"
#include <stdio.h>
#include <string.h>

// Define color codes for highlighting matches
#define COLOR_MATCH FOREGROUND_RED | FOREGROUND_INTENSITY
#define COLOR_INFO FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define COLOR_RESULT_HIGHLIGHT                                                 \
  FOREGROUND_GREEN | FOREGROUND_INTENSITY // Renamed to avoid conflict
#define COLOR_BOX FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_INTENSITY

// Structure to hold a single grep result
typedef struct {
  char filename[MAX_PATH]; // File containing the match
  int line_number;         // Line number in the file
  char line_content[4096]; // Content of the line
  int match_start;         // Position where match begins in the line
  int match_length;        // Length of the match
} GrepResult;

// Structure to hold all grep results
typedef struct {
  GrepResult *results; // Array of results
  int count;           // Number of results
  int capacity;        // Allocated capacity
  int current_index;   // Currently displayed result index
  BOOL is_active;      // Whether results view is active
} GrepResultList;

// Global result list
static GrepResultList grep_results = {0};
static int open_file_in_editor(const char *file_path, int line_number);

// Helper function to calculate number of digits in an integer
static int count_digits(int number) {
  if (number == 0)
    return 1;
  int digits = 0;
  while (number > 0) {
    digits++;
    number /= 10;
  }
  return digits;
}

/**
 * Helper function to get minimum of two integers
 */
static int grep_min(int a, int b) { return (a < b) ? a : b; }

/**
 * Helper function to get maximum of two integers
 */
static int grep_max(int a, int b) { return (a > b) ? a : b; }

// Function declarations
static void search_file_collect(const char *filename, const char *pattern,
                                int line_numbers, int ignore_case,
                                int recursive);
static void search_directory_collect(const char *directory, const char *pattern,
                                     int line_numbers, int ignore_case,
                                     int recursive);
static int is_text_file(const char *filename);
static void display_grep_results();
static void add_grep_result(const char *filename, int line_number,
                            const char *line, int match_start,
                            int match_length);
static void free_grep_results();
void show_file_detail_view(GrepResult *result);

/**
 * Command handler for the "grep" command
 * Usage: grep [options] pattern [file/directory]
 * Options:
 *   -n, --line-numbers  Show line numbers
 *   -i, --ignore-case   Ignore case distinctions
 *   -r, --recursive     Search directories recursively
 */
int lsh_grep(char **args) {
  if (args[1] == NULL) {
    printf("Usage: grep [options] pattern [file/directory]\n");
    printf("Options:\n");
    printf("  -n, --line-numbers  Show line numbers\n");
    printf("  -i, --ignore-case   Ignore case distinctions\n");
    printf("  -r, --recursive     Search directories recursively\n");
    return 1;
  }

  // Parse options
  int arg_index = 1;
  int line_numbers = 0;
  int ignore_case = 0;
  int recursive = 0;

  // Process options
  while (args[arg_index] != NULL && args[arg_index][0] == '-') {
    if (strcmp(args[arg_index], "-n") == 0 ||
        strcmp(args[arg_index], "--line-numbers") == 0) {
      line_numbers = 1;
    } else if (strcmp(args[arg_index], "-i") == 0 ||
               strcmp(args[arg_index], "--ignore-case") == 0) {
      ignore_case = 1;
    } else if (strcmp(args[arg_index], "-r") == 0 ||
               strcmp(args[arg_index], "--recursive") == 0) {
      recursive = 1;
    } else {
      printf("grep: unknown option: %s\n", args[arg_index]);
      return 1;
    }
    arg_index++;
  }

  // After options, we need a pattern at minimum
  if (args[arg_index] == NULL) {
    printf("grep: missing pattern\n");
    return 1;
  }

  // Get pattern
  const char *pattern = args[arg_index++];

  // Reset grep results if any previous search was done
  free_grep_results();

  // Check if a file/directory was specified
  if (args[arg_index] == NULL) {
    // No file specified, search current directory
    search_directory_collect(".", pattern, line_numbers, ignore_case,
                             recursive);
  } else {
    // Process each specified file/directory
    while (args[arg_index] != NULL) {
      // Get file attributes to check if it's a directory
      DWORD attr = GetFileAttributes(args[arg_index]);

      if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("grep: %s: No such file or directory\n", args[arg_index]);
      } else if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        // It's a directory
        search_directory_collect(args[arg_index], pattern, line_numbers,
                                 ignore_case, recursive);
      } else {
        // It's a file
        search_file_collect(args[arg_index], pattern, line_numbers, ignore_case,
                            0);
      }

      arg_index++;
    }
  }

  // Display the interactive results if any were found
  if (grep_results.count > 0) {
    printf("Found %d matches. Press TAB to navigate through results...\n",
           grep_results.count);
    display_grep_results();
  } else {
    printf("No matches found\n");
  }

  // Clean up
  free_grep_results();

  return 1;
}

/**
 * Add a grep result to the results list
 */
static void add_grep_result(const char *filename, int line_number,
                            const char *line, int match_start,
                            int match_length) {
  // Resize if needed
  if (grep_results.count >= grep_results.capacity) {
    grep_results.capacity =
        grep_results.capacity == 0 ? 10 : grep_results.capacity * 2;
    grep_results.results = (GrepResult *)realloc(
        grep_results.results, grep_results.capacity * sizeof(GrepResult));
    if (!grep_results.results) {
      fprintf(stderr, "grep: memory allocation error\n");
      return;
    }
  }

  // Add the result
  GrepResult *result = &grep_results.results[grep_results.count++];
  strncpy(result->filename, filename, MAX_PATH - 1);
  result->filename[MAX_PATH - 1] = '\0';
  result->line_number = line_number;
  strncpy(result->line_content, line, sizeof(result->line_content) - 1);
  result->line_content[sizeof(result->line_content) - 1] = '\0';
  result->match_start = match_start;
  result->match_length = match_length;
}

/**
 * Free the grep results list
 */
static void free_grep_results() {
  if (grep_results.results) {
    free(grep_results.results);
    grep_results.results = NULL;
  }
  grep_results.count = 0;
  grep_results.capacity = 0;
  grep_results.current_index = 0;
  grep_results.is_active = FALSE;
}

/**
 * Search a file for a pattern and collect results
 */
static void search_file_collect(const char *filename, const char *pattern,
                                int line_numbers, int ignore_case,
                                int recursive) {
  // Skip non-text files for safety
  if (!is_text_file(filename)) {
    return;
  }

  FILE *file = fopen(filename, "r");
  if (!file) {
    printf("grep: %s: Cannot open file\n", filename);
    return;
  }

  char line[4096];
  int line_num = 0;

  // Read the file line by line
  while (fgets(line, sizeof(line), file) != NULL) {
    line_num++;

    // Remove newline character if present
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }

    // Search for pattern in the line
    char *match = NULL;

    if (ignore_case) {
      // Case insensitive search
      char *line_lower = _strdup(line);
      char *pattern_lower = _strdup(pattern);

      if (line_lower && pattern_lower) {
        // Convert to lowercase
        for (char *p = line_lower; *p; p++)
          *p = tolower(*p);
        for (char *p = pattern_lower; *p; p++)
          *p = tolower(*p);

        // Find match
        match = strstr(line_lower, pattern_lower);

        // Calculate offset in original line if match found
        if (match) {
          int match_pos = match - line_lower;
          int match_len = strlen(pattern_lower);

          // Add to results
          add_grep_result(filename, line_num, line, match_pos, match_len);
        }

        free(line_lower);
        free(pattern_lower);
      }
    } else {
      // Case sensitive search
      match = strstr(line, pattern);

      // If pattern found in the line, add to results
      if (match) {
        int match_pos = match - line;
        int match_len = strlen(pattern);

        // Add to results
        add_grep_result(filename, line_num, line, match_pos, match_len);
      }
    }
  }

  fclose(file);
}

/**
 * Search a directory for files containing a pattern
 */
static void search_directory_collect(const char *directory, const char *pattern,
                                     int line_numbers, int ignore_case,
                                     int recursive) {
  char search_path[MAX_PATH];
  WIN32_FIND_DATA findData;
  HANDLE hFind;

  // Prepare search pattern for all files in directory
  snprintf(search_path, sizeof(search_path), "%s\\*", directory);

  // Start finding files
  hFind = FindFirstFile(search_path, &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    printf("grep: %s: Cannot access directory\n", directory);
    return;
  }

  do {
    // Skip "." and ".." directories
    if (strcmp(findData.cFileName, ".") == 0 ||
        strcmp(findData.cFileName, "..") == 0) {
      continue;
    }

    // Build full path
    char full_path[MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s\\%s", directory,
             findData.cFileName);

    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // It's a directory - recursively search if recursive flag is set
      if (recursive) {
        search_directory_collect(full_path, pattern, line_numbers, ignore_case,
                                 recursive);
      }
    } else {
      // It's a file - search it
      search_file_collect(full_path, pattern, line_numbers, ignore_case,
                          recursive);
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);
}

static void display_grep_results() {
  if (grep_results.count == 0) {
    return;
  }

  // Get handles to console
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  WORD originalAttrs;

  // Save original console attributes
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  originalAttrs = csbi.wAttributes;

  // Save original console mode
  DWORD originalMode;
  GetConsoleMode(hStdin, &originalMode);

  // Set console mode for raw input
  SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT);

  // Initialize results viewer
  grep_results.current_index = 0;
  grep_results.is_active = TRUE;

  // Calculate dimensions
  int console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  int console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

  // Adjust display to use about 70% of screen height
  int display_height = (int)(console_height * 0.7);
  if (display_height < 10)
    display_height = 10; // Minimum reasonable size

  // Set fixed position for display (center in screen)
  int top_margin = (console_height - display_height) / 2;
  if (top_margin < 0)
    top_margin = 0;

  // Calculate split dimensions - left side is fixed width for filenames
  int left_width = 30; // Fixed width for file list
  if (left_width > console_width / 3)
    left_width = console_width / 3; // Cap at 1/3 of console width

  int right_width = console_width - left_width - 3; // -3 for separators

  // Store original cursor position
  COORD originalCursorPos = csbi.dwCursorPosition;

  // Clear the screen initially
  system("cls");

  // Draw the initial static heading only once
  SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
  printf("Grep Results (%d matches)\n", grep_results.count);

  // Draw separator under title
  SetConsoleTextAttribute(hConsole, COLOR_BOX);
  for (int i = 0; i < console_width; i++)
    printf("─");
  printf("\n\n");

  // Calculate the fixed display area coordinates
  COORD displayAreaStart = {0, 3};      // Start after title and separator
  int list_height = display_height - 7; // Reserve space for header and footer

  // Main navigation loop
  while (grep_results.is_active) {
    // Get current result
    GrepResult *result = &grep_results.results[grep_results.current_index];

    // Hide cursor during redraw
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    BOOL originalCursorVisible = cursorInfo.bVisible;
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Save the buffer info again to ensure we have current dimensions
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    right_width = console_width - left_width - 3;

    // Position at the start of the display area
    SetConsoleCursorPosition(hConsole, displayAreaStart);

    // Clear entire display area including footer and navigation
    for (int y = displayAreaStart.Y; y < displayAreaStart.Y + list_height + 3;
         y++) {
      COORD clearPos = {0, y};
      DWORD written;
      FillConsoleOutputCharacter(hConsole, ' ', console_width, clearPos,
                                 &written);
      FillConsoleOutputAttribute(hConsole, originalAttrs, console_width,
                                 clearPos, &written);
    }

    // Return to display area start
    SetConsoleCursorPosition(hConsole, displayAreaStart);

    // Calculate how many files to show
    int files_to_show = grep_min(list_height, grep_results.count);

    // Calculate start index to keep selected item in view
    int start_index = 0;
    if (grep_results.current_index >= list_height) {
      // If current index is off-screen, adjust start index
      start_index = grep_results.current_index - list_height + 1;
    }

    // Display the file list
    for (int i = 0; i < files_to_show; i++) {
      int result_idx = start_index + i;
      if (result_idx >= grep_results.count)
        break;

      GrepResult *match = &grep_results.results[result_idx];

      // Extract just the filename without path
      char *filename = match->filename;
      char *lastSlash = strrchr(filename, '\\');
      if (lastSlash) {
        filename = lastSlash + 1;
      }

      // Highlight current selection
      if (result_idx == grep_results.current_index) {
        SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
        printf("→ ");
      } else {
        SetConsoleTextAttribute(hConsole, originalAttrs);
        printf("  ");
      }

      // Truncate filename if too long
      char display_name[50]; // Buffer for truncated name
      strncpy(display_name, filename, sizeof(display_name) - 1);
      display_name[sizeof(display_name) - 1] = '\0';

      if (strlen(display_name) >
          left_width - 10) { // Adjusted for wider line number field
        // Truncate with ellipsis
        display_name[left_width - 10] = '.';
        display_name[left_width - 9] = '.';
        display_name[left_width - 8] = '.';
        display_name[left_width - 7] = '\0';
      }

      // Use fixed width (4 digits) for line numbers to ensure consistent
      // alignment
      printf("%-*s:%4d", left_width - 10, display_name, match->line_number);

      // Draw separator between file list and preview
      SetConsoleTextAttribute(hConsole, COLOR_BOX);
      printf(" │ ");

      // Show preview for the selected file only
      if (result_idx == grep_results.current_index) {
        // Improved compact preview that shows multiple lines without breaking
        // layout
        FILE *preview_file = fopen(match->filename, "r");
        if (preview_file) {
          // Setup context variables
          int target_line = match->line_number;
          int context_lines =
              5; // Show 5 lines above and below for a total of 11 lines
          int start_line =
              (target_line <= context_lines) ? 1 : target_line - context_lines;
          int end_line = target_line + context_lines;
          int current_line = 1;
          char line_buffer[256];

          // Skip to start line
          while (current_line < start_line &&
                 fgets(line_buffer, sizeof(line_buffer), preview_file)) {
            current_line++;
          }

          // Collect the context lines
          int lines_collected = 0;
          char context_lines_text[11][256] = {0}; // 5 above, 1 match, 5 below

          while (current_line <= end_line &&
                 fgets(line_buffer, sizeof(line_buffer), preview_file)) {
            // Trim newline
            size_t len = strlen(line_buffer);
            if (len > 0 && line_buffer[len - 1] == '\n') {
              line_buffer[len - 1] = '\0';
              len--;
            }

            // Truncate if too long (use less space for context lines)
            int max_len = (current_line == target_line) ? 60 : 45;
            if ((int)len > max_len) {
              line_buffer[max_len - 3] = '.';
              line_buffer[max_len - 2] = '.';
              line_buffer[max_len - 1] = '.';
              line_buffer[max_len] = '\0';
            }

            // Store in the appropriate slot
            int slot_idx = current_line - start_line;
            if (slot_idx >= 0 && slot_idx < 9) {
              // Use fixed width for line numbers (4 digits) to ensure alignment
              snprintf(context_lines_text[slot_idx], 256, "%4d: %s",
                       current_line, line_buffer);
              lines_collected++;
            }

            current_line++;
          }

          fclose(preview_file);

          // Now display the collected lines with proper spacing
          if (lines_collected > 0) {
            // Calculate which lines to display to keep match centered
            int target_slot = target_line - start_line;

            // We want to show all 11 lines when possible
            int max_display_lines = grep_min(
                lines_collected, 11); // Show up to 11 lines when available

            // Calculate start and end indices to center the match line
            int mid_point = max_display_lines / 2;
            int start_idx = target_slot - mid_point;
            if (start_idx < 0)
              start_idx = 0;

            int end_idx = start_idx + max_display_lines - 1;
            if (end_idx >= lines_collected) {
              end_idx = lines_collected - 1;
              start_idx = grep_max(0, end_idx - max_display_lines + 1);
            }

            // Display centered context
            for (int line_idx = start_idx; line_idx <= end_idx; line_idx++) {
              if (line_idx > start_idx) {
                // New line for additional context lines
                printf("\n%*s│ ", left_width, "");
              }

              if (line_idx == target_slot) {
                // This is the matched line - highlight it
                SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
                printf("► ");

                // Find match in the line for highlighting
                char *line_text = context_lines_text[line_idx];
                char *colon_pos = strchr(line_text, ':');
                char *match_start = NULL;

                if (colon_pos && match->match_length > 0) {
                  char match_text[256] = {0};
                  strncpy(match_text, match->line_content + match->match_start,
                          match->match_length < 255 ? match->match_length
                                                    : 255);
                  match_text[match->match_length < 255 ? match->match_length
                                                       : 255] = '\0';

                  match_start = strstr(colon_pos, match_text);
                }

                // Print with match highlighting
                if (match_start) {
                  // Calculate positions
                  char *line_start = line_text;
                  int prefix_len = match_start - line_start;

                  // Print prefix
                  SetConsoleTextAttribute(hConsole, originalAttrs);
                  printf("%.*s", prefix_len, line_start);

                  // Print match with highlight
                  SetConsoleTextAttribute(hConsole, COLOR_MATCH);
                  printf("%.*s", match->match_length, match_start);

                  // Print suffix
                  SetConsoleTextAttribute(hConsole, originalAttrs);
                  printf("%s", match_start + match->match_length);
                } else {
                  // Just print the whole line
                  SetConsoleTextAttribute(hConsole, originalAttrs);
                  printf("%s", line_text);
                }
              } else {
                // Regular context line
                SetConsoleTextAttribute(hConsole, originalAttrs);
                printf("  %s", context_lines_text[line_idx]);
              }
            }
          } else {
            // Fallback if no lines could be collected
            SetConsoleTextAttribute(hConsole, originalAttrs);
            printf("<Could not read file content>");
          }
        } else {
          // Fallback to show just the matched text directly
          int start_pos = 0;
          if (match->match_start > 10) {
            start_pos = match->match_start - 10;
            printf("...");
          }

          SetConsoleTextAttribute(hConsole, originalAttrs);
          for (int j = start_pos; j < match->match_start; j++) {
            printf("%c", match->line_content[j]);
          }

          SetConsoleTextAttribute(hConsole, COLOR_MATCH);
          for (int j = match->match_start;
               j < match->match_start + match->match_length; j++) {
            printf("%c", match->line_content[j]);
          }

          SetConsoleTextAttribute(hConsole, originalAttrs);
          int chars_printed = 0;
          int remaining = right_width - (match->match_start - start_pos) -
                          match->match_length - 3;

          for (int j = match->match_start + match->match_length;
               match->line_content[j] && chars_printed < remaining; j++) {
            printf("%c", match->line_content[j]);
            chars_printed++;
          }

          if (strlen(match->line_content + match->match_start +
                     match->match_length) > chars_printed) {
            printf("...");
          }
        }
      }

      printf("\n");
    }

    // Draw remaining empty lines to keep the display stable
    for (int i = files_to_show; i < list_height; i++) {
      SetConsoleTextAttribute(hConsole, COLOR_BOX);
      printf("%*s │\n", left_width, "");
    }

    // Draw footer separator
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    for (int i = 0; i < console_width; i++)
      printf("─");
    printf("\n");

    // Show currently selected file with absolute path
    SetConsoleTextAttribute(hConsole, COLOR_INFO);

    // Get absolute path for display
    char absolutePath[MAX_PATH];
    if (_fullpath(absolutePath, result->filename, MAX_PATH) != NULL) {
      printf("File: %s\n", absolutePath);
    } else {
      printf("File: %s\n", result->filename);
    }

    // Show navigation help
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("Navigation: TAB/j/DOWN - Next  SHIFT+TAB/k/UP - Prev  o - Open "
           "in Editor  ENTER - Detail View  ESC/Q - Exit\n");

    // Restore cursor visibility
    cursorInfo.bVisible = originalCursorVisible;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Process input for navigation
    INPUT_RECORD inputRecord;
    DWORD numEvents;

    if (WaitForSingleObject(hStdin, INFINITE) == WAIT_OBJECT_0) {
      ReadConsoleInput(hStdin, &inputRecord, 1, &numEvents);

      if (numEvents > 0 && inputRecord.EventType == KEY_EVENT &&
          inputRecord.Event.KeyEvent.bKeyDown) {

        WORD keyCode = inputRecord.Event.KeyEvent.wVirtualKeyCode;
        DWORD controlKeyState = inputRecord.Event.KeyEvent.dwControlKeyState;

        // Check for navigation keys
        if (keyCode == VK_TAB) {
          if (controlKeyState & SHIFT_PRESSED) {
            // Shift+Tab - Previous result
            if (grep_results.current_index > 0) {
              grep_results.current_index--;
            } else {
              grep_results.current_index = grep_results.count - 1;
            }
          } else {
            // Tab - Next result
            grep_results.current_index =
                (grep_results.current_index + 1) % grep_results.count;
          }
        } else if (keyCode == VK_UP) {
          // Up arrow - Previous result
          if (grep_results.current_index > 0) {
            grep_results.current_index--;
          } else {
            grep_results.current_index = grep_results.count - 1;
          }
        } else if (keyCode == VK_DOWN) {
          // Down arrow - Next result
          grep_results.current_index =
              (grep_results.current_index + 1) % grep_results.count;
        } else if (keyCode == 'J') {
          // j key - Next result (same as Down arrow)
          grep_results.current_index =
              (grep_results.current_index + 1) % grep_results.count;
        } else if (keyCode == 'K') {
          // k key - Previous result (same as Up arrow)
          if (grep_results.current_index > 0) {
            grep_results.current_index--;
          } else {
            grep_results.current_index = grep_results.count - 1;
          }
        } else if (keyCode == VK_RETURN) {
          // Enter - Open directly in editor
          GrepResult *result =
              &grep_results.results[grep_results.current_index];

          // Open the file directly in an editor
          open_file_in_editor(result->filename, result->line_number);

          // Keep grep mode active after returning from the editor
          // This requires screen refresh
          system("cls");

          // Redraw the static title
          SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
          printf("Grep Results (%d matches)\n", grep_results.count);

          // Draw separator under title
          SetConsoleTextAttribute(hConsole, COLOR_BOX);
          for (int i = 0; i < console_width; i++)
            printf("─");
          printf("\n\n");

          // Reset display position to ensure clean redraw
          SetConsoleCursorPosition(hConsole, displayAreaStart);
        } else if (keyCode == 'O' || keyCode == 'o') {
          // 'o' key - Show detail view
          show_file_detail_view(
              &grep_results.results[grep_results.current_index]);

          // Completely redraw the UI to fix duplicate lines issue
          system("cls");

          // Redraw the static title
          SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
          printf("Grep Results (%d matches)\n", grep_results.count);

          // Draw separator under title
          SetConsoleTextAttribute(hConsole, COLOR_BOX);
          for (int i = 0; i < console_width; i++)
            printf("─");
          printf("\n\n");

          // Reset display position to ensure clean redraw
          SetConsoleCursorPosition(hConsole, displayAreaStart);
        } else if (keyCode == VK_ESCAPE || keyCode == 'Q') {
          // Escape or Q - Exit results view
          grep_results.is_active = FALSE;
        }
      }
    }
  }

  // Restore console settings
  SetConsoleTextAttribute(hConsole, originalAttrs);
  SetConsoleMode(hStdin, originalMode);

  // Clear the screen completely to remove all previous content
  system("cls");

  // Position cursor precisely at the top-left of the screen (0,0)
  // This ensures the next prompt will start from the top
  COORD topPos = {0, 0};
  SetConsoleCursorPosition(hConsole, topPos);

  // Print a blank line to ensure proper spacing for the prompt
  printf("\n");
}

static int open_file_in_editor(const char *file_path, int line_number) {
  char command[2048] = {0};
  int success = 0;

  // Try to detect available editors (in order of preference)
  FILE *test_nvim = _popen("nvim --version 2>nul", "r");
  if (test_nvim != NULL) {
    // Neovim is available - construct command with +line_number
    _pclose(test_nvim);
    snprintf(command, sizeof(command), "nvim +%d \"%s\"", line_number,
             file_path);
    success = 1;
  } else {
    // Try vim next
    FILE *test_vim = _popen("vim --version 2>nul", "r");
    if (test_vim != NULL) {
      // Vim is available
      _pclose(test_vim);
      snprintf(command, sizeof(command), "vim +%d \"%s\"", line_number,
               file_path);
      success = 1;
    } else {
      // Try VSCode as last resort
      FILE *test_code = _popen("code --version 2>nul", "r");
      if (test_code != NULL) {
        _pclose(test_code);
        // VSCode uses filename:line_number syntax
        snprintf(command, sizeof(command), "code -g \"%s:%d\"", file_path,
                 line_number);
        success = 1;
      }
    }
  }

  if (success) {
    // Create a message for the user
    printf("Opening %s at line %d...\n", file_path, line_number);

    // Execute the command
    success = (system(command) == 0);

    // Wait for a brief moment to let the user see the message
    Sleep(500);

    return success;
  } else {
    // No suitable editor found
    printf("No compatible editor (neovim, vim, or VSCode) found.\n");
    printf("Press any key to continue...\n");
    _getch();
    return 0;
  }
}

void show_file_detail_view(GrepResult *result) {
  // Get handle to console
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD originalAttrs;

  // Save original console attributes
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
  originalAttrs = consoleInfo.wAttributes;

  // Clear the screen completely
  system("cls");

  // Show file information header
  SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
  printf("File: %s (line %d)\n\n", result->filename, result->line_number);

  // Show file content preview
  FILE *file = fopen(result->filename, "r");
  if (file) {
    // Calculate context - show 5 lines before and after match
    int context_lines = 5;
    int start_line = grep_max(1, result->line_number - context_lines);
    int end_line = result->line_number + context_lines;
    int current_line = 1;
    char line[4096];

    // Skip to start line
    while (current_line < start_line && fgets(line, sizeof(line), file)) {
      current_line++;
    }

    // Read and display lines with context
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("Preview:\n");
    while (current_line <= end_line && fgets(line, sizeof(line), file)) {
      // Remove newline if present
      size_t len = strlen(line);
      if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len--;
      }

      // Show line number
      if (current_line == result->line_number) {
        // Highlight the matching line
        SetConsoleTextAttribute(hConsole, COLOR_INFO);
        printf("%4d → ", current_line);

        // Print parts before match
        SetConsoleTextAttribute(hConsole, originalAttrs);
        for (int i = 0; i < result->match_start; i++) {
          printf("%c", line[i]);
        }

        // Print match with highlight
        SetConsoleTextAttribute(hConsole, COLOR_MATCH);
        for (int i = result->match_start;
             i < result->match_start + result->match_length && i < len; i++) {
          printf("%c", line[i]);
        }

        // Print after match
        SetConsoleTextAttribute(hConsole, originalAttrs);
        for (int i = result->match_start + result->match_length; i < len; i++) {
          printf("%c", line[i]);
        }

        printf("\n");
      } else {
        // Regular line
        printf("%4d   %s\n", current_line, line);
      }

      current_line++;
    }

    fclose(file);
  } else {
    // Could not open file
    SetConsoleTextAttribute(hConsole, COLOR_MATCH);
    printf("Could not open file for preview\n");
  }

  // Show options for the user
  printf("\n");
  SetConsoleTextAttribute(hConsole, COLOR_BOX);
  printf("Press ENTER to open in editor, any other key to return to results "
         "view...");

  // Get user input
  int key = _getch();

  // If ENTER (13) pressed, open the file in an editor
  if (key == KEY_ENTER || key == 13) {
    open_file_in_editor(result->filename, result->line_number);
  }

  // Restore console color
  SetConsoleTextAttribute(hConsole, originalAttrs);
}

/**
 * Check if a file is likely to be a text file
 * This helps avoid reading binary files
 */
static int is_text_file(const char *filename) {
  // Check file extension
  const char *ext = strrchr(filename, '.');
  if (ext) {
    // Skip common binary file extensions
    static const char *binary_exts[] = {
        ".exe", ".dll", ".obj", ".bin", ".dat",  ".png", ".jpg", ".jpeg",
        ".gif", ".bmp", ".zip", ".rar", ".7z",   ".gz",  ".mp3", ".mp4",
        ".avi", ".mov", ".pdf", ".doc", ".docx", ".xls", ".xlsx"};

    for (int i = 0; i < sizeof(binary_exts) / sizeof(binary_exts[0]); i++) {
      if (_stricmp(ext, binary_exts[i]) == 0) {
        return 0; // Binary file
      }
    }

    // Check for common text file extensions
    static const char *text_exts[] = {".txt", ".c",    ".cpp", ".h",    ".hpp",
                                      ".cs",  ".js",   ".py",  ".html", ".css",
                                      ".xml", ".json", ".md",  ".log",  ".sh",
                                      ".bat", ".cmd",  ".ini", ".conf", ".cfg"};

    for (int i = 0; i < sizeof(text_exts) / sizeof(text_exts[0]); i++) {
      if (_stricmp(ext, text_exts[i]) == 0) {
        return 1; // Text file
      }
    }
  }

  // If we can't determine by extension, check the first few bytes
  FILE *file = fopen(filename, "rb");
  if (!file) {
    return 0; // Can't open, assume it's not text
  }

  // Read the first 512 bytes
  unsigned char buffer[512];
  size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
  fclose(file);

  if (bytes_read == 0) {
    return 1; // Empty file, consider it text
  }

  // Count non-printable, non-whitespace characters
  int binary_chars = 0;
  for (size_t i = 0; i < bytes_read; i++) {
    if (buffer[i] < 32 && buffer[i] != '\t' && buffer[i] != '\n' &&
        buffer[i] != '\r') {
      binary_chars++;
    }
  }

  // If more than 10% of characters are binary, consider it a binary file
  return (binary_chars < bytes_read / 10);
}
