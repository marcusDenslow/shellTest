/**
 * fzf.c
 * Implementation of fuzzy file finder with interactive navigation
 */

#include "fzf.h"
#include "builtins.h"
#include <stdio.h>
#include <string.h>

// Define color codes for highlighting matches (similar to grep.c)
#define COLOR_MATCH FOREGROUND_RED | FOREGROUND_INTENSITY
#define COLOR_INFO FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define COLOR_RESULT_HIGHLIGHT FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_BOX                                                              \
  FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY

// For syntax highlighting - define our own file types since these are missing
#define FZF_FILE_TYPE_UNKNOWN 0
#define FZF_FILE_TYPE_C 1
#define FZF_FILE_TYPE_CPP 2
#define FZF_FILE_TYPE_H 3
#define FZF_FILE_TYPE_PY 4
#define FZF_FILE_TYPE_JS 5
#define FZF_FILE_TYPE_HTML 6
#define FZF_FILE_TYPE_CSS 7
#define FZF_FILE_TYPE_MD 8
#define FZF_FILE_TYPE_JSON 9

// Structure to hold a single fuzzy match result
typedef struct {
  char filename[MAX_PATH];        // Matched file path
  char display_name[MAX_PATH];    // Displayed file name (might be relative)
  int score;                      // Match score (higher is better)
  char match_positions[MAX_PATH]; // Positions of matched characters
} FuzzyResult;

// Structure to hold all fuzzy search results
typedef struct {
  FuzzyResult *results; // Array of results
  int count;            // Number of results
  int capacity;         // Allocated capacity
  int current_index;    // Currently displayed result index
  BOOL is_active;       // Whether results view is active
  char pattern[256];    // Search pattern
} FuzzyResultList;

// Global result list
static FuzzyResultList fuzzy_results = {0};

// Forward declarations for internal functions
static void add_fuzzy_result(const char *filename, const char *display_name,
                             int score, const char *match_positions);
static void free_fuzzy_results(void);
static void search_directory_fuzzy(const char *directory, const char *pattern,
                                   int recursive);
static void display_fuzzy_results(void);
static int open_file_in_editor(const char *file_path, int line_number);
static void show_file_detail_view(FuzzyResult *result);
static void print_file_preview(HANDLE hConsole, const char *filename,
                               WORD originalAttrs, int right_width,
                               int left_width);

/**
 * Determine file type from extension (our simple implementation since the
 * original is missing)
 */
static int get_file_type(const char *filename) {
  char *ext = strrchr(filename, '.');
  if (ext == NULL)
    return FZF_FILE_TYPE_UNKNOWN;

  ext++; // Skip the dot

  if (_stricmp(ext, "c") == 0)
    return FZF_FILE_TYPE_C;
  if (_stricmp(ext, "cpp") == 0 || _stricmp(ext, "cc") == 0)
    return FZF_FILE_TYPE_CPP;
  if (_stricmp(ext, "h") == 0 || _stricmp(ext, "hpp") == 0)
    return FZF_FILE_TYPE_H;
  if (_stricmp(ext, "py") == 0)
    return FZF_FILE_TYPE_PY;
  if (_stricmp(ext, "js") == 0)
    return FZF_FILE_TYPE_JS;
  if (_stricmp(ext, "html") == 0 || _stricmp(ext, "htm") == 0)
    return FZF_FILE_TYPE_HTML;
  if (_stricmp(ext, "css") == 0)
    return FZF_FILE_TYPE_CSS;
  if (_stricmp(ext, "md") == 0 || _stricmp(ext, "markdown") == 0)
    return FZF_FILE_TYPE_MD;
  if (_stricmp(ext, "json") == 0)
    return FZF_FILE_TYPE_JSON;

  return FZF_FILE_TYPE_UNKNOWN;
}

/**
 * Calculate a fuzzy match score between a pattern and a string
 * Higher score means better match
 *
 * @param pattern The search pattern
 * @param str The string to match against
 * @param match_positions Output array marking matched positions (1 for match, 0
 * for no match)
 * @return Match score (higher is better) or -1 if no match
 */
