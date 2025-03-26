/**
 * shell.h
 * Core shell functionality
 */

#ifndef SHELL_H
#define SHELL_H

#include "common.h"
#include "structured_data.h"

/**
 * Display a welcome banner with BBQ sauce invention time
 */
void display_welcome_banner(void);

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

/**
 * Initialize the status bar at the bottom of the screen
 *
 * @param hConsole Handle to the console
 * @return 1 if successful, 0 on failure
 */
int init_status_bar(HANDLE hConsole);

/**
 * Temporarily hide the status bar before command execution
 *
 * @param hConsole Handle to the console
 */
void hide_status_bar(HANDLE hConsole);

/**
 * Ensures there's space for the status bar by scrolling if needed
 *
 * @param hConsole Handle to the console
 */
void ensure_status_bar_space(HANDLE hConsole);

/**
 * Check for console window resize and update status bar position
 *
 * @param hConsole Handle to the console
 */
void check_console_resize(HANDLE hConsole);

/**
 * Update the status bar with Git information
 *
 * @param hConsole Handle to the console
 * @param git_info The Git information to display in the status bar
 */
void update_status_bar(HANDLE hConsole, const char *git_info);

/**
 * Get the name of the parent and current directories from a path
 *
 * @param cwd The current working directory path
 * @param parent_dir_name Buffer to store the parent directory name
 * @param current_dir_name Buffer to store the current directory name
 * @param buf_size Size of the buffers
 */
void get_path_display(const char *cwd, char *parent_dir_name,
                      char *current_dir_name, size_t buf_size);

#endif // SHELL_H
