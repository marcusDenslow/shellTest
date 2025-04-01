/**
 * persistent_history.h
 * Functions for persistent command history and frequency tracking
 */

#ifndef PERSISTENT_HISTORY_H
#define PERSISTENT_HISTORY_H

#include "common.h"
#include "shell.h"

// Increase history size for persistent storage
#define PERSISTENT_HISTORY_SIZE 1000

// Structure for a history entry
typedef struct {
  char *command;
  SYSTEMTIME timestamp;
} PersistentHistoryEntry;

// Structure for command frequency tracking
typedef struct {
  char *command;
  int count;
} CommandFrequency;

// Initialize persistent history
void init_persistent_history(void);

// Clean up persistent history
void cleanup_persistent_history(void);

// Add a command to persistent history
void add_to_persistent_history(const char *command);

// Get command suggestions based on prefix and frequency
char **get_frequency_suggestions(const char *prefix, int *num_suggestions);

// Save history to file
void save_history_to_file(void);

// Load history from file
void load_history_from_file(void);

// Update command frequency
void update_command_frequency(const char *command);

// Save frequencies to file
void save_frequencies_to_file(void);

// Load frequencies from file
void load_frequencies_from_file(void);

// Get the history entry at specified index
PersistentHistoryEntry *get_history_entry(int index);

// Get the total number of history entries
int get_history_count(void);

// Find the best matching command based on frequency
char *find_best_frequency_match(const char *prefix);

void debug_print_frequencies(void);
char *_stristr(const char *haystack, const char *needle);

#endif // PERSISTENT_HISTORY_H