static int fuzzy_match(const char *pattern, const char *str,
                       char *match_positions) {
  if (!pattern || !str || !*pattern) {
    return -1;
  }

  // Initialize match positions to 0
  memset(match_positions, 0, strlen(str));

  const char *p = pattern;
  const char *s = str;
  int score = 0;
  int consecutive = 0;
  int position = 0;

  // Convert both strings to lowercase for case-insensitive matching
  char pattern_lower[256] = {0};
  char str_lower[MAX_PATH] = {0};

  strncpy(pattern_lower, pattern, sizeof(pattern_lower) - 1);
  strncpy(str_lower, str, sizeof(str_lower) - 1);

  for (char *c = pattern_lower; *c; c++) {
    *c = tolower(*c);
  }

  for (char *c = str_lower; *c; c++) {
    *c = tolower(*c);
  }

  // For each character in the pattern
  while (*p) {
    // Find this pattern character in the string
    const char *found = strchr(s, *p);
    if (!found) {
      // Try case-insensitive search
      found = strchr(str_lower + (s - str), tolower(*p));
      if (!found) {
        return -1; // Character not found, no match
      }
      // Adjust found pointer to point to the original string
      found = str + (found - str_lower);
    }

    // Calculate position in the string
    position = found - str;

    // Mark this position as matched
    match_positions[position] = 1;

    // Check if this match is consecutive with the previous one
    if (s == found) {
      consecutive++;
    } else {
      consecutive = 1;
    }

    // Score calculation:
    // - Base score is 1
    // - Consecutive matches get bonus points
    // - Matches at the start of the string or after separators get bonus points
    // - Earlier matches are slightly better
    int base_score = 1;
    int consecutive_bonus = consecutive * 2;
    int position_bonus = 0;

    // Bonus for matching at start of string or after separators
    if (position == 0 || strchr("_-./\\", str[position - 1])) {
      position_bonus = 5;
    }

    // Penalty for later positions
    int position_penalty = position / 10;

    // Calculate total score for this character
    int char_score =
        base_score + consecutive_bonus + position_bonus - position_penalty;
    score += char_score;

    // Move string pointer past this match
    s = found + 1;

    // Move to next pattern character
    p++;
  }

  return score;
}

/**
 * Command handler for the "fzf" command
 * Usage: fzf [options] [pattern]
 * Options:
 *   -r, --recursive     Search directories recursively
 */
int lsh_fzf(char **args) {
  if (args[1] && strcmp(args[1], "--help") == 0) {
    printf("Usage: fzf [options] [pattern]\n");
    printf("Fuzzy file finder with interactive navigation.\n");
    printf("Options:\n");
    printf("  -r, --recursive     Search directories recursively\n");
    printf("\nControls:\n");
    printf("  j/DOWN      - Next result\n");
    printf("  k/UP        - Previous result\n");
    printf("  /           - Interactive search\n");
    printf("  ENTER       - Open in Editor\n");
    printf("  o           - Detail View\n");
    printf("  ESC/Q       - Exit\n");
    return 1;
  }

  // Parse options
  int arg_index = 1;
  int recursive = 0;
  char pattern[256] = "";

  // Process options
  while (args[arg_index] != NULL && args[arg_index][0] == '-') {
    if (strcmp(args[arg_index], "-r") == 0 ||
        strcmp(args[arg_index], "--recursive") == 0) {
      recursive = 1;
      arg_index++;
    } else {
      printf("fzf: unknown option: %s\n", args[arg_index]);
      return 1;
    }
  }

  // Get the pattern if provided
  if (args[arg_index] != NULL) {
    strncpy(pattern, args[arg_index], sizeof(pattern) - 1);
  }

  // Reset fuzzy results if any previous search was done
  free_fuzzy_results();

  // Store the pattern in the global struct
  strncpy(fuzzy_results.pattern, pattern, sizeof(fuzzy_results.pattern) - 1);

  // Search current directory with the pattern
  search_directory_fuzzy(".", pattern, recursive);

  // Display the interactive results if any were found
  if (fuzzy_results.count > 0) {
    display_fuzzy_results();
  } else {
    if (pattern[0] != '\0') {
      printf("No matches found for pattern: \"%s\"\n", pattern);
    } else {
      printf("No files found\n");
    }
  }

  // Clean up
  free_fuzzy_results();

  return 1;
}

/**
 * Add a fuzzy match result to the results list
 */
