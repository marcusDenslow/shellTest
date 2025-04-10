/**
 * builtins.c hello L
 * Implementation of all built-in shell commands
 */

#include "builtins.h"
#include "common.h"
#include "filters.h"
#include "fzf_native.h"
#include "grep.h"
#include "persistent_history.h"
#include "structured_data.h"
#include "themes.h"
#include <Psapi.h>
#include <ShlObj.h>
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <ole2.h>
#include <olectl.h>
#include <stddef.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <winbase.h>
#include <wincrypt.h>
#include <wininet.h>
#include <winuser.h>
#pragma comment(lib, "wininet.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

#define BUFFER_SIZE 8192
#define GITHUB_API_HOST "api.github.com"
#define GITHUB_API_PATH "/repos/marcusDenslow/shellTest/commits"

// History command implementation
#define HISTORY_SIZE 10

// Structure to store file info for directory listing
typedef struct {
  char timeString[32];
  char sizeString[32];
  char fileType[16];
  char fileName[MAX_PATH];
  BOOL isDirectory;
} FileInfo;

// Compare function for sorting directory entries
int compare_dir_entries(const void *a, const void *b) {
  const FileInfo *fa = (const FileInfo *)a;
  const FileInfo *fb = (const FileInfo *)b;

  // First sort by type (directories first)
  if (fa->isDirectory && !fb->isDirectory)
    return -1;
  if (!fa->isDirectory && fb->isDirectory)
    return 1;

  // Then sort by name
  return _stricmp(fa->fileName, fb->fileName);
}

HistoryEntry command_history[HISTORY_SIZE] = {0};
int history_count = 0; // Total number of commands entered
int history_index = 0; // Current index in the circular buffer

char *builtin_str[] = {
    "cd",       "help",      "exit",        "ls",
    "dir",      "clear",     "cls",         "mkdir",
    "rmdir",    "del",       "rm",          "touch",
    "pwd",      "cat",       "history",     "copy",
    "cp",       "paste",     "move",        "mv",
    "ps",       "news",      "focus-timer", "timer",
    "alias",   // Added for alias support
    "unalias", // Added for alias support
    "aliases", // New command to list all aliases
    "bookmark", "bookmarks", "goto",        "unbookmark",
    "weather",  "grep",      "cities",      "fzf",
    "ripgrep",  "clip",      "echo",        "self-destruct",
    "theme",    "loc",
};

// Add to the builtin_func array:
int (*builtin_func[])(char **) = {
    &lsh_cd,
    &lsh_help,
    &lsh_exit,
    &lsh_dir,
    &lsh_dir,
    &lsh_clear,
    &lsh_clear,
    &lsh_mkdir,
    &lsh_rmdir,
    &lsh_del,
    &lsh_del,
    &lsh_touch,
    &lsh_pwd,
    &lsh_cat,
    &lsh_history,
    &lsh_copy,
    &lsh_copy,
    &lsh_paste,
    &lsh_move,
    &lsh_move,
    &lsh_ps,
    &lsh_news,
    &lsh_focus_timer,
    &lsh_focus_timer,
    &lsh_alias,   // Added for alias support
    &lsh_unalias, // Added for alias support
    &lsh_aliases,
    &lsh_bookmark,
    &lsh_bookmarks,
    &lsh_goto,
    &lsh_unbookmark,
    &lsh_weather,
    &lsh_grep,
    &lsh_cities,
    &lsh_fzf_native,
    &lsh_ripgrep,
    &lsh_clip,
    &lsh_echo,
    &lsh_self_destruct,
    &lsh_theme,
    &lsh_loc,
};

// Return the number of built-in commands
int lsh_num_builtins() { return sizeof(builtin_str) / sizeof(char *); }

/**
 * Command handler for the "aliases" command
 * Simply displays all defined aliases
 */

void lsh_add_to_history(const char *command) {
  if (command == NULL || command[0] == '\0') {
    return; // Don't add empty commands
  }

  // Add to in-memory history for the current session
  // Free the string at the current index if it exists
  if (command_history[history_index].command != NULL) {
    free(command_history[history_index].command);
  }

  // Save the current command
  command_history[history_index].command = _strdup(command);

  // Save the current time
  GetLocalTime(&command_history[history_index].timestamp);

  // Update index and count
  history_index = (history_index + 1) % HISTORY_SIZE;
  history_count++;

  // Also add to persistent history
  add_to_persistent_history(command);
}

int lsh_history(char **args) {
  int history_count = get_history_count();

  if (history_count == 0) {
    printf("No commands in history\n");
    return 1;
  }

  // Determine how many entries to display
  int entries_to_show = history_count;

  // If the user specified a number, use that
  if (args[1] != NULL) {
    int requested = atoi(args[1]);
    if (requested > 0 && requested < history_count) {
      entries_to_show = requested;
    }
  }

  // Calculate the starting index for display
  int start_idx = history_count - entries_to_show;
  if (start_idx < 0)
    start_idx = 0;

  // Display the entries with timestamps
  printf("\nCommand History (most recent last):\n\n");
  for (int i = start_idx; i < history_count; i++) {
    PersistentHistoryEntry *entry = get_history_entry(i);
    if (entry && entry->command) {
      SYSTEMTIME *ts = &entry->timestamp;
      printf("[%04d-%02d-%02d %02d:%02d:%02d] %s\n", ts->wYear, ts->wMonth,
             ts->wDay, ts->wHour, ts->wMinute, ts->wSecond, entry->command);
    }
  }

  return 1;
}

// Print current working directory
int lsh_pwd(char **args) {
  char cwd[1024];

  if (_getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("lsh: pwd");
    return 1;
  }

  printf("\n%s\n\n", cwd);
  return 1;
}

/**
 * Determine file type from extension
 */
typedef enum {
  FILE_TYPE_TEXT,
  FILE_TYPE_C,
  FILE_TYPE_CPP,
  FILE_TYPE_H,
  FILE_TYPE_PY,
  FILE_TYPE_JS,
  FILE_TYPE_HTML,
  FILE_TYPE_CSS,
  FILE_TYPE_MD,
  FILE_TYPE_JSON
} FileType;

/**
 * Get file type from filename
 */
FileType get_file_type(const char *filename) {
  char *ext = strrchr(filename, '.');
  if (ext == NULL)
    return FILE_TYPE_TEXT;

  ext++; // Skip the dot

  if (_stricmp(ext, "c") == 0)
    return FILE_TYPE_C;
  if (_stricmp(ext, "cpp") == 0 || _stricmp(ext, "cc") == 0)
    return FILE_TYPE_CPP;
  if (_stricmp(ext, "h") == 0 || _stricmp(ext, "hpp") == 0)
    return FILE_TYPE_H;
  if (_stricmp(ext, "py") == 0)
    return FILE_TYPE_PY;
  if (_stricmp(ext, "js") == 0)
    return FILE_TYPE_JS;
  if (_stricmp(ext, "html") == 0 || _stricmp(ext, "htm") == 0)
    return FILE_TYPE_HTML;
  if (_stricmp(ext, "css") == 0)
    return FILE_TYPE_CSS;
  if (_stricmp(ext, "md") == 0 || _stricmp(ext, "markdown") == 0)
    return FILE_TYPE_MD;
  if (_stricmp(ext, "json") == 0)
    return FILE_TYPE_JSON;

  return FILE_TYPE_TEXT;
}

/**
 * Set console text color
 */
void set_color(int color_role) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD color;

  // Map syntax roles to theme colors
  switch (color_role) {
  case COLOR_KEYWORD:
    color = current_theme.SYNTAX_KEYWORD;
    break;
  case COLOR_STRING:
    color = current_theme.SYNTAX_STRING;
    break;
  case COLOR_COMMENT:
    color = current_theme.SYNTAX_COMMENT;
    break;
  case COLOR_NUMBER:
    color = current_theme.SYNTAX_NUMBER;
    break;
  case COLOR_PREPROCESSOR:
    color = current_theme.SYNTAX_PREPROCESSOR;
    break;
  case COLOR_IDENTIFIER:
    color = current_theme.SECONDARY_COLOR;
    break;
  default:
    color = current_theme.PRIMARY_COLOR;
    break;
  }

  SetConsoleTextAttribute(hConsole, color);
}
/**
 * Reset console text color to default
 */
void reset_color() {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);
}
/**
 * Check if a character is a word boundary character
 */
int is_separator(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '(' ||
         c == ')' || c == '[' || c == ']' || c == '{' || c == '}' || c == '.' ||
         c == ',' || c == ';' || c == ':' || c == '+' || c == '-' || c == '/' ||
         c == '*' || c == '%' || c == '=' || c == '<' || c == '>' || c == '&' ||
         c == '|' || c == '^' || c == '!' || c == '~' || c == '?' || c == '"' ||
         c == '\'' || c == '\\';
}

/**
 * Check if string matches any keyword in the array
 */
int is_keyword(const char *word, const char **keywords, int num_keywords) {
  for (int i = 0; i < num_keywords; i++) {
    if (strcmp(word, keywords[i]) == 0) {
      return 1;
    }
  }
  return 0;
}

/**
 * Print C/C++ file with syntax highlighting
 */
void print_c_file_highlighted(FILE *file) {
  static const char *c_keywords[] = {
      "auto",     "break",  "case",    "char",   "const",    "continue",
      "default",  "do",     "double",  "else",   "enum",     "extern",
      "float",    "for",    "goto",    "if",     "int",      "long",
      "register", "return", "short",   "signed", "sizeof",   "static",
      "struct",   "switch", "typedef", "union",  "unsigned", "void",
      "volatile", "while",  "include", "define", "ifdef",    "ifndef",
      "endif",    "pragma", "error",   "warning"};
  static const int c_keywords_count =
      sizeof(c_keywords) / sizeof(c_keywords[0]);

  char line[4096];
  char word[256];
  int in_comment = 0;
  int in_string = 0;
  int in_char = 0;
  int in_preprocessor = 0;

  // Process file line by line
  while (fgets(line, sizeof(line), file) != NULL) {
    char *p = line;
    int word_pos = 0;

    while (*p) {
      // Already in a multi-line comment
      if (in_comment) {
        set_color(COLOR_COMMENT);
        putchar(*p);

        if (*p == '*' && *(p + 1) == '/') {
          putchar(*(p + 1));
          p++;
          in_comment = 0;
          reset_color();
        }
      }
      // Line starts with # (preprocessor directive)
      else if (p == line && *p == '#') {
        in_preprocessor = 1;
        set_color(COLOR_PREPROCESSOR);
        putchar(*p);
      }
      // Inside a string literal
      else if (in_string) {
        set_color(COLOR_STRING);
        putchar(*p);

        if (*p == '\\' && *(p + 1)) {
          putchar(*(p + 1));
          p++;
        } else if (*p == '"') {
          in_string = 0;
          reset_color();
        }
      }
      // Inside a character literal
      else if (in_char) {
        set_color(COLOR_STRING);
        putchar(*p);

        if (*p == '\\' && *(p + 1)) {
          putchar(*(p + 1));
          p++;
        } else if (*p == '\'') {
          in_char = 0;
          reset_color();
        }
      }
      // Start of a comment
      else if (*p == '/' && *(p + 1) == '/') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        set_color(COLOR_COMMENT);
        putchar(*p);
        putchar(*(p + 1));
        p++;

        // Print the rest of the line as a comment
        p++;
        while (*p) {
          putchar(*p++);
        }
        reset_color();
        break;
      }
      // Start of multi-line comment
      else if (*p == '/' && *(p + 1) == '*') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_comment = 1;
        set_color(COLOR_COMMENT);
        putchar(*p);
        putchar(*(p + 1));
        p++;
      }
      // Start of string literal
      else if (*p == '"') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_string = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Start of character literal
      else if (*p == '\'') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_char = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Word boundary
      else if (is_separator(*p)) {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';

          // Check if it's a keyword or a number
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            // Regular identifier
            printf("%s", word);
          }

          word_pos = 0;
        }

        // Print the separator
        if (in_preprocessor && *p != '\n') {
          set_color(COLOR_PREPROCESSOR);
          putchar(*p);
          reset_color();
        } else {
          putchar(*p);
        }

        // End of preprocessor directive
        if (in_preprocessor && *p == '\n') {
          in_preprocessor = 0;
          reset_color();
        }
      }
      // Part of a word
      else {
        if (word_pos < sizeof(word) - 1) {
          word[word_pos++] = *p;
        }
        // DON'T print the character here - we'll print the whole word when we
        // hit a boundary
      }

      p++;
    }

    // Handle word at end of line if there is one
    if (word_pos > 0) {
      word[word_pos] = '\0';

      if (is_keyword(word, c_keywords, c_keywords_count)) {
        set_color(COLOR_KEYWORD);
        printf("%s", word);
        reset_color();
      } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
        set_color(COLOR_NUMBER);
        printf("%s", word);
        reset_color();
      } else {
        printf("%s", word);
      }
    }
  }

  reset_color();
}
/**
 * Print Python file with syntax highlighting
 */
