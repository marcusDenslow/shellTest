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
#define COLOR_BOX                                                              \
  FOREGROUND_BLUE | FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY

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
static void print_file_preview(HANDLE hConsole, GrepResult *match,
                               WORD originalAttrs, int right_width,
                               int left_width);

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
    printf("Usage: grep [options] pattern_words... [--file "
           "file_or_directory...]\n");
    printf("Options:\n");
    printf("  -n, --line-numbers  Show line numbers\n");
    printf("  -i, --ignore-case   Ignore case distinctions\n");
    printf("  -r, --recursive     Search directories recursively\n");
    printf("  --file              Specify files/directories to search "
           "(otherwise searches current dir)\n");
    printf("\nExamples:\n");
    printf("  grep #include \"common.h\"       - Search for '#include "
           "\"common.h\"' in current directory\n");
    printf("  grep -i hello world            - Search for 'hello world' "
           "case-insensitively\n");
    printf("  grep TODO --file *.c           - Search for 'TODO' in all .c "
           "files\n");
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
      arg_index++;
    } else if (strcmp(args[arg_index], "-i") == 0 ||
               strcmp(args[arg_index], "--ignore-case") == 0) {
      ignore_case = 1;
      arg_index++;
    } else if (strcmp(args[arg_index], "-r") == 0 ||
               strcmp(args[arg_index], "--recursive") == 0) {
      recursive = 1;
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

  // Reset grep results if any previous search was done
  free_grep_results();

  // Check if a file/directory was specified after --file
  if (file_args_start < 0 || args[file_args_start] == NULL) {
    // No files specified, search current directory
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
    printf("Found %d matches for pattern: \"%s\"\n", grep_results.count,
           pattern);
    printf("Press TAB to navigate through results...\n");
    display_grep_results();
  } else {
    printf("No matches found for pattern: \"%s\"\n", pattern);
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

/**
 * Helper function to print the file preview for a grep result
 * Completely rewritten to fix display issues
 */
static void print_file_preview(HANDLE hConsole, GrepResult *match,
                               WORD originalAttrs, int right_width,
                               int left_width) {
  // Get the file path
  char *filename = match->filename;

  // Try to open the file for preview
  FILE *preview_file = fopen(filename, "r");
  if (!preview_file) {
    // If we can't open the file, just show the matched text
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("<Could not open file>");
    return;
  }

  // Set up variables for context display
  int target_line = match->line_number;
  int context_lines = 2; // Reduced context to avoid display issues - show just
                         // 2 lines before/after
  int start_line =
      (target_line <= context_lines) ? 1 : target_line - context_lines;
  int end_line = target_line + context_lines;
  int current_line = 1;
  char line_buffer[512];

  // We'll collect context lines first, then display them
  char context_lines_text[5][256] = {
      0}; // Max 5 lines (2 before, match, 2 after)
  int lines_collected = 0;

  // Skip to start line
  while (current_line < start_line &&
         fgets(line_buffer, sizeof(line_buffer), preview_file)) {
    current_line++;
  }

  // Collect context lines
  while (current_line <= end_line &&
         fgets(line_buffer, sizeof(line_buffer), preview_file) &&
         lines_collected < 5) {
    // Remove newline
    size_t len = strlen(line_buffer);
    if (len > 0 && line_buffer[len - 1] == '\n') {
      line_buffer[len - 1] = '\0';
      len--;
    }

    // Truncate line if too long to fit in display
    int max_display_width = right_width - 8; // Leave some margin
    if (max_display_width < 20)
      max_display_width = 20; // Minimum width

    if ((int)len > max_display_width) {
      line_buffer[max_display_width - 3] = '.';
      line_buffer[max_display_width - 2] = '.';
      line_buffer[max_display_width - 1] = '.';
      line_buffer[max_display_width] = '\0';
    }

    // Store the line with its line number
    snprintf(context_lines_text[lines_collected], 256, "%4d| %s", current_line,
             line_buffer);

    lines_collected++;
    current_line++;
  }

  fclose(preview_file);

  // Now display the collected lines
  if (lines_collected > 0) {
    int match_line_idx = target_line - start_line;

    for (int i = 0; i < lines_collected; i++) {
      if (i > 0) {
        // Move to next line and add separator for each line after the first
        printf("\n%*s│ ", left_width, "");
      }

      if (i == match_line_idx) {
        // This is the match line - add highlight
        SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
        printf("► ");

        // Add match highlighting within the line
        char *line_text = context_lines_text[i];
        char *line_content_start = strchr(line_text, '|');

        if (line_content_start) {
          line_content_start += 2; // Skip "| "

          // Try to locate the match text in the line
          char match_text[256] = {0};
          strncpy(match_text, match->line_content + match->match_start,
                  match->match_length < 255 ? match->match_length : 255);
          match_text[match->match_length < 255 ? match->match_length : 255] =
              '\0';

          char *match_in_line = strstr(line_content_start, match_text);

          if (match_in_line) {
            // Print before match
            int prefix_len = match_in_line - line_content_start;
            SetConsoleTextAttribute(hConsole, originalAttrs);
            printf("%.*s", prefix_len + (line_content_start - line_text),
                   line_text);

            // Print match with highlight
            SetConsoleTextAttribute(hConsole, COLOR_MATCH);
            printf("%.*s", match->match_length, match_in_line);

            // Print rest of line
            SetConsoleTextAttribute(hConsole, originalAttrs);
            printf("%s", match_in_line + match->match_length);
          } else {
            // Just print the whole line if match not found
            SetConsoleTextAttribute(hConsole, originalAttrs);
            printf("%s", line_text);
          }
        } else {
          // Just print the line if pipe not found
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("%s", line_text);
        }
      } else {
        // Regular context line
        SetConsoleTextAttribute(hConsole, originalAttrs);
        printf("  %s", context_lines_text[i]);
      }
    }
  } else {
    // No lines collected
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("<No preview available>");
  }

  // Always reset attributes at the end
  SetConsoleTextAttribute(hConsole, originalAttrs);
}

/**
 * Display grep results with completely rewritten drawing logic to fix display
 * issues
 */
static void display_grep_results() {
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
        console_height - 7; // Reserve space for headers and footers
    if (list_height < 5)
      list_height = 5; // Minimum reasonable size

    // Calculate preview area - completely separate from list
    int preview_top =
        5; // Increased from 3 to accommodate file path and empty line
    int preview_height = 20; // Increased to show more context lines

    // Make sure preview area fits in the console
    if (preview_height > list_height - 2)
      preview_height = list_height - 2;

    // Only redraw everything if dimensions changed or first time
    BOOL full_redraw = (previous_index == -1);

    if (full_redraw) {
      // Draw header
      SetConsoleCursorPosition(hConsole, (COORD){0, 0});
      SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
      printf("Grep Results (%d matches)", grep_results.count);

      // Move to next line for separator
      COORD sepPos = {0, 1};
      SetConsoleCursorPosition(hConsole, sepPos);

      // Draw separator
      SetConsoleTextAttribute(hConsole, COLOR_BOX);
      for (int i = 0; i < console_width; i++)
        printf("─");

      // Add extra space after separator
      printf("\n\n");
    }

    // Calculate visible range
    int visible_items =
        list_height < grep_results.count ? list_height : grep_results.count;
    int start_index = 0;

    // Adjust start index to keep selection in view
    if (grep_results.current_index >= list_height) {
      start_index = grep_results.current_index - list_height + 1;

      // Ensure we don't go past the end
      if (start_index + visible_items > grep_results.count) {
        start_index = grep_results.count - visible_items;
      }
    }

    // If the visible range changed, we need a full redraw
    static int previous_start_index = -1;
    if (previous_start_index != start_index) {
      full_redraw = TRUE;
      previous_start_index = start_index;
    }

    // Only redraw the list if needed
    if (full_redraw || previous_index != grep_results.current_index) {
      // Draw the file list first (complete separate entity)
      for (int i = 0; i < visible_items; i++) {
        int result_idx = start_index + i;
        GrepResult *result = &grep_results.results[result_idx];

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
        if (result_idx == grep_results.current_index) {
          SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
          printf("→ ");
        } else {
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("  ");
        }

        // Print filename and line number
        printf("%-*s:%4d", left_width - 10, display_name, result->line_number);
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

      // Clear preview area first
      SetConsoleTextAttribute(hConsole, originalAttrs);
      for (int i = 0; i < preview_height; i++) {
        SetConsoleCursorPosition(hConsole,
                                 (COORD){left_width + 3, preview_top + i});
        printf("%-*s", right_width, "");
      }

      // Get current result's filename for display
      GrepResult *current = &grep_results.results[grep_results.current_index];

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
      if (grep_results.current_index >= 0 &&
          grep_results.current_index < grep_results.count) {
        GrepResult *current = &grep_results.results[grep_results.current_index];

        // Position for preview content (skip title line)
        SetConsoleCursorPosition(hConsole,
                                 (COORD){left_width + 3, preview_top + 1});

        // Load preview lines directly - with expanded context
        FILE *preview_file = fopen(current->filename, "r");
        if (preview_file) {
          int target_line = current->line_number;

          // Increased context lines - show roughly (preview_height-1)/2 lines
          // before and after
          int context_lines = (preview_height - 2) / 2;

          int start_line =
              (target_line <= context_lines) ? 1 : target_line - context_lines;
          int line_to_show = start_line;
          char buffer[512];

          // Skip to start line
          int line_count = 1;
          while (line_count < start_line &&
                 fgets(buffer, sizeof(buffer), preview_file)) {
            line_count++;
          }

          // Show preview lines (one per line in the preview area)
          for (int i = 0; i < preview_height - 1 &&
                          fgets(buffer, sizeof(buffer), preview_file);
               i++) {
            // Position cursor for this preview line
            SetConsoleCursorPosition(
                hConsole, (COORD){left_width + 3, preview_top + 1 + i});

            // Remove newline
            size_t len = strlen(buffer);
            if (len > 0 && buffer[len - 1] == '\n') {
              buffer[len - 1] = '\0';
              len--;
            }

            // Truncate if too long
            if (len > (size_t)right_width - 10) {
              buffer[right_width - 10] = '.';
              buffer[right_width - 9] = '.';
              buffer[right_width - 8] = '.';
              buffer[right_width - 7] = '\0';
            }

            // Highlight the match line
            if (line_to_show == target_line) {
              SetConsoleTextAttribute(hConsole, COLOR_RESULT_HIGHLIGHT);
              printf("► ");
            } else {
              SetConsoleTextAttribute(hConsole, originalAttrs);
              printf("  ");
            }

            printf("%4d| ", line_to_show);

            // Show content with match highlighting if current line
            if (line_to_show == target_line) {
              // Try to highlight the matching text
              char *line_start = buffer;
              char *match_text = current->line_content + current->match_start;
              int match_len = current->match_length;
              char *match_pos = strstr(line_start, match_text);

              if (match_pos) {
                // Print before match
                int prefix_len = match_pos - line_start;
                SetConsoleTextAttribute(hConsole, originalAttrs);
                printf("%.*s", prefix_len, line_start);

                // Print match with highlight
                SetConsoleTextAttribute(hConsole, COLOR_MATCH);
                printf("%.*s", match_len, match_pos);

                // Print after match
                SetConsoleTextAttribute(hConsole, originalAttrs);
                printf("%s", match_pos + match_len);
              } else {
                SetConsoleTextAttribute(hConsole, originalAttrs);
                printf("%s", buffer);
              }
            } else {
              // Regular line - no highlighting
              SetConsoleTextAttribute(hConsole, originalAttrs);
              printf("%s", buffer);
            }

            line_to_show++;
          }

          fclose(preview_file);
        } else {
          SetConsoleTextAttribute(hConsole, originalAttrs);
          printf("Could not open file for preview");
        }
      }

      // Remember what we just displayed
      previous_index = grep_results.current_index;
    }

    // Always update file info and help text
    // Show file information
    SetConsoleCursorPosition(hConsole, (COORD){0, 3 + list_height});
    SetConsoleTextAttribute(hConsole, COLOR_BOX);
    for (int i = 0; i < console_width; i++)
      printf("─");

    // Clear the file info line (since we now show it at the top of preview)
    SetConsoleCursorPosition(hConsole, (COORD){0, 4 + list_height});
    SetConsoleTextAttribute(hConsole, originalAttrs);
    // Print spaces to clear the line
    for (int i = 0; i < console_width; i++) {
      printf(" ");
    }

    // Navigation help - updated for swapped keys
    SetConsoleCursorPosition(hConsole, (COORD){0, 5 + list_height});
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("Navigation: TAB/j/DOWN - Next  SHIFT+TAB/k/UP - Prev  ENTER - Open "
           "in Editor  o - Detail View  ESC/Q - Exit");

    // Position cursor at the bottom of the screen for user input
    SetConsoleCursorPosition(hConsole, (COORD){0, 6 + list_height});

    // Make cursor visible now that drawing is complete
    cursorInfo.bVisible = TRUE;
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

        // Navigation keys
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
        } else if (keyCode == VK_UP || keyCode == 'K') {
          // Up arrow or K - Previous result
          if (grep_results.current_index > 0) {
            grep_results.current_index--;
          } else {
            grep_results.current_index = grep_results.count - 1;
          }
        } else if (keyCode == VK_DOWN || keyCode == 'J') {
          // Down arrow or J - Next result
          grep_results.current_index =
              (grep_results.current_index + 1) % grep_results.count;
        } else if (keyCode == VK_RETURN) {
          // ENTER - Now opens directly in editor
          open_file_in_editor(
              grep_results.results[grep_results.current_index].filename,
              grep_results.results[grep_results.current_index].line_number);

          // Need a full redraw after returning from editor
          full_redraw = TRUE;
          previous_index = -1;
          system("cls");
        } else if (keyCode == 'O') {
          // 'O' - Now shows detail view
          show_file_detail_view(
              &grep_results.results[grep_results.current_index]);

          // Need a full redraw after returning from detail view
          full_redraw = TRUE;
          previous_index = -1;
          system("cls");
        } else if (keyCode == VK_ESCAPE || keyCode == 'Q') {
          // Escape or Q - Exit
          grep_results.is_active = FALSE;
        }
      }
    }
  }

  // Final cleanup
  SetConsoleTextAttribute(hConsole, originalAttrs);
  SetConsoleMode(hStdin, originalMode);

  // Restore original cursor info
  SetConsoleCursorInfo(hConsole, &originalCursorInfo);

  system("cls");

  // Position cursor properly for next prompt
  COORD topPos = {0, 0};
  SetConsoleCursorPosition(hConsole, topPos);
  printf("\n");
}

/**
 * Show detailed view of a grep result
 * Simplified to ensure consistent display
 */
void show_file_detail_view(GrepResult *result) {
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
    int context_lines = 10;
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
        printf("%4d → ", current_line);

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
