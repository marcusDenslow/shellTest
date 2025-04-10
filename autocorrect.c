/**
 * autocorrect.c
 * Implementation of command auto-correction functionality
 */

#include "autocorrect.h"
#include "builtins.h"
#include <winbase.h>

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

  // Get handle to console for colored output
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  WORD originalAttributes;

  // Get original console attributes to restore later
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  originalAttributes = csbi.wAttributes;

  // Reconstruct the original command line for display
  char cmdLine[1024] = "";
  for (int i = 0; args[i] != NULL; i++) {
    if (i > 0)
      strcat(cmdLine, " ");
    strcat(cmdLine, args[i]);
  }

  // Print error message with command and visual indicator
  fprintf(stderr, "Command not found:\n");

  // First line: show the full command
  fprintf(stderr, "  %s\n", cmdLine);

  // Second line: create an arrow pointing to the error
  // Calculate the position for the arrow (include 2 spaces padding)
  int arrowPos = 2;

  // Create the arrow line with proper spacing
  char arrowLine[1024] = "  ";
  for (int i = 0; i < strlen(args[0]); i++) {
    if (i == 0) {
      strcat(arrowLine, "^");
    } else {
      strcat(arrowLine, "~");
    }
  }

  // Print the arrow in red
  SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
  fprintf(stderr, "%s\n", arrowLine);

  // Print suggestion in highlighted color
  SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_INTENSITY);
  fprintf(stderr, "help: Did you mean '%s'?\n\n", suggestion);

  // Reset color
  SetConsoleTextAttribute(hConsole, originalAttributes);

  // Free the suggestion
  free(suggestion);

  return 0; // Return 0 to continue with the shell loop
}
