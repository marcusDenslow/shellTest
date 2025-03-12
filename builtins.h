/**
 * builtins.h
 * Declarations for all built-in shell commands
 */

#ifndef BUILTINS_H
#define BUILTINS_H

#include "common.h"

// Syntax highlighting definitions
#define COLOR_DEFAULT 7   // FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
#define COLOR_KEYWORD 11  // FOREGROUND_CYAN | FOREGROUND_INTENSITY
#define COLOR_STRING 10   // FOREGROUND_GREEN | FOREGROUND_INTENSITY
#define COLOR_COMMENT 8   // FOREGROUND_INTENSITY (gray)
#define COLOR_NUMBER 13   // FOREGROUND_MAGENTA | FOREGROUND_INTENSITY
#define COLOR_PREPROCESSOR 14  // FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY (yellow)
#define COLOR_IDENTIFIER 15 // FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY

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

// Add command to history
void lsh_add_to_history(const char *command);

// Get number of builtin commands
int lsh_num_builtins(void);

// Expose the builtin command strings and function pointers
extern char *builtin_str[];
extern int (*builtin_func[]) (char **);

#endif // BUILTINS_H