void print_py_file_highlighted(FILE *file) {
  static const char *py_keywords[] = {
      "and",  "as",       "assert", "break",  "class", "continue", "def",
      "del",  "elif",     "else",   "except", "False", "finally",  "for",
      "from", "global",   "if",     "import", "in",    "is",       "lambda",
      "None", "nonlocal", "not",    "or",     "pass",  "raise",    "return",
      "True", "try",      "while",  "with",   "yield"};
  static const int py_keywords_count =
      sizeof(py_keywords) / sizeof(py_keywords[0]);

  char line[4096];
  char word[256];
  int in_string_single = 0;
  int in_string_double = 0;
  int in_triple_single = 0;
  int in_triple_double = 0;

  // Process file line by line
  while (fgets(line, sizeof(line), file) != NULL) {
    char *p = line;
    int word_pos = 0;

    while (*p) {
      // Already in a string
      if (in_string_single || in_string_double || in_triple_single ||
          in_triple_double) {
        set_color(COLOR_STRING);
        putchar(*p);

        if (in_string_single && *p == '\'') {
          in_string_single = 0;
          reset_color();
        } else if (in_string_double && *p == '"') {
          in_string_double = 0;
          reset_color();
        } else if (in_triple_single && *p == '\'' && *(p + 1) == '\'' &&
                   *(p + 2) == '\'') {
          putchar(*(p + 1));
          putchar(*(p + 2));
          p += 2;
          in_triple_single = 0;
          reset_color();
        } else if (in_triple_double && *p == '"' && *(p + 1) == '"' &&
                   *(p + 2) == '"') {
          putchar(*(p + 1));
          putchar(*(p + 2));
          p += 2;
          in_triple_double = 0;
          reset_color();
        }
      }
      // Start of a comment
      else if (*p == '#') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        set_color(COLOR_COMMENT);
        // Print the rest of the line as a comment
        while (*p) {
          putchar(*p++);
        }
        reset_color();
        break;
      }
      // Start of triple quoted string
      else if (*p == '\'' && *(p + 1) == '\'' && *(p + 2) == '\'') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_triple_single = 1;
        set_color(COLOR_STRING);
        putchar(*p);
        putchar(*(p + 1));
        putchar(*(p + 2));
        p += 2;
      } else if (*p == '"' && *(p + 1) == '"' && *(p + 2) == '"') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_triple_double = 1;
        set_color(COLOR_STRING);
        putchar(*p);
        putchar(*(p + 1));
        putchar(*(p + 2));
        p += 2;
      }
      // Start of single quoted string
      else if (*p == '\'') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_string_single = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Start of double quoted string
      else if (*p == '"') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_string_double = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Word boundary
      else if (is_separator(*p)) {
        if (word_pos > 0) {
          word[word_pos] = '\0';

          // Check if it's a keyword or a number
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            // Regular identifier
            printf("%s", word);
          }

          word_pos = 0;
        }

        // Print the separator
        putchar(*p);
      }
      // Part of a word
      else {
        if (word_pos < sizeof(word) - 1) {
          word[word_pos++] = *p;
        }
        // Don't print characters immediately - wait until we have a complete
        // word
      }

      p++;
    }

    // Handle word at end of line
    if (word_pos > 0) {
      word[word_pos] = '\0';

      if (is_keyword(word, py_keywords, py_keywords_count)) {
        set_color(COLOR_KEYWORD);
        printf("%s", word);
        reset_color();
      } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
        set_color(COLOR_NUMBER);
        printf("%s", word);
        reset_color();
      } else {
        printf("%s", word);
      }
    }
  }

  reset_color();
}
/**
 * Print HTML/XML file with syntax highlighting
 */
void print_html_file_highlighted(FILE *file) {
  char line[4096];
  int in_tag = 0;
  int in_attribute = 0;
  int in_string = 0;
  int in_comment = 0;

  // Process file line by line
  while (fgets(line, sizeof(line), file) != NULL) {
    char *p = line;

    while (*p) {
      // Inside a comment
      if (in_comment) {
        set_color(COLOR_COMMENT);
        putchar(*p);

        if (*p == '-' && *(p + 1) == '-' && *(p + 2) == '>') {
          putchar(*(p + 1));
          putchar(*(p + 2));
          p += 2;
          in_comment = 0;
          reset_color();
        }
      }
      // Start of a comment
      else if (*p == '<' && *(p + 1) == '!' && *(p + 2) == '-' &&
               *(p + 3) == '-') {
        in_comment = 1;
        set_color(COLOR_COMMENT);
        putchar(*p);
        putchar(*(p + 1));
        putchar(*(p + 2));
        putchar(*(p + 3));
        p += 3;
      }
      // Inside a string
      else if (in_string) {
        set_color(COLOR_STRING);
        putchar(*p);

        if ((*p == '"' || *p == '\'') && *(p - 1) != '\\') {
          in_string = 0;
          if (in_tag)
            set_color(COLOR_KEYWORD);
          else
            reset_color();
        }
      }
      // Start of a tag
      else if (*p == '<' && *(p + 1) != '!') {
        in_tag = 1;
        in_attribute = 0;
        set_color(COLOR_KEYWORD);
        putchar(*p);
      }
      // End of a tag
      else if (*p == '>' && in_tag) {
        in_tag = 0;
        in_attribute = 0;
        putchar(*p);
        reset_color();
      }
      // Inside a tag, start of attribute
      else if (in_tag && *p == ' ' && !in_attribute) {
        in_attribute = 1;
        putchar(*p);
        set_color(COLOR_IDENTIFIER);
      }
      // Start of string inside tag
      else if (in_tag && (*p == '"' || *p == '\'')) {
        in_string = 1;
        putchar(*p);
      }
      // Equals sign in attribute
      else if (in_tag && in_attribute && *p == '=') {
        putchar(*p);
        set_color(COLOR_KEYWORD);
      }
      // Just print the character
      else {
        putchar(*p);
      }

      p++;
    }
  }

  reset_color();
}
/**
 * Print C/C++ file with syntax highlighting and line numbers
 */
void print_c_file_highlighted_with_line_numbers(FILE *file) {
  static const char *c_keywords[] = {
      "auto",     "break",  "case",    "char",   "const",    "continue",
      "default",  "do",     "double",  "else",   "enum",     "extern",
      "float",    "for",    "goto",    "if",     "int",      "long",
      "register", "return", "short",   "signed", "sizeof",   "static",
      "struct",   "switch", "typedef", "union",  "unsigned", "void",
      "volatile", "while",  "include", "define", "ifdef",    "ifndef",
      "endif",    "pragma", "error",   "warning"};
  static const int c_keywords_count =
      sizeof(c_keywords) / sizeof(c_keywords[0]);

  char line[4096];
  char word[256];
  int in_comment = 0;
  int in_string = 0;
  int in_char = 0;
  int in_preprocessor = 0;
  int line_number = 1;

  // Process file line by line
  while (fgets(line, sizeof(line), file) != NULL) {
    // Print line number first
    printf("%5d\t", line_number++);

    char *p = line;
    int word_pos = 0;

    while (*p) {
      // Already in a multi-line comment
      if (in_comment) {
        set_color(COLOR_COMMENT);
        putchar(*p);

        if (*p == '*' && *(p + 1) == '/') {
          putchar(*(p + 1));
          p++;
          in_comment = 0;
          reset_color();
        }
      }
      // Line starts with # (preprocessor directive)
      else if (p == line && *p == '#') {
        in_preprocessor = 1;
        set_color(COLOR_PREPROCESSOR);
        putchar(*p);
      }
      // Inside a string literal
      else if (in_string) {
        set_color(COLOR_STRING);
        putchar(*p);

        if (*p == '\\' && *(p + 1)) {
          putchar(*(p + 1));
          p++;
        } else if (*p == '"') {
          in_string = 0;
          reset_color();
        }
      }
      // Inside a character literal
      else if (in_char) {
        set_color(COLOR_STRING);
        putchar(*p);

        if (*p == '\\' && *(p + 1)) {
          putchar(*(p + 1));
          p++;
        } else if (*p == '\'') {
          in_char = 0;
          reset_color();
        }
      }
      // Start of a comment
      else if (*p == '/' && *(p + 1) == '/') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        set_color(COLOR_COMMENT);
        putchar(*p);
        putchar(*(p + 1));
        p++;

        // Print the rest of the line as a comment
        p++;
        while (*p) {
          putchar(*p++);
        }
        reset_color();
        break;
      }
      // Start of multi-line comment
      else if (*p == '/' && *(p + 1) == '*') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_comment = 1;
        set_color(COLOR_COMMENT);
        putchar(*p);
        putchar(*(p + 1));
        p++;
      }
      // Start of string literal
      else if (*p == '"') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_string = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Start of character literal
      else if (*p == '\'') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_char = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Word boundary
      else if (is_separator(*p)) {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';

          // Check if it's a keyword or a number
          if (is_keyword(word, c_keywords, c_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            // Regular identifier
            printf("%s", word);
          }

          word_pos = 0;
        }

        // Print the separator
        if (in_preprocessor && *p != '\n') {
          set_color(COLOR_PREPROCESSOR);
          putchar(*p);
          reset_color();
        } else {
          putchar(*p);
        }

        // End of preprocessor directive
        if (in_preprocessor && *p == '\n') {
          in_preprocessor = 0;
          reset_color();
        }
      }
      // Part of a word
      else {
        if (word_pos < sizeof(word) - 1) {
          word[word_pos++] = *p;
        }
        // DON'T print the character here - we'll print the whole word when we
        // hit a boundary
      }

      p++;
    }

    // Handle word at end of line if there is one
    if (word_pos > 0) {
      word[word_pos] = '\0';

      if (is_keyword(word, c_keywords, c_keywords_count)) {
        set_color(COLOR_KEYWORD);
        printf("%s", word);
        reset_color();
      } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
        set_color(COLOR_NUMBER);
        printf("%s", word);
        reset_color();
      } else {
        printf("%s", word);
      }
    }
  }

  reset_color();
}

/**
 * Print Python file with syntax highlighting and line numbers
 */
