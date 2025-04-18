/**
 * persistent_history.c
 * Implementation of persistent command history and frequency tracking
 */

#include "persistent_history.h"
#include "command_docs.h"
#include "external_commands.h"
#include <direct.h> // For _getcwd
#include <shlobj.h> // For SHGetFolderPath

// Global variables for persistent history
static PersistentHistoryEntry *history_entries = NULL;
static int history_size = 0;
static int history_capacity = 0;
static int history_position = 0;

// Global variables for frequency tracking
static CommandFrequency *command_frequencies = NULL;
static int frequency_count = 0;
static int frequency_capacity = 0;

// File paths for persistence
static char history_file_path[MAX_PATH] = "";
static char frequency_file_path[MAX_PATH] = "";

/**
 * Get the path to the user's home directory
 */
static void get_home_directory(char *path, size_t size) {
  if (SHGetFolderPath(NULL, CSIDL_PROFILE, NULL, 0, path) != S_OK) {
    // Fallback to current directory if home not available
    _getcwd(path, size);
  }
}

/**
 * Initialize the persistent history system
 */
void init_persistent_history(void) {
  // Allocate initial history buffer
  history_capacity = 100; // Start with space for 100 entries
  history_entries = (PersistentHistoryEntry *)malloc(
      history_capacity * sizeof(PersistentHistoryEntry));
  if (!history_entries) {
    fprintf(stderr, "Failed to allocate memory for persistent history\n");
    return;
  }

  // Initialize history entries
  for (int i = 0; i < history_capacity; i++) {
    history_entries[i].command = NULL;
  }

  // Allocate initial frequency buffer
  frequency_capacity = 100; // Start with space for 100 entries
  command_frequencies =
      (CommandFrequency *)malloc(frequency_capacity * sizeof(CommandFrequency));
  if (!command_frequencies) {
    fprintf(stderr, "Failed to allocate memory for command frequencies\n");
    return;
  }

  // Initialize frequency entries
  for (int i = 0; i < frequency_capacity; i++) {
    command_frequencies[i].command = NULL;
    command_frequencies[i].count = 0;
  }

  // Determine file paths
  char home_dir[MAX_PATH];
  get_home_directory(home_dir, MAX_PATH);

  sprintf(history_file_path, "%s\\.lsh_history", home_dir);
  sprintf(frequency_file_path, "%s\\.lsh_frequencies", home_dir);

  // Load existing data
  load_history_from_file();
  load_frequencies_from_file();
}

/**
 * Clean up the persistent history system
 */
void cleanup_persistent_history(void) {
  // Save data before cleanup
  save_history_to_file();
  save_frequencies_to_file();

  // Free history entries
  if (history_entries) {
    for (int i = 0; i < history_size; i++) {
      if (history_entries[i].command) {
        free(history_entries[i].command);
      }
    }
    free(history_entries);
    history_entries = NULL;
  }

  // Free frequency entries
  if (command_frequencies) {
    for (int i = 0; i < frequency_count; i++) {
      if (command_frequencies[i].command) {
        free(command_frequencies[i].command);
      }
    }
    free(command_frequencies);
    command_frequencies = NULL;
  }
}

void add_to_persistent_history(const char *command) {
  if (!command || !command[0] || !history_entries) {
    return; // Don't add empty commands or if history not initialized
  }

  // Ignore commands that look like partial entries
  // For example, commands that are just a few characters that are likely part
  // of a longer command
  if (strlen(command) < 3) {
    return; // Skip very short commands as they're likely partial
  }

  // Check if we need to expand the history buffer
  if (history_size >= history_capacity) {
    int new_capacity = history_capacity * 2;
    PersistentHistoryEntry *new_entries = (PersistentHistoryEntry *)realloc(
        history_entries, new_capacity * sizeof(PersistentHistoryEntry));

    if (!new_entries) {
      fprintf(stderr, "Failed to expand history buffer\n");
      return;
    }

    // Initialize the new entries
    for (int i = history_capacity; i < new_capacity; i++) {
      new_entries[i].command = NULL;
    }

    history_entries = new_entries;
    history_capacity = new_capacity;
  }

  // Add the command to history
  if (history_position < history_size) {
    // Replace an existing entry
    if (history_entries[history_position].command) {
      free(history_entries[history_position].command);
    }
  } else {
    // Add a new entry
    history_size++;
  }

  history_entries[history_position].command = _strdup(command);
  GetLocalTime(&history_entries[history_position].timestamp);

  // Update position
  history_position = (history_position + 1) % PERSISTENT_HISTORY_SIZE;
  if (history_position >= history_capacity) {
    history_position = 0; // Wrap around if needed
  }

  // Update command frequency
  update_command_frequency(command);

  // Save to file every 10 commands (can adjust this for performance)
  if (history_size % 10 == 0) {
    save_history_to_file();
    save_frequencies_to_file();
  }
}

