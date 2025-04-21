/**
 * line_reader.h
 * Functions for reading and parsing command lines
 */

#ifndef LINE_READER_H
#define LINE_READER_H

#include "common.h"

/**
 * Read a line of input from the user with tab completion
 *
 * @return The line read from stdin (must be freed by caller)
 */

int is_valid_command(const char *cmd);

int update_command_validity(const char *buffer, int position, HANDLE hConsole,
                            COORD promptEndPos, int previousValid);

char *lsh_read_line(void);

/**
 * Split a line into tokens
 *
 * @param line The line to split
 * @return An array of tokens (must be freed by caller)
 */
char **lsh_split_line(char *line);

/**
 * Split a line into commands separated by pipes
 *
 * @param line The line to split
 * @return A NULL-terminated array of command token arrays
 */
char ***lsh_split_commands(char *line);

#endif // LINE_READER_H