void print_py_file_highlighted_with_line_numbers(FILE *file) {
  static const char *py_keywords[] = {
      "and",  "as",       "assert", "break",  "class", "continue", "def",
      "del",  "elif",     "else",   "except", "False", "finally",  "for",
      "from", "global",   "if",     "import", "in",    "is",       "lambda",
      "None", "nonlocal", "not",    "or",     "pass",  "raise",    "return",
      "True", "try",      "while",  "with",   "yield"};
  static const int py_keywords_count =
      sizeof(py_keywords) / sizeof(py_keywords[0]);

  char line[4096];
  char word[256];
  int in_string_single = 0;
  int in_string_double = 0;
  int in_triple_single = 0;
  int in_triple_double = 0;
  int line_number = 1;

  // Process file line by line
  while (fgets(line, sizeof(line), file) != NULL) {
    // Print line number first
    printf("%5d\t", line_number++);

    char *p = line;
    int word_pos = 0;

    while (*p) {
      // Already in a string
      if (in_string_single || in_string_double || in_triple_single ||
          in_triple_double) {
        set_color(COLOR_STRING);
        putchar(*p);

        if (in_string_single && *p == '\'') {
          in_string_single = 0;
          reset_color();
        } else if (in_string_double && *p == '"') {
          in_string_double = 0;
          reset_color();
        } else if (in_triple_single && *p == '\'' && *(p + 1) == '\'' &&
                   *(p + 2) == '\'') {
          putchar(*(p + 1));
          putchar(*(p + 2));
          p += 2;
          in_triple_single = 0;
          reset_color();
        } else if (in_triple_double && *p == '"' && *(p + 1) == '"' &&
                   *(p + 2) == '"') {
          putchar(*(p + 1));
          putchar(*(p + 2));
          p += 2;
          in_triple_double = 0;
          reset_color();
        }
      }
      // Start of a comment
      else if (*p == '#') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        set_color(COLOR_COMMENT);
        // Print the rest of the line as a comment
        while (*p) {
          putchar(*p++);
        }
        reset_color();
        break;
      }
      // Start of triple quoted string
      else if (*p == '\'' && *(p + 1) == '\'' && *(p + 2) == '\'') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_triple_single = 1;
        set_color(COLOR_STRING);
        putchar(*p);
        putchar(*(p + 1));
        putchar(*(p + 2));
        p += 2;
      } else if (*p == '"' && *(p + 1) == '"' && *(p + 2) == '"') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_triple_double = 1;
        set_color(COLOR_STRING);
        putchar(*p);
        putchar(*(p + 1));
        putchar(*(p + 2));
        p += 2;
      }
      // Start of single quoted string
      else if (*p == '\'') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_string_single = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Start of double quoted string
      else if (*p == '"') {
        // Print any word we've been accumulating
        if (word_pos > 0) {
          word[word_pos] = '\0';
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            printf("%s", word);
          }
          word_pos = 0;
        }

        in_string_double = 1;
        set_color(COLOR_STRING);
        putchar(*p);
      }
      // Word boundary
      else if (is_separator(*p)) {
        if (word_pos > 0) {
          word[word_pos] = '\0';

          // Check if it's a keyword or a number
          if (is_keyword(word, py_keywords, py_keywords_count)) {
            set_color(COLOR_KEYWORD);
            printf("%s", word);
            reset_color();
          } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
            set_color(COLOR_NUMBER);
            printf("%s", word);
            reset_color();
          } else {
            // Regular identifier
            printf("%s", word);
          }

          word_pos = 0;
        }

        // Print the separator
        putchar(*p);
      }
      // Part of a word
      else {
        if (word_pos < sizeof(word) - 1) {
          word[word_pos++] = *p;
        }
        // Don't print characters immediately - wait until we have a complete
        // word
      }

      p++;
    }

    // Handle word at end of line
    if (word_pos > 0) {
      word[word_pos] = '\0';

      if (is_keyword(word, py_keywords, py_keywords_count)) {
        set_color(COLOR_KEYWORD);
        printf("%s", word);
        reset_color();
      } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
        set_color(COLOR_NUMBER);
        printf("%s", word);
        reset_color();
      } else {
        printf("%s", word);
      }
    }
  }

  reset_color();
}

/**
 * Print HTML/XML file with syntax highlighting and line numbers
 */
void print_html_file_highlighted_with_line_numbers(FILE *file) {
  char line[4096];
  int in_tag = 0;
  int in_attribute = 0;
  int in_string = 0;
  int in_comment = 0;
  int line_number = 1;

  // Process file line by line
  while (fgets(line, sizeof(line), file) != NULL) {
    // Print line number first
    printf("%5d\t", line_number++);

    char *p = line;

    while (*p) {
      // Inside a comment
      if (in_comment) {
        set_color(COLOR_COMMENT);
        putchar(*p);

        if (*p == '-' && *(p + 1) == '-' && *(p + 2) == '>') {
          putchar(*(p + 1));
          putchar(*(p + 2));
          p += 2;
          in_comment = 0;
          reset_color();
        }
      }
      // Start of a comment
      else if (*p == '<' && *(p + 1) == '!' && *(p + 2) == '-' &&
               *(p + 3) == '-') {
        in_comment = 1;
        set_color(COLOR_COMMENT);
        putchar(*p);
        putchar(*(p + 1));
        putchar(*(p + 2));
        putchar(*(p + 3));
        p += 3;
      }
      // Inside a string
      else if (in_string) {
        set_color(COLOR_STRING);
        putchar(*p);

        if ((*p == '"' || *p == '\'') && *(p - 1) != '\\') {
          in_string = 0;
          if (in_tag)
            set_color(COLOR_KEYWORD);
          else
            reset_color();
        }
      }
      // Start of a tag
      else if (*p == '<' && *(p + 1) != '!') {
        in_tag = 1;
        in_attribute = 0;
        set_color(COLOR_KEYWORD);
        putchar(*p);
      }
      // End of a tag
      else if (*p == '>' && in_tag) {
        in_tag = 0;
        in_attribute = 0;
        putchar(*p);
        reset_color();
      }
      // Inside a tag, start of attribute
      else if (in_tag && *p == ' ' && !in_attribute) {
        in_attribute = 1;
        putchar(*p);
        set_color(COLOR_IDENTIFIER);
      }
      // Start of string inside tag
      else if (in_tag && (*p == '"' || *p == '\'')) {
        in_string = 1;
        putchar(*p);
      }
      // Equals sign in attribute
      else if (in_tag && in_attribute && *p == '=') {
        putchar(*p);
        set_color(COLOR_KEYWORD);
      }
      // Just print the character
      else {
        putchar(*p);
      }

      p++;
    }
  }

  reset_color();
}

/**
 * Print a file with language-specific syntax highlighting and line numbers
 */
void print_file_with_highlighting(FILE *file, FileType type) {
  switch (type) {
  case FILE_TYPE_C:
  case FILE_TYPE_CPP:
  case FILE_TYPE_H:
    print_c_file_highlighted_with_line_numbers(file);
    break;
  case FILE_TYPE_PY:
    print_py_file_highlighted_with_line_numbers(file);
    break;
  case FILE_TYPE_HTML:
    print_html_file_highlighted_with_line_numbers(file);
    break;
  default:
    // For unsupported file types, just print the content with line numbers
    char line[4096];
    int line_number = 1;

    while (fgets(line, sizeof(line), file) != NULL) {
      printf("%5d\t%s", line_number++, line);

      // If the line doesn't end with a newline, add one
      if (strlen(line) > 0 && line[strlen(line) - 1] != '\n') {
        printf("\n");
      }
    }
    break;
  }
}

/**
 * Display file contents with optional line numbers and syntax highlighting
 */
int lsh_cat(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file argument to \"cat\"\n");
    return 1;
  }

  // Check for flags (e.g., -s for syntax highlighting)
  int use_highlighting = 0; // Default to no highlighting
  int use_line_numbers = 0; // Default to no line numbers
  int start_index = 1;

  if (args[1][0] == '-') {
    if (strcmp(args[1], "-s") == 0 || strcmp(args[1], "--syntax") == 0) {
      use_highlighting = 1;
      use_line_numbers = 1; // Syntax highlighting includes line numbers
      start_index = 2;
    } else if (strcmp(args[1], "-n") == 0 || strcmp(args[1], "--number") == 0) {
      use_line_numbers = 1; // Line numbers only
      start_index = 2;
    }

    if (start_index >= 2 && args[start_index] == NULL) {
      fprintf(stderr, "lsh: expected file argument after %s\n", args[1]);
      return 1;
    }
  }

  // Process each file argument
  int i = start_index;
  int success = 1;

  while (args[i] != NULL) {
    // Print filename and blank line before content
    printf("\n--- %s ---\n\n", args[i]);

    // Open the file
    FILE *file = fopen(args[i], "r");

    if (file == NULL) {
      fprintf(stderr, "lsh: cannot open '%s': ", args[i]);
      perror("");
      success = 0;
      i++;
      continue;
    }

    if (use_highlighting) {
      // Determine file type for highlighting
      FileType type = get_file_type(args[i]);

      // Use the highlighting function that includes line numbers
      print_file_with_highlighting(file, type);
    } else if (use_line_numbers) {
      // Read and print file with line numbers only (no highlighting)
      char line[4096];
      int line_number = 1;

      while (fgets(line, sizeof(line), file) != NULL) {
        // Print line number and tab
        printf("%5d\t", line_number++);

        // Print the line
        printf("%s", line);

        // If the line doesn't end with a newline, add one
        if (strlen(line) > 0 && line[strlen(line) - 1] != '\n') {
          printf("\n");
        }
      }
    } else {
      // Fast mode - read and write in binary chunks without line numbers
      char buffer[8192]; // Larger buffer for faster reads
      size_t bytes_read;

      // Set file and stdout to binary mode to avoid newline translations
      int old_file_mode = _setmode(_fileno(file), _O_BINARY);
      int old_stdout_mode = _setmode(_fileno(stdout), _O_BINARY);

      // Read and write in chunks
      while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        fwrite(buffer, 1, bytes_read, stdout);
      }

      // Reset modes to original values
      _setmode(_fileno(stdout), old_stdout_mode);
      _setmode(_fileno(file), old_file_mode);
    }

    // Check for read errors
    if (ferror(file)) {
      fprintf(stderr, "lsh: error reading from '%s': ", args[i]);
      perror("");
      success = 0;
    }

    // Close the file
    fclose(file);

    // Print blank line after content
    printf("\n\n");

    i++;
  }

  // Make sure we reset the console color
  reset_color();

  return success;
}

// Delete files
int lsh_del(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file argument to \"del\"\n");
    return 1;
  }

  // Handle multiple files
  int i = 1;
  int success = 1;

  while (args[i] != NULL) {
    // Try deleting each file
    if (DeleteFile(args[i]) == 0) {
      // DeleteFile returns 0 on failure, non-zero on success
      DWORD error = GetLastError();
      fprintf(stderr, "lsh: failed to delete '%s': ", args[i]);

      switch (error) {
      case ERROR_FILE_NOT_FOUND:
        fprintf(stderr, "file not found\n");
        break;
      case ERROR_ACCESS_DENIED:
        fprintf(stderr, "access denied\n");
        break;
      default:
        fprintf(stderr, "error code %lu\n", error);
        break;
      }

      success = 0;
    } else {
      printf("Deleted '%s'\n", args[i]);
    }

    i++;
  }

  return success;
}

// Create directory
int lsh_mkdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"mkdir\"\n");
    return 1;
  }

  if (_mkdir(args[1]) != 0) {
    perror("lsh: mkdir");
  }

  return 1;
}

// Remove directory
int lsh_rmdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"rmdir\"\n");
    return 1;
  }

  if (_rmdir(args[1]) != 0) {
    perror("lsh: rmdir");
  }

  return 1;
}

// Touch a file (create or update timestamp)
int lsh_touch(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file argument to \"touch\"\n");
    return 1;
  }

  // Handle multiple files
  int i = 1;
  int success = 1;

  while (args[i] != NULL) {
    // Check if file exists
    HANDLE hFile =
        CreateFile(args[i],                            // filename
                   FILE_WRITE_ATTRIBUTES,              // access mode
                   FILE_SHARE_READ | FILE_SHARE_WRITE, // share mode
                   NULL,                               // security attributes
                   OPEN_ALWAYS, // create if doesn't exist, open if does
                   FILE_ATTRIBUTE_NORMAL, // file attributes
                   NULL                   // template file
        );

    if (hFile == INVALID_HANDLE_VALUE) {
      // Failed to create/open file
      DWORD error = GetLastError();
      fprintf(stderr, "lsh: failed to touch '%s': error code %lu\n", args[i],
              error);
      success = 0;
    } else {
      // File was created or opened successfully
      // Get current system time
      SYSTEMTIME st;
      FILETIME ft;
      GetSystemTime(&st);
      SystemTimeToFileTime(&st, &ft);

      // Update file times
      if (!SetFileTime(hFile, &ft, &ft, &ft)) {
        fprintf(stderr, "lsh: failed to update timestamps for '%s'\n", args[i]);
        success = 0;
      }

      // Close file handle
      CloseHandle(hFile);

      printf("Created/updated '%s'\n", args[i]);
    }

    i++;
  }

  return success;
}

// Change directory
int lsh_cd(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"cd\"\n");
  } else {
    if (_chdir(args[1]) != 0) { // Use _chdir for Windows
      perror("lsh");
    }
  }
  return 1;
}

