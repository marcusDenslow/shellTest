/**
 * shell.h
 * Core shell functionality
 */

#ifndef SHELL_H
#define SHELL_H

#include "common.h"

/**
 * Execute a command
 * 
 * @param args Null-terminated array of command arguments
 * @return 1 to continue the shell, 0 to exit
 */
int lsh_execute(char **args);

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

#endif // SHELL_H