/**
 * Find the best matching command based on frequency
 */
char *find_best_frequency_match(const char *prefix) {
  if (!prefix || !prefix[0] || !command_frequencies) {
    return NULL;
  }

  char *best_match = NULL;
  int highest_freq = 0;

  // Find the most frequent command that starts with the prefix
  for (int i = 0; i < frequency_count; i++) {
    if (command_frequencies[i].command &&
        _strnicmp(command_frequencies[i].command, prefix, strlen(prefix)) ==
            0) {
      if (command_frequencies[i].count > highest_freq) {
        highest_freq = command_frequencies[i].count;
        if (best_match)
          free(best_match);
        best_match = _strdup(command_frequencies[i].command);
      }
    }
  }

  return best_match;
}

/**
 * Update command frequency
 */

/**
 * Improved update_command_frequency function for persistent_history.c
 * This gives more weight to recent commands and saves more frequently
 */
void update_command_frequency(const char *command) {
  if (!command || !command[0] || !command_frequencies) {
    return;
  }

  // Check if command already exists in frequency list
  for (int i = 0; i < frequency_count; i++) {
    if (command_frequencies[i].command &&
        strcmp(command_frequencies[i].command, command) == 0) {
      // Command found, increment count with higher weight
      command_frequencies[i].count +=
          2; // Give more weight to recently used commands

      // Save frequencies immediately after updating
      save_frequencies_to_file();
      return;
    }
  }

  // Command not found, add it
  if (frequency_count >= frequency_capacity) {
    // Expand the buffer
    int new_capacity = frequency_capacity * 2;
    CommandFrequency *new_frequencies = (CommandFrequency *)realloc(
        command_frequencies, new_capacity * sizeof(CommandFrequency));

    if (!new_frequencies) {
      fprintf(stderr, "Failed to expand frequency buffer\n");
      return;
    }

    // Initialize new entries
    for (int i = frequency_capacity; i < new_capacity; i++) {
      new_frequencies[i].command = NULL;
      new_frequencies[i].count = 0;
    }

    command_frequencies = new_frequencies;
    frequency_capacity = new_capacity;
  }

  // Add the new command with initial count of 1
  command_frequencies[frequency_count].command = _strdup(command);
  command_frequencies[frequency_count].count = 1;
  frequency_count++;

  // Save frequencies immediately after adding
  save_frequencies_to_file();
}

/**
 * Add this debug function to persistent_history.c
 * to help diagnose frequency tracking issues
 */
void debug_print_frequencies() {
  printf("\n--- Current Command Frequencies ---\n");

  // Sort the frequencies by count (highest first)
  CommandFrequency *sorted =
      (CommandFrequency *)malloc(frequency_count * sizeof(CommandFrequency));

  // Copy all entries
  for (int i = 0; i < frequency_count; i++) {
    sorted[i].command = command_frequencies[i].command;
    sorted[i].count = command_frequencies[i].count;
  }

  // Sort (bubble sort for simplicity)
  for (int i = 0; i < frequency_count - 1; i++) {
    for (int j = 0; j < frequency_count - i - 1; j++) {
      if (sorted[j].count < sorted[j + 1].count) {
        // Swap
        CommandFrequency temp = sorted[j];
        sorted[j] = sorted[j + 1];
        sorted[j + 1] = temp;
      }
    }
  }

  // Print top 10 or all if less than 10
  int to_print = frequency_count < 10 ? frequency_count : 10;
  for (int i = 0; i < to_print; i++) {
    printf("%d: %s (count: %d)\n", i + 1, sorted[i].command, sorted[i].count);
  }

  printf("------------------------------\n");

  // Free the sorted array (but not the strings, as they're shared with the
  // original)
  free(sorted);
}

