/**
 * autocorrect.c
 * Implementation of command auto-correction functionality
 */

#include "autocorrect.h"
#include "builtins.h"

/**
 * Calculate Levenshtein distance between two strings
 * Used to determine similarity between mistyped command and potential
 * corrections
 */
int levenshtein_distance(const char *s1, const char *s2) {
  int len1 = strlen(s1);
  int len2 = strlen(s2);

  // Create a matrix to store distances
  int **matrix = (int **)malloc((len1 + 1) * sizeof(int *));
  for (int i = 0; i <= len1; i++) {
    matrix[i] = (int *)malloc((len2 + 1) * sizeof(int));
  }

  // Initialize the matrix
  for (int i = 0; i <= len1; i++) {
    matrix[i][0] = i;
  }

  for (int j = 0; j <= len2; j++) {
    matrix[0][j] = j;
  }

  // Fill the matrix
  for (int i = 1; i <= len1; i++) {
    for (int j = 1; j <= len2; j++) {
      int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;

      int deletion = matrix[i - 1][j] + 1;
      int insertion = matrix[i][j - 1] + 1;
      int substitution = matrix[i - 1][j - 1] + cost;

      matrix[i][j] =
          min_distance(min_distance(deletion, insertion), substitution);
    }
  }

  // Get the result
  int result = matrix[len1][len2];

  // Free the matrix
  for (int i = 0; i <= len1; i++) {
    free(matrix[i]);
  }
  free(matrix);

  return result;
}

/**
 * Find the minimum of two integers
 */
int min_distance(int a, int b) { return (a < b) ? a : b; }

/**
 * Find command suggestions based on similarity
 * Returns the most similar command found within threshold
 */
char *find_command_suggestion(const char *mistyped_cmd) {
  int best_distance = INT_MAX;
  int best_index = -1;

  // Threshold for considering a command as a viable suggestion
  // Lower = stricter matching
  const int DISTANCE_THRESHOLD = 3;

  // Compare with builtin commands
  for (int i = 0; i < lsh_num_builtins(); i++) {
    // Only consider commands with similar length (optimization)
    if (abs((int)strlen(builtin_str[i]) - (int)strlen(mistyped_cmd)) <=
        DISTANCE_THRESHOLD) {
      int distance = levenshtein_distance(mistyped_cmd, builtin_str[i]);

      if (distance < best_distance && distance <= DISTANCE_THRESHOLD) {
        best_distance = distance;
        best_index = i;
      }
    }
  }

  // Check aliases as well
  if (aliases != NULL) {
    for (int i = 0; i < alias_count; i++) {
      if (abs((int)strlen(aliases[i].name) - (int)strlen(mistyped_cmd)) <=
          DISTANCE_THRESHOLD) {
        int distance = levenshtein_distance(mistyped_cmd, aliases[i].name);

        if (distance < best_distance && distance <= DISTANCE_THRESHOLD) {
          // Found a closer match in an alias
          return _strdup(aliases[i].name);
        }
      }
    }
  }

  // If a good built-in command match was found, return it
  if (best_index != -1) {
    return _strdup(builtin_str[best_index]);
  }

  // Also check for common external commands
  static const char *common_commands[] = {
      "git",  "npm",  "python", "python3", "pip", "gcc",    "make",
      "curl", "wget", "ssh",    "code",    "vim", "notepad"};

  best_distance = INT_MAX;
  const char *best_match = NULL;

  for (int i = 0; i < sizeof(common_commands) / sizeof(common_commands[0]);
       i++) {
    if (abs((int)strlen(common_commands[i]) - (int)strlen(mistyped_cmd)) <=
        DISTANCE_THRESHOLD) {
      int distance = levenshtein_distance(mistyped_cmd, common_commands[i]);

      if (distance < best_distance && distance <= DISTANCE_THRESHOLD) {
        best_distance = distance;
        best_match = common_commands[i];
      }
    }
  }

  if (best_match != NULL) {
    return _strdup(best_match);
  }

  // No good suggestions found
  return NULL;
}

/**
 * Attempt to suggest corrections for a command
 * Returns 1 if user accepts suggestion and the corrected command was executed
 * Returns 0 if no suggestion or user rejects suggestion
 */
int attempt_command_correction(char **args) {
  if (args == NULL || args[0] == NULL) {
    return 0;
  }

  char *suggestion = find_command_suggestion(args[0]);
  if (suggestion == NULL) {
    return 0;
  }

  // Prepare the suggested command with all the original args
  char suggested_cmd[1024] = "";
  strcpy(suggested_cmd, suggestion);

  for (int i = 1; args[i] != NULL; i++) {
    strcat(suggested_cmd, " ");
    strcat(suggested_cmd, args[i]);
  }

  // Ask the user if they want to run the suggested command
  printf("Command not found: '%s'. Did you mean '%s'? (y/n): ", args[0],
         suggested_cmd);

  char response[10];
  if (fgets(response, sizeof(response), stdin) == NULL ||
      (response[0] != 'y' && response[0] != 'Y')) {
    // User rejected suggestion
    free(suggestion);
    return 0;
  }

  // Free the original suggestion, we don't need it anymore
  free(suggestion);

  // Parse and execute the suggested command
  char *cmd_copy = _strdup(suggested_cmd);
  if (cmd_copy == NULL) {
    fprintf(stderr, "lsh: memory allocation error in autocorrect\n");
    return 0;
  }

  // Parse the suggested command
  char **corrected_args = lsh_split_line(cmd_copy);

  // Execute the suggested command
  int result = lsh_execute(corrected_args);

  // Clean up
  free(cmd_copy);
  free(corrected_args);

  return 1;
}