// Clear the screen completely including scrollback buffer
int lsh_clear(char **args) {
  // Method 1: Use system cls command - simplest and most reliable approach for
  // completely clearing the screen This will completely clear the console
  // including scrollback history
  int result = system("cls");

  // THIS IS HERE IF I NEED IT LATER. THE SYSTEM CLS IS WORKING FOR ALL THE
  // TERMINALS I TESTED (ATLEAST ON WINDOWS) but this could come in handy later

  // if (result != 0) {
  //   // If system command fails, fall back to manual clearing
  //   HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  //
  //   if (hConsole == INVALID_HANDLE_VALUE) {
  //     perror("lsh: failed to get console handle");
  //     return 1;
  //   }
  //
  //   // Get console information
  //   CONSOLE_SCREEN_BUFFER_INFO csbi;
  //   if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
  //     perror("lsh: failed to get console info");
  //     return 1;
  //   }
  //
  //   // Calculate total cells in console
  //   DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
  //
  //   // Fill the entire buffer with spaces
  //   DWORD count;
  //   COORD homeCoords = {0, 0};
  //
  //   if (!FillConsoleOutputCharacter(hConsole, ' ', cellCount, homeCoords,
  //                                   &count)) {
  //     perror("lsh: failed to fill console");
  //     return 1;
  //   }
  //
  //   // Fill the entire buffer with the current attributes
  //   if (!FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount,
  //                                   homeCoords, &count)) {
  //     perror("lsh: failed to set console attributes");
  //     return 1;
  //   }
  //
  //   // Move the cursor to home position
  //   SetConsoleCursorPosition(hConsole, homeCoords);
  // }

  return 1;
}

// Function to determine color based on file extension
WORD get_file_color(const char *filename) {
  // Default file color from theme
  WORD color = current_theme.TEXT_FILE_COLOR;

  // Find the file extension
  char *dot = strrchr(filename, '.');
  if (!dot || dot == filename) {
    return color; // No extension found, use default
  }

  // Move past the dot
  char *ext = dot + 1;

  // C/C++ source files
  if (stricmp(ext, "c") == 0 || stricmp(ext, "cpp") == 0 ||
      stricmp(ext, "cc") == 0) {
    return current_theme.CODE_FILE_COLOR;
  }

  // C/C++ header files
  if (stricmp(ext, "h") == 0 || stricmp(ext, "hpp") == 0) {
    return current_theme.CODE_FILE_COLOR;
  }

  // Python files
  if (stricmp(ext, "py") == 0 || stricmp(ext, "pyc") == 0 ||
      stricmp(ext, "pyd") == 0 || stricmp(ext, "pyw") == 0) {
    return current_theme.CODE_FILE_COLOR;
  }

  // JavaScript/TypeScript files
  if (stricmp(ext, "js") == 0 || stricmp(ext, "ts") == 0 ||
      stricmp(ext, "jsx") == 0 || stricmp(ext, "tsx") == 0) {
    return current_theme.CODE_FILE_COLOR;
  }

  // Java files
  if (stricmp(ext, "java") == 0 || stricmp(ext, "class") == 0 ||
      stricmp(ext, "jar") == 0) {
    return current_theme.CODE_FILE_COLOR;
  }

  // Executable files
  if (stricmp(ext, "exe") == 0 || stricmp(ext, "dll") == 0 ||
      stricmp(ext, "sys") == 0) {
    return current_theme.EXECUTABLE_COLOR;
  }

  // Images
  if (stricmp(ext, "jpg") == 0 || stricmp(ext, "jpeg") == 0 ||
      stricmp(ext, "png") == 0 || stricmp(ext, "gif") == 0 ||
      stricmp(ext, "bmp") == 0) {
    return current_theme.IMAGE_FILE_COLOR;
  }

  // Archives
  if (stricmp(ext, "zip") == 0 || stricmp(ext, "rar") == 0 ||
      stricmp(ext, "7z") == 0 || stricmp(ext, "gz") == 0 ||
      stricmp(ext, "tar") == 0) {
    return current_theme.ARCHIVE_FILE_COLOR;
  }

  return color; // Default color for other file types
}
int lsh_dir(char **args) {
  char cwd[1024];
  WIN32_FIND_DATA findData;
  HANDLE hFind;

  // Get handle to console for output
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

  // Get current time for relative time calculations
  SYSTEMTIME currentTime;
  GetLocalTime(&currentTime);
  FILETIME currentFileTime;
  SystemTimeToFileTime(&currentTime, &currentFileTime);
  ULARGE_INTEGER currentTimeValue;
  currentTimeValue.LowPart = currentFileTime.dwLowDateTime;
  currentTimeValue.HighPart = currentFileTime.dwHighDateTime;

  // Get console screen size
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  int consoleWidth = 80;  // Default width
  int consoleHeight = 25; // Default height

  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    consoleHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }

  // Get current directory
  if (_getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("lsh");
    return 1;
  }

  // Calculate directory info box width based on path length
  int dirInfoWidth = strlen(cwd) + 14; // "Directory: " + path
  int itemsLineWidth = 20;             // "Items: " + number (estimated)
  int infoBoxWidth =
      (dirInfoWidth > itemsLineWidth) ? dirInfoWidth : itemsLineWidth;

  // Add padding to infoBoxWidth
  infoBoxWidth += 6; // 3 chars on each side for padding

  // Cap the infoBoxWidth to console width - 4 (for some margin)
  if (infoBoxWidth > consoleWidth - 4) {
    infoBoxWidth = consoleWidth - 4;
  }

  // Prepare search pattern for all files
  char searchPath[1024];
  strcpy(searchPath, cwd);
  strcat(searchPath, "\\*");

  // First pass to count files
  int fileCount = 0;
  hFind = FindFirstFile(searchPath, &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "lsh: Failed to list directory contents\n");
    return 1;
  }

  do {
    if (strcmp(findData.cFileName, ".") != 0 &&
        strcmp(findData.cFileName, "..") != 0) {
      fileCount++;
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);

  // Dynamic column width calculation - start with minimum sizes
  int nameColWidth = 4;      // Minimum for "Name"
  int sizeColWidth = 4;      // Minimum for "Size"
  int typeColWidth = 9;      // Minimum for "Directory"
  int modifiedColWidth = 16; // Minimum for "Last Modified"

  // Allocate array for all files
  FileInfo *fileInfoArray =
      (FileInfo *)malloc(sizeof(FileInfo) * (fileCount > 0 ? fileCount : 1));
  if (!fileInfoArray) {
    fprintf(stderr, "lsh: allocation error\n");
    return 1;
  }

  int fileInfoIndex = 0;

  // Second pass to process files and compute maximum column widths
  hFind = FindFirstFile(searchPath, &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "lsh: Failed to list directory contents\n");
    free(fileInfoArray);
    return 1;
  }

  // Process all files and determine max column widths dynamically
  do {
    if (strcmp(findData.cFileName, ".") != 0 &&
        strcmp(findData.cFileName, "..") != 0) {
      // Format the last modified time as a relative time
      ULARGE_INTEGER fileTimeValue;
      fileTimeValue.LowPart = findData.ftLastWriteTime.dwLowDateTime;
      fileTimeValue.HighPart = findData.ftLastWriteTime.dwHighDateTime;

      // Calculate difference in 100-nanosecond intervals
      ULONGLONG timeDiff =
          (currentTimeValue.QuadPart - fileTimeValue.QuadPart) /
          10000000; // Convert to seconds

      char timeString[64];
      if (timeDiff < 60) {
        sprintf(timeString, "%llu seconds ago", timeDiff);
      } else if (timeDiff < 3600) {
        sprintf(timeString, "%llu minutes ago", timeDiff / 60);
      } else if (timeDiff < 86400) {
        sprintf(timeString, "%llu hours ago", timeDiff / 3600);
      } else if (timeDiff < 604800) {
        sprintf(timeString, "%llu days ago", timeDiff / 86400);
      } else if (timeDiff < 2629800) { // ~1 month in seconds
        sprintf(timeString, "%llu weeks ago", timeDiff / 604800);
      } else if (timeDiff < 31557600) { // ~1 year in seconds
        sprintf(timeString, "%llu months ago", timeDiff / 2629800);
      } else {
        sprintf(timeString, "%llu years ago", timeDiff / 31557600);
      }

      // Update max width for modified column
      int len = strlen(timeString);
      if (len > modifiedColWidth)
        modifiedColWidth = len;

      // Check if it's a directory
      BOOL isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
      const char *fileType = isDirectory ? "Directory" : "File";

      // Update max width for type column
      len = strlen(fileType);
      if (len > typeColWidth)
        typeColWidth = len;

      // Format size (only for files)
      char sizeString[32];
      if (isDirectory) {
        strcpy(sizeString, "-");
      } else {
        if (findData.nFileSizeLow < 1024) {
          sprintf(sizeString, "%lu B", findData.nFileSizeLow);
        } else if (findData.nFileSizeLow < 1024 * 1024) {
          sprintf(sizeString, "%.1f KB", findData.nFileSizeLow / 1024.0);
        } else {
          sprintf(sizeString, "%.1f MB",
                  findData.nFileSizeLow / (1024.0 * 1024.0));
        }
      }

      // Update max width for size column
      len = strlen(sizeString);
      if (len > sizeColWidth)
        sizeColWidth = len;

      // Update max width for name column
      len = strlen(findData.cFileName);
      if (len > nameColWidth)
        nameColWidth = len;

      // Store file info
      strcpy(fileInfoArray[fileInfoIndex].timeString, timeString);
      strcpy(fileInfoArray[fileInfoIndex].sizeString, sizeString);
      strcpy(fileInfoArray[fileInfoIndex].fileType, fileType);
      strcpy(fileInfoArray[fileInfoIndex].fileName, findData.cFileName);
      fileInfoArray[fileInfoIndex].isDirectory = isDirectory;
      fileInfoIndex++;
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);

  // Add padding to column widths
  nameColWidth += 2;
  sizeColWidth += 2;
  typeColWidth += 2;
  modifiedColWidth += 2;

  // Sort the array
  qsort(fileInfoArray, fileInfoIndex, sizeof(FileInfo), compare_dir_entries);

  // Calculate total table width
  int tableWidth = nameColWidth + sizeColWidth + typeColWidth +
                   modifiedColWidth + 5; // +5 for the borders

  // Ensure the table is at least as wide as the info box
  if (tableWidth < infoBoxWidth) {
    // Distribute the extra width to the name column (most flexible)
    nameColWidth += (infoBoxWidth - tableWidth);
    tableWidth = infoBoxWidth;
  }

  // Save original console attributes to restore later
  WORD originalAttributes;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  originalAttributes = csbi.wAttributes;

  // Use theme header color for info box
  SetConsoleTextAttribute(hConsole, current_theme.HEADER_COLOR);

  // Print directory info box with proper width
  printf("\n\u250C");
  for (int i = 0; i < infoBoxWidth - 2; i++)
    printf("\u2500");
  printf("\u2510\n");

  // Calculate max display length for directory path
  // The -13 accounts for: "│ Directory: " (13 chars) and "│" (1 char) at the
  // end
  int maxPathDisplayLen = infoBoxWidth - 14;

  // Truncate path if needed with ellipsis
  char displayPath[1024];
  if (strlen(cwd) > maxPathDisplayLen) {
    // Find a good place to truncate
    int keepEnd = maxPathDisplayLen - 3; // 3 chars for "..."
    strcpy(displayPath, "...");
    strcat(displayPath, cwd + strlen(cwd) - keepEnd);
  } else {
    strcpy(displayPath, cwd);
  }

  // Fixed padding for alignment - exactly align with infoBoxWidth
  // Use a fixed-width field for the directory path
  printf("\u2502 Directory: %-*s\u2502\n", maxPathDisplayLen, displayPath);

  // Fixed padding for the items count too
  // The items count field width is infoBoxWidth - 10 (for "│ Items: " and "│")
  printf("\u2502 Items: %-*d\u2502\n", infoBoxWidth - 10, fileInfoIndex);

  printf("\u2514");
  for (int i = 0; i < infoBoxWidth - 2; i++)
    printf("\u2500");
  printf("\u2518\n\n");

  // Create format strings for headers
  char headerFmt[256];

  // Build format string with columns reordered: Name, Size, Type, Last Modified
  sprintf(headerFmt,
          "\u2502 %%-%ds \u2502 %%-%ds \u2502 %%-%ds \u2502 %%-%ds \u2502",
          nameColWidth - 2, sizeColWidth - 2, typeColWidth - 2,
          modifiedColWidth - 2);

  // Use theme header color for table header
  SetConsoleTextAttribute(hConsole, current_theme.HEADER_COLOR);

  // Print table header
  printf("\u250C");
  for (int i = 0; i < nameColWidth; i++)
    printf("\u2500");
  printf("\u252C");
  for (int i = 0; i < sizeColWidth; i++)
    printf("\u2500");
  printf("\u252C");
  for (int i = 0; i < typeColWidth; i++)
    printf("\u2500");
  printf("\u252C");
  for (int i = 0; i < modifiedColWidth; i++)
    printf("\u2500");
  printf("\u2510\n");

  // Print header with new column order
  printf(headerFmt, "Name", "Size", "Type", "Last Modified");
  printf("\n");

  // Separator line
  printf("\u251C");
  for (int i = 0; i < nameColWidth; i++)
    printf("\u2500");
  printf("\u253C");
  for (int i = 0; i < sizeColWidth; i++)
    printf("\u2500");
  printf("\u253C");
  for (int i = 0; i < typeColWidth; i++)
    printf("\u2500");
  printf("\u253C");
  for (int i = 0; i < modifiedColWidth; i++)
    printf("\u2500");
  printf("\u2524\n");

  // Reset to default color
  SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);

  // Print data rows with colored fields and new column order
  for (int i = 0; i < fileInfoIndex; i++) {
    // Start the row
    printf("\u2502 ");

    // Print filename with color based on extension or directory status
    if (fileInfoArray[i].isDirectory) {
      SetConsoleTextAttribute(hConsole, current_theme.DIRECTORY_COLOR);
    } else {
      // Use extension-based coloring for files
      SetConsoleTextAttribute(hConsole,
                              get_file_color(fileInfoArray[i].fileName));
    }
    printf("%-*s", nameColWidth - 2, fileInfoArray[i].fileName);
    SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);

    // Print size in theme accent color
    printf(" \u2502 ");
    SetConsoleTextAttribute(hConsole, current_theme.ACCENT_COLOR);
    printf("%-*s", sizeColWidth - 2, fileInfoArray[i].sizeString);
    SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);

    // Print type (default color)
    printf(" \u2502 %-*s \u2502 ", typeColWidth - 2, fileInfoArray[i].fileType);

    // Print last modified time (default color)
    printf("%-*s \u2502\n", modifiedColWidth - 2, fileInfoArray[i].timeString);
  }

  // Bottom border with theme header color
  SetConsoleTextAttribute(hConsole, current_theme.HEADER_COLOR);
  printf("\u2514");
  for (int i = 0; i < nameColWidth; i++)
    printf("\u2500");
  printf("\u2534");
  for (int i = 0; i < sizeColWidth; i++)
    printf("\u2500");
  printf("\u2534");
  for (int i = 0; i < typeColWidth; i++)
    printf("\u2500");
  printf("\u2534");
  for (int i = 0; i < modifiedColWidth; i++)
    printf("\u2500");
  printf("\u2518\n");

  printf("\n");

  // Restore original console attributes
  SetConsoleTextAttribute(hConsole, originalAttributes);

  free(fileInfoArray);
  return 1;
}
static char *copied_file_path = NULL;
static char *copied_file_name = NULL;