/**
 * Save history to file
 */
void save_history_to_file(void) {
  if (!history_file_path[0] || !history_entries || history_size == 0) {
    return;
  }

  FILE *file = fopen(history_file_path, "w");
  if (!file) {
    fprintf(stderr, "Failed to open history file for writing: %s\n",
            history_file_path);
    return;
  }

  // Write history entries in order (oldest to newest)
  int start = (history_position - history_size + PERSISTENT_HISTORY_SIZE) %
              PERSISTENT_HISTORY_SIZE;
  if (start < 0)
    start += PERSISTENT_HISTORY_SIZE;

  for (int i = 0; i < history_size; i++) {
    int idx = (start + i) % PERSISTENT_HISTORY_SIZE;
    if (idx >= history_capacity)
      idx = 0; // Wrap around if needed

    if (history_entries[idx].command) {
      // Format: YYYY-MM-DD HH:MM:SS|command
      SYSTEMTIME *ts = &history_entries[idx].timestamp;
      fprintf(file, "%04d-%02d-%02d %02d:%02d:%02d|%s\n", ts->wYear, ts->wMonth,
              ts->wDay, ts->wHour, ts->wMinute, ts->wSecond,
              history_entries[idx].command);
    }
  }

  fclose(file);
}

/**
 * Load history from file
 */
void load_history_from_file(void) {
  if (!history_file_path[0] || !history_entries) {
    return;
  }

  FILE *file = fopen(history_file_path, "r");
  if (!file) {
    // It's OK if the file doesn't exist yet
    return;
  }

  // Clear existing history
  for (int i = 0; i < history_capacity; i++) {
    if (history_entries[i].command) {
      free(history_entries[i].command);
      history_entries[i].command = NULL;
    }
  }

  history_size = 0;
  history_position = 0;

  // Read history entries
  char line[1024];
  while (fgets(line, sizeof(line), file) &&
         history_size < PERSISTENT_HISTORY_SIZE) {
    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }

    // Find separator between timestamp and command
    char *separator = strchr(line, '|');
    if (!separator) {
      continue; // Invalid line format
    }

    // Split line into timestamp and command
    *separator = '\0';
    char *timestamp_str = line;
    char *command = separator + 1;

    // Parse timestamp
    SYSTEMTIME ts = {0};
    sscanf(timestamp_str, "%d-%d-%d %d:%d:%d", &ts.wYear, &ts.wMonth, &ts.wDay,
           &ts.wHour, &ts.wMinute, &ts.wSecond);

    // Add to history
    if (history_size >= history_capacity) {
      // Expand history buffer if needed
      int new_capacity = history_capacity * 2;
      PersistentHistoryEntry *new_entries = (PersistentHistoryEntry *)realloc(
          history_entries, new_capacity * sizeof(PersistentHistoryEntry));

      if (!new_entries) {
        fprintf(stderr, "Failed to expand history buffer during load\n");
        break;
      }

      // Initialize new entries
      for (int i = history_capacity; i < new_capacity; i++) {
        new_entries[i].command = NULL;
      }

      history_entries = new_entries;
      history_capacity = new_capacity;
    }

    // Store the command
    history_entries[history_size].command = _strdup(command);
    history_entries[history_size].timestamp = ts;

    history_size++;
  }

  history_position = history_size % PERSISTENT_HISTORY_SIZE;

  fclose(file);
}

/**
 * Save frequencies to file
 */
void save_frequencies_to_file(void) {
  if (!frequency_file_path[0] || !command_frequencies || frequency_count == 0) {
    return;
  }

  FILE *file = fopen(frequency_file_path, "w");
  if (!file) {
    fprintf(stderr, "Failed to open frequency file for writing: %s\n",
            frequency_file_path);
    return;
  }

  // Write frequency entries
  for (int i = 0; i < frequency_count; i++) {
    if (command_frequencies[i].command) {
      fprintf(file, "%d|%s\n", command_frequencies[i].count,
              command_frequencies[i].command);
    }
  }

  fclose(file);
}

