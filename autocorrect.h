/**
 * autocorrect.h
 * Header for command auto-correction functionality
 */

#ifndef AUTOCORRECT_H
#define AUTOCORRECT_H

#include "aliases.h"
#include "common.h"
#include "line_reader.h"
#include "shell.h"
#include <limits.h>

/**
 * Calculate Levenshtein distance between two strings
 *
 * @param s1 First string
 * @param s2 Second string
 * @return Edit distance between the strings
 */
int levenshtein_distance(const char *s1, const char *s2);

/**
 * Find minimum of two integers
 */
int min_distance(int a, int b);

/**
 * Find command suggestions based on similarity
 *
 * @param mistyped_cmd The mistyped command
 * @return The suggested command (must be freed by caller) or NULL if no
 * suggestion
 */
char *find_command_suggestion(const char *mistyped_cmd);

/**
 * Attempt to suggest corrections for a command
 *
 * @param args Command arguments
 * @return 1 if user accepts suggestion and command was executed, 0 otherwise
 */
int attempt_command_correction(char **args);

#endif // AUTOCORRECT_H
