/**
* builtins.c
* Implementation of all built-in shell commands
*/

#include "builtins.h"
#include "common.h"
#include <fileapi.h>
#include <handleapi.h>
#include <minwinbase.h>
#include <minwindef.h>
#include <stdio.h>
#include <ShlObj.h>
#include <ole2.h>
#include <olectl.h>
#include <wincrypt.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

// History command implementation
#define HISTORY_SIZE 10

// New struct to hold history entries with timestamps
typedef struct {
  char *command;
  SYSTEMTIME timestamp;
} HistoryEntry;

// Structure to store file info for directory listing
typedef struct {
    char timeString[32];
    char sizeString[32];
    char fileType[16];
    char fileName[MAX_PATH];
    BOOL isDirectory;
} FileInfo;

// Compare function for sorting directory entries
int compare_dir_entries(const void* a, const void* b) {
    const FileInfo* fa = (const FileInfo*)a;
    const FileInfo* fb = (const FileInfo*)b;
            
    // First sort by type (directories first)
    if (fa->isDirectory && !fb->isDirectory) return -1;
    if (!fa->isDirectory && fb->isDirectory) return 1;
            
    // Then sort by name
    return _stricmp(fa->fileName, fb->fileName);
}

static HistoryEntry command_history[HISTORY_SIZE] = {0};
static int history_count = 0;  // Total number of commands entered
static int history_index = 0;  // Current index in the circular buffer

// Array of built-in command names
char *builtin_str[] = {
"cd",
"help",
"exit",
"ls",
"dir",
"clear",
"cls",
"mkdir",
"rmdir",
"del",
"rm",
"touch",
"pwd",
"cat",
"history", 
"copy",
"cp",
"paste",
"move",
"mv",
};

// Array of built-in command function pointers
int (*builtin_func[]) (char **) = {
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
};

// Return the number of built-in commands
int lsh_num_builtins() {
return sizeof(builtin_str) / sizeof(char *);
}

// Add function to store commands in history with timestamps
void lsh_add_to_history(const char *command) {
  if (command == NULL || command[0] == '\0') {
      return;  // Don't add empty commands
  }
  
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
}