/**
 * Load frequencies from file
 */
void load_frequencies_from_file(void) {
  if (!frequency_file_path[0] || !command_frequencies) {
    return;
  }

  FILE *file = fopen(frequency_file_path, "r");
  if (!file) {
    // It's OK if the file doesn't exist yet
    return;
  }

  // Clear existing frequencies
  for (int i = 0; i < frequency_count; i++) {
    if (command_frequencies[i].command) {
      free(command_frequencies[i].command);
      command_frequencies[i].command = NULL;
    }
    command_frequencies[i].count = 0;
  }

  frequency_count = 0;

  // Read frequency entries
  char line[1024];
  while (fgets(line, sizeof(line), file)) {
    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
    }

    // Find separator between count and command
    char *separator = strchr(line, '|');
    if (!separator) {
      continue; // Invalid line format
    }

    // Split line into count and command
    *separator = '\0';
    char *count_str = line;
    char *command = separator + 1;

    // Parse count
    int count = atoi(count_str);
    if (count <= 0) {
      continue; // Invalid count
    }

    // Add to frequencies
    if (frequency_count >= frequency_capacity) {
      // Expand frequency buffer if needed
      int new_capacity = frequency_capacity * 2;
      CommandFrequency *new_frequencies = (CommandFrequency *)realloc(
          command_frequencies, new_capacity * sizeof(CommandFrequency));

      if (!new_frequencies) {
        fprintf(stderr, "Failed to expand frequency buffer during load\n");
        break;
      }

      // Initialize new entries
      for (int i = frequency_capacity; i < new_capacity; i++) {
        new_frequencies[i].command = NULL;
        new_frequencies[i].count = 0;
      }

      command_frequencies = new_frequencies;
      frequency_capacity = new_capacity;
    }

    // Store the command and count
    command_frequencies[frequency_count].command = _strdup(command);
    command_frequencies[frequency_count].count = count;

    frequency_count++;
  }

  fclose(file);
}

/**
 * Get the history entry at specified index
 */
PersistentHistoryEntry *get_history_entry(int index) {
  if (!history_entries || index < 0 || index >= history_size) {
    return NULL;
  }

  int actual_index =
      (history_position - history_size + index + PERSISTENT_HISTORY_SIZE) %
      PERSISTENT_HISTORY_SIZE;
  if (actual_index < 0)
    actual_index += PERSISTENT_HISTORY_SIZE;
  if (actual_index >= history_capacity)
    actual_index = 0; // Wrap around if needed

  return &history_entries[actual_index];
}

/**
 * Put this in persistent_history.c to ensure the function signature matches the
 * usage This can replace the existing function if there's a mismatch
 */
/**
 * Get suggestions based on command frequency with external commands support
 *
 * @param prefix The prefix to match against
 * @param count Pointer to store the number of suggestions
 * @return Array of suggestions (caller must free)
 */
