/**
 * grep.c
 * Fast implementation of text searching in files with Boyer-Moore algorithm
 */

#include "grep.h"
#include "builtins.h"
#include <ctype.h>
#include <process.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// Define color codes for highlighting matches
#define COLOR_MATCH FOREGROUND_RED | FOREGROUND_INTENSITY
#define COLOR_INFO FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY
#define COLOR_RESULT_HIGHLIGHT FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_BOX                                                              \
  FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY

// Configuration constants
#define MAX_BUFFER_SIZE (1024 * 1024) // 1MB read buffer
#define MAX_LINE_LENGTH 8192          // Max line length to process
#define MAX_PREVIEW_LINES 10          // Number of context lines to show
#define MAX_THREAD_COUNT 8            // Maximum number of threads to use

// Search mode configuration
typedef enum {
  SEARCH_MODE_PLAIN,       // Plain string matching (case sensitive)
  SEARCH_MODE_IGNORE_CASE, // String matching (case insensitive)
  SEARCH_MODE_FUZZY        // Fuzzy matching (for approximate matches)
} SearchMode;

// Structure to hold a single grep result
typedef struct {
  char filename[MAX_PATH];            // File containing the match
  int line_number;                    // Line number in the file
  char line_content[MAX_LINE_LENGTH]; // Content of the line
  int match_start;                    // Position where match begins in the line
  int match_length;                   // Length of the match
  double match_score; // Score for fuzzy matches (higher is better)
} GrepResult;

// Structure to hold all grep results
typedef struct {
  GrepResult *results; // Array of results
  int count;           // Number of results
  int capacity;        // Allocated capacity
  int current_index;   // Currently displayed result index
  BOOL is_active;      // Whether results view is active
} GrepResultList;

// Thread-related structures
typedef struct {
  char filename[MAX_PATH];   // File to search
  const char *pattern;       // Pattern to search for
  const char *pattern_lower; // Lowercase pattern for case-insensitive search
  SearchMode mode;           // Search mode
  int line_numbers;          // Whether to show line numbers
  BOOL recursive;            // Whether to search recursively
  HANDLE mutex;              // Mutex for thread synchronization
} ThreadData;

// Global result list and mutex
static GrepResultList grep_results = {0};
static HANDLE result_mutex = NULL;

// Forward declarations for all static functions
static void search_file(const char *filename, const char *pattern,
                        const char *pattern_lower, SearchMode mode,
                        int line_numbers);
static void search_directory(const char *directory, const char *pattern,
                             const char *pattern_lower, SearchMode mode,
                             int line_numbers, BOOL recursive);
static BOOL is_text_file(const char *filename);
static void display_grep_results(void);
static void add_grep_result(const char *filename, int line_number,
                            const char *line, int match_start, int match_length,
                            double score);
static void free_grep_results(void);
static BOOL ends_with(const char *str, const char *suffix);
static int boyer_moore_search(const char *text, int text_len,
                              const char *pattern, int pattern_len);
static int boyer_moore_case_insensitive(const char *text, int text_len,
                                        const char *pattern_lower,
                                        int pattern_len);
static double fuzzy_search(const char *text, const char *pattern,
                           int *match_start, int *match_length);
static int open_file_in_editor(const char *file_path, int line_number);
static void show_file_detail_view(GrepResult *result);
static unsigned __stdcall search_thread(void *arg);
static int should_skip_file(const char *filename);
static char *extract_line_from_buffer(const char *buffer, int buffer_size,
                                      int line_start, int *line_length);
static void run_grep_interactive_session(void);
static void display_grep_results_interactive(const char *query,
                                             int selected_index,
                                             int results_count);
static void update_selection_highlight(int new_index, int old_index,
                                       int results_count);

/**
 * Boyer-Moore string search implementation
 * @return Index of the first occurrence, or -1 if not found
 */
static int boyer_moore_search(const char *text, int text_len,
                              const char *pattern, int pattern_len) {
  if (pattern_len == 0)
    return 0;
  if (pattern_len > text_len)
    return -1;

  // Initialize bad character skip table
  int bad_char_skip[256];
  for (int i = 0; i < 256; i++) {
    bad_char_skip[i] = pattern_len;
  }

  // Fill the bad character skip table
  for (int i = 0; i < pattern_len - 1; i++) {
    bad_char_skip[(unsigned char)pattern[i]] = pattern_len - 1 - i;
  }

  // Search for the pattern
  int s = 0; // The shift of the pattern
  while (s <= text_len - pattern_len) {
    int j = pattern_len - 1;

    // Match from right to left
    while (j >= 0 && pattern[j] == text[s + j]) {
      j--;
    }

    if (j < 0) {
      // Pattern found
      return s;
    } else {
      // Shift by bad character rule
      s += bad_char_skip[(unsigned char)text[s + pattern_len - 1]];
    }
  }

  return -1; // Pattern not found
}

/**
 * Case-insensitive Boyer-Moore search
 */
static int boyer_moore_case_insensitive(const char *text, int text_len,
                                        const char *pattern_lower,
                                        int pattern_len) {
  if (pattern_len == 0)
    return 0;
  if (pattern_len > text_len)
    return -1;

  // Create lowercase copy of text for searching
  char *text_lower = (char *)malloc(text_len + 1);
  if (!text_lower) {
    return -1; // Memory allocation failed
  }

  // Convert text to lowercase
  for (int i = 0; i < text_len; i++) {
    text_lower[i] = tolower(text[i]);
  }
  text_lower[text_len] = '\0';

  // Initialize bad character skip table
  int bad_char_skip[256];
  for (int i = 0; i < 256; i++) {
    bad_char_skip[i] = pattern_len;
  }

  // Fill the bad character skip table
  for (int i = 0; i < pattern_len - 1; i++) {
    bad_char_skip[(unsigned char)pattern_lower[i]] = pattern_len - 1 - i;
  }

  // Search for the pattern
  int s = 0; // The shift of the pattern
  int result = -1;

  while (s <= text_len - pattern_len) {
    int j = pattern_len - 1;

    // Match from right to left
    while (j >= 0 && pattern_lower[j] == text_lower[s + j]) {
      j--;
    }

    if (j < 0) {
      // Pattern found
      result = s;
      break;
    } else {
      // Shift by bad character rule
      s += bad_char_skip[(unsigned char)text_lower[s + pattern_len - 1]];
    }
  }

  // Free the lowercase copy of text
  free(text_lower);

  return result; // Return match position or -1 if not found
}

/**
 * Fuzzy search implementation
 * @return Score between 0.0 and 1.0, with higher being better match
 */
static double fuzzy_search(const char *text, const char *pattern,
                           int *match_start, int *match_length) {
  int text_len = strlen(text);
  int pattern_len = strlen(pattern);

  if (pattern_len == 0)
    return 0.0;
  if (pattern_len > text_len)
    return 0.0;

  // Score variables
  double best_score = 0.0;
  int best_start = 0;
  int best_length = 0;

  // Try matching from each position in text
  for (int i = 0; i <= text_len - pattern_len; i++) {
    int match_pos = 0;   // Current position in pattern
    int consecutive = 0; // Count of consecutive matches
    double local_score = 0.0;
    int matched_chars = 0;
    int first_match = -1;
    int last_match = -1;

    // Try to match pattern from current position
    for (int j = i; j < text_len && match_pos < pattern_len; j++) {
      if (tolower(text[j]) == tolower(pattern[match_pos])) {
        if (first_match == -1)
          first_match = j;
        last_match = j;

        // Give more weight to consecutive matches
        consecutive++;
        local_score += consecutive * 0.1;

        matched_chars++;
        match_pos++;
      } else {
        // Penalize non-consecutive matches
        consecutive = 0;
      }
    }

    // Calculate final score based on matched chars and compactness
    if (matched_chars == pattern_len) {
      int match_span = last_match - first_match + 1;
      double proximity = (double)pattern_len / match_span;

      local_score += proximity * 0.5;

      // Bonus for matching at start of words
      if (i == 0 || !isalnum(text[i - 1])) {
        local_score += 0.2;
      }

      // If this is the best score so far, save it
      if (local_score > best_score) {
        best_score = local_score;
        best_start = first_match;
        best_length = last_match - first_match + 1;
      }
    }
  }

  // Normalize score to range [0,1]
  best_score = best_score > 1.0 ? 1.0 : best_score;

  // Set output parameters
  *match_start = best_start;
  *match_length = best_length;

  return best_score;
}

