/**
 * fzf.c
 * Implementation of fuzzy file finder with interactive navigation
 */

#include "fzf.h"
#include "builtins.h"
#include <minwindef.h>
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

#define MAX_PREVIEW_CACHE 10

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

// Global filtered results variables
static FuzzyResult *filtered_results = NULL;
static int filtered_count = 0;
static int filtered_capacity = 0;

typedef struct {
  char filename[MAX_PATH];
  char **lines;
  int line_count;
  int file_type;
  DWORD last_accessed;
} PreviewCache;

static PreviewCache preview_cache[MAX_PREVIEW_CACHE];
static int cache_count = 0;

// Global result list
static FuzzyResultList fuzzy_results = {0};

static int get_file_type(const char *filename);

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

static void apply_search_filter(HANDLE hConsole, const char *search_filter,
                                int preview_top, int left_width,
                                BOOL *full_redraw, int *previous_index);

/**
 * Case-insensitive substring search function
 */
static char *fzf_strcasestr(const char *haystack, const char *needle) {
  if (!haystack || !needle)
    return NULL;

  size_t needle_len = strlen(needle);
  if (needle_len == 0)
    return (char *)haystack;

  size_t haystack_len = strlen(haystack);
  if (haystack_len < needle_len)
    return NULL;

  for (size_t i = 0; i <= haystack_len - needle_len; i++) {
    if (_strnicmp(haystack + i, needle, needle_len) == 0) {
      return (char *)(haystack + i);
    }
  }

  return NULL;
}

static PreviewCache *get_cached_preview(const char *filename) {
  int i;
  DWORD current_time = GetTickCount();

  // First check if preview is already cached
  for (i = 0; i < cache_count; i++) {
    if (_stricmp(preview_cache[i].filename, filename) == 0) {
      // Update last accessed time
      preview_cache[i].last_accessed = current_time;
      return &preview_cache[i];
    }
  }

  // Not found in cache, load it

  // If cache is full, replace least recently used entry
  if (cache_count >= MAX_PREVIEW_CACHE) {
    int oldest_idx = 0;
    DWORD oldest_time = preview_cache[0].last_accessed;

    for (i = 1; i < cache_count; i++) {
      if (preview_cache[i].last_accessed < oldest_time) {
        oldest_idx = i;
        oldest_time = preview_cache[i].last_accessed;
      }
    }

    // Free the older cache entry
    if (preview_cache[oldest_idx].lines) {
      for (i = 0; i < preview_cache[oldest_idx].line_count; i++) {
        free(preview_cache[oldest_idx].lines[i]);
      }
      free(preview_cache[oldest_idx].lines);
    }

    // Reuse this slot
    i = oldest_idx;
  } else {
    // Use next available slot
    i = cache_count++;
  }

  // Initialize new cache entry
  strncpy(preview_cache[i].filename, filename, MAX_PATH - 1);
  preview_cache[i].filename[MAX_PATH - 1] = '\0';
  preview_cache[i].file_type = get_file_type(filename);
  preview_cache[i].last_accessed = current_time;
  preview_cache[i].lines = NULL;
  preview_cache[i].line_count = 0;

  // Load file content into cache
  FILE *file = fopen(filename, "r");
  if (!file) {
    return &preview_cache[i]; // Return empty cache entry
  }

  // Allocate initial lines array (start with space for 50 lines)
  int capacity = 50;
  preview_cache[i].lines = (char **)malloc(capacity * sizeof(char *));
  if (!preview_cache[i].lines) {
    fclose(file);
    return &preview_cache[i]; // Return empty cache entry on allocation failure
  }

  // Read file line by line
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), file)) {
    // Remove newline if present
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
      len--;
    }

    // Expand capacity if needed
    if (preview_cache[i].line_count >= capacity) {
      capacity *= 2;
      char **new_lines =
          (char **)realloc(preview_cache[i].lines, capacity * sizeof(char *));
      if (!new_lines) {
        // Failed to expand, just use what we have
        break;
      }
      preview_cache[i].lines = new_lines;
    }

    // Store the line
    preview_cache[i].lines[preview_cache[i].line_count] = _strdup(buffer);
    preview_cache[i].line_count++;
  }

  fclose(file);
  return &preview_cache[i];
}

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

