/**
 * builtins.h
 * Declarations for all built-in shell commands
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include "aliases.h"   // Added for alias support
#include "bookmarks.h" // Added for bookmark support
#include "common.h"
#include "favorite_cities.h" // Added for favorite cities support
#include "fzf_native.h"
#include "structured_data.h" // Add this include

// Syntax highlighting definitions
#define COLOR_DEFAULT 7  // FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
#define COLOR_KEYWORD 11 // FOREGROUND_CYAN | FOREGROUND_INTENSITY
#define COLOR_STRING 10  // FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_COMMENT 8  // FOREGROUND_INTENSITY (gray)
#define COLOR_NUMBER 13  // FOREGROUND_MAGENTA | FOREGROUND_INTENSITY
#define COLOR_PREPROCESSOR                                                     \
  14 // FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY (yellow)
#define COLOR_IDENTIFIER                                                       \
  15 // FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
     // FOREGROUND_INTENSITY

// Add declarations for color functions
void set_color(int color);
void reset_color();

// History command implementation
#define HISTORY_SIZE 10

// New struct to hold history entries with timestamps
typedef struct {
  char *command;
  SYSTEMTIME timestamp;
} HistoryEntry;

// History variables - made extern to be accessible from line_reader.c
extern HistoryEntry command_history[HISTORY_SIZE];
extern int history_count;
extern int history_index;

// Built-in command declarations
int lsh_cd(char **args);
int lsh_help(char **args);
int lsh_exit(char **args);
int lsh_dir(char **args);
int lsh_clear(char **args);
int lsh_mkdir(char **args);
int lsh_rmdir(char **args);
int lsh_del(char **args);
int lsh_touch(char **args);
int lsh_pwd(char **args);
int lsh_cat(char **args);
int lsh_history(char **args);
int lsh_copy(char **args);
int lsh_paste(char **args);
int lsh_move(char **args);
int lsh_ps(char **args);
int lsh_news(char **args);
// Alias command declarations - added for alias support
int lsh_alias(char **args);
int lsh_unalias(char **args);
int lsh_aliases(char **args); // New command to list all aliases
// Bookmark command declarations - added for bookmark support
int lsh_bookmark(char **args);
int lsh_bookmarks(char **args);
int lsh_goto(char **args);
int lsh_unbookmark(char **args);
int lsh_focus_timer(char **args);
int lsh_weather(char **args);
// Text search command
int lsh_grep(char **args);
int lsh_actual_grep(char **args);
int lsh_ripgrep(char **args);
int lsh_fzf_native(char **args);

// Commands with structured output
TableData *lsh_dir_structured(char **args);
TableData *lsh_ps_structured(char **args);

// Add command to history
void lsh_add_to_history(const char *command);

// Get number of builtin commands
int lsh_num_builtins(void);

// Expose the builtin command strings and function pointers
extern char *builtin_str[];
extern int (*builtin_func[])(char **);

/**
 * Extract a string value from a JSON object
 *
 * @param json The JSON string to parse
 * @param key The key to find
 * @return Allocated string containing the value (caller must free)
 */
char *extract_json_string(const char *json, const char *key);

// Include filter command declarations from filters.h
#include "filters.h"

#endif // BUILTINS_H