/**
 * Display grep results in a side-by-side view for interactive mode
 */
static void display_grep_results_interactive(const char *query,
                                             int selected_index,
                                             int results_count) {
  // Get console handle and dimensions
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);

  int console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  int console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  WORD originalAttrs = csbi.wAttributes;

  // Save cursor position after the "Search: " prompt
  COORD searchPos = csbi.dwCursorPosition;

  // Calculate layout
  int left_width = 30; // Fixed width for file list
  if (left_width > console_width / 3)
    left_width = console_width / 3;

  int right_width = console_width - left_width - 3; // Space for separators
  int display_height = console_height - 5; // Space for header and footer

  // Clear the display area
  COORD clearPos = {0, searchPos.Y + 1};
  DWORD written;

  // Clear each line in the display area
  for (int i = 0; i < display_height; i++) {
    FillConsoleOutputCharacter(hConsole, ' ', console_width, clearPos,
                               &written);
    clearPos.Y++;
  }

  // Reset cursor to after the search prompt
  SetConsoleCursorPosition(hConsole, searchPos);
  printf("\n\n");

  // Show results count
  SetConsoleTextAttribute(hConsole, COLOR_INFO);
  printf("Found %d matches", results_count);
  SetConsoleTextAttribute(hConsole, originalAttrs);
  printf("\n");

  // Draw headers for columns
  SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
  printf(" %-*s | %s\n", left_width - 2, "File:Line", "Match");

  // Draw separator line
  SetConsoleTextAttribute(hConsole, COLOR_BOX);
  for (int i = 0; i < console_width; i++) {
    printf("-");
  }
  printf("\n");
  SetConsoleTextAttribute(hConsole, originalAttrs);

  // Calculate how many results we can display
  int max_display = display_height - 4; // Account for headers and footer
  if (max_display < 1)
    max_display = 1;

  // Determine which results to show based on selected index
  int start_idx = 0;
  if (selected_index >= max_display) {
    start_idx = selected_index - (max_display / 2);
    if (start_idx + max_display > results_count) {
      start_idx = results_count - max_display;
    }
  }
  if (start_idx < 0)
    start_idx = 0;

  // Display results in a two-column layout
  for (int i = 0; i < max_display && i + start_idx < results_count; i++) {
    int result_idx = i + start_idx;
    GrepResult *result = &grep_results.results[result_idx];

    // Extract just the filename from path
    char *filename = result->filename;
    char *last_slash = strrchr(filename, '\\');
    if (last_slash) {
      filename = last_slash + 1;
    }

    // Create file:line display
    char file_line[256];
    snprintf(file_line, sizeof(file_line), "%s:%d", filename,
             result->line_number);

    // Truncate if too long
    if (strlen(file_line) > (size_t)(left_width - 5)) {
      file_line[left_width - 8] = '.';
      file_line[left_width - 7] = '.';
      file_line[left_width - 6] = '.';
      file_line[left_width - 5] = '\0';
    }

    // Highlight current selection
    if (result_idx == selected_index) {
      SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
      printf(">");
    } else {
      printf(" ");
    }

    // Print file:line
    if (result_idx == selected_index) {
      printf(" %-*s ", left_width - 3, file_line);
    } else {
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf(" %-*s ", left_width - 3, file_line);
    }

    // Separator
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    printf("|");
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf(" ");

    // Print match content with highlighting
    char display_line[512] = {0};
    strncpy(display_line, result->line_content, sizeof(display_line) - 1);

    // Truncate if too long
    if (strlen(display_line) > (size_t)(right_width - 2)) {
      // Center around the match if possible
      int match_center = result->match_start + (result->match_length / 2);
      int half_width = (right_width - 2) / 2;

      if (match_center > half_width &&
          match_center + half_width < (int)strlen(display_line)) {
        int start_pos = match_center - half_width;
        memmove(display_line, display_line + start_pos,
                strlen(display_line + start_pos) + 1);

        display_line[0] = '.';
        display_line[1] = '.';
        display_line[2] = '.';

        // Adjust match position
        result->match_start -= (start_pos - 3);
        if (result->match_start < 0)
          result->match_start = 0;

        // Add ellipsis at end if needed
        if (strlen(display_line) > (size_t)(right_width - 2)) {
          display_line[right_width - 5] = '.';
          display_line[right_width - 4] = '.';
          display_line[right_width - 3] = '.';
          display_line[right_width - 2] = '\0';
        }
      } else {
        // Simple truncation with ellipsis
        display_line[right_width - 5] = '.';
        display_line[right_width - 4] = '.';
        display_line[right_width - 3] = '.';
        display_line[right_width - 2] = '\0';
      }
    }

    // Print match with highlighting
    // First part before match
    printf("%.*s", result->match_start, display_line);

    // Matched part
    SetConsoleTextAttribute(hConsole, COLOR_MATCH);
    int match_len = result->match_length;
    if (result->match_start + match_len > (int)strlen(display_line)) {
      match_len = strlen(display_line) - result->match_start;
    }
    if (match_len > 0 && result->match_start < (int)strlen(display_line)) {
      printf("%.*s", match_len, display_line + result->match_start);
    }

    // Part after match
    SetConsoleTextAttribute(hConsole, originalAttrs);
    if (result->match_start + match_len < (int)strlen(display_line)) {
      printf("%s", display_line + result->match_start + match_len);
    }

    printf("\n");
  }

  // Footer with navigation help
  SetConsoleCursorPosition(
      hConsole, (COORD){0, csbi.dwCursorPosition.Y + max_display + 4});
  SetConsoleTextAttribute(hConsole, COLOR_BOX);
  for (int i = 0; i < console_width; i++) {
    printf("-");
  }
  printf("\n");
  SetConsoleTextAttribute(hConsole, originalAttrs);
  printf("Navigate: Ctrl+N (down), Ctrl+P (up), Enter (open), Ctrl+C (exit)\n");

  // Restore cursor position after the search prompt
  SetConsoleCursorPosition(hConsole,
                           (COORD){searchPos.X + strlen(query), searchPos.Y});
}

/**
 * Update just the selection highlight without redrawing everything
 */