/**
 * Copy a file for later pasting with optional raw content copy to clipboard
 */
int lsh_copy(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file argument to \"copy\"\n");
    fprintf(stderr, "Usage: copy [OPTION] FILE\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -raw    Copy file contents to clipboard instead of the "
                    "file itself\n");
    return 1;
  }

  // Check for -raw flag
  int raw_mode = 0;
  char *filename = args[1];

  if (strcmp(args[1], "-raw") == 0) {
    raw_mode = 1;

    if (args[2] == NULL) {
      fprintf(stderr, "lsh: expected file argument after -raw\n");
      return 1;
    }

    filename = args[2];
  }

  // Check if file exists
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    fprintf(stderr, "lsh: cannot find '%s': ", filename);
    perror("");
    return 1;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  if (raw_mode) {
    // RAW MODE: Copy file contents to clipboard

    // Read file content
    char *file_content = (char *)malloc(file_size + 1);
    if (!file_content) {
      fprintf(stderr, "lsh: failed to allocate memory for file content\n");
      fclose(file);
      return 1;
    }

    size_t bytes_read = fread(file_content, 1, file_size, file);
    file_content[bytes_read] = '\0'; // Null-terminate the string
    fclose(file);

    // Allocate global memory for clipboard
    HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, bytes_read + 1);
    if (h_mem == NULL) {
      fprintf(stderr, "lsh: failed to allocate global memory for clipboard\n");
      free(file_content);
      return 1;
    }

    // Lock the memory and copy file content
    char *clipboard_content = (char *)GlobalLock(h_mem);
    if (clipboard_content == NULL) {
      fprintf(stderr, "lsh: failed to lock global memory\n");
      GlobalFree(h_mem);
      free(file_content);
      return 1;
    }

    memcpy(clipboard_content, file_content, bytes_read);
    clipboard_content[bytes_read] = '\0'; // Null-terminate the string
    GlobalUnlock(h_mem);

    // Open clipboard and set data
    if (!OpenClipboard(NULL)) {
      fprintf(stderr, "lsh: failed to open clipboard\n");
      GlobalFree(h_mem);
      free(file_content);
      return 1;
    }

    EmptyClipboard();
    HANDLE h_clipboard_data = SetClipboardData(CF_TEXT, h_mem);
    CloseClipboard();

    if (h_clipboard_data == NULL) {
      fprintf(stderr, "lsh: failed to set clipboard data\n");
      GlobalFree(h_mem);
      free(file_content);
      return 1;
    }

    // Don't free h_mem here - Windows takes ownership of it
    free(file_content);

    printf("Copied contents of '%s' to clipboard (%ld bytes)\n", filename,
           bytes_read);
  } else {
    // NORMAL MODE: Copy file for internal paste operation
    fclose(file); // We don't need the file content for this mode

    // Get full path
    char fullPath[1024];
    if (_fullpath(fullPath, filename, sizeof(fullPath)) == NULL) {
      fprintf(stderr, "lsh: failed to get full path for '%s'\n", filename);
      return 1;
    }

    // Extract just the filename from the path
    char *fileBaseName = filename;
    char *lastSlash = strrchr(filename, '\\');
    if (lastSlash != NULL) {
      fileBaseName = lastSlash + 1;
    }

    // Free previous copied file info if any
    if (copied_file_path != NULL) {
      free(copied_file_path);
      copied_file_path = NULL;
    }
    if (copied_file_name != NULL) {
      free(copied_file_name);
      copied_file_name = NULL;
    }

    // Store the file path and name
    copied_file_path = _strdup(fullPath);
    copied_file_name = _strdup(fileBaseName);

    if (copied_file_path == NULL || copied_file_name == NULL) {
      fprintf(stderr, "lsh: memory allocation error\n");
      if (copied_file_path != NULL) {
        free(copied_file_path);
        copied_file_path = NULL;
      }
      if (copied_file_name != NULL) {
        free(copied_file_name);
        copied_file_name = NULL;
      }
      return 1;
    }

    printf("Copied '%s' - ready for pasting with the 'paste' command\n",
           fileBaseName);
  }

  return 1;
}

/**
 * Paste a previously copied file into the current directory
 */
int lsh_paste(char **args) {
  if (copied_file_path == NULL || copied_file_name == NULL) {
    fprintf(stderr, "lsh: no file has been copied\n");
    return 1;
  }

  // Construct destination path (current directory + filename)
  char destPath[1024];
  if (_getcwd(destPath, sizeof(destPath)) == NULL) {
    fprintf(stderr, "lsh: failed to get current directory\n");
    return 1;
  }

  // Add backslash if needed
  size_t len = strlen(destPath);
  if (len > 0 && destPath[len - 1] != '\\') {
    strcat(destPath, "\\");
  }
  strcat(destPath, copied_file_name);

  // Check if source and destination are the same
  if (strcmp(copied_file_path, destPath) == 0) {
    fprintf(stderr, "lsh: source and destination are the same file\n");
    return 1;
  }

  // Check if destination file already exists
  FILE *destFile = fopen(destPath, "rb");
  if (destFile != NULL) {
    fclose(destFile);
    char response[10];
    printf("File '%s' already exists. Overwrite? (y/n): ", copied_file_name);
    if (fgets(response, sizeof(response), stdin) == NULL ||
        (response[0] != 'y' && response[0] != 'Y')) {
      printf("Paste canceled\n");
      return 1;
    }
  }

  // Open source file
  FILE *sourceFile = fopen(copied_file_path, "rb");
  if (sourceFile == NULL) {
    fprintf(stderr, "lsh: cannot open source file '%s': ", copied_file_path);
    perror("");
    return 1;
  }

  // Open destination file
  destFile = fopen(destPath, "wb");
  if (destFile == NULL) {
    fprintf(stderr, "lsh: cannot create destination file '%s': ", destPath);
    perror("");
    fclose(sourceFile);
    return 1;
  }

  // Copy the file contents
  char buffer[4096];
  size_t bytesRead;
  size_t totalBytes = 0;

  while ((bytesRead = fread(buffer, 1, sizeof(buffer), sourceFile)) > 0) {
    size_t bytesWritten = fwrite(buffer, 1, bytesRead, destFile);
    if (bytesWritten != bytesRead) {
      fprintf(stderr, "lsh: error writing to '%s'\n", destPath);
      fclose(sourceFile);
      fclose(destFile);
      return 1;
    }
    totalBytes += bytesRead;
  }

  // Close files
  fclose(sourceFile);
  fclose(destFile);

  printf("Pasted '%s' (%zu bytes)\n", copied_file_name, totalBytes);
  return 1;
}

int lsh_move(char **args) {
  if (args[1] == NULL || args[2] == NULL) {
    fprintf(stderr,
            "lsh: expected source and destination arguments for \"move\"\n");
    fprintf(stderr, "Usage: move <source> <destination>\n");
    return 1;
  }

  // Check if source exists
  WIN32_FIND_DATA findData;
  HANDLE hFind = FindFirstFile(args[1], &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "lsh: cannot find source '%s': No such file or directory\n",
            args[1]);
    return 1;
  }

  FindClose(hFind);

  // Get absolute paths for source and destination
  char srcPath[MAX_PATH];
  char destPath[MAX_PATH];

  if (_fullpath(srcPath, args[1], MAX_PATH) == NULL) {
    fprintf(stderr, "lsh: failed to get full path for '%s'\n", args[1]);
    return 1;
  }

  if (_fullpath(destPath, args[2], MAX_PATH) == NULL) {
    fprintf(stderr, "lsh: failed to get full path for '%s'\n", args[2]);
    return 1;
  }

  // Check if destination is a directory
  DWORD destAttrs = GetFileAttributes(destPath);
  if (destAttrs != INVALID_FILE_ATTRIBUTES &&
      (destAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
    // If destination is a directory, append the source filename
    char *srcFilename = strrchr(srcPath, '\\');
    if (srcFilename) {
      srcFilename++; // Skip the backslash

      // Add backslash to destination path if needed
      size_t destLen = strlen(destPath);
      if (destLen > 0 && destPath[destLen - 1] != '\\') {
        strcat(destPath, "\\");
      }

      // Append the source filename to the destination path
      strcat(destPath, srcFilename);
    }
  }

  // Check if source and destination are the same
  if (_stricmp(srcPath, destPath) == 0) {
    fprintf(stderr, "lsh: '%s' and '%s' are the same file\n", srcPath,
            destPath);
    return 1;
  }

  // Check if destination already exists
  if (GetFileAttributes(destPath) != INVALID_FILE_ATTRIBUTES) {
    char response[10];
    printf("'%s' already exists. Overwrite? (y/n): ", destPath);
    if (fgets(response, sizeof(response), stdin) == NULL ||
        (response[0] != 'y' && response[0] != 'Y')) {
      printf("Move canceled\n");
      return 1;
    }
  }

  // Perform the move operation
  BOOL result = MoveFileEx(srcPath, destPath,
                           MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING);

  if (!result) {
    DWORD error = GetLastError();
    fprintf(stderr, "lsh: failed to move '%s' to '%s': ", srcPath, destPath);

    switch (error) {
    case ERROR_FILE_NOT_FOUND:
      fprintf(stderr, "file not found\n");
      break;
    case ERROR_ACCESS_DENIED:
      fprintf(stderr, "access denied\n");
      break;
    case ERROR_ALREADY_EXISTS:
      fprintf(stderr, "destination already exists\n");
      break;
    case ERROR_PATH_NOT_FOUND:
      fprintf(stderr, "path not found\n");
      break;
    default:
      fprintf(stderr, "error code %lu\n", error);
      break;
    }

    return 1;
  }

  printf("Moved '%s' to '%s'\n", args[1], destPath);
  return 1;
}