// Implement the history command with timestamps
int lsh_history(char **args) {
  if (history_count == 0) {
      printf("No commands in history\n");
      return 1;
  }
  
  // Calculate how many commands to display
  int num_to_display = (history_count < HISTORY_SIZE) ? history_count : HISTORY_SIZE;
  
  // Calculate starting index
  int start_idx;
  if (history_count <= HISTORY_SIZE) {
      // Haven't filled the buffer yet
      start_idx = 0;
  } else {
      // Buffer is full, start from the oldest command
      start_idx = history_index;  // Next slot to overwrite contains oldest command
  }
  
  // Display the commands with timestamps
  for (int i = 0; i < num_to_display; i++) {
      int idx = (start_idx + i) % HISTORY_SIZE;
      SYSTEMTIME *ts = &command_history[idx].timestamp;
      printf("[%02d:%02d:%02d] %s\n", 
             ts->wHour, ts->wMinute, ts->wSecond, 
             command_history[idx].command);
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
  if (ext == NULL) return FILE_TYPE_TEXT;
  
  ext++; // Skip the dot
  
  if (_stricmp(ext, "c") == 0) return FILE_TYPE_C;
  if (_stricmp(ext, "cpp") == 0 || _stricmp(ext, "cc") == 0) return FILE_TYPE_CPP;
  if (_stricmp(ext, "h") == 0 || _stricmp(ext, "hpp") == 0) return FILE_TYPE_H;
  if (_stricmp(ext, "py") == 0) return FILE_TYPE_PY;
  if (_stricmp(ext, "js") == 0) return FILE_TYPE_JS;
  if (_stricmp(ext, "html") == 0 || _stricmp(ext, "htm") == 0) return FILE_TYPE_HTML;
  if (_stricmp(ext, "css") == 0) return FILE_TYPE_CSS;
  if (_stricmp(ext, "md") == 0 || _stricmp(ext, "markdown") == 0) return FILE_TYPE_MD;
  if (_stricmp(ext, "json") == 0) return FILE_TYPE_JSON;
  
  return FILE_TYPE_TEXT;
}

/**
* Set console text color
*/
void set_color(int color) {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  SetConsoleTextAttribute(hConsole, color);
}

/**
* Reset console text color to default
*/
void reset_color() {
  set_color(COLOR_DEFAULT);
}

/**
* Check if a character is a word boundary character
*/
int is_separator(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
         c == '(' || c == ')' || c == '[' || c == ']' ||
         c == '{' || c == '}' || c == '.' || c == ',' ||
         c == ';' || c == ':' || c == '+' || c == '-' ||
         c == '/' || c == '*' || c == '%' || c == '=' ||
         c == '<' || c == '>' || c == '&' || c == '|' ||
         c == '^' || c == '!' || c == '~' || c == '?' ||
         c == '"' || c == '\'' || c == '\\';
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
      "auto", "break", "case", "char", "const", "continue", "default", "do",
      "double", "else", "enum", "extern", "float", "for", "goto", "if",
      "int", "long", "register", "return", "short", "signed", "sizeof", "static",
      "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
      "include", "define", "ifdef", "ifndef", "endif", "pragma", "error", "warning"
  };
  static const int c_keywords_count = sizeof(c_keywords) / sizeof(c_keywords[0]);
  
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
              
              if (*p == '*' && *(p+1) == '/') {
                  putchar(*(p+1));
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
              
              if (*p == '\\' && *(p+1)) {
                  putchar(*(p+1));
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
              
              if (*p == '\\' && *(p+1)) {
                  putchar(*(p+1));
                  p++;
              } else if (*p == '\'') {
                  in_char = 0;
                  reset_color();
              }
          }
          // Start of a comment
          else if (*p == '/' && *(p+1) == '/') {
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
              putchar(*(p+1));
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
          else if (*p == '/' && *(p+1) == '*') {
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
              putchar(*(p+1));
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
              // DON'T print the character here - we'll print the whole word when we hit a boundary
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
      "and", "as", "assert", "break", "class", "continue", "def", "del",
      "elif", "else", "except", "False", "finally", "for", "from", "global",
      "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
      "or", "pass", "raise", "return", "True", "try", "while", "with", "yield"
  };
  static const int py_keywords_count = sizeof(py_keywords) / sizeof(py_keywords[0]);
  
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
          if (in_string_single || in_string_double || in_triple_single || in_triple_double) {
              set_color(COLOR_STRING);
              putchar(*p);
              
              if (in_string_single && *p == '\'') {
                  in_string_single = 0;
                  reset_color();
              } else if (in_string_double && *p == '"') {
                  in_string_double = 0;
                  reset_color();
              } else if (in_triple_single && *p == '\'' && *(p+1) == '\'' && *(p+2) == '\'') {
                  putchar(*(p+1));
                  putchar(*(p+2));
                  p += 2;
                  in_triple_single = 0;
                  reset_color();
              } else if (in_triple_double && *p == '"' && *(p+1) == '"' && *(p+2) == '"') {
                  putchar(*(p+1));
                  putchar(*(p+2));
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
          else if (*p == '\'' && *(p+1) == '\'' && *(p+2) == '\'') {
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
              putchar(*(p+1));
              putchar(*(p+2));
              p += 2;
          }
          else if (*p == '"' && *(p+1) == '"' && *(p+2) == '"') {
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
              putchar(*(p+1));
              putchar(*(p+2));
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
              // Don't print characters immediately - wait until we have a complete word
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
              
              if (*p == '-' && *(p+1) == '-' && *(p+2) == '>') {
                  putchar(*(p+1));
                  putchar(*(p+2));
                  p += 2;
                  in_comment = 0;
                  reset_color();
              }
          }
          // Start of a comment
          else if (*p == '<' && *(p+1) == '!' && *(p+2) == '-' && *(p+3) == '-') {
              in_comment = 1;
              set_color(COLOR_COMMENT);
              putchar(*p);
              putchar(*(p+1));
              putchar(*(p+2));
              putchar(*(p+3));
              p += 3;
          }
          // Inside a string
          else if (in_string) {
              set_color(COLOR_STRING);
              putchar(*p);
              
              if ((*p == '"' || *p == '\'') && *(p-1) != '\\') {
                  in_string = 0;
                  if (in_tag) set_color(COLOR_KEYWORD);
                  else reset_color();
              }
          }
          // Start of a tag
          else if (*p == '<' && *(p+1) != '!') {
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
      "auto", "break", "case", "char", "const", "continue", "default", "do",
      "double", "else", "enum", "extern", "float", "for", "goto", "if",
      "int", "long", "register", "return", "short", "signed", "sizeof", "static",
      "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
      "include", "define", "ifdef", "ifndef", "endif", "pragma", "error", "warning"
  };
  static const int c_keywords_count = sizeof(c_keywords) / sizeof(c_keywords[0]);
  
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
              
              if (*p == '*' && *(p+1) == '/') {
                  putchar(*(p+1));
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
              
              if (*p == '\\' && *(p+1)) {
                  putchar(*(p+1));
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
              
              if (*p == '\\' && *(p+1)) {
                  putchar(*(p+1));
                  p++;
              } else if (*p == '\'') {
                  in_char = 0;
                  reset_color();
              }
          }
          // Start of a comment
          else if (*p == '/' && *(p+1) == '/') {
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
              putchar(*(p+1));
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
          else if (*p == '/' && *(p+1) == '*') {
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
              putchar(*(p+1));
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
              // DON'T print the character here - we'll print the whole word when we hit a boundary
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
      "and", "as", "assert", "break", "class", "continue", "def", "del",
      "elif", "else", "except", "False", "finally", "for", "from", "global",
      "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
      "or", "pass", "raise", "return", "True", "try", "while", "with", "yield"
  };
  static const int py_keywords_count = sizeof(py_keywords) / sizeof(py_keywords[0]);
  
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
          if (in_string_single || in_string_double || in_triple_single || in_triple_double) {
              set_color(COLOR_STRING);
              putchar(*p);
              
              if (in_string_single && *p == '\'') {
                  in_string_single = 0;
                  reset_color();
              } else if (in_string_double && *p == '"') {
                  in_string_double = 0;
                  reset_color();
              } else if (in_triple_single && *p == '\'' && *(p+1) == '\'' && *(p+2) == '\'') {
                  putchar(*(p+1));
                  putchar(*(p+2));
                  p += 2;
                  in_triple_single = 0;
                  reset_color();
              } else if (in_triple_double && *p == '"' && *(p+1) == '"' && *(p+2) == '"') {
                  putchar(*(p+1));
                  putchar(*(p+2));
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
          else if (*p == '\'' && *(p+1) == '\'' && *(p+2) == '\'') {
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
              putchar(*(p+1));
              putchar(*(p+2));
              p += 2;
          }
          else if (*p == '"' && *(p+1) == '"' && *(p+2) == '"') {
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
              putchar(*(p+1));
              putchar(*(p+2));
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
              // Don't print characters immediately - wait until we have a complete word
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
              
              if (*p == '-' && *(p+1) == '-' && *(p+2) == '>') {
                  putchar(*(p+1));
                  putchar(*(p+2));
                  p += 2;
                  in_comment = 0;
                  reset_color();
              }
          }
          // Start of a comment
          else if (*p == '<' && *(p+1) == '!' && *(p+2) == '-' && *(p+3) == '-') {
              in_comment = 1;
              set_color(COLOR_COMMENT);
              putchar(*p);
              putchar(*(p+1));
              putchar(*(p+2));
              putchar(*(p+3));
              p += 3;
          }
          // Inside a string
          else if (in_string) {
              set_color(COLOR_STRING);
              putchar(*p);
              
              if ((*p == '"' || *p == '\'') && *(p-1) != '\\') {
                  in_string = 0;
                  if (in_tag) set_color(COLOR_KEYWORD);
                  else reset_color();
              }
          }
          // Start of a tag
          else if (*p == '<' && *(p+1) != '!') {
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
* Display file contents with line numbers and optional syntax highlighting
*/
int lsh_cat(char **args) {
  if (args[1] == NULL) {
      fprintf(stderr, "lsh: expected file argument to \"cat\"\n");
      return 1;
  }
  
  // Check for flags (e.g., -s for syntax highlighting)
  int use_highlighting = 0; // Default to no highlighting
  int start_index = 1;
  
  if (args[1][0] == '-') {
      if (strcmp(args[1], "-s") == 0 || strcmp(args[1], "--syntax") == 0) {
          use_highlighting = 1;
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
      } else {
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
  HANDLE hFile = CreateFile(
    args[i],                       // filename
    FILE_WRITE_ATTRIBUTES,         // access mode
    FILE_SHARE_READ | FILE_SHARE_WRITE, // share mode
    NULL,                          // security attributes
    OPEN_ALWAYS,                   // create if doesn't exist, open if does
    FILE_ATTRIBUTE_NORMAL,         // file attributes
    NULL                           // template file
  );
  
  if (hFile == INVALID_HANDLE_VALUE) {
    // Failed to create/open file
    DWORD error = GetLastError();
    fprintf(stderr, "lsh: failed to touch '%s': error code %lu\n", args[i], error);
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
  if (_chdir(args[1]) != 0) {  // Use _chdir for Windows
    perror("lsh");
  }
}
return 1;
}

// Clear the screen
int lsh_clear(char **args) {
// Get the handle to the console
HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  
if (hConsole == INVALID_HANDLE_VALUE) {
  perror("lsh: failed to get console handle");
  return 1;
}
  
// Get console information
CONSOLE_SCREEN_BUFFER_INFO csbi;
if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
  perror("lsh: failed to get console info");
  return 1;
}
  
// Calculate total cells in console
DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
  
// Fill the entire buffer with spaces
DWORD count;
COORD homeCoords = {0, 0};
  
if (!FillConsoleOutputCharacter(hConsole, ' ', cellCount, homeCoords, &count)) {
  perror("lsh: failed to fill console");
  return 1;
}
  
// Fill the entire buffer with the current attributes
if (!FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, homeCoords, &count)) {
  perror("lsh: failed to set console attributes");
  return 1;
}
  
// Move the cursor to home position
SetConsoleCursorPosition(hConsole, homeCoords);
  
return 1;
}

// List directory contents with creation time and file type
int lsh_dir(char **args) {
    char cwd[1024];
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    
    // Define column widths for better alignment
    const int timeColWidth = 20;
    const int sizeColWidth = 15;
    const int typeColWidth = 10;
    const int nameColWidth = 30;
    
    // Get handle to console for output
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    // Get console screen size
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int consoleHeight = 0;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        consoleHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    } else {
        // Fallback if we can't get the console info
        consoleHeight = 25; // Common default height
    }
    
    // Get current directory
    if (_getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("lsh");
        return 1;
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
        // Skip . and .. directories for cleaner output
        if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
            fileCount++;
        }
    } while (FindNextFile(hFind, &findData));
    
    FindClose(hFind);
    
    // Calculate listing height
    // Title line + blank line = 2
    // Top header (3 lines)
    // One line per file
    // Bottom border line
    // Total = 2 + 3 + fileCount + 1 = 6 + fileCount
    int listingHeight = 6 + fileCount;
    
    // Determine if the listing will be taller than the console height
    // If yes, we'll add headers at the bottom
    int needBottomHeader = (listingHeight > consoleHeight);
    
    // Allocate array for all files
    FileInfo *fileInfoArray = (FileInfo*)malloc(sizeof(FileInfo) * (fileCount > 0 ? fileCount : 1));
    if (!fileInfoArray) {
        fprintf(stderr, "lsh: allocation error\n");
        return 1;
    }
    
    int fileInfoIndex = 0;
    
    // Second pass to actually process files
    hFind = FindFirstFile(searchPath, &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "lsh: Failed to list directory contents\n");
        free(fileInfoArray);
        return 1;
    }

    // Process all files and store in array
    do {
        // Skip . and .. directories for cleaner output
        if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
            // Convert file creation time to system time
            SYSTEMTIME fileTime;
            FileTimeToSystemTime(&findData.ftCreationTime, &fileTime);
            
            // Format creation time as string
            char timeString[32];
            sprintf(timeString, "%04d-%02d-%02d %02d:%02d:%02d", 
                    fileTime.wYear, fileTime.wMonth, fileTime.wDay,
                    fileTime.wHour, fileTime.wMinute, fileTime.wSecond);
            
            // Check if it's a directory
            BOOL isDirectory = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
            const char* fileType = isDirectory ? "Directory" : "File";
            
            // Format size (only for files)
            char sizeString[32];
            if (isDirectory) {
                strcpy(sizeString, "-");
            } else {
                // Format size with appropriate units
                if (findData.nFileSizeLow < 1024) {
                    sprintf(sizeString, "%lu B", findData.nFileSizeLow);
                } else if (findData.nFileSizeLow < 1024 * 1024) {
                    sprintf(sizeString, "%.1f KB", findData.nFileSizeLow / 1024.0);
                } else {
                    sprintf(sizeString, "%.1f MB", findData.nFileSizeLow / (1024.0 * 1024.0));
                }
            }
            
            // Truncate filename if too long
            char truncName[nameColWidth];
            strncpy(truncName, findData.cFileName, nameColWidth-3);
            truncName[nameColWidth-3] = '\0';
            if (strlen(findData.cFileName) > nameColWidth-3) {
                strcat(truncName, "...");
            }
            
            // Store in array for sorting and display
            strcpy(fileInfoArray[fileInfoIndex].timeString, timeString);
            strcpy(fileInfoArray[fileInfoIndex].sizeString, sizeString);
            strcpy(fileInfoArray[fileInfoIndex].fileType, fileType);
            strcpy(fileInfoArray[fileInfoIndex].fileName, truncName);
            fileInfoArray[fileInfoIndex].isDirectory = isDirectory;
            fileInfoIndex++;
        }
    } while (FindNextFile(hFind, &findData));

    // Close find handle
    FindClose(hFind);
    
    // Sort the array by type (directories first) and then by name
    qsort(fileInfoArray, fileInfoIndex, sizeof(FileInfo), compare_dir_entries);
    
    // Print directory info at the top
    printf("\nContents of directory: %s (%d items)\n\n", cwd, fileInfoIndex);
    
    // Print table header
    printf("+%.*s+%.*s+%.*s+%.*s+\n", 
        timeColWidth, "--------------------", 
        sizeColWidth, "---------------", 
        typeColWidth, "----------", 
        nameColWidth, "------------------------------");
        
    printf("| %-*s | %-*s | %-*s | %-*s |\n", 
        timeColWidth-2, "Created", 
        sizeColWidth-2, "Size", 
        typeColWidth-2, "Type", 
        nameColWidth-2, "Name");
        
    printf("+%.*s+%.*s+%.*s+%.*s+\n", 
        timeColWidth, "--------------------", 
        sizeColWidth, "---------------", 
        typeColWidth, "----------", 
        nameColWidth, "------------------------------");
    
    // Print all stored file information
    for (int i = 0; i < fileInfoIndex; i++) {
        printf("| %-*s | %-*s | %-*s | %-*s |\n", 
            timeColWidth-2, fileInfoArray[i].timeString, 
            sizeColWidth-2, fileInfoArray[i].sizeString, 
            typeColWidth-2, fileInfoArray[i].fileType, 
            nameColWidth-2, fileInfoArray[i].fileName);
    }
    
    // Print bottom border of table
    printf("+%.*s+%.*s+%.*s+%.*s+\n", 
        timeColWidth, "--------------------", 
        sizeColWidth, "---------------", 
        typeColWidth, "----------", 
        nameColWidth, "------------------------------");
        
    // Print headers again at the bottom ONLY if the listing is taller than the console
    if (needBottomHeader) {
        printf("| %-*s | %-*s | %-*s | %-*s |\n", 
            timeColWidth-2, "Created", 
            sizeColWidth-2, "Size", 
            typeColWidth-2, "Type", 
            nameColWidth-2, "Name");
            
        printf("+%.*s+%.*s+%.*s+%.*s+\n", 
            timeColWidth, "--------------------", 
            sizeColWidth, "---------------", 
            typeColWidth, "----------", 
            nameColWidth, "------------------------------");
    }
    
    // Clean up
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
      fprintf(stderr, "  -raw    Copy file contents to clipboard instead of the file itself\n");
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
      char *file_content = (char*)malloc(file_size + 1);
      if (!file_content) {
          fprintf(stderr, "lsh: failed to allocate memory for file content\n");
          fclose(file);
          return 1;
      }
      
      size_t bytes_read = fread(file_content, 1, file_size, file);
      file_content[bytes_read] = '\0';  // Null-terminate the string
      fclose(file);
      
      // Allocate global memory for clipboard
      HGLOBAL h_mem = GlobalAlloc(GMEM_MOVEABLE, bytes_read + 1);
      if (h_mem == NULL) {
          fprintf(stderr, "lsh: failed to allocate global memory for clipboard\n");
          free(file_content);
          return 1;
      }
      
      // Lock the memory and copy file content
      char *clipboard_content = (char*)GlobalLock(h_mem);
      if (clipboard_content == NULL) {
          fprintf(stderr, "lsh: failed to lock global memory\n");
          GlobalFree(h_mem);
          free(file_content);
          return 1;
      }
      
      memcpy(clipboard_content, file_content, bytes_read);
      clipboard_content[bytes_read] = '\0';  // Null-terminate the string
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
      
      printf("Copied contents of '%s' to clipboard (%ld bytes)\n", filename, bytes_read);
  } else {
      // NORMAL MODE: Copy file for internal paste operation
      fclose(file);  // We don't need the file content for this mode
      
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
      
      printf("Copied '%s' - ready for pasting with the 'paste' command\n", fileBaseName);
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
      if (fgets(response, sizeof(response), stdin) == NULL || (response[0] != 'y' && response[0] != 'Y')) {
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
        fprintf(stderr, "lsh: expected source and destination arguments for \"move\"\n");
        fprintf(stderr, "Usage: move <source> <destination>\n");
        return 1;
    }
    
    // Check if source exists
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(args[1], &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "lsh: cannot find source '%s': No such file or directory\n", args[1]);
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
    if (destAttrs != INVALID_FILE_ATTRIBUTES && (destAttrs & FILE_ATTRIBUTE_DIRECTORY)) {
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
        fprintf(stderr, "lsh: '%s' and '%s' are the same file\n", srcPath, destPath);
        return 1;
    }
    
    // Check if destination already exists
    if (GetFileAttributes(destPath) != INVALID_FILE_ATTRIBUTES) {
        char response[10];
        printf("'%s' already exists. Overwrite? (y/n): ", destPath);
        if (fgets(response, sizeof(response), stdin) == NULL || (response[0] != 'y' && response[0] != 'Y')) {
            printf("Move canceled\n");
            return 1;
        }
    }
    
    // Perform the move operation
    BOOL result = MoveFileEx(
        srcPath,
        destPath,
        MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING
    );
    
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
int i;
printf("Marcus Denslow's LSH\n");
printf("Type program names and arguments, and hit enter.\n");
printf("The following are built in:\n");

for (i = 0; i < lsh_num_builtins(); i++) {
  printf("  %s\n", builtin_str[i]);
}

printf("Use the command for information on other programs.\n");
return 1;
}

// Exit the shell
int lsh_exit(char **args) {
  return 0;
}