static void print_file_preview_optimized(HANDLE hConsole, const char *filename,
                                         WORD originalAttrs, int right_width,
                                         int left_width,
                                         int max_preview_lines) {
  PreviewCache *cache = get_cached_preview(filename);

  // Handle empty cache or failed loading
  if (!cache || !cache->lines || cache->line_count == 0) {
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("<Could not load preview>");
    return;
  }

  // Get current cursor position - this is where the preview starts
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  int start_x = csbi.dwCursorPosition.X;
  int start_y = csbi.dwCursorPosition.Y;

  // Calculate number of lines to show
  int lines_to_show = cache->line_count < max_preview_lines ? cache->line_count
                                                            : max_preview_lines;

  // Get console width for text truncation
  int max_text_width = right_width - 8;
  if (max_text_width < 20)
    max_text_width = 20; // Minimum width

  // Hide cursor during drawing
  CONSOLE_CURSOR_INFO cursorInfo;
  GetConsoleCursorInfo(hConsole, &cursorInfo);
  BOOL originalCursorVisible = cursorInfo.bVisible;
  cursorInfo.bVisible = FALSE;
  SetConsoleCursorInfo(hConsole, &cursorInfo);

  // First, clear the entire preview area
  COORD clearPos;
  DWORD written;
  char clearBuf[1024];
  memset(clearBuf, ' ',
         right_width < sizeof(clearBuf) ? right_width : sizeof(clearBuf) - 1);
  clearBuf[right_width < sizeof(clearBuf) ? right_width
                                          : sizeof(clearBuf) - 1] = '\0';

  for (int i = 0; i < lines_to_show; i++) {
    SetConsoleCursorPosition(hConsole, (COORD){start_x, start_y + i});
    printf("%s", clearBuf);
  }

  // Prepare all formatted lines in memory first
  typedef struct {
    char *text;
    int has_comment;
    int comment_pos;
    char *comment_text;
  } FormattedLine;

  FormattedLine *formatted_lines =
      (FormattedLine *)malloc(lines_to_show * sizeof(FormattedLine));
  if (!formatted_lines) {
    // Restore cursor and return if allocation fails
    cursorInfo.bVisible = originalCursorVisible;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    return;
  }

  // First pass: prepare all the lines with formatting info
  for (int i = 0; i < lines_to_show; i++) {
    formatted_lines[i].text = (char *)malloc(max_text_width + 10);
    if (!formatted_lines[i].text) {
      formatted_lines[i].has_comment = 0;
      formatted_lines[i].comment_text = NULL;
      continue;
    }

    // Format line number
    sprintf(formatted_lines[i].text, "%4d  ", i + 1);

    // Get the line and truncate if needed
    char *line = cache->lines[i];
    size_t line_len = strlen(line);
    if ((int)line_len > max_text_width - 6) { // Account for line number format
      strncat(formatted_lines[i].text, line, max_text_width - 9);
      strcat(formatted_lines[i].text, "...");
      line_len = max_text_width - 6;
    } else {
      strcat(formatted_lines[i].text, line);
    }

    // Find comments in C/C++ and Python files
    formatted_lines[i].has_comment = 0;
    formatted_lines[i].comment_text = NULL;

    switch (cache->file_type) {
    case FZF_FILE_TYPE_C:
    case FZF_FILE_TYPE_CPP:
    case FZF_FILE_TYPE_H: {
      char *comment = strstr(line, "//");
      if (comment) {
        formatted_lines[i].has_comment = 1;
        formatted_lines[i].comment_pos =
            comment - line + 6; // +6 for line number format
        formatted_lines[i].comment_text = _strdup(comment);
      }
      break;
    }
    case FZF_FILE_TYPE_PY: {
      char *comment = strchr(line, '#');
      if (comment) {
        formatted_lines[i].has_comment = 1;
        formatted_lines[i].comment_pos =
            comment - line + 6; // +6 for line number format
        formatted_lines[i].comment_text = _strdup(comment);
      }
      break;
    }
    }
  }

  // Second pass: draw all the lines efficiently
  for (int i = 0; i < lines_to_show; i++) {
    if (!formatted_lines[i].text)
      continue;

    // Position cursor once per line
    SetConsoleCursorPosition(hConsole, (COORD){start_x, start_y + i});

    if (formatted_lines[i].has_comment && formatted_lines[i].comment_text) {
      // Print up to the comment
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%.*s", formatted_lines[i].comment_pos, formatted_lines[i].text);

      // Print comment in comment color
      set_color(COLOR_COMMENT);
      printf("%s", formatted_lines[i].comment_text);
      reset_color();
    } else {
      // No comment, print the whole line
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s", formatted_lines[i].text);
    }
  }

  // Clean up
  for (int i = 0; i < lines_to_show; i++) {
    if (formatted_lines[i].text) {
      free(formatted_lines[i].text);
    }
    if (formatted_lines[i].comment_text) {
      free(formatted_lines[i].comment_text);
    }
  }
  free(formatted_lines);

  // Restore cursor visibility
  cursorInfo.bVisible = originalCursorVisible;
  SetConsoleCursorInfo(hConsole, &cursorInfo);
}

/**
 * Apply search filter to results
 */