static void add_fuzzy_result(const char *filename, const char *display_name,
                             int score, const char *match_positions) {
  // Resize if needed
  if (fuzzy_results.count >= fuzzy_results.capacity) {
    fuzzy_results.capacity =
        fuzzy_results.capacity == 0 ? 10 : fuzzy_results.capacity * 2;
    fuzzy_results.results = (FuzzyResult *)realloc(
        fuzzy_results.results, fuzzy_results.capacity * sizeof(FuzzyResult));
    if (!fuzzy_results.results) {
      fprintf(stderr, "fzf: memory allocation error\n");
      return;
    }
  }

  // Add the result
  FuzzyResult *result = &fuzzy_results.results[fuzzy_results.count++];
  strncpy(result->filename, filename, MAX_PATH - 1);
  result->filename[MAX_PATH - 1] = '\0';

  strncpy(result->display_name, display_name, MAX_PATH - 1);
  result->display_name[MAX_PATH - 1] = '\0';

  result->score = score;

  strncpy(result->match_positions, match_positions, MAX_PATH - 1);
  result->match_positions[MAX_PATH - 1] = '\0';
}

/**
 * Free the fuzzy results list
 */
static void free_fuzzy_results(void) {
  if (fuzzy_results.results) {
    free(fuzzy_results.results);
    fuzzy_results.results = NULL;
  }
  fuzzy_results.count = 0;
  fuzzy_results.capacity = 0;
  fuzzy_results.current_index = 0;
  fuzzy_results.is_active = FALSE;
  fuzzy_results.pattern[0] = '\0';
}

/**
 * Search a directory for files with fuzzy name matching
 */
static void search_directory_fuzzy(const char *directory, const char *pattern,
                                   int recursive) {
  char search_path[MAX_PATH];
  WIN32_FIND_DATA findData;
  HANDLE hFind;

  // Prepare search pattern for all files in directory
  snprintf(search_path, sizeof(search_path), "%s\\*", directory);

  // Start finding files
  hFind = FindFirstFile(search_path, &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    printf("fzf: %s: Cannot access directory\n", directory);
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

    // Create display name (relative to search root)
    char display_name[MAX_PATH];
    if (strcmp(directory, ".") == 0) {
      strcpy(display_name, findData.cFileName);
    } else {
      // Strip leading ".\" if present
      if (strncmp(directory, ".\\", 2) == 0) {
        snprintf(display_name, sizeof(display_name), "%s\\%s", directory + 2,
                 findData.cFileName);
      } else {
        snprintf(display_name, sizeof(display_name), "%s\\%s", directory,
                 findData.cFileName);
      }
    }

    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // It's a directory - recursively search if option is set
      if (recursive) {
        search_directory_fuzzy(full_path, pattern, recursive);
      }
    } else {
      // It's a file - perform fuzzy matching if pattern is provided
      if (pattern[0] != '\0') {
        char match_positions[MAX_PATH] = {0};
        int score = fuzzy_match(pattern, findData.cFileName, match_positions);

        // If it's a match, add to results
        if (score >= 0) {
          add_fuzzy_result(full_path, display_name, score, match_positions);
        }
      } else {
        // No pattern - add all files
        char match_positions[MAX_PATH] = {0}; // Empty match positions
        add_fuzzy_result(full_path, display_name, 0, match_positions);
      }
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);
}

/**
 * Custom comparison function for sorting fuzzy results by score (descending)
 */
static int compare_fuzzy_results(const void *a, const void *b) {
  const FuzzyResult *resultA = (const FuzzyResult *)a;
  const FuzzyResult *resultB = (const FuzzyResult *)b;

  // Sort by score (descending order)
  return resultB->score - resultA->score;
}

/**
 * Print a file preview for the selected result
 */
