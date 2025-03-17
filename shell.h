/**
 * shell.h
 * Core shell functionality
 */

#ifndef SHELL_H
#define SHELL_H

#include "common.h"
#include "structured_data.h"  // Add this include

/**
 * Execute a command
 * 
 * @param args Null-terminated array of command arguments
 * @return 1 to continue the shell, 0 to exit
 */
int lsh_execute(char **args);

/**
 * Execute a pipeline of commands
 * 
 * @param commands Null-terminated array of command arrays
 * @return 1 to continue the shell, 0 to exit
 */
int lsh_execute_piped(char ***commands);

/**
 * Launch an external program
 * 
 * @param args Null-terminated array of command arguments
 * @return Always returns 1 (continue the shell)
 */
int lsh_launch(char **args);

/**
 * Main shell loop
 */
void lsh_loop(void);

/**
 * Free memory for a command array from lsh_split_commands
 */
void free_commands(char ***commands);

#endif // SHELL_H
