/**
 * external_commands.h
 * Functions for detecting and validating external commands in the PATH
 */

#ifndef EXTERNAL_COMMANDS_H
#define EXTERNAL_COMMANDS_H

#include "common.h"

/**
 * Initialize the external commands cache by scanning the PATH
 * This should be called during shell startup
 */
void init_external_commands(void);

/**
 * Clean up the external commands cache
 * This should be called during shell shutdown
 */
void cleanup_external_commands(void);

/**
 * Check if a command exists in the PATH
 *
 * @param cmd The command to check
 * @return 1 if command exists, 0 otherwise
 */
int is_external_command(const char *cmd);

/**
 * Get a list of all external commands for autocompletion
 *
 * @param prefix The prefix to match against
 * @param count Pointer to store the number of matches
 * @return Array of matched command names (caller must free)
 */
char **get_external_command_matches(const char *prefix, int *count);

/**
 * Manually add an external command to the cache
 * Useful for commands detected through other means
 *
 * @param cmd The command to add
 */
void add_external_command(const char *cmd);

/**
 * Refresh the external commands cache
 * This rescans the PATH for any new executables
 */
void refresh_external_commands(void);

#endif // EXTERNAL_COMMANDS_H