// Display help
int lsh_help(char **args) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD originalAttrs;
  CONSOLE_SCREEN_BUFFER_INFO csbi;

  // Get original attributes
  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    originalAttrs = csbi.wAttributes;
  } else {
    originalAttrs = current_theme.PRIMARY_COLOR;
  }

  // Use header color for title
  SetConsoleTextAttribute(hConsole, current_theme.HEADER_COLOR);
  printf("\nMarcus Denslow's LSH\n");
  SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);

  printf("Type program names and arguments, and hit enter.\n\n");

  // Use accent color for section title
  SetConsoleTextAttribute(hConsole, current_theme.ACCENT_COLOR);
  printf("Built-in commands:\n");
  SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);

  // Print commands in a nice colored grid
  int columns = 4; // Number of columns to display
  int i = 0;

  while (i < lsh_num_builtins()) {
    // Start a new row
    printf("  ");

    // Print up to 'columns' commands per row
    for (int col = 0; col < columns && i < lsh_num_builtins(); col++, i++) {
      // Use secondary color for command names
      SetConsoleTextAttribute(hConsole, current_theme.SECONDARY_COLOR);
      printf("%-15s", builtin_str[i]);
      SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);
    }
    printf("\n");
  }

  printf("\n");
  printf("Use the command for information on other programs.\n");
  printf("Type 'theme list' to view available themes.\n\n");

  // Restore original attributes
  SetConsoleTextAttribute(hConsole, originalAttrs);

  return 1;
}
/**
 * Create structured output for ls/dir command
 */
TableData *lsh_dir_structured(char **args) {
  char cwd[1024];
  WIN32_FIND_DATA findData;
  HANDLE hFind;

  // Create table with appropriate headers - matching the order in lsh_dir()
  char *headers[] = {"Name", "Size", "Type", "Last Modified"};
  TableData *table = create_table(headers, 4);
  if (!table) {
    return NULL;
  }

  // Get current directory
  if (_getcwd(cwd, sizeof(cwd)) == NULL) {
    fprintf(stderr, "lsh: failed to get current directory\n");
    free_table(table);
    return NULL;
  }

  // Get current time for relative time calculations
  SYSTEMTIME currentTime;
  GetLocalTime(&currentTime);
  FILETIME currentFileTime;
  SystemTimeToFileTime(&currentTime, &currentFileTime);
  ULARGE_INTEGER currentTimeValue;
  currentTimeValue.LowPart = currentFileTime.dwLowDateTime;
  currentTimeValue.HighPart = currentFileTime.dwHighDateTime;

  // Prepare search pattern for all files
  char searchPath[1024];
  strcpy(searchPath, cwd);
  strcat(searchPath, "\\*");

  // First find to count files
  hFind = FindFirstFile(searchPath, &findData);

  if (hFind == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "lsh: Failed to list directory contents\n");
    free_table(table);
    return NULL;
  }

  // Count files for initial allocation
  int fileCount = 0;
  do {
    if (strcmp(findData.cFileName, ".") != 0 &&
        strcmp(findData.cFileName, "..") != 0) {
      fileCount++;
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);

  // Structure to hold file info for sorting
  typedef struct {
    char timeString[64];
    char sizeString[32];
    char fileType[16];
    char fileName[MAX_PATH];
    BOOL isDirectory;
    ULONGLONG timeDiff; // For sorting by time
  } FileInfoItem;

  // Allocate array for sorting
  FileInfoItem *fileInfoArray =
      (FileInfoItem *)malloc(sizeof(FileInfoItem) * fileCount);
  if (!fileInfoArray) {
    fprintf(stderr, "lsh: allocation error in lsh_dir_structured\n");
    free_table(table);
    return NULL;
  }

  // Second pass to collect file info
  hFind = FindFirstFile(searchPath, &findData);
  if (hFind == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "lsh: Failed to list directory contents\n");
    free(fileInfoArray);
    free_table(table);
    return NULL;
  }

  int fileIndex = 0;

  // Process all files
  do {
    // Skip . and .. directories for cleaner output
    if (strcmp(findData.cFileName, ".") != 0 &&
        strcmp(findData.cFileName, "..") != 0) {
      // Format the last modified time as a relative time
      ULARGE_INTEGER fileTimeValue;
      fileTimeValue.LowPart = findData.ftLastWriteTime.dwLowDateTime;
      fileTimeValue.HighPart = findData.ftLastWriteTime.dwHighDateTime;

      // Calculate difference in 100-nanosecond intervals
      ULONGLONG timeDiff =
          (currentTimeValue.QuadPart - fileTimeValue.QuadPart) /
          10000000; // Convert to seconds

      // Store the time difference for sorting
      fileInfoArray[fileIndex].timeDiff = timeDiff;

      // Format the time difference as a human-readable string
      if (timeDiff < 60) {
        sprintf(fileInfoArray[fileIndex].timeString, "%llu seconds ago",
                timeDiff);
      } else if (timeDiff < 3600) {
        sprintf(fileInfoArray[fileIndex].timeString, "%llu minutes ago",
                timeDiff / 60);
      } else if (timeDiff < 86400) {
        sprintf(fileInfoArray[fileIndex].timeString, "%llu hours ago",
                timeDiff / 3600);
      } else if (timeDiff < 604800) {
        sprintf(fileInfoArray[fileIndex].timeString, "%llu days ago",
                timeDiff / 86400);
      } else if (timeDiff < 2629800) { // ~1 month in seconds
        sprintf(fileInfoArray[fileIndex].timeString, "%llu weeks ago",
                timeDiff / 604800);
      } else if (timeDiff < 31557600) { // ~1 year in seconds
        sprintf(fileInfoArray[fileIndex].timeString, "%llu months ago",
                timeDiff / 2629800);
      } else {
        sprintf(fileInfoArray[fileIndex].timeString, "%llu years ago",
                timeDiff / 31557600);
      }

      // Check if it's a directory
      BOOL isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
      strcpy(fileInfoArray[fileIndex].fileType,
             isDirectory ? "Directory" : "File");
      fileInfoArray[fileIndex].isDirectory = isDirectory;

      // Format size (only for files)
      if (isDirectory) {
        strcpy(fileInfoArray[fileIndex].sizeString, "-");
      } else {
        if (findData.nFileSizeLow < 1024) {
          sprintf(fileInfoArray[fileIndex].sizeString, "%lu B",
                  findData.nFileSizeLow);
        } else if (findData.nFileSizeLow < 1024 * 1024) {
          sprintf(fileInfoArray[fileIndex].sizeString, "%.1f KB",
                  findData.nFileSizeLow / 1024.0);
        } else {
          sprintf(fileInfoArray[fileIndex].sizeString, "%.1f MB",
                  findData.nFileSizeLow / (1024.0 * 1024.0));
        }
      }

      // Store filename
      strcpy(fileInfoArray[fileIndex].fileName, findData.cFileName);

      fileIndex++;
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);

  // Sort the files - directories first, then by name
  for (int i = 0; i < fileCount - 1; i++) {
    for (int j = 0; j < fileCount - i - 1; j++) {
      // First sort by type (directories first)
      if (fileInfoArray[j].isDirectory && !fileInfoArray[j + 1].isDirectory) {
        continue; // Already in correct order
      } else if (!fileInfoArray[j].isDirectory &&
                 fileInfoArray[j + 1].isDirectory) {
        // Swap
        FileInfoItem temp = fileInfoArray[j];
        fileInfoArray[j] = fileInfoArray[j + 1];
        fileInfoArray[j + 1] = temp;
      } else {
        // Same type, sort by name
        if (strcasecmp(fileInfoArray[j].fileName,
                       fileInfoArray[j + 1].fileName) > 0) {
          // Swap
          FileInfoItem temp = fileInfoArray[j];
          fileInfoArray[j] = fileInfoArray[j + 1];
          fileInfoArray[j + 1] = temp;
        }
      }
    }
  }

  // Now add the sorted files to the table
  for (int i = 0; i < fileCount; i++) {
    // Create a new row for this file entry
    DataValue *row = (DataValue *)malloc(4 * sizeof(DataValue));
    if (!row) {
      fprintf(stderr, "lsh: allocation error in lsh_dir_structured\n");
      free(fileInfoArray);
      free_table(table);
      return NULL;
    }

    // COLUMN 1: Name - Match the order in lsh_dir()
    row[0].type = TYPE_STRING;
    row[0].value.str_val = _strdup(fileInfoArray[i].fileName);

    // COLUMN 2: Size
    row[1].type = TYPE_SIZE; // Use SIZE type for better filtering support
    row[1].value.str_val = _strdup(fileInfoArray[i].sizeString);

    // COLUMN 3: Type
    row[2].type = TYPE_STRING;
    row[2].value.str_val = _strdup(fileInfoArray[i].fileType);

    // COLUMN 4: Last Modified
    row[3].type = TYPE_STRING;
    row[3].value.str_val = _strdup(fileInfoArray[i].timeString);

    // Set highlighting based on file type
    // For directories, use is_highlighted=1
    // For files, store file color in is_highlighted with values > 1
    if (fileInfoArray[i].isDirectory) {
      row[0].is_highlighted = 1; // Directory
    } else {
      // Determine color based on extension
      WORD fileColor = get_file_color(fileInfoArray[i].fileName);
      // Store color value + 1 (to avoid conflict with is_highlighted=1 for
      // directories)
      row[0].is_highlighted =
          (int)fileColor + 10; // Offset to avoid conflict with directory flag
    }

    // Add the row to the table
    add_table_row(table, row);
  }

  // Clean up
  free(fileInfoArray);

  return table;
}

/**
 * Get the latest commit message from GitHub with improved error handling
 * This version is designed to be more robust on restricted networks
 * while preserving the original styled output with centered box
 */
int lsh_news(char **args) {
  HINTERNET hInternet = NULL, hConnect = NULL, hRequest = NULL;
  char buffer[BUFFER_SIZE];
  DWORD bytesRead;
  char response[32768] = "";
  BOOL success = FALSE;
  DWORD error = 0;
  DWORD timeout = 10000; // 10 second timeout

  // Get handle to console for output
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

  // Use theme colors for UI elements
  WORD boxColor = current_theme.ACCENT_COLOR;
  WORD textColor = current_theme.PRIMARY_COLOR;
  WORD originalAttrs;

  // Get original console attributes
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
  originalAttrs = consoleInfo.wAttributes;

  // Print starting message
  printf("\nFetching latest news from GitHub...\n\n");

  // Initialize WinINet with proxy detection
  hInternet =
      InternetOpen("LSH GitHub Commit Fetcher/1.0",
                   INTERNET_OPEN_TYPE_PRECONFIG, // Use system proxy settings
                   NULL, NULL, 0);

  if (!hInternet) {
    error = GetLastError();
    printf("Error initializing Internet connection: %lu\n", error);
    goto cleanup;
  }

  // Set timeouts to prevent hanging
  InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &timeout,
                    sizeof(timeout));
  InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &timeout,
                    sizeof(timeout));
  InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout,
                    sizeof(timeout));

  // Connect to GitHub API
  hConnect =
      InternetConnect(hInternet, GITHUB_API_HOST, INTERNET_DEFAULT_HTTPS_PORT,
                      NULL, NULL, INTERNET_SERVICE_HTTP, 0, 0);

  if (!hConnect) {
    error = GetLastError();
    printf("Error connecting to GitHub API: %lu\n", error);
    printf("This could be due to network restrictions on your school PC.\n");
    goto cleanup;
  }

  // Create HTTP request
  hRequest = HttpOpenRequest(hConnect, "GET", GITHUB_API_PATH, NULL, NULL, NULL,
                             INTERNET_FLAG_SECURE | INTERNET_FLAG_RELOAD |
                                 INTERNET_FLAG_NO_CACHE_WRITE,
                             0);

  if (!hRequest) {
    error = GetLastError();
    printf("Error creating HTTP request: %lu\n", error);
    goto cleanup;
  }

  // Add User-Agent header (required by GitHub API)
  if (!HttpAddRequestHeaders(hRequest,
                             "User-Agent: LSH GitHub Commit Fetcher\r\n"
                             "Accept: application/vnd.github.v3+json\r\n",
                             -1, HTTP_ADDREQ_FLAG_ADD)) {
    error = GetLastError();
    printf("Error adding request headers: %lu\n", error);
    goto cleanup;
  }

  // Set security options to be more permissive with school proxy servers
  DWORD securityFlags = 0;
  DWORD flagsSize = sizeof(securityFlags);
  InternetQueryOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &securityFlags,
                      &flagsSize);
  securityFlags |=
      SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_REVOCATION;
  InternetSetOption(hRequest, INTERNET_OPTION_SECURITY_FLAGS, &securityFlags,
                    sizeof(securityFlags));

  // Send the request
  if (!HttpSendRequest(hRequest, NULL, 0, NULL, 0)) {
    error = GetLastError();
    printf("Error sending HTTP request: %lu\n", error);

    // Display more helpful error message
    if (error == ERROR_INTERNET_TIMEOUT)
      printf("The connection timed out. Your school network might be blocking "
             "this connection.\n");
    else if (error == ERROR_INTERNET_NAME_NOT_RESOLVED)
      printf("Could not resolve GitHub's address. Check if you have internet "
             "access.\n");
    else if (error == ERROR_INTERNET_CANNOT_CONNECT)
      printf("Could not connect to GitHub. The site might be blocked on your "
             "network.\n");

    goto cleanup;
  }

  // Check if request succeeded
  DWORD statusCode = 0;
  DWORD statusCodeSize = sizeof(statusCode);
  if (!HttpQueryInfo(hRequest, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER,
                     &statusCode, &statusCodeSize, NULL)) {
    error = GetLastError();
    printf("Error querying HTTP status: %lu\n", error);
    goto cleanup;
  }

  if (statusCode != 200) {
    printf("GitHub API returned error: HTTP %lu\n", statusCode);
    goto cleanup;
  }

  // Read the response safely
  DWORD responsePos = 0;
  while (InternetReadFile(hRequest, buffer, BUFFER_SIZE - 1, &bytesRead) &&
         bytesRead > 0) {
    // Make sure we don't overflow our buffer
    if (responsePos + bytesRead >= sizeof(response) - 1) {
      bytesRead = sizeof(response) - responsePos - 1;
      if (bytesRead <= 0)
        break;
    }

    // Copy safely
    memcpy(response + responsePos, buffer, bytesRead);
    responsePos += bytesRead;
    response[responsePos] = '\0';
  }

  // Check if we got a valid response
  if (responsePos == 0) {
    printf("No data received from GitHub\n");
    goto cleanup;
  }

  success = TRUE;