static void update_selection_highlight(int new_index, int old_index,
                                       int results_count) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  WORD originalAttrs = csbi.wAttributes;

  // Save current cursor position
  COORD cursorPos = csbi.dwCursorPosition;

  // Calculate layout
  int console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  int console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  int display_height = console_height - 5;
  int max_display = display_height - 4;

  // Calculate which range of results is currently displayed
  int left_width = 30;
  if (left_width > console_width / 3)
    left_width = console_width / 3;

  int start_idx = 0;
  if (new_index >= max_display) {
    start_idx = new_index - (max_display / 2);
    if (start_idx + max_display > results_count) {
      start_idx = results_count - max_display;
    }
  }
  if (start_idx < 0)
    start_idx = 0;

  // Check if we need to redraw everything due to scrolling
  int old_start_idx = 0;
  if (old_index >= max_display) {
    old_start_idx = old_index - (max_display / 2);
    if (old_start_idx + max_display > results_count) {
      old_start_idx = results_count - max_display;
    }
  }
  if (old_start_idx < 0)
    old_start_idx = 0;

  // If scrolling happened, redraw everything
  if (start_idx != old_start_idx) {
    // Find the search input at the top
    char search_query[256] = {0};
    COORD searchPos = {0, 0};

    // Look for the "Search: " line
    for (int i = 0; i < 5; i++) {
      SetConsoleCursorPosition(hConsole, (COORD){0, i});
      char line[256] = {0};
      DWORD read;
      DWORD coord = 0;
      ReadConsoleOutputCharacter(hConsole, line, sizeof(line) - 1,
                                 (COORD){0, i}, &read);
      line[read] = '\0';

      if (strstr(line, "Search: ")) {
        searchPos.Y = i;
        char *search_start = strstr(line, "Search: ") + 8;
        strcpy(search_query, search_start);
        break;
      }
    }

    // Redraw everything with the new scroll position
    display_grep_results_interactive(search_query, new_index, results_count);

    // Restore cursor to original position
    SetConsoleCursorPosition(hConsole, cursorPos);
    return;
  }

  // Otherwise, just update the highlight for old and new positions

  // Calculate the line number for the old index in the display
  int old_display_line = 5 + (old_index - start_idx);

  // Calculate the line number for the new index in the display
  int new_display_line = 5 + (new_index - start_idx);

  // Update the old selection (remove highlight)
  if (old_index >= start_idx && old_index < start_idx + max_display) {
    GrepResult *result = &grep_results.results[old_index];

    // Extract filename
    char *filename = result->filename;
    char *last_slash = strrchr(filename, '\\');
    if (last_slash) {
      filename = last_slash + 1;
    }

    // Create file:line display
    char file_line[256];
    snprintf(file_line, sizeof(file_line), "%s:%d", filename,
             result->line_number);

    // Truncate if too long
    if (strlen(file_line) > (size_t)(left_width - 5)) {
      file_line[left_width - 8] = '.';
      file_line[left_width - 7] = '.';
      file_line[left_width - 6] = '.';
      file_line[left_width - 5] = '\0';
    }

    // Position at start of the old selection line
    SetConsoleCursorPosition(hConsole, (COORD){0, old_display_line});

    // Clear the current line
    DWORD written;
    FillConsoleOutputCharacter(hConsole, ' ', console_width,
                               (COORD){0, old_display_line}, &written);

    // Rewrite the line without highlight
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("  %-*s ", left_width - 3, file_line);

    // Separator
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    printf("|");
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf(" ");

    // Print match content with highlighting if needed
    char display_line[512] = {0};
    strncpy(display_line, result->line_content, sizeof(display_line) - 1);

    // Apply same truncation logic as in display function
    if (strlen(display_line) > (size_t)(console_width - left_width - 3)) {
      int match_center = result->match_start + (result->match_length / 2);
      int half_width = (console_width - left_width - 3) / 2;

      if (match_center > half_width &&
          match_center + half_width < (int)strlen(display_line)) {
        // Center around match
        int start_pos = match_center - half_width;
        memmove(display_line, display_line + start_pos,
                strlen(display_line + start_pos) + 1);

        display_line[0] = '.';
        display_line[1] = '.';
        display_line[2] = '.';

        // Adjust match position
        int match_start = result->match_start - (start_pos - 3);
        if (match_start < 0)
          match_start = 0;

        // Print with highlighting
        printf("%.*s", match_start, display_line);

        SetConsoleTextAttribute(hConsole, COLOR_MATCH);
        int match_len = result->match_length;
        if (match_start + match_len > (int)strlen(display_line)) {
          match_len = strlen(display_line) - match_start;
        }
        if (match_len > 0) {
          printf("%.*s", match_len, display_line + match_start);
        }

        SetConsoleTextAttribute(hConsole, originalAttrs);
        printf("%s", display_line + match_start + match_len);
      } else {
        // Simple truncation
        display_line[console_width - left_width - 6] = '.';
        display_line[console_width - left_width - 5] = '.';
        display_line[console_width - left_width - 4] = '.';
        display_line[console_width - left_width - 3] = '\0';

        // Print with highlighting
        printf("%.*s", result->match_start, display_line);

        SetConsoleTextAttribute(hConsole, COLOR_MATCH);
        int match_len = result->match_length;
        if (result->match_start + match_len > (int)strlen(display_line)) {
          match_len = strlen(display_line) - result->match_start;
        }
        if (match_len > 0 && result->match_start < (int)strlen(display_line)) {
          printf("%.*s", match_len, display_line + result->match_start);
        }

        SetConsoleTextAttribute(hConsole, originalAttrs);
        if (result->match_start + match_len < (int)strlen(display_line)) {
          printf("%s", display_line + result->match_start + match_len);
        }
      }
    } else {
      // No truncation needed
      printf("%.*s", result->match_start, display_line);

      SetConsoleTextAttribute(hConsole, COLOR_MATCH);
      int match_len = result->match_length;
      printf("%.*s", match_len, display_line + result->match_start);

      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s", display_line + result->match_start + match_len);
    }
  }

  // Update the new selection (add highlight)
  if (new_index >= start_idx && new_index < start_idx + max_display) {
    GrepResult *result = &grep_results.results[new_index];

    // Extract filename
    char *filename = result->filename;
    char *last_slash = strrchr(filename, '\\');
    if (last_slash) {
      filename = last_slash + 1;
    }

    // Create file:line display
    char file_line[256];
    snprintf(file_line, sizeof(file_line), "%s:%d", filename,
             result->line_number);

    // Truncate if too long
    if (strlen(file_line) > (size_t)(left_width - 5)) {
      file_line[left_width - 8] = '.';
      file_line[left_width - 7] = '.';
      file_line[left_width - 6] = '.';
      file_line[left_width - 5] = '\0';
    }

    // Position at start of the new selection line
    SetConsoleCursorPosition(hConsole, (COORD){0, new_display_line});

    // Clear the current line
    DWORD written;
    FillConsoleOutputCharacter(hConsole, ' ', console_width,
                               (COORD){0, new_display_line}, &written);

    // Rewrite the line with highlight
    SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
    printf("> %-*s ", left_width - 3, file_line);

    // Separator
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    printf("|");
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf(" ");

    // Print match content with highlighting if needed
    char display_line[512] = {0};
    strncpy(display_line, result->line_content, sizeof(display_line) - 1);

    // Apply same truncation logic as in display function
    if (strlen(display_line) > (size_t)(console_width - left_width - 3)) {
      int match_center = result->match_start + (result->match_length / 2);
      int half_width = (console_width - left_width - 3) / 2;

      if (match_center > half_width &&
          match_center + half_width < (int)strlen(display_line)) {
        // Center around match
        int start_pos = match_center - half_width;
        memmove(display_line, display_line + start_pos,
                strlen(display_line + start_pos) + 1);

        display_line[0] = '.';
        display_line[1] = '.';
        display_line[2] = '.';

        // Adjust match position
        int match_start = result->match_start - (start_pos - 3);
        if (match_start < 0)
          match_start = 0;

        // Print with highlighting
        printf("%.*s", match_start, display_line);

        SetConsoleTextAttribute(hConsole, COLOR_MATCH);
        int match_len = result->match_length;
        if (match_start + match_len > (int)strlen(display_line)) {
          match_len = strlen(display_line) - match_start;
        }
        if (match_len > 0) {
          printf("%.*s", match_len, display_line + match_start);
        }

        SetConsoleTextAttribute(hConsole, originalAttrs);
        printf("%s", display_line + match_start + match_len);
      } else {
        // Simple truncation
        display_line[console_width - left_width - 6] = '.';
        display_line[console_width - left_width - 5] = '.';
        display_line[console_width - left_width - 4] = '.';
        display_line[console_width - left_width - 3] = '\0';

        // Print with highlighting
        printf("%.*s", result->match_start, display_line);

        SetConsoleTextAttribute(hConsole, COLOR_MATCH);
        int match_len = result->match_length;
        if (result->match_start + match_len > (int)strlen(display_line)) {
          match_len = strlen(display_line) - result->match_start;
        }
        if (match_len > 0 && result->match_start < (int)strlen(display_line)) {
          printf("%.*s", match_len, display_line + result->match_start);
        }

        SetConsoleTextAttribute(hConsole, originalAttrs);
        if (result->match_start + match_len < (int)strlen(display_line)) {
          printf("%s", display_line + result->match_start + match_len);
        }
      }
    } else {
      // No truncation needed
      printf("%.*s", result->match_start, display_line);

      SetConsoleTextAttribute(hConsole, COLOR_MATCH);
      int match_len = result->match_length;
      printf("%.*s", match_len, display_line + result->match_start);

      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s", display_line + result->match_start + match_len);
    }
  }

  // Restore original cursor position
  SetConsoleCursorPosition(hConsole, cursorPos);
  SetConsoleTextAttribute(hConsole, originalAttrs);
}