static void print_file_preview(HANDLE hConsole, const char *filename,
                               WORD originalAttrs, int right_width,
                               int left_width) {
  // Try to open the file
  FILE *file = fopen(filename, "r");
  if (!file) {
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("<Could not open file>");
    return;
  }

  // Determine file type for syntax highlighting
  int type = get_file_type(filename);

  // Calculate number of lines to show
  int preview_lines = 30; // Show more lines in preview

  // Get current cursor position - this is where the preview starts
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  int start_x = csbi.dwCursorPosition.X;
  int start_y = csbi.dwCursorPosition.Y;

  // Read and print the file content with syntax highlighting
  char line[4096];
  int line_number = 1;
  int preview_line = 0;

  while (fgets(line, sizeof(line), file) && preview_line < preview_lines) {
    // Remove newline if present
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }

    // Skip empty lines at the beginning
    if (line_number == 1 && len == 0) {
      continue;
    }

    // Position cursor for this line in the preview area
    SetConsoleCursorPosition(hConsole,
                             (COORD){start_x, start_y + preview_line});

    // Print line number
    printf("%4d  ", line_number++);

    // Apply syntax highlighting based on file type
    switch (type) {
    case FZF_FILE_TYPE_C:
    case FZF_FILE_TYPE_CPP:
    case FZF_FILE_TYPE_H:
      // Simplified syntax highlighting for preview
      {
        BOOL in_comment = FALSE;
        BOOL in_string = FALSE;

        for (size_t i = 0; i < len; i++) {
          if (in_comment) {
            set_color(COLOR_COMMENT);
            putchar(line[i]);
            if (line[i] == '*' && i + 1 < len && line[i + 1] == '/') {
              putchar(line[++i]);
              in_comment = FALSE;
              reset_color();
            }
          } else if (in_string) {
            set_color(COLOR_STRING);
            putchar(line[i]);
            if (line[i] == '"' && (i == 0 || line[i - 1] != '\\')) {
              in_string = FALSE;
              reset_color();
            }
          } else if (line[i] == '/' && i + 1 < len && line[i + 1] == '*') {
            in_comment = TRUE;
            set_color(COLOR_COMMENT);
            putchar(line[i]);
            putchar(line[++i]);
          } else if (line[i] == '/' && i + 1 < len && line[i + 1] == '/') {
            set_color(COLOR_COMMENT);
            printf("%s", line + i);
            break;
          } else if (line[i] == '"') {
            in_string = TRUE;
            set_color(COLOR_STRING);
            putchar(line[i]);
          } else if (line[i] == '#' && (i == 0 || isspace(line[i - 1]))) {
            // Preprocessor directive
            set_color(COLOR_PREPROCESSOR);
            putchar(line[i]);
          } else {
            putchar(line[i]);
          }
        }

        reset_color();
      }
      break;

    case FZF_FILE_TYPE_PY:
    case FZF_FILE_TYPE_JS:
    case FZF_FILE_TYPE_HTML:
    case FZF_FILE_TYPE_CSS:
    case FZF_FILE_TYPE_MD:
    case FZF_FILE_TYPE_JSON:
      // More specific highlighting could be implemented
      // For now, just print in default color
      printf("%s", line);
      break;

    default:
      // No syntax highlighting for unknown file types
      printf("%s", line);
      break;
    }

    // Move to next line in preview area (don't use printf("\n"))
    preview_line++;
  }

  fclose(file);
}

/**
 * Display fuzzy search results with interactive navigation
 */