cleanup:
  // Clean up in reverse order of creation
  if (hRequest)
    InternetCloseHandle(hRequest);
  if (hConnect)
    InternetCloseHandle(hConnect);
  if (hInternet)
    InternetCloseHandle(hInternet);

  // If we successfully got a response, parse and display it
  if (success && strlen(response) > 0) {
    // Extract and display commit info
    char *sha = extract_json_string(response, "sha");
    char *author = extract_json_string(response, "name");
    char *date = extract_json_string(response, "date");

    // First, find the commit message in the response
    char *message = NULL;
    char *commit_pos = strstr(response, "\"commit\":");

    if (commit_pos) {
      // Now look for the message within the commit object
      char *message_pos = strstr(commit_pos, "\"message\":");
      if (message_pos) {
        message_pos += 11; // Skip past "message":"

        // Find the closing quote, with escaping handled
        char *message_end = message_pos;
        int in_escape = 0;

        while (*message_end) {
          if (in_escape) {
            in_escape = 0;
          } else if (*message_end == '\\') {
            in_escape = 1;
          } else if (*message_end == '"') {
            break;
          }
          message_end++;
        }

        if (*message_end == '"') {
          int message_len = message_end - message_pos;
          message = (char *)malloc(message_len + 1);
          if (message) {
            strncpy(message, message_pos, message_len);
            message[message_len] = '\0';

            // Unescape the string with proper handling of escape sequences
            char *src = message;
            char *dst = message;
            int escaped = 0;

            while (*src) {
              if (escaped) {
                // Handle special escape sequences properly
                switch (*src) {
                case 'n': // Newline
                  *dst++ = '\n';
                  break;
                case 't': // Tab
                  *dst++ = '\t';
                  break;
                case 'r': // Carriage return
                  *dst++ = '\r';
                  break;
                case '\\': // Backslash
                  *dst++ = '\\';
                  break;
                default: // Copy as-is for other escape sequences
                  *dst++ = *src;
                  break;
                }
                escaped = 0;
              } else if (*src == '\\') {
                escaped = 1;
              } else {
                *dst++ = *src;
              }
              src++;
            }
            *dst = '\0';
          }
        }
      }
    }

    // Define a constant box width - make it wide enough for messages
    const int BOX_WIDTH = 76;

    // Save all the news content in a buffer so we can format it properly
    char news_buffer[4096] = "";
    char line_buffer[256];

    // Format the commit header information into our buffer
    if (sha) {
      sprintf(line_buffer, "Commit: %.8s\n", sha);
      strcat(news_buffer, line_buffer);
    }

    if (author) {
      sprintf(line_buffer, "Author: %s\n", author);
      strcat(news_buffer, line_buffer);
    }

    if (date) {
      // Format date nicely if possible (GitHub date format:
      // 2023-03-17T12:34:56Z)
      char year[5], month[3], day[3], time[9];
      if (sscanf(date, "%4s-%2s-%2sT%8s", year, month, day, time) == 4) {
        sprintf(line_buffer, "Date:   %s-%s-%s %s\n", year, month, day, time);
      } else {
        sprintf(line_buffer, "Date:   %s\n", date);
      }
      strcat(news_buffer, line_buffer);
    }

    // Add a separator line before the commit message
    strcat(news_buffer, "\n");

    // Add commit message with proper word wrapping
    if (message) {
      strcat(news_buffer, "Commit Message:\n");

      // Word wrap the message at BOX_WIDTH-6 chars (allowing for borders and
      // padding)
      const int WRAP_WIDTH = BOX_WIDTH - 6;
      int line_length = 0;
      char *word_start = message;
      char line[256]; // Fixed size buffer, large enough for our needs
      line[0] = '\0'; // Initialize as empty string

      for (char *p = message; *p; p++) {
        if (*p == ' ' || *p == '\n') {
          // Found a word boundary
          int word_len = p - word_start;

          // Check if adding this word would exceed our wrap width
          if (line_length + word_len > WRAP_WIDTH && line_length > 0) {
            // Add the current line to our buffer and start a new line
            strcat(news_buffer, line);
            strcat(news_buffer, "\n");
            line[0] = '\0';
            line_length = 0;
          }

          // Add the word to the current line
          strncat(line, word_start, word_len);
          if (*p == ' ') {
            strcat(line, " ");
            line_length += word_len + 1;
          } else { // newline
            strcat(news_buffer, line);
            strcat(news_buffer, "\n");
            line[0] = '\0';
            line_length = 0;
          }

          // Move to the next word
          word_start = p + 1;
        }
      }

      // Add any remaining text in the line
      if (line_length > 0) {
        strcat(news_buffer, line);
        strcat(news_buffer, "\n");
      }

      // Add any remaining word that might not have a space or newline after it
      if (word_start && *word_start) {
        strcat(news_buffer, word_start);
        strcat(news_buffer, "\n");
      }
    } else {
      strcat(news_buffer, "No commit message found.\n");
    }

    // Get console width to center the box
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int consoleWidth = 80; // Default width if we can't get actual console info

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
      consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }

    // Calculate left padding to center the box
    int leftPadding = (consoleWidth - BOX_WIDTH - 2) /
                      2; // -2 accounts for the border characters
    if (leftPadding < 0)
      leftPadding = 0; // Ensure we don't have negative padding

    // Now draw the box with all the content inside

    // Calculate how many lines of content we have
    int line_count = 0;
    for (char *p = news_buffer; *p; p++) {
      if (*p == '\n')
        line_count++;
    }

    // Use theme accent color for the borders and header
    SetConsoleTextAttribute(hConsole, boxColor);

    // Top border with centering
    printf("%*s", leftPadding, ""); // Add left padding
    printf("\u250C");               // Top-left corner
    for (int i = 0; i < BOX_WIDTH; i++) {
      printf("\u2500"); // Horizontal line
    }
    printf("\u2510\n"); // Top-right corner

    // Title row - centered within the box
    printf("%*s", leftPadding, ""); // Add left padding
    printf("\u2502");               // Left border
    const char *title = "LATEST REPOSITORY NEWS";
    int title_padding = (BOX_WIDTH - strlen(title)) / 2;
    printf("%*s%s%*s", title_padding, "", title,
           BOX_WIDTH - title_padding - strlen(title), "");
    printf("\u2502\n"); // Right border

    // Separator line
    printf("%*s", leftPadding, ""); // Add left padding
    printf("\u251C");               // Left T-junction
    for (int i = 0; i < BOX_WIDTH; i++) {
      printf("\u2500"); // Horizontal line
    }
    printf("\u2524\n"); // Right T-junction

    // Content lines - use primary color for content text
    char *line_start = news_buffer;
    char *line_end;

    while ((line_end = strchr(line_start, '\n')) != NULL) {
      *line_end = '\0'; // Temporarily terminate this line

      // Add left padding for centering
      printf("%*s", leftPadding, "");

      // Print left border in accent color
      SetConsoleTextAttribute(hConsole, boxColor);
      printf("\u2502");

      // Print content in theme primary color
      SetConsoleTextAttribute(hConsole, textColor);

      // Create a padded line with exact width
      char paddedLine[BOX_WIDTH + 3]; // +3 for safety
      snprintf(paddedLine, sizeof(paddedLine), " %-*s ", BOX_WIDTH - 1,
               line_start);

      // Print exactly BOX_WIDTH characters
      printf("%.*s", BOX_WIDTH, paddedLine);

      // Print right border in accent color
      SetConsoleTextAttribute(hConsole, boxColor);
      printf("\u2502\n");

      *line_end = '\n';          // Restore the newline
      line_start = line_end + 1; // Move to the start of the next line
    }

    // Bottom border in accent color
    printf("%*s", leftPadding, ""); // Add left padding
    SetConsoleTextAttribute(hConsole, boxColor);
    printf("\u2514"); // Bottom-left corner
    for (int i = 0; i < BOX_WIDTH; i++) {
      printf("\u2500"); // Horizontal line
    }
    printf("\u2518\n\n"); // Bottom-right corner with extra newline for spacing

    SetConsoleTextAttribute(hConsole, originalAttrs);

    // Cleanup
    if (message)
      free(message);
    if (sha)
      free(sha);
    if (author)
      free(author);
    if (date)
      free(date);
  } else {
    // Display a fallback message in a centered, styled box
    const int BOX_WIDTH = 76;

    // Get console width to center the box
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int consoleWidth = 80; // Default width if we can't get actual console info

    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
      consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }

    // Calculate left padding to center the box
    int leftPadding = (consoleWidth - BOX_WIDTH - 2) / 2;
    if (leftPadding < 0)
      leftPadding = 0;

    // Use warning color for error message borders
    SetConsoleTextAttribute(hConsole, current_theme.WARNING_COLOR);

    // Top border with centering
    printf("%*s", leftPadding, "");
    printf("\u250C"); // Top-left corner
    for (int i = 0; i < BOX_WIDTH; i++) {
      printf("\u2500"); // Horizontal line
    }
    printf("\u2510\n"); // Top-right corner

    // Title row
    printf("%*s", leftPadding, "");
    printf("\u2502");
    const char *title = "CONNECTION ERROR";
    int title_padding = (BOX_WIDTH - strlen(title)) / 2;
    printf("%*s%s%*s", title_padding, "", title,
           BOX_WIDTH - title_padding - strlen(title), "");
    printf("\u2502\n");

    // Separator line
    printf("%*s", leftPadding, "");
    printf("\u251C");
    for (int i = 0; i < BOX_WIDTH; i++) {
      printf("\u2500");
    }
    printf("\u2524\n");

    // Error message lines
    const char *messages[] = {
        "Could not retrieve repository news.",
        "",
        "The shell was unable to connect to GitHub to fetch the latest news.",
        "This is likely due to network restrictions on your school computer.",
        "",
        "Things you can try:",
        "1. Check if you have internet access",
        "2. Ask your IT department if GitHub API access is blocked",
        "3. Try running the shell with administrator privileges",
        "4. Try using other commands that don't require internet access"};

    for (int i = 0; i < sizeof(messages) / sizeof(messages[0]); i++) {
      printf("%*s", leftPadding, "");
      printf("\u2502");

      // Print message text in primary color
      SetConsoleTextAttribute(hConsole, current_theme.PRIMARY_COLOR);
      printf(" %-*s ", BOX_WIDTH - 2, messages[i]);

      // Return to warning color for the border
      SetConsoleTextAttribute(hConsole, current_theme.WARNING_COLOR);
      printf("\u2502\n");
    }

    // Bottom border
    printf("%*s", leftPadding, "");
    printf("\u2514"); // Bottom-left corner
    for (int i = 0; i < BOX_WIDTH; i++) {
      printf("\u2500"); // Horizontal line
    }
    printf("\u2518\n\n"); // Bottom-right corner

    // Reset colors
    SetConsoleTextAttribute(hConsole, originalAttrs);
  }

  return 1;
}
/**
 * Extract a string value from a JSON object
 */