/**
 * Run an interactive grep session
 * Shows all files initially and allows filtering with real-time search
 */
static void run_grep_interactive_session(void) {
  // Initialize with empty search to show all files
  char search_query[256] = "";
  char last_query[256] = "";

  // Save original console mode to restore later
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD originalMode;
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  WORD originalAttrs;

  // Save original console settings
  GetConsoleMode(hStdin, &originalMode);
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  originalAttrs = csbi.wAttributes;

  // Set console mode for raw input
  DWORD newMode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
  SetConsoleMode(hStdin, newMode);

  // Set up mutex for thread synchronization
  if (result_mutex == NULL) {
    result_mutex = CreateMutex(NULL, FALSE, NULL);
  }

  // Clear screen and show initial UI
  system("cls");
  SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
  printf("Interactive Grep Search (Recursive, Case-insensitive, Fuzzy)\n");
  SetConsoleTextAttribute(hConsole, originalAttrs);
  printf(
      "Type to filter | Ctrl+N/P: navigate | Enter: open | Ctrl+C: exit\n\n");
  printf("Search: ");

  int running = 1;
  int selected_index = 0;
  int results_count = 0;
  int first_run = 1;
  int console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
  int console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  int left_width = 30;
  if (left_width > console_width / 3)
    left_width = console_width / 3;
  int right_width = console_width - left_width - 3;

  // Initial search to populate results
  char *pattern_lower = _strdup("");
  if (pattern_lower) {
    // Initially search the current directory recursively with default options
    search_directory(".", "", pattern_lower, SEARCH_MODE_FUZZY, 1, TRUE);
    free(pattern_lower);
  }

  // Get initial results count
  results_count = grep_results.count;

  // Draw the initial results view
  display_grep_results_interactive(search_query, selected_index, results_count);

  // Main interaction loop
  while (running) {
    // Process user input
    int c = _getch();

    if (c == 3) { // Ctrl+C
      running = 0;
    } else if (c == 13) { // Enter
      // If we have search results and a selected index, open the file
      if (results_count > 0 && selected_index < results_count) {
        GrepResult *result = &grep_results.results[selected_index];
        open_file_in_editor(result->filename, result->line_number);

        // After returning from editor, refresh the UI
        system("cls");
        SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
        printf(
            "Interactive Grep Search (Recursive, Case-insensitive, Fuzzy)\n");
        SetConsoleTextAttribute(hConsole, originalAttrs);
        printf("Type to filter | Ctrl+N/P: navigate | Enter: open | Ctrl+C: "
               "exit\n\n");
        printf("Search: %s", search_query);

        // Redraw results view
        display_grep_results_interactive(search_query, selected_index,
                                         results_count);
      }
    } else if (c == 14) { // Ctrl+N
      if (results_count > 0) {
        selected_index = (selected_index + 1) % results_count;
        update_selection_highlight(selected_index, selected_index - 1,
                                   results_count);
      }
    } else if (c == 16) { // Ctrl+P
      if (results_count > 0) {
        int prev_index = selected_index;
        selected_index = (selected_index - 1 + results_count) % results_count;
        update_selection_highlight(selected_index, prev_index, results_count);
      }
    } else if (c == 8) { // Backspace
      // Remove last character from search query
      size_t len = strlen(search_query);
      if (len > 0) {
        search_query[len - 1] = '\0';

        // Check if the query has changed and we need to run search again
        if (strcmp(search_query, last_query) != 0) {
          // Reset results
          free_grep_results();

          // Create lowercase version of pattern for case-insensitive search
          pattern_lower = _strdup(search_query);
          if (pattern_lower) {
            for (char *p = pattern_lower; *p; p++) {
              *p = tolower(*p);
            }

            // Search the current directory recursively with the new query
            search_directory(".", search_query, pattern_lower,
                             SEARCH_MODE_FUZZY, 1, TRUE);
            free(pattern_lower);
          }

          // Update results count and reset selection
          results_count = grep_results.count;
          selected_index = 0;
          strcpy(last_query, search_query);

          // Redraw the entire results view with the new search
          printf("\rSearch: %s   ", search_query);
          display_grep_results_interactive(search_query, selected_index,
                                           results_count);
        } else {
          // Just update the search text if the actual search hasn't changed
          printf("\rSearch: %s   ", search_query);
        }
      }
    } else if (isprint(c)) { // Printable character
      // Add character to search query
      size_t len = strlen(search_query);
      if (len < sizeof(search_query) - 1) {
        search_query[len] = c;
        search_query[len + 1] = '\0';

        // Check if the query has changed and we need to run search again
        if (strcmp(search_query, last_query) != 0) {
          // Reset results
          free_grep_results();

          // Create lowercase version of pattern for case-insensitive search
          pattern_lower = _strdup(search_query);
          if (pattern_lower) {
            for (char *p = pattern_lower; *p; p++) {
              *p = tolower(*p);
            }

            // Search the current directory recursively with the new query
            search_directory(".", search_query, pattern_lower,
                             SEARCH_MODE_FUZZY, 1, TRUE);
            free(pattern_lower);
          }

          // Update results count and reset selection
          results_count = grep_results.count;
          selected_index = 0;
          strcpy(last_query, search_query);

          // Redraw the entire results view with the new search
          printf("\rSearch: %s   ", search_query);
          display_grep_results_interactive(search_query, selected_index,
                                           results_count);
        } else {
          // Just update the search text if the actual search hasn't changed
          printf("\rSearch: %s   ", search_query);
        }
      }
    }
  }

  // Clean up
  if (result_mutex) {
    CloseHandle(result_mutex);
    result_mutex = NULL;
  }

  free_grep_results();

  // Restore original console mode
  SetConsoleTextAttribute(hConsole, originalAttrs);
  SetConsoleMode(hStdin, originalMode);

  // Final screen clear
  system("cls");
}

/**
 * Thread function for searching files in parallel
 */
static unsigned __stdcall search_thread(void *arg) {
  ThreadData *data = (ThreadData *)arg;

  // Check if it's a directory or file
  DWORD attr = GetFileAttributes(data->filename);

  if (attr != INVALID_FILE_ATTRIBUTES) {
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
      // It's a directory
      search_directory(data->filename, data->pattern, data->pattern_lower,
                       data->mode, data->line_numbers, data->recursive);
    } else {
      // It's a file
      search_file(data->filename, data->pattern, data->pattern_lower,
                  data->mode, data->line_numbers);
    }
  }

  // Thread cleanup
  free(data);
  return 0;
}