static void display_fuzzy_results(void) {
  // Sort results by score
  qsort(fuzzy_results.results, fuzzy_results.count, sizeof(FuzzyResult),
        compare_fuzzy_results);

  if (fuzzy_results.count == 0) {
    return;
  }

  // Get handles and initial settings
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  WORD originalAttrs;

  // Save original console attributes and mode
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  originalAttrs = csbi.wAttributes;
  DWORD originalMode;
  GetConsoleMode(hStdin, &originalMode);
  SetConsoleMode(hStdin, ENABLE_PROCESSED_INPUT);

  // Save original cursor info
  CONSOLE_CURSOR_INFO originalCursorInfo;
  GetConsoleCursorInfo(hConsole, &originalCursorInfo);

  // Initialize results viewer
  fuzzy_results.current_index = 0;
  fuzzy_results.is_active = TRUE;

  // Initial screen clear
  system("cls");

  // Previous selection index to track what needs updating
  int previous_index = -1;

  // Search filter
  char search_filter[256] = "";
  int filter_pos = 0;
  BOOL filter_active = FALSE;

  // Filtered results
  FuzzyResult *filtered_results = NULL;
  int filtered_count = 0;
  int filtered_capacity = 0;

  // Main navigation loop
  while (fuzzy_results.is_active) {
    // Hide cursor during drawing to prevent jumping
    CONSOLE_CURSOR_INFO cursorInfo = {1, FALSE}; // Size 1, invisible
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Get current console dimensions
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    // Layout calculations
    int left_width = 40; // Fixed width for file list
    if (left_width > console_width / 3)
      left_width = console_width / 3;

    int right_width = console_width - left_width - 3; // Space for separators
    int list_height = console_height -
                      9; // Reserve space for headers, footers, and search bar
    if (list_height < 5)
      list_height = 5; // Minimum reasonable size

    // Calculate preview area position
    int preview_top = 6; // After header and search bar
    int preview_height = list_height - 1;

    // Only redraw everything if dimensions changed or first time
    BOOL full_redraw = (previous_index == -1);

    if (full_redraw) {
      // Draw header
      SetConsoleCursorPosition(hConsole, (COORD){0, 0});
      SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
      printf("Fuzzy File Finder (%d files)", fuzzy_results.count);

      // Move to next line for search filter
      SetConsoleCursorPosition(hConsole, (COORD){0, 1});
      SetConsoleTextAttribute(hConsole, COLOR_BOX);
      printf("Search: ");
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s", search_filter);

      // Move to next line for separator
      SetConsoleCursorPosition(hConsole, (COORD){0, 2});
      SetConsoleTextAttribute(hConsole, COLOR_BOX);
      for (int i = 0; i < console_width; i++)
        printf("─");

      printf("\n\n");
    }

    // Apply search filter if active
    if (filter_active && search_filter[0] != '\0') {
      // Apply filter to original results
      // First free any previous filtered results
      if (filtered_results) {
        free(filtered_results);
        filtered_results = NULL;
      }

      filtered_count = 0;
      filtered_capacity = fuzzy_results.count > 0 ? fuzzy_results.count : 10;
      filtered_results =
          (FuzzyResult *)malloc(filtered_capacity * sizeof(FuzzyResult));

      if (filtered_results) {
        // Filter results based on search term
        for (int i = 0; i < fuzzy_results.count; i++) {
          // Case insensitive search
          if (my_strcasestr(fuzzy_results.results[i].display_name,
                            search_filter)) {
            filtered_results[filtered_count++] = fuzzy_results.results[i];
          }
        }

        // Reset current index if it's now out of bounds
        if (fuzzy_results.current_index >= filtered_count) {
          fuzzy_results.current_index = filtered_count > 0 ? 0 : -1;
        }
      }
    } else {
      // Not filtering - free any filtered results
      if (filtered_results) {
        free(filtered_results);
        filtered_results = NULL;
        filtered_count = 0;
      }
    }

    // Get the effective results array and count
    FuzzyResult *effective_results =
        filtered_results ? filtered_results : fuzzy_results.results;
    int effective_count =
        filtered_results ? filtered_count : fuzzy_results.count;

    // Calculate visible range
    int visible_items =
        list_height < effective_count ? list_height : effective_count;
    int start_index = 0;

    // Adjust start index to keep selection in view
    if (fuzzy_results.current_index >= list_height) {
      start_index = fuzzy_results.current_index - list_height + 1;

      // Ensure we don't go past the end
      if (start_index + visible_items > effective_count) {
        start_index = effective_count - visible_items;
        if (start_index < 0)
          start_index = 0;
      }
    }

    // If the visible range changed, we need a full redraw
    static int previous_start_index = -1;
    if (previous_start_index != start_index) {
      full_redraw = TRUE;
      previous_start_index = start_index;
    }

    // Only redraw the list if needed
    if (full_redraw || previous_index != fuzzy_results.current_index) {
      // If just changing selection (not a full redraw), only update the
      // affected items
      if (!full_redraw && previous_index != -1 &&
          previous_index < effective_count) {
        // First, update the previously selected item to remove highlight
        if (previous_index >= start_index &&
            previous_index < start_index + visible_items) {
          int prev_display_idx = previous_index - start_index;
          // Position cursor for previous selection
          SetConsoleCursorPosition(hConsole, (COORD){0, 3 + prev_display_idx});

          // Normal color for previously selected item
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("  "); // Remove arrow

          // Extract just the filename from path
          char *filename = effective_results[previous_index].filename;
          char *last_slash = strrchr(filename, '\\');
          if (last_slash) {
            filename = last_slash + 1;
          }

          // Display filename without highlight
          char display_name[40] = {0};
          strncpy(display_name, filename, sizeof(display_name) - 1);
          if (strlen(display_name) > (size_t)(left_width - 10)) {
            display_name[left_width - 13] = '.';
            display_name[left_width - 12] = '.';
            display_name[left_width - 11] = '.';
            display_name[left_width - 10] = '\0';
          }

          printf("%-*s", left_width - 2, display_name);
        }

        // Now update the newly selected item to add highlight
        if (fuzzy_results.current_index >= start_index &&
            fuzzy_results.current_index < start_index + visible_items) {
          int curr_display_idx = fuzzy_results.current_index - start_index;
          // Position cursor for new selection
          SetConsoleCursorPosition(hConsole, (COORD){0, 3 + curr_display_idx});

          // Highlight new selection
          SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
          printf("→ ");

          // Extract just the filename from path
          char *filename =
              effective_results[fuzzy_results.current_index].filename;
          char *last_slash = strrchr(filename, '\\');
          if (last_slash) {
            filename = last_slash + 1;
          }

          // Display filename with highlight
          char display_name[40] = {0};
          strncpy(display_name, filename, sizeof(display_name) - 1);
          if (strlen(display_name) > (size_t)(left_width - 10)) {
            display_name[left_width - 13] = '.';
            display_name[left_width - 12] = '.';
            display_name[left_width - 11] = '.';
            display_name[left_width - 10] = '\0';
          }

          printf("%-*s", left_width - 2, display_name);
        }
      } else {
        // Full redraw of the file list is needed
        // Hide cursor during full redraw to reduce flicker
        CONSOLE_CURSOR_INFO cursorInfo = {1, FALSE}; // Size 1, invisible
        SetConsoleCursorInfo(hConsole, &cursorInfo);

        // Draw the file list first (complete separate entity)
        for (int i = 0; i < visible_items; i++) {
          int result_idx = start_index + i;
          if (result_idx >= effective_count)
            break;

          FuzzyResult *result = &effective_results[result_idx];

          // Position cursor for this list item
          SetConsoleCursorPosition(hConsole, (COORD){0, 3 + i});

          // Extract just the filename from path
          char *filename = result->filename;
          char *last_slash = strrchr(filename, '\\');
          if (last_slash) {
            filename = last_slash + 1;
          }

          // Truncate filename if too long
          char display_name[40] = {0};
          strncpy(display_name, filename, sizeof(display_name) - 1);
          if (strlen(display_name) > (size_t)(left_width - 10)) {
            display_name[left_width - 13] = '.';
            display_name[left_width - 12] = '.';
            display_name[left_width - 11] = '.';
            display_name[left_width - 10] = '\0';
          }

          // Highlight current selection
          if (result_idx == fuzzy_results.current_index) {
            SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
            printf("→ ");
          } else {
            SetConsoleTextAttribute(hConsole, originalAttrs);
            printf("  ");
          }

          // Print filename and line number
          printf("%-*s", left_width - 2, display_name);
        }

        // Fill remaining list area with empty lines
        for (int i = visible_items; i < list_height; i++) {
          SetConsoleCursorPosition(hConsole, (COORD){0, 3 + i});
          printf("%-*s", left_width, "");
        }

        // Draw the vertical separator line for the entire height
        SetConsoleTextAttribute(hConsole, COLOR_BOX);
        for (int i = 0; i < list_height; i++) {
          SetConsoleCursorPosition(hConsole, (COORD){left_width, 3 + i});
          printf(" │ ");
        }
      }

      // Clear preview area first
      SetConsoleTextAttribute(hConsole, originalAttrs);
      for (int i = 0; i < preview_height; i++) {
        SetConsoleCursorPosition(hConsole,
                                 (COORD){left_width + 3, preview_top + i});
        printf("%-*s", right_width, "");
      }

      // Get current result's filename for display
      FuzzyResult *current = &effective_results[fuzzy_results.current_index];

      // Extract directory and filename components
      char *filename = current->filename;
      char dirAndFile[MAX_PATH] = {0};

      // Find the last backslash
      char *lastSlash = strrchr(filename, '\\');
      if (lastSlash) {
        // Get the filename part (after last backslash)
        char *filenamePart = lastSlash + 1;

        // Get directory part (everything before last backslash)
        char dirPart[MAX_PATH] = {0};
        int dirLen = (int)(lastSlash - filename);
        strncpy(dirPart, filename, dirLen);
        dirPart[dirLen] = '\0';

        // Find the last directory name (after the second-last backslash)
        char *secondLastSlash = strrchr(dirPart, '\\');
        if (secondLastSlash) {
          // Use just the last directory name
          snprintf(dirAndFile, sizeof(dirAndFile), "%s/%s", secondLastSlash + 1,
                   filenamePart);
        } else {
          // Use the entire directory part
          snprintf(dirAndFile, sizeof(dirAndFile), "%s/%s", dirPart,
                   filenamePart);
        }
      } else {
        // No directory separator found, just use filename
        snprintf(dirAndFile, sizeof(dirAndFile), "%s", filename);
      }

      // Clear the entire line first
      SetConsoleCursorPosition(hConsole,
                               (COORD){left_width + 3, preview_top - 2});
      // Print enough spaces to clear any previous content
      for (int i = 0; i < right_width; i++) {
        printf(" ");
      }

      // Draw file path first - now with cleared line
      SetConsoleCursorPosition(hConsole,
                               (COORD){left_width + 3, preview_top - 2});
      SetConsoleTextAttribute(hConsole, COLOR_INFO);
      printf("File: %s", dirAndFile);

      // Empty line between path and preview title
      SetConsoleCursorPosition(hConsole,
                               (COORD){left_width + 3, preview_top - 1});
      printf(" ");

      // Draw preview title
      SetConsoleCursorPosition(hConsole, (COORD){left_width + 3, preview_top});
      SetConsoleTextAttribute(hConsole, COLOR_INFO);
      printf("Preview:");

      // Show preview for current item in dedicated area
      if (fuzzy_results.current_index >= 0 &&
          fuzzy_results.current_index < effective_count) {
        // Position for preview content
        SetConsoleCursorPosition(hConsole,
                                 (COORD){left_width + 3, preview_top + 1});

        // Reset text color for preview
        SetConsoleTextAttribute(hConsole, originalAttrs);

        // Print file preview with appropriate syntax highlighting
        print_file_preview(hConsole, current->filename, originalAttrs,
                           right_width, left_width);
      }

      // Remember what we just displayed
      previous_index = fuzzy_results.current_index;
    }

    // Always update search bar if active
    if (filter_active) {
      SetConsoleCursorPosition(hConsole,
                               (COORD){8, 1}); // Position after "Search: "
      // Clear the line
      for (int i = 0; i < console_width - 8; i++) {
        printf(" ");
      }
      SetConsoleCursorPosition(hConsole, (COORD){8, 1});
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s", search_filter);
    }

    // Status line at bottom
    SetConsoleCursorPosition(hConsole, (COORD){0, 4 + list_height});
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    for (int i = 0; i < console_width; i++)
      printf("─");

    // Navigation help
    SetConsoleCursorPosition(hConsole, (COORD){0, 5 + list_height});
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("Navigation: ↓/j - Next  ↑/k - Prev  Enter - Open  o - Full view / "
           "- Search  "
           "Esc/q - Exit");

    // Make cursor visible for search input
    if (filter_active) {
      cursorInfo.bVisible = TRUE;
      SetConsoleCursorInfo(hConsole, &cursorInfo);
      SetConsoleCursorPosition(hConsole, (COORD){8 + filter_pos, 1});
    } else {
      cursorInfo.bVisible = FALSE;
      SetConsoleCursorInfo(hConsole, &cursorInfo);
    }

    // Process input
    INPUT_RECORD inputRecord;
    DWORD numEvents;

    if (WaitForSingleObject(hStdin, INFINITE) == WAIT_OBJECT_0) {
      ReadConsoleInput(hStdin, &inputRecord, 1, &numEvents);

      if (numEvents > 0 && inputRecord.EventType == KEY_EVENT &&
          inputRecord.Event.KeyEvent.bKeyDown) {

        WORD keyCode = inputRecord.Event.KeyEvent.wVirtualKeyCode;

        // Handle input mode for search filter
        if (filter_active) {
          if (keyCode == VK_ESCAPE) {
            // Exit search mode
            filter_active = FALSE;
          } else if (keyCode == VK_RETURN) {
            // Apply filter and exit search mode
            filter_active = FALSE;
          } else if (keyCode == VK_BACK) {
            // Backspace - remove character
            if (filter_pos > 0) {
              memmove(&search_filter[filter_pos - 1],
                      &search_filter[filter_pos],
                      strlen(search_filter) - filter_pos + 1);
              filter_pos--;
            }
          } else {
            // Add character to filter
            char c = inputRecord.Event.KeyEvent.uChar.AsciiChar;
            if (isprint(c) && filter_pos < sizeof(search_filter) - 1) {
              memmove(&search_filter[filter_pos + 1],
                      &search_filter[filter_pos],
                      strlen(search_filter) - filter_pos + 1);
              search_filter[filter_pos] = c;
              filter_pos++;
            }
          }

          // Force redraw when filtering
          full_redraw = TRUE;
          previous_index = -1;
          continue;
        }

        // Normal navigation keys
        switch (keyCode) {
        case 'J':
        case VK_DOWN:
          // Down arrow or J - Next result
          if (effective_count > 0 &&
              fuzzy_results.current_index < effective_count - 1) {
            fuzzy_results.current_index++;
          }
          break;

        case 'K':
        case VK_UP:
          // Up arrow or K - Previous result
          if (fuzzy_results.current_index > 0) {
            fuzzy_results.current_index--;
          }
          break;

        case VK_RETURN:
          // ENTER - Open in editor
          if (fuzzy_results.current_index >= 0 &&
              fuzzy_results.current_index < effective_count) {
            open_file_in_editor(
                effective_results[fuzzy_results.current_index].filename, 1);
            // Need a full redraw after returning from editor
            full_redraw = TRUE;
            previous_index = -1;
            system("cls");
          }
          break;

        case 'O':
          // 'O' - Show detail view
          if (fuzzy_results.current_index >= 0 &&
              fuzzy_results.current_index < effective_count) {
            show_file_detail_view(
                &effective_results[fuzzy_results.current_index]);
            // Need a full redraw after returning from detail view
            full_redraw = TRUE;
            previous_index = -1;
            system("cls");
          }
          break;

        case VK_OEM_2: // '/' key
        case 'S':      // 's' for search
          // Activate search filter
          filter_active = TRUE;

          // If there's already filter text, position cursor at the end
          filter_pos = strlen(search_filter);

          // Force redraw
          full_redraw = TRUE;
          previous_index = -1;
          break;

        case VK_ESCAPE:
        case 'Q':
          // Escape or Q - Exit
          fuzzy_results.is_active = FALSE;
          break;
        }
      }
    }
  }

  // Final cleanup
  if (filtered_results) {
    free(filtered_results);
  }

  SetConsoleTextAttribute(hConsole, originalAttrs);
  SetConsoleMode(hStdin, originalMode);

  // Restore original cursor info
  SetConsoleCursorInfo(hConsole, &originalCursorInfo);

  system("cls");
}