char *extract_json_string(const char *json, const char *key) {
  char search_key[256];
  char *value = NULL;
  char *key_pos, *value_start, *value_end;
  int value_len;

  // Format the key with quotes
  snprintf(search_key, sizeof(search_key), "\"%s\":", key);

  // Find the key in the JSON
  key_pos = strstr(json, search_key);
  if (!key_pos)
    return NULL;

  // Move past the key and colon
  key_pos += strlen(search_key);

  // Skip whitespace
  while (*key_pos && isspace(*key_pos))
    key_pos++;

  if (*key_pos == '"') {
    // String value
    value_start = key_pos + 1;
    value_end = value_start;

    // Find the end of the string (accounting for escaped quotes)
    int escaped = 0;
    while (*value_end) {
      if (escaped) {
        escaped = 0;
      } else if (*value_end == '\\') {
        escaped = 1;
      } else if (*value_end == '"') {
        break;
      }
      value_end++;
    }

    if (*value_end == '"') {
      value_len = value_end - value_start;
      value = (char *)malloc(value_len + 1);
      if (value) {
        strncpy(value, value_start, value_len);
        value[value_len] = '\0';

        // Unescape the string
        char *src = value;
        char *dst = value;
        escaped = 0;

        while (*src) {
          if (escaped) {
            *dst++ = *src++;
            escaped = 0;
          } else if (*src == '\\') {
            escaped = 1;
            src++;
          } else {
            *dst++ = *src++;
          }
        }
        *dst = '\0';
      }
    }
  }

  return value;
}

/**
 * Copy file contents to clipboard
 *
 * @param args Command arguments (args[1] should be the filename)
 * @return Always returns 1 to continue the shell
 */
int lsh_clip(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file argument to \"clip\"\n");
    fprintf(stderr, "Usage: clip FILE\n");
    return 1;
  }

  // Open the file
  FILE *file = fopen(args[1], "rb");
  if (file == NULL) {
    fprintf(stderr, "lsh: cannot find '%s': ", args[1]);
    perror("");
    return 1;
  }

  // Get file size
  fseek(file, 0, SEEK_END);
  long file_size = ftell(file);
  fseek(file, 0, SEEK_SET);

  // Read file content
  char *file_content = (char *)malloc(file_size + 1);
  if (!file_content) {
    fprintf(stderr, "lsh: failed to allocate memory for file content\n");
    fclose(file);
    return 1;
  }

  size_t bytes_read = fread(file_content, 1, file_size, file);
  file_content[bytes_read] = '\0'; // Null-terminate the string
  fclose(file);

  // Allocate global memory for clipboard
  HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, bytes_read + 1);
  if (h_mem == NULL) {
    fprintf(stderr, "lsh: failed to allocate global memory for clipboard\n");
    free(file_content);
    return 1;
  }

  // Lock the memory and copy file content
  char *clipboard_content = (char *)GlobalLock(h_mem);
  if (clipboard_content == NULL) {
    fprintf(stderr, "lsh: failed to lock global memory\n");
    GlobalFree(h_mem);
    free(file_content);
    return 1;
  }

  memcpy(clipboard_content, file_content, bytes_read);
  clipboard_content[bytes_read] = '\0'; // Null-terminate the string
  GlobalUnlock(h_mem);

  // Open clipboard and set data
  if (!OpenClipboard(NULL)) {
    fprintf(stderr, "lsh: failed to open clipboard\n");
    GlobalFree(h_mem);
    free(file_content);
    return 1;
  }

  EmptyClipboard();
  HANDLE h_clipboard_data = SetClipboardData(CF_TEXT, h_mem);
  CloseClipboard();

  if (h_clipboard_data == NULL) {
    fprintf(stderr, "lsh: failed to set clipboard data\n");
    GlobalFree(h_mem);
    free(file_content);
    return 1;
  }

  // Don't free h_mem here - Windows takes ownership of it
  free(file_content);

  printf("Copied contents of '%s' to clipboard (%ld bytes)\n", args[1],
         bytes_read);
  return 1;
}

int lsh_echo(char **args) {
  if (args[1] == NULL) {
    printf("expected argument string type\n");
    printf("e.g.: echo hello world\n");
    return 1;
  }

  if (strcmp(args[1], "cwd") == 0) {
    char cwd[1024];

    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
      perror("lsh: unable to get cwd");
      return 1;
    }
    printf("\n%s\n\n", cwd);
    return 1;
  }

  int i = 1;

  while (args[i] != NULL) {
    printf("%s", args[i]);

    if (args[i + 1] != NULL) {
      printf(" ");
    }
    i++;
  }
  printf("\n");
  return 1;
}

int lsh_self_destruct() {
  printf("\n");
  printf("self fucking destructed bro\n");
  return 1;
}

/**
 * Forward declarations for helper functions
 */
int is_source_code_file(const char *filename);
unsigned long count_lines_in_file(const char *filename);
void count_lines_in_directory(const char *directory, unsigned long *total_files,
                              unsigned long *total_lines, int recursive,
                              int verbose, HANDLE hConsole);

/**
 * Count lines of code in a project directory
 *
 * @param args Command arguments
 * @return Always returns 1 to continue the shell
 */
int lsh_loc(char **args) {
  // Default to current directory if no path provided
  const char *path = args[1] ? args[1] : ".";
  // Track statistics
  unsigned long total_files = 0;
  unsigned long total_lines = 0;
  // Flag to control recursion (default to recursive)
  int recursive = 1;
  // Flag to control verbose output (default to non-verbose)
  int verbose = 0;

  // Process command flags
  int path_arg_index = 1;
  for (int i = 1; args[i] && args[i][0] == '-'; i++) {
    if (strcmp(args[i], "-n") == 0 || strcmp(args[i], "--no-recursive") == 0) {
      recursive = 0;
      path_arg_index = i + 1;
    } else if (strcmp(args[i], "-v") == 0 ||
               strcmp(args[i], "--verbose") == 0) {
      verbose = 1;
      path_arg_index = i + 1;
    } else if (strcmp(args[i], "-h") == 0 || strcmp(args[i], "--help") == 0) {
      printf("\nUsage: loc [options] [directory]\n");
      printf("Count lines of code in files within a directory.\n\n");
      printf("Options:\n");
      printf("  -n, --no-recursive   Don't recurse into subdirectories\n");
      printf("  -v, --verbose        Show details for each file\n");
      printf("  -h, --help           Display this help message\n\n");
      return 1;
    }
  }

  // Update path if a flag was specified
  if (args[path_arg_index]) {
    path = args[path_arg_index];
  }

  // Get handle to console for colored output
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD originalAttributes;
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  originalAttributes = csbi.wAttributes;

  // Use theme header color for title
  SetConsoleTextAttribute(hConsole, current_theme.HEADER_COLOR);
  printf("\nCounting lines of code in '%s'%s...\n", path,
         recursive ? " (recursive)" : "");
  SetConsoleTextAttribute(hConsole, originalAttributes);

  // Start the count process
  count_lines_in_directory(path, &total_files, &total_lines, recursive, verbose,
                           hConsole);

  // Print the results with nice formatting
  printf("\n");
  SetConsoleTextAttribute(hConsole, current_theme.ACCENT_COLOR);
  printf("Results:\n");
  SetConsoleTextAttribute(hConsole, originalAttributes);

  printf("  Files scanned: ");
  SetConsoleTextAttribute(hConsole, current_theme.DIRECTORY_COLOR);
  printf("%lu\n", total_files);
  SetConsoleTextAttribute(hConsole, originalAttributes);

  printf("  Total lines:   ");
  SetConsoleTextAttribute(hConsole, current_theme.CODE_FILE_COLOR);
  printf("%lu\n\n", total_lines);
  SetConsoleTextAttribute(hConsole, originalAttributes);

  return 1;
}

/**
 * Check if a file is a source code file based on its extension
 */
int is_source_code_file(const char *filename) {
  // Get the file extension
  const char *ext = strrchr(filename, '.');
  if (!ext) {
    return 0; // No extension, don't count it
  }

  ext++; // Skip the dot

  // List of common source code file extensions
  const char *code_extensions[] = {
      // C and C++
      "c", "h", "cpp", "hpp", "cc", "c++", "cxx", "hxx",
      // Web
      "html", "htm", "css", "js", "jsx", "ts", "tsx", "php",
      // Other languages
      "py", "java", "cs", "go", "rb", "pl", "swift", "kt", "rs", "scala",
      "groovy", "lua", "r", "m", "mm",
      // Scripts
      "sh", "bat", "ps1", "cmd",
      // Data/config
      "json", "xml", "yaml", "yml", "toml", "ini", "conf", "md", NULL};

  // Check if extension is in the include list
  for (int i = 0; code_extensions[i]; i++) {
    if (strcasecmp(ext, code_extensions[i]) == 0) {
      return 1; // Included extension
    }
  }

  // Default behavior - don't count other files
  return 0;
}

/**
 * Count lines in a file
 */
unsigned long count_lines_in_file(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    return 0; // Unable to open file
  }

  unsigned long line_count = 0;
  int ch;
  int prev_ch = 0;

  while ((ch = fgetc(file)) != EOF) {
    // Count newlines
    if (ch == '\n') {
      line_count++;
    }
    prev_ch = ch;
  }

  // Count last line if it doesn't end with a newline
  if (prev_ch != '\n' && prev_ch != 0) {
    line_count++;
  }

  fclose(file);
  return line_count;
}

/**
 * Helper function to count lines in a directory
 */
void count_lines_in_directory(const char *directory, unsigned long *total_files,
                              unsigned long *total_lines, int recursive,
                              int verbose, HANDLE hConsole) {
  char search_path[MAX_PATH];
  WIN32_FIND_DATA find_data;
  HANDLE h_find;
  WORD originalAttributes;

  // Get original console attributes
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  originalAttributes = csbi.wAttributes;

  // Construct search pattern
  snprintf(search_path, sizeof(search_path), "%s\\*", directory);

  // Start file search
  h_find = FindFirstFile(search_path, &find_data);

  if (h_find == INVALID_HANDLE_VALUE) {
    SetConsoleTextAttribute(hConsole, current_theme.WARNING_COLOR);
    fprintf(stderr, "Error: Failed to access directory '%s'\n", directory);
    SetConsoleTextAttribute(hConsole, originalAttributes);
    return;
  }

  // If verbose mode is enabled, print the directory we're processing
  if (verbose) {
    SetConsoleTextAttribute(hConsole, current_theme.SECONDARY_COLOR);
    printf("\nDirectory: %s\n", directory);
    SetConsoleTextAttribute(hConsole, originalAttributes);
  }

  // Process all files in the directory
  do {
    // Skip "." and ".." directories
    if (strcmp(find_data.cFileName, ".") == 0 ||
        strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    // Construct full path to the file/directory
    char full_path[MAX_PATH];
    snprintf(full_path, sizeof(full_path), "%s\\%s", directory,
             find_data.cFileName);

    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // It's a directory - recurse if enabled
      if (recursive) {
        count_lines_in_directory(full_path, total_files, total_lines, recursive,
                                 verbose, hConsole);
      }
    } else {
      // It's a file - count lines if it's a source code file
      if (is_source_code_file(find_data.cFileName)) {
        unsigned long lines = count_lines_in_file(full_path);
        (*total_files)++;
        (*total_lines) += lines;

        if (verbose) {
          // Get file color based on extension
          WORD fileColor = get_file_color(find_data.cFileName);

          printf("  ");
          SetConsoleTextAttribute(hConsole, fileColor);
          printf("%-40s", find_data.cFileName);
          SetConsoleTextAttribute(hConsole, originalAttributes);
          printf(": ");
          SetConsoleTextAttribute(hConsole, current_theme.ACCENT_COLOR);
          printf("%lu lines\n", lines);
          SetConsoleTextAttribute(hConsole, originalAttributes);
        }
      }
    }
  } while (FindNextFile(h_find, &find_data));

  FindClose(h_find);
}

int lsh_exit(char **args) { return 0; }