static void apply_search_filter(HANDLE hConsole, const char *search_filter,
                                int preview_top, int left_width,
                                BOOL *full_redraw, int *previous_index) {
  // Show "Filtering..." indicator during search
  if (search_filter[0] != '\0') {
    SetConsoleCursorPosition(hConsole,
                             (COORD){left_width + 3, preview_top + 1});
    SetConsoleTextAttribute(hConsole, COLOR_INFO);
    printf("Filtering...");
  }

  // Apply filter to original results
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
      if (search_filter[0] == '\0' ||
          fzf_strcasestr(fuzzy_results.results[i].display_name,
                         search_filter)) {
        filtered_results[filtered_count++] = fuzzy_results.results[i];
      }
    }

    // Reset current index if it's now out of bounds
    if (fuzzy_results.current_index >= filtered_count) {
      fuzzy_results.current_index = filtered_count > 0 ? 0 : -1;
    }
  }

  // Force full redraw after filter change
  *full_redraw = TRUE;
  *previous_index = -1;
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

  // Debounce search variables
  DWORD last_filter_change = 0;
  BOOL need_filter_update = FALSE;
  const DWORD FILTER_DELAY = 500; // 500ms debounce time

  // Tracking variables for UI optimization
  BOOL console_size_changed = TRUE; // Force full redraw first time
  int last_console_width = 0;
  int last_console_height = 0;

  // Last drawn preview file
  char last_preview_file[MAX_PATH] = "";
  DWORD last_preview_time = 0;
  const DWORD PREVIEW_THROTTLE = 100; // 100ms throttle for previews

  // Main navigation loop
  while (fuzzy_results.is_active) {
    // Get current time for debounce check
    DWORD current_time = GetTickCount();
    BOOL should_apply_filter =
        need_filter_update &&
        (current_time - last_filter_change >= FILTER_DELAY);

    // Hide cursor during drawing to prevent jumping
    CONSOLE_CURSOR_INFO cursorInfo = {1, FALSE}; // Size 1, invisible
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Get current console dimensions
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    // Check if console size changed
    if (console_width != last_console_width ||
        console_height != last_console_height) {
      console_size_changed = TRUE;
      last_console_width = console_width;
      last_console_height = console_height;
    }

    // Layout calculations
    int left_width = 40; // Fixed width for file list
    if (left_width > console_width / 3)
      left_width = console_width / 3;

    int right_width = console_width - left_width - 3; // Space for separators
    int list_height =
        console_height - 9; // Reserve space for headers, footers, search
    if (list_height < 5)
      list_height = 5; // Minimum reasonable size

    // Calculate preview area position
    int preview_top = 6; // After header and search bar
    int preview_height = list_height - 1;
    int max_preview_lines = 20; // Limit preview size for performance

    // Only redraw everything if dimensions changed or first time
    BOOL full_redraw = (previous_index == -1) || console_size_changed;
    console_size_changed = FALSE;

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
        printf("-");

      printf("\n\n");
    }

    // Apply search filter if enough time has passed since last keystroke
    if (should_apply_filter) {
      apply_search_filter(hConsole, search_filter, preview_top, left_width,
                          &full_redraw, &previous_index);
      need_filter_update = FALSE;
    }

    // If we're not actively filtering and search is empty, use original results
    if (!filter_active && search_filter[0] == '\0' && filtered_results) {
      free(filtered_results);
      filtered_results = NULL;
      filtered_count = 0;
      full_redraw = TRUE;
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
      // Draw the file list first
      for (int i = 0; i < visible_items; i++) {
        int result_idx = start_index + i;
        FuzzyResult *result = &effective_results[result_idx];

        // Position cursor for this list item
        SetConsoleCursorPosition(hConsole, (COORD){0, 4 + i});

        // Clear this line
        for (int j = 0; j < left_width; j++) {
          printf(" ");
        }
        SetConsoleCursorPosition(hConsole, (COORD){0, 4 + i});

        // Highlight current selection
        if (result_idx == fuzzy_results.current_index) {
          SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
          printf("-> ");
        } else {
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("   ");
        }

        // Print filename with match highlight if applicable
        if (result->match_positions[0] != '\0') {
          // Display with match highlighting
          for (int j = 0; j < strlen(result->display_name); j++) {
            if (result->match_positions[j] == 1) {
              // Highlighted character
              SetConsoleTextAttribute(hConsole, COLOR_MATCH);
            } else {
              // Normal character
              SetConsoleTextAttribute(hConsole, originalAttrs);
            }
            printf("%c", result->display_name[j]);
          }
        } else {
          // No match info, display normally
          printf("%s", result->display_name);
        }

        // Reset color
        SetConsoleTextAttribute(hConsole, originalAttrs);
      }

      // Fill any unused list rows with blank space
      for (int i = visible_items; i < list_height; i++) {
        SetConsoleCursorPosition(hConsole, (COORD){0, 4 + i});
        for (int j = 0; j < left_width; j++) {
          printf(" ");
        }
      }

      // Draw vertical separator
      SetConsoleTextAttribute(hConsole, COLOR_BOX);
      for (int i = 0; i < list_height; i++) {
        SetConsoleCursorPosition(hConsole, (COORD){left_width, 4 + i});
        printf(" | ");
      }

      // Draw file preview if we have a selected file
      if (fuzzy_results.current_index >= 0 &&
          fuzzy_results.current_index < effective_count) {

        FuzzyResult *current = &effective_results[fuzzy_results.current_index];

        // Only update preview if the file changed or it's been a while since
        // last update
        if (strcmp(current->filename, last_preview_file) != 0 ||
            (current_time - last_preview_time) > PREVIEW_THROTTLE) {

          // Clear preview area
          for (int i = 0; i < preview_height; i++) {
            SetConsoleCursorPosition(hConsole,
                                     (COORD){left_width + 3, preview_top + i});
            for (int j = 0; j < right_width; j++) {
              printf(" ");
            }
          }

          // Show file path
          SetConsoleCursorPosition(hConsole,
                                   (COORD){left_width + 3, preview_top});
          SetConsoleTextAttribute(hConsole, COLOR_INFO);
          printf("File: %s", current->filename);

          // Show preview
          SetConsoleCursorPosition(hConsole,
                                   (COORD){left_width + 3, preview_top + 2});
          SetConsoleTextAttribute(hConsole, originalAttrs);
          print_file_preview_optimized(hConsole, current->filename,
                                       originalAttrs, right_width, left_width,
                                       max_preview_lines);

          // Remember this file
          strcpy(last_preview_file, current->filename);
          last_preview_time = current_time;
        }
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
      printf("-");

    // Show file count info
    SetConsoleCursorPosition(hConsole, (COORD){2, 3 + list_height});
    if (filtered_results) {
      printf("Showing %d of %d files", filtered_count, fuzzy_results.count);
    } else {
      printf("Showing %d files", fuzzy_results.count);
    }

    // Navigation help
    SetConsoleCursorPosition(hConsole, (COORD){0, 5 + list_height});
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("Navigation: j/DOWN - Next  k/UP - Prev  ENTER - Open  o - Full "
           "view  / - Search  ESC/q - Exit");

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
            need_filter_update = TRUE;
            last_filter_change =
                GetTickCount() - FILTER_DELAY * 2; // Force immediate update
          } else if (keyCode == VK_RETURN) {
            // Apply filter and exit search mode
            filter_active = FALSE;
            need_filter_update = TRUE;
            last_filter_change =
                GetTickCount() - FILTER_DELAY * 2; // Force immediate update
          } else if (keyCode == VK_BACK) {
            // Backspace - remove character
            if (filter_pos > 0) {
              // Update the filter string
              memmove(&search_filter[filter_pos - 1],
                      &search_filter[filter_pos],
                      strlen(search_filter) - filter_pos + 1);
              filter_pos--;

              // Update the search bar immediately
              SetConsoleCursorPosition(hConsole, (COORD){8, 1});
              SetConsoleTextAttribute(hConsole, originalAttrs);
              printf("%-*s", console_width - 9, search_filter);
              SetConsoleCursorPosition(hConsole, (COORD){8 + filter_pos, 1});

              // Mark for filter update but debounce
              need_filter_update = TRUE;
              last_filter_change = GetTickCount();
            }
          } else {
            // Add character to filter
            char c = inputRecord.Event.KeyEvent.uChar.AsciiChar;
            if (isprint(c) && filter_pos < sizeof(search_filter) - 1) {
              // Update the filter string
              memmove(&search_filter[filter_pos + 1],
                      &search_filter[filter_pos],
                      strlen(search_filter) - filter_pos + 1);
              search_filter[filter_pos] = c;
              filter_pos++;

              // Update the search bar immediately
              SetConsoleCursorPosition(hConsole, (COORD){8, 1});
              SetConsoleTextAttribute(hConsole, originalAttrs);
              printf("%-*s", console_width - 9, search_filter);
              SetConsoleCursorPosition(hConsole, (COORD){8 + filter_pos, 1});

              // Mark for filter update but debounce
              need_filter_update = TRUE;
              last_filter_change = GetTickCount();
            }
          }

          // Skip the rest of the loop to avoid navigation handling
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

  // Reset console attributes
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

/**
 * Add cleanup function to free the preview cache when the program exits
 */
void cleanup_preview_cache(void) {
  for (int i = 0; i < cache_count; i++) {
    if (preview_cache[i].lines) {
      for (int j = 0; j < preview_cache[i].line_count; j++) {
        free(preview_cache[i].lines[j]);
      }
      free(preview_cache[i].lines);
    }
  }
  cache_count = 0;
}