/**
 * Open the file in an appropriate editor
 */
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
    printf("Opening %s...\n", file_path);

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

/**
 * Show detailed view of a file
 */
static void show_file_detail_view(FuzzyResult *result) {
  // Get handle to console
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD originalAttrs;

  // Save original console attributes
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
  originalAttrs = consoleInfo.wAttributes;

  // Clear the screen
  system("cls");

  // Show file information header
  SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
  printf("File: %s\n\n", result->filename);

  // Show file content with more detail
  FILE *file = fopen(result->filename, "r");
  if (file) {
    // Show the first 40 lines of the file with line numbers
    char line[4096];
    int line_number = 1;
    int max_lines = 40;

    // Find file type for syntax highlighting
    int file_type = get_file_type(result->filename);

    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("File content:\n\n");

    while (fgets(line, sizeof(line), file) && line_number <= max_lines) {
      // Remove newline if present
      size_t len = strlen(line);
      if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len--;
      }

      // Print line number
      printf("%4d  ", line_number++);

      // You could apply more sophisticated syntax highlighting here
      // based on file_type, similar to what we do in the preview function
      // For simplicity, we're just using normal color here
      printf("%s\n", line);
    }

    fclose(file);
  } else {
    // Could not open file
    SetConsoleTextAttribute(hConsole, COLOR_MATCH);
    printf("Could not open file for preview\n");
  }

  // Show options
  printf("\n");
  SetConsoleTextAttribute(hConsole, COLOR_BOX);
  printf("Press ENTER to open in editor, any other key to return to results "
         "view...");

  // Get user input
  int key = _getch();

  // If ENTER pressed, open in editor
  if (key == 13) {
    open_file_in_editor(result->filename, 1);
  }

  // Reset console color
  SetConsoleTextAttribute(hConsole, originalAttrs);
}