char **get_frequency_suggestions(const char *prefix, int *count) {
  *count = 0;
  if (!prefix || prefix[0] == '\0')
    return NULL;

  // Get all persistent history entries
  int history_count = get_history_count();
  if (history_count == 0)
    return NULL;

  // Frequency counter for commands
  typedef struct {
    char *command;
    int frequency;
  } CommandFrequency;

  // Count frequencies
  CommandFrequency *frequencies = NULL;
  int freq_count = 0;
  int freq_capacity = 10;

  frequencies =
      (CommandFrequency *)malloc(freq_capacity * sizeof(CommandFrequency));
  if (!frequencies)
    return NULL;

  // Process history entries
  for (int i = 0; i < history_count; i++) {
    PersistentHistoryEntry *entry = get_history_entry(i);
    if (entry && entry->command) {
      // Extract first word (the command)
      char cmd[256] = "";
      int cmd_len = 0;
      int j = 0;

      // Skip leading whitespace
      while (entry->command[j] && isspace(entry->command[j]))
        j++;

      // Copy command name until space or end
      while (entry->command[j] && !isspace(entry->command[j]) &&
             cmd_len < sizeof(cmd) - 1) {
        cmd[cmd_len++] = entry->command[j++];
      }
      cmd[cmd_len] = '\0';

      // Skip if empty
      if (cmd_len == 0)
        continue;

      // Check if it starts with our prefix
      if (_strnicmp(cmd, prefix, strlen(prefix)) != 0)
        continue;

      // Check if we already have this command
      int found = 0;
      for (int k = 0; k < freq_count; k++) {
        if (strcasecmp(frequencies[k].command, cmd) == 0) {
          frequencies[k].frequency++;
          found = 1;
          break;
        }
      }

      // If not found, add it
      if (!found) {
        // Resize if needed
        if (freq_count >= freq_capacity) {
          freq_capacity *= 2;
          CommandFrequency *new_freqs = (CommandFrequency *)realloc(
              frequencies, freq_capacity * sizeof(CommandFrequency));
          if (!new_freqs) {
            // Free existing array
            for (int k = 0; k < freq_count; k++) {
              free(frequencies[k].command);
            }
            free(frequencies);
            return NULL;
          }
          frequencies = new_freqs;
        }

        // Add new entry
        frequencies[freq_count].command = _strdup(cmd);
        frequencies[freq_count].frequency = 1;
        freq_count++;
      }
    }
  }

  // If no matches from history, try external commands
  if (freq_count == 0 && is_external_command(prefix)) {
    // Try external commands
    int ext_count = 0;
    char **ext_matches = get_external_command_matches(prefix, &ext_count);

    if (ext_matches && ext_count > 0) {
      // Return directly
      *count = ext_count;
      return ext_matches;
    }

    // Free matches if found
    if (ext_matches) {
      free(ext_matches);
    }
  }

  // If we found history matches, sort them by frequency
  if (freq_count > 0) {
    // Sort by frequency (descending)
    for (int i = 0; i < freq_count - 1; i++) {
      for (int j = 0; j < freq_count - i - 1; j++) {
        if (frequencies[j].frequency < frequencies[j + 1].frequency) {
          // Swap
          CommandFrequency temp = frequencies[j];
          frequencies[j] = frequencies[j + 1];
          frequencies[j + 1] = temp;
        }
      }
    }

    // Create result array
    char **suggestions = (char **)malloc(freq_count * sizeof(char *));
    if (!suggestions) {
      for (int i = 0; i < freq_count; i++) {
        free(frequencies[i].command);
      }
      free(frequencies);
      return NULL;
    }

    // Fill result array
    for (int i = 0; i < freq_count; i++) {
      suggestions[i] = _strdup(frequencies[i].command);
    }

    // Clean up
    for (int i = 0; i < freq_count; i++) {
      free(frequencies[i].command);
    }
    free(frequencies);

    *count = freq_count;
    return suggestions;
  }

  // Clean up if no matches
  free(frequencies);
  return NULL;
}
/**
 * Helper function for case-insensitive substring search
 * Add this function to persistent_history.c
 */
char *_stristr(const char *haystack, const char *needle) {
  if (!haystack || !needle)
    return NULL;

  char *haystack_lower = _strdup(haystack);
  char *needle_lower = _strdup(needle);

  // Convert both strings to lowercase
  for (int i = 0; haystack_lower[i]; i++) {
    haystack_lower[i] = tolower(haystack_lower[i]);
  }

  for (int i = 0; needle_lower[i]; i++) {
    needle_lower[i] = tolower(needle_lower[i]);
  }

  // Find the substring
  char *result = strstr(haystack_lower, needle_lower);

  // Calculate the position difference
  char *original_result = NULL;
  if (result) {
    int pos = result - haystack_lower;
    original_result = (char *)(haystack + pos);
  }

  // Free the temp strings
  free(haystack_lower);
  free(needle_lower);

  return original_result;
}

/**
 * Get the total number of history entries
 */
int get_history_count(void) { return history_size; }