/**
 * Check if a file should be skipped based on its extension or properties
 */
static int should_skip_file(const char *filename) {
  // Skip hidden files (starting with .)
  const char *base_name = strrchr(filename, '\\');
  if (base_name) {
    base_name++; // Move past the backslash
  } else {
    base_name = filename;
  }

  if (base_name[0] == '.') {
    return 1;
  }

  // Skip common version control directories
  if (strstr(filename, "\\.git\\") || strstr(filename, "\\.svn\\") ||
      strstr(filename, "\\.hg\\")) {
    return 1;
  }

  // Skip common binary file extensions
  static const char *binary_exts[] = {
      ".exe", ".dll",  ".obj", ".o",    ".lib", ".bin", ".dat", ".png",
      ".jpg", ".jpeg", ".gif", ".bmp",  ".ico", ".zip", ".rar", ".7z",
      ".gz",  ".tar",  ".mp3", ".mp4",  ".avi", ".mov", ".wav", ".pdf",
      ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx"};

  for (int i = 0; i < sizeof(binary_exts) / sizeof(binary_exts[0]); i++) {
    if (ends_with(filename, binary_exts[i])) {
      return 1;
    }
  }

  return 0;
}

/**
 * Check if a string ends with a specific suffix
 */
static BOOL ends_with(const char *str, const char *suffix) {
  if (!str || !suffix)
    return FALSE;

  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);

  if (suffix_len > str_len)
    return FALSE;

  return _stricmp(str + str_len - suffix_len, suffix) == 0;
}

/**
 * Search a directory for files containing a pattern
 */
static void search_directory(const char *directory, const char *pattern,
                             const char *pattern_lower, SearchMode mode,
                             int line_numbers, BOOL recursive) {
  char search_path[MAX_PATH];
  WIN32_FIND_DATA findData;
  HANDLE hFind;

  // Prepare search pattern for all files in directory
  snprintf(search_path, sizeof(search_path), "%s\\*", directory);

  // Start finding files
  hFind = FindFirstFile(search_path, &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }

  // Thread handles and count for parallel processing
  HANDLE thread_handles[MAX_THREAD_COUNT] = {0};
  int thread_count = 0;

  // Queue for pending directories (for recursive search)
  char **dir_queue = NULL;
  int dir_queue_size = 0;
  int dir_queue_capacity = 0;

  if (recursive) {
    // Initialize directory queue
    dir_queue_capacity = 100;
    dir_queue = (char **)malloc(dir_queue_capacity * sizeof(char *));
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

    // Skip files that should be ignored
    if (should_skip_file(full_path)) {
      continue;
    }

    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // It's a directory - save for later recursive search if needed
      if (recursive && dir_queue) {
        // Add to directory queue
        if (dir_queue_size >= dir_queue_capacity) {
          dir_queue_capacity *= 2;
          dir_queue =
              (char **)realloc(dir_queue, dir_queue_capacity * sizeof(char *));
        }
        dir_queue[dir_queue_size++] = _strdup(full_path);
      }
    } else {
      // It's a file - search it (potentially in a new thread)
      if (thread_count < MAX_THREAD_COUNT) {
        // Create a new thread to search this file
        ThreadData *thread_data = (ThreadData *)malloc(sizeof(ThreadData));
        if (thread_data) {
          strcpy(thread_data->filename, full_path);
          thread_data->pattern = pattern;
          thread_data->pattern_lower = pattern_lower;
          thread_data->mode = mode;
          thread_data->line_numbers = line_numbers;
          thread_data->recursive = recursive;
          thread_data->mutex = result_mutex;

          // Create thread
          thread_handles[thread_count] = (HANDLE)_beginthreadex(
              NULL, 0, search_thread, thread_data, 0, NULL);
          if (thread_handles[thread_count]) {
            thread_count++;
          } else {
            // Thread creation failed, search in current thread
            free(thread_data);
            search_file(full_path, pattern, pattern_lower, mode, line_numbers);
          }
        } else {
          // Memory allocation failed, search in current thread
          search_file(full_path, pattern, pattern_lower, mode, line_numbers);
        }
      } else {
        // Too many threads active, search in current thread
        search_file(full_path, pattern, pattern_lower, mode, line_numbers);
      }
    }
  } while (FindNextFile(hFind, &findData));

  // Wait for all threads to complete
  if (thread_count > 0) {
    WaitForMultipleObjects(thread_count, thread_handles, TRUE, INFINITE);

    // Close thread handles
    for (int i = 0; i < thread_count; i++) {
      CloseHandle(thread_handles[i]);
    }
  }

  // Process directory queue for recursive search
  if (recursive && dir_queue) {
    for (int i = 0; i < dir_queue_size; i++) {
      // Search each directory recursively
      search_directory(dir_queue[i], pattern, pattern_lower, mode, line_numbers,
                       recursive);
      free(dir_queue[i]);
    }
    free(dir_queue);
  }

  FindClose(hFind);
}

/**
 * Extract a line from a buffer starting at a given position
 */
static char *extract_line_from_buffer(const char *buffer, int buffer_size,
                                      int line_start, int *line_length) {
  int i = line_start;

  // Find the end of the line (newline or buffer end)
  while (i < buffer_size && buffer[i] != '\n' && buffer[i] != '\r') {
    i++;
  }

  // Calculate line length
  *line_length = i - line_start;

  // Copy the line to a new buffer
  char *line = (char *)malloc(*line_length + 1);
  if (!line)
    return NULL;

  memcpy(line, buffer + line_start, *line_length);
  line[*line_length] = '\0';

  return line;
}

/**
 * Search a file for a pattern using Boyer-Moore algorithm
 */
static void search_file(const char *filename, const char *pattern,
                        const char *pattern_lower, SearchMode mode,
                        int line_numbers) {
  // Skip non-text files based on extension
  if (!is_text_file(filename)) {
    return;
  }

  FILE *file = fopen(filename, "rb");
  if (!file) {
    return;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Skip empty files
  if (file_size == 0) {
    fclose(file);
    return;
  }

  // Allocate a buffer for the file content
  int buffer_size = (file_size < MAX_BUFFER_SIZE) ? file_size : MAX_BUFFER_SIZE;
  char *buffer = (char *)malloc(buffer_size + 1); // +1 for null terminator

  if (!buffer) {
    fclose(file);
    return;
  }

  int pattern_len = strlen(pattern);
  int line_number = 1;
  long bytes_read_total = 0;

  // Process the file in chunks
  while (bytes_read_total < file_size) {
    // Read a chunk of the file
    int bytes_to_read = buffer_size;
    if (bytes_read_total + bytes_to_read > file_size) {
      bytes_to_read = file_size - bytes_read_total;
    }

    int bytes_read = fread(buffer, 1, bytes_to_read, file);
    if (bytes_read <= 0) {
      break;
    }

    // Null-terminate the buffer
    buffer[bytes_read] = '\0';

    // Count newlines before current position to maintain correct line numbers
    if (bytes_read_total > 0) {
      for (int i = 0; i < bytes_read; i++) {
        if (buffer[i] == '\n') {
          line_number++;
        }
      }
    }

    // Process each line in the buffer
    int pos = 0;
    while (pos < bytes_read) {
      // Find the start of the next line
      int line_start = pos;

      // Find the end of the current line
      while (pos < bytes_read && buffer[pos] != '\n' && buffer[pos] != '\r') {
        pos++;
      }

      // Extract the line
      int line_length = pos - line_start;

      // Process the line if it's not empty
      if (line_length > 0) {
        // Null-terminate the line (temporarily)
        char original_char = buffer[pos];
        buffer[pos] = '\0';

        BOOL found_match = FALSE;
        int match_start = 0;
        int match_length = 0;
        double match_score = 0.0;

        // Match according to mode
        if (mode == SEARCH_MODE_PLAIN) {
          // Case sensitive Boyer-Moore search
          match_start = boyer_moore_search(buffer + line_start, line_length,
                                           pattern, pattern_len);

          if (match_start >= 0) {
            found_match = TRUE;
            match_length = pattern_len;
            match_score = 1.0;
          }
        } else if (mode == SEARCH_MODE_IGNORE_CASE) {
          // Case insensitive search
          if (pattern_lower) {
            match_start = boyer_moore_case_insensitive(
                buffer + line_start, line_length, pattern_lower, pattern_len);

            if (match_start >= 0) {
              found_match = TRUE;
              match_length = pattern_len;
              match_score = 1.0;
            }
          }
        } else if (mode == SEARCH_MODE_FUZZY) {
          // Fuzzy matching
          match_score = fuzzy_search(buffer + line_start, pattern, &match_start,
                                     &match_length);

          if (match_score > 0.5) { // Adjust threshold as needed
            found_match = TRUE;
          }
        }

        // Report the match if found
        if (found_match) {
          WaitForSingleObject(result_mutex, INFINITE);
          add_grep_result(filename, line_number, buffer + line_start,
                          match_start, match_length, match_score);
          ReleaseMutex(result_mutex);
        }

        // Restore the original character
        buffer[pos] = original_char;
      }

      // Move past newline characters
      if (pos < bytes_read && buffer[pos] == '\r') {
        pos++;
      }
      if (pos < bytes_read && buffer[pos] == '\n') {
        pos++;
        line_number++;
      }
    }

    bytes_read_total += bytes_read;
  }

  // Clean up
  free(buffer);
  fclose(file);
}

/**
 * Add a grep result to the results list
 */
static void add_grep_result(const char *filename, int line_number,
                            const char *line, int match_start, int match_length,
                            double score) {
  // Resize if needed
  if (grep_results.count >= grep_results.capacity) {
    grep_results.capacity =
        grep_results.capacity == 0 ? 100 : grep_results.capacity * 2;
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
  result->match_score = score;
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
 * Display grep results with context view like in the provided screenshot
 */
static void display_grep_results(void) {
  if (grep_results.count == 0) {
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
  grep_results.current_index = 0;
  grep_results.is_active = TRUE;

  // Initial screen clear only once at the beginning
  system("cls");

  // Previous selection index to track what needs updating
  int previous_index = -1;

  // Main navigation loop
  while (grep_results.is_active) {
    // Hide cursor during drawing to prevent jumping
    CONSOLE_CURSOR_INFO cursorInfo = {1, FALSE}; // Size 1, invisible
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Get current console dimensions
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int console_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int console_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    // Layout calculations
    int left_width = 30; // Fixed width for file list
    if (left_width > console_width / 3)
      left_width = console_width / 3;

    int right_width = console_width - left_width - 3; // Space for separators
    int list_height =
        console_height - 5; // Reserve space for headers and footers
    if (list_height < 5)
      list_height = 5; // Minimum reasonable size

    // Calculate visible range
    int visible_items =
        list_height < grep_results.count ? list_height : grep_results.count;
    int start_index = 0;

    // Adjust start index to keep selection in view
    if (grep_results.current_index >= visible_items) {
      start_index = grep_results.current_index - (visible_items / 2);

      // Ensure we don't go past the end
      if (start_index + visible_items > grep_results.count) {
        start_index = grep_results.count - visible_items;
      }
    }

    // Ensure start_index is not negative
    if (start_index < 0)
      start_index = 0;

    // Only redraw everything if dimensions changed or first time
    BOOL full_redraw = (previous_index == -1);

    if (full_redraw) {
      // Draw header - Exact match for the screenshot
      SetConsoleCursorPosition(hConsole, (COORD){0, 0});
      SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
      printf("Boyer-Moore Grep Results (%d matches)", grep_results.count);

      // Move to next line for separator
      COORD sepPos = {0, 1};
      SetConsoleCursorPosition(hConsole, sepPos);

      // Draw separator line - Exact match
      SetConsoleTextAttribute(hConsole, COLOR_BOX);
      for (int i = 0; i < console_width; i++)
        printf("-");

      printf("\n");
    }

    // Update the file list (left column) - make it match screenshot exactly
    for (int i = 0; i < visible_items; i++) {
      int result_idx = start_index + i;
      if (result_idx >= grep_results.count)
        break;

      GrepResult *result = &grep_results.results[result_idx];

      // Position cursor for this list item
      SetConsoleCursorPosition(hConsole, (COORD){0, 2 + i});

      // Extract just the filename from path
      char *filename = result->filename;
      char *last_slash = strrchr(filename, '\\');
      if (last_slash) {
        filename = last_slash + 1;
      }

      // Format to exactly match the screenshot
      if (result_idx == grep_results.current_index) {
        SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
        printf("-> %-*s : %4d", left_width - 10, filename, result->line_number);
      } else {
        SetConsoleTextAttribute(hConsole, originalAttrs);
        printf("   %-*s : %4d", left_width - 10, filename, result->line_number);
      }
    }

    // Clear any remaining lines in the list area
    for (int i = visible_items; i < list_height; i++) {
      SetConsoleCursorPosition(hConsole, (COORD){0, 2 + i});
      printf("%-*s", left_width, "");
    }

    // Draw the vertical separator line for the entire height
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    for (int i = 0; i < list_height; i++) {
      SetConsoleCursorPosition(hConsole, (COORD){left_width, 2 + i});
      printf(" | ");
    }

    // Reset to original color
    SetConsoleTextAttribute(hConsole, originalAttrs);

    // Draw current result details (right column - context view)
    if (grep_results.current_index >= 0 &&
        grep_results.current_index < grep_results.count) {
      GrepResult *current = &grep_results.results[grep_results.current_index];

      // Clear the right side
      for (int i = 0; i < list_height; i++) {
        SetConsoleCursorPosition(hConsole, (COORD){left_width + 3, 2 + i});
        printf("%-*s", right_width, "");
      }

      // First line: File path and line info - exact match for screenshot
      SetConsoleCursorPosition(hConsole, (COORD){left_width + 3, 2});
      printf("File: %s (Line %d)", current->filename, current->line_number);

      // Second line: Match line with highlighting
      SetConsoleCursorPosition(hConsole, (COORD){left_width + 3, 3});
      printf("Match: ");

      // First part before match
      printf("%.*s", current->match_start, current->line_content);

      // Matched part - highlight in red
      SetConsoleTextAttribute(hConsole, COLOR_MATCH);
      printf("%.*s", current->match_length,
             current->line_content + current->match_start);

      // Reset color and print the rest
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s", current->line_content + current->match_start +
                       current->match_length);

      // Print "Context:" label
      SetConsoleCursorPosition(hConsole, (COORD){left_width + 3, 4});
      printf("Context:");

      // Show file content with context lines
      FILE *preview_file = fopen(current->filename, "r");
      if (preview_file) {
        // Define how many context lines to show before and after
        int context_lines = MAX_PREVIEW_LINES;
        int target_line = current->line_number;
        int start_line =
            (target_line <= context_lines) ? 1 : target_line - context_lines;
        int end_line = target_line + context_lines;
        int current_line = 1;
        char line_buffer[1024];

        // Skip to start line
        while (current_line < start_line &&
               fgets(line_buffer, sizeof(line_buffer), preview_file)) {
          current_line++;
        }

        // Show context lines
        int display_line = 0;
        while (current_line <= end_line &&
               fgets(line_buffer, sizeof(line_buffer), preview_file) &&
               display_line <
                   list_height - 6) { // -6 to account for header lines

          // Remove newline
          size_t len = strlen(line_buffer);
          if (len > 0 && line_buffer[len - 1] == '\n') {
            line_buffer[len - 1] = '\0';
          }

          // Position cursor
          SetConsoleCursorPosition(hConsole,
                                   (COORD){left_width + 3, 5 + display_line});

          // Format to match screenshot exactly
          if (current_line == target_line) {
            SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
            printf("%3d -> %s", current_line, line_buffer);
          } else {
            SetConsoleTextAttribute(hConsole, originalAttrs);
            printf("%3d | %s", current_line, line_buffer);
          }

          current_line++;
          display_line++;
        }

        fclose(preview_file);
      } else {
        // Could not open file
        SetConsoleCursorPosition(hConsole, (COORD){left_width + 3, 5});
        SetConsoleTextAttribute(hConsole, COLOR_MATCH);
        printf("Could not open file for preview");
      }
    }

    // Draw bottom separator line
    SetConsoleCursorPosition(hConsole, (COORD){0, console_height - 2});
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    for (int i = 0; i < console_width; i++) {
      printf("-");
    }

    // Navigation instructions
    SetConsoleCursorPosition(hConsole, (COORD){0, console_height - 1});
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("Navigation: UP/DOWN/j/k - Navigate  ENTER - Open in Editor  TAB - "
           "Detail View  ESC/q - Exit");

    // Make cursor visible again
    cursorInfo.bVisible = TRUE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);

    // Update previous index
    previous_index = grep_results.current_index;

    // Process input for navigation
    INPUT_RECORD inputRecord;
    DWORD numEvents;

    // Wait for input
    ReadConsoleInput(hStdin, &inputRecord, 1, &numEvents);

    if (numEvents > 0 && inputRecord.EventType == KEY_EVENT &&
        inputRecord.Event.KeyEvent.bKeyDown) {

      WORD keyCode = inputRecord.Event.KeyEvent.wVirtualKeyCode;
      char keyChar = inputRecord.Event.KeyEvent.uChar.AsciiChar;

      // Navigation keys - exactly match what's shown in the navigation help
      if (keyCode == VK_UP || keyChar == 'k') {
        // Up arrow or k - Previous result
        if (grep_results.current_index > 0) {
          grep_results.current_index--;
        } else {
          grep_results.current_index = grep_results.count - 1;
        }
      } else if (keyCode == VK_DOWN || keyChar == 'j') {
        // Down arrow or j - Next result
        grep_results.current_index =
            (grep_results.current_index + 1) % grep_results.count;
      } else if (keyCode == VK_RETURN) {
        // Enter - Open in editor
        open_file_in_editor(
            grep_results.results[grep_results.current_index].filename,
            grep_results.results[grep_results.current_index].line_number);

        // Redraw everything after returning from editor
        full_redraw = TRUE;
        previous_index = -1;
        system("cls");
      } else if (keyCode == VK_TAB) {
        // Tab - Show detail view
        show_file_detail_view(
            &grep_results.results[grep_results.current_index]);

        // Redraw everything after returning from detail view
        full_redraw = TRUE;
        previous_index = -1;
        system("cls");
      } else if (keyCode == VK_ESCAPE || keyChar == 'q') {
        // Escape or q - Exit
        grep_results.is_active = FALSE;
      }
    }
  }

  // Final cleanup
  SetConsoleTextAttribute(hConsole, originalAttrs);
  SetConsoleMode(hStdin, originalMode);
  SetConsoleCursorInfo(hConsole, &originalCursorInfo);

  system("cls");

  // Position cursor properly for next prompt
  COORD topPos = {0, 0};
  SetConsoleCursorPosition(hConsole, topPos);
  printf("\n");
}

/**
 * Show detailed view of a grep result
 */
static void show_file_detail_view(GrepResult *result) {
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
  printf("File: %s (line %d)\n\n", result->filename, result->line_number);

  // Show file content with context
  FILE *file = fopen(result->filename, "r");
  if (file) {
    // Show more context in the detail view
    int context_lines = 20; // Increased context in detail view
    int start_line = (result->line_number <= context_lines)
                         ? 1
                         : result->line_number - context_lines;
    int end_line = result->line_number + context_lines;
    int current_line = 1;
    char line[1024];

    // Skip to start line
    while (current_line < start_line && fgets(line, sizeof(line), file)) {
      current_line++;
    }

    // Read and display lines with context
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("Content preview:\n\n");

    while (current_line <= end_line && fgets(line, sizeof(line), file)) {
      // Remove newline if present
      size_t len = strlen(line);
      if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
        len--;
      }

      // Show line with appropriate highlighting
      if (current_line == result->line_number) {
        // Highlight the matching line
        SetConsoleTextAttribute(hConsole, COLOR_INFO);
        printf("%4d -> ", current_line);

        // Try to highlight the match within the line
        if (result->match_start >= 0 && result->match_start < (int)len &&
            result->match_length > 0) {
          // Print parts before match
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("%.*s", result->match_start, line);

          // Print match with highlight
          SetConsoleTextAttribute(hConsole, COLOR_MATCH);
          printf("%.*s", result->match_length, line + result->match_start);

          // Print after match
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("%s\n", line + result->match_start + result->match_length);
        } else {
          // Fallback if match position is not correct
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("%s\n", line);
        }
      } else {
        // Regular line
        SetConsoleTextAttribute(hConsole, originalAttrs);
        printf("%4d | %s\n", current_line, line);
      }

      current_line++;
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
    open_file_in_editor(result->filename, result->line_number);
  }

  // Reset console color
  SetConsoleTextAttribute(hConsole, originalAttrs);
}

/**
 * Check if a file is likely to be a text file
 * Uses smarter heuristics to avoid reading binary files
 */
static BOOL is_text_file(const char *filename) {
  // Skip files that should be ignored
  if (should_skip_file(filename)) {
    return FALSE;
  }

  // Check extensions first (most efficient)
  const char *ext = strrchr(filename, '.');
  if (ext) {
    // Skip common binary file extensions
    static const char *binary_exts[] = {
        ".exe", ".dll",  ".obj", ".o",    ".lib", ".bin", ".dat", ".png",
        ".jpg", ".jpeg", ".gif", ".bmp",  ".ico", ".zip", ".rar", ".7z",
        ".gz",  ".tar",  ".mp3", ".mp4",  ".avi", ".mov", ".wav", ".pdf",
        ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx"};

    for (int i = 0; i < sizeof(binary_exts) / sizeof(binary_exts[0]); i++) {
      if (_stricmp(ext, binary_exts[i]) == 0) {
        return FALSE; // Binary file
      }
    }

    // Check for common text file extensions
    static const char *text_exts[] = {
        ".txt", ".c",    ".cpp", ".h",   ".hpp",  ".cs",  ".js",
        ".py",  ".html", ".css", ".xml", ".json", ".md",  ".log",
        ".sh",  ".bat",  ".cmd", ".ini", ".conf", ".cfg", ".sql"};

    for (int i = 0; i < sizeof(text_exts) / sizeof(text_exts[0]); i++) {
      if (_stricmp(ext, text_exts[i]) == 0) {
        return TRUE; // Text file
      }
    }
  }

  // If we can't determine by extension, check the first few bytes
  FILE *file = fopen(filename, "rb");
  if (!file) {
    return FALSE; // Can't open, assume it's not text
  }

  // Read the first 512 bytes
  unsigned char buffer[512];
  size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
  fclose(file);

  if (bytes_read == 0) {
    return TRUE; // Empty file, consider it text
  }

  // Check for binary content
  int binary_chars = 0;
  int text_chars = 0;

  for (size_t i = 0; i < bytes_read; i++) {
    // Null bytes, control chars (except tab, CR, LF)
    if (buffer[i] == 0 || (buffer[i] < 32 && buffer[i] != '\t' &&
                           buffer[i] != '\n' && buffer[i] != '\r')) {
      binary_chars++;
    } else if (buffer[i] >= 32 && buffer[i] < 127) {
      // Printable ASCII chars
      text_chars++;
    }

    // Early detection for common binary file signatures
    if (i == 0) {
      // Check for executable/library signatures
      if (buffer[0] == 'M' && buffer[1] == 'Z') { // EXE/DLL
        return FALSE;
      }
      // Check for image signatures
      if (buffer[0] == 0xFF && buffer[1] == 0xD8 && buffer[2] == 0xFF) { // JPEG
        return FALSE;
      }
      if (buffer[0] == 0x89 && buffer[1] == 'P' && buffer[2] == 'N' &&
          buffer[3] == 'G') { // PNG
        return FALSE;
      }
      if (buffer[0] == 'G' && buffer[1] == 'I' && buffer[2] == 'F' &&
          buffer[3] == '8') { // GIF
        return FALSE;
      }
      // Check for archive signatures
      if (buffer[0] == 'P' && buffer[1] == 'K') { // ZIP
        return FALSE;
      }
      if (buffer[0] == 0x52 && buffer[1] == 0x61 && buffer[2] == 0x72 &&
          buffer[3] == 0x21) { // RAR
        return FALSE;
      }
    }
  }

  // If more than 10% binary or less than 50% text, consider it binary
  return (binary_chars < bytes_read / 10 && text_chars > bytes_read / 2);
}

/**
 * Open the file in an appropriate editor at the specified line
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
      // Try VSCode
      FILE *test_code = _popen("code --version 2>nul", "r");
      if (test_code != NULL) {
        _pclose(test_code);
        // VSCode uses filename:line_number syntax
        snprintf(command, sizeof(command), "code -g \"%s:%d\"", file_path,
                 line_number);
        success = 1;
      } else {
        // Fall back to notepad (which doesn't support line numbers)
        snprintf(command, sizeof(command), "notepad \"%s\"", file_path);
        success = 1;
      }
    }
  }

  if (success) {
    // Execute the command
    system(command);
    return 1;
  } else {
    // No suitable editor found
    printf("No compatible editor found.\n");
    printf("Press any key to continue...\n");
    _getch();
    return 0;
  }
}

/**
 * Command handler for the "grep" command
 * Usage: grep [options] pattern [file/directory]
 * Options:
 *   -n, --line-numbers  Show line numbers
 *   -i, --ignore-case   Ignore case distinctions
 *   -r, --recursive     Search directories recursively
 *   -f, --fuzzy         Use fuzzy matching instead of exact
 */
int lsh_grep(char **args) {
  if (args[1] == NULL) {
    // No arguments provided, launch interactive mode
    run_grep_interactive_session();
    return 1;
  }

  // Parse options
  int arg_index = 1;
  int line_numbers = 0;
  BOOL recursive = FALSE;
  SearchMode mode = SEARCH_MODE_PLAIN;

  // Process options
  while (args[arg_index] != NULL && args[arg_index][0] == '-') {
    if (strcmp(args[arg_index], "-n") == 0 ||
        strcmp(args[arg_index], "--line-numbers") == 0) {
      line_numbers = 1;
      arg_index++;
    } else if (strcmp(args[arg_index], "-i") == 0 ||
               strcmp(args[arg_index], "--ignore-case") == 0) {
      mode = SEARCH_MODE_IGNORE_CASE;
      arg_index++;
    } else if (strcmp(args[arg_index], "-r") == 0 ||
               strcmp(args[arg_index], "--recursive") == 0) {
      recursive = TRUE;
      arg_index++;
    } else if (strcmp(args[arg_index], "-f") == 0 ||
               strcmp(args[arg_index], "--fuzzy") == 0) {
      mode = SEARCH_MODE_FUZZY;
      arg_index++;
    } else if (strcmp(args[arg_index], "--file") == 0) {
      // Stop here - this marks the beginning of file arguments
      break;
    } else {
      printf("grep: unknown option: %s\n", args[arg_index]);
      return 1;
    }
  }

  // Check if we have any arguments left for the pattern
  if (args[arg_index] == NULL) {
    printf("grep: missing pattern\n");
    return 1;
  }

  // Collect all arguments into the pattern until we hit --file or end of args
  char pattern_buffer[4096] = "";
  int pattern_start_index = arg_index;

  // Find where the file args start (if any)
  int file_args_start = -1;
  for (int i = arg_index; args[i] != NULL; i++) {
    if (strcmp(args[i], "--file") == 0) {
      file_args_start = i + 1; // Start right after the --file flag
      break;
    }
  }

  // If we found --file, only use args up to that point for the pattern
  int pattern_end_index = (file_args_start > 0) ? file_args_start - 2 : -1;

  // If we didn't find --file, use all remaining args for the pattern
  if (pattern_end_index < 0) {
    while (args[arg_index] != NULL) {
      // Add space between pattern parts except before the first one
      if (arg_index > pattern_start_index) {
        strcat(pattern_buffer, " ");
      }
      strcat(pattern_buffer, args[arg_index]);
      arg_index++;
    }
  } else {
    // Use only args up to the --file flag for the pattern
    for (int i = pattern_start_index; i <= pattern_end_index; i++) {
      // Add space between pattern parts except before the first one
      if (i > pattern_start_index) {
        strcat(pattern_buffer, " ");
      }
      strcat(pattern_buffer, args[i]);
    }
    arg_index = file_args_start; // Move to the file arguments
  }

  const char *pattern = pattern_buffer;

  // Create mutex for thread synchronization
  result_mutex = CreateMutex(NULL, FALSE, NULL);
  if (result_mutex == NULL) {
    printf("grep: failed to create mutex\n");
    return 1;
  }

  // Reset grep results if any previous search was done
  free_grep_results();

  // Convert pattern to lowercase for case-insensitive search if needed
  char *pattern_lower = NULL;
  if (mode == SEARCH_MODE_IGNORE_CASE) {
    pattern_lower = _strdup(pattern);
    if (pattern_lower) {
      for (char *p = pattern_lower; *p; p++) {
        *p = tolower(*p);
      }
    }
  }

  // Display search mode info
  printf("Searching for: \"%s\" (", pattern);
  if (mode == SEARCH_MODE_FUZZY) {
    printf("fuzzy matching");
  } else if (mode == SEARCH_MODE_IGNORE_CASE) {
    printf("case insensitive");
  } else {
    printf("exact matching");
  }
  printf(")\n");

  // Start time measurement
  clock_t start_time = clock();

  // Check if specific files/directories were specified
  if (file_args_start < 0 || args[file_args_start] == NULL) {
    // No files specified, search current directory
    search_directory(".", pattern, pattern_lower, mode, line_numbers,
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
        search_directory(args[arg_index], pattern, pattern_lower, mode,
                         line_numbers, recursive);
      } else {
        // It's a file
        search_file(args[arg_index], pattern, pattern_lower, mode,
                    line_numbers);
      }

      arg_index++;
    }
  }

  // End time measurement
  clock_t end_time = clock();
  double search_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;

  // Clean up mutex and pattern
  CloseHandle(result_mutex);
  result_mutex = NULL;

  if (pattern_lower) {
    free(pattern_lower);
  }

  // Display the interactive results if any were found
  if (grep_results.count > 0) {
    printf("Found %d matches in %.2f seconds\n", grep_results.count,
           search_time);
    display_grep_results();
  } else {
    printf("No matches found for pattern: \"%s\" (search completed in %.2f "
           "seconds)\n",
           pattern, search_time);
  }

  // Clean up
  free_grep_results();

  return 1;
}

/**
 * Actual grep implementation with simpler interface
 */
int lsh_actual_grep(char **args) { return lsh_grep(args); }
