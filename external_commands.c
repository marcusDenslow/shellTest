/**
 * external_commands.c
 * Implementation of external command detection and validation
 */

#include "external_commands.h"
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Define hashmap size for storing commands (prime number for better
// distribution)
#define HASH_TABLE_SIZE 503

// Structure for hash table entry
typedef struct CommandEntry {
  char *name;
  struct CommandEntry *next;
} CommandEntry;

// Hash table for fast command lookup
static CommandEntry *command_table[HASH_TABLE_SIZE] = {NULL};
static int initialized = 0;

// Common executable extensions
static const char *EXECUTABLE_EXTENSIONS[] = {".exe", ".com", ".bat", ".cmd",
                                              ""};
static const int NUM_EXTENSIONS =
    5; // Number of elements in EXECUTABLE_EXTENSIONS

/**
 * Simple hash function for strings
 */
static unsigned int hash_string(const char *str) {
  unsigned int hash = 0;
  while (*str) {
    hash = (hash * 31) + (*str++);
  }
  return hash % HASH_TABLE_SIZE;
}

/**
 * Add a command to the hash table
 */
void add_external_command(const char *cmd) {
  if (!cmd || cmd[0] == '\0')
    return;

  // Normalize to lowercase for case-insensitive comparison
  char *normalized = _strdup(cmd);
  if (!normalized)
    return;

  for (char *p = normalized; *p; p++) {
    *p = tolower(*p);
  }

  // Calculate hash
  unsigned int hash = hash_string(normalized);

  // Check if command already exists
  CommandEntry *entry = command_table[hash];
  while (entry) {
    if (strcmp(entry->name, normalized) == 0) {
      // Command already in table
      free(normalized);
      return;
    }
    entry = entry->next;
  }

  // Create new entry
  CommandEntry *new_entry = (CommandEntry *)malloc(sizeof(CommandEntry));
  if (!new_entry) {
    free(normalized);
    return;
  }

  new_entry->name = normalized;
  new_entry->next = command_table[hash];
  command_table[hash] = new_entry;
}

/**
 * Check if a file exists and is executable
 */
static int is_executable(const char *path) {
  DWORD attributes = GetFileAttributes(path);
  if (attributes == INVALID_FILE_ATTRIBUTES) {
    return 0;
  }

  // Make sure it's a file, not a directory
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) {
    return 0;
  }

  return 1;
}

/**
 * Scan a directory for executable files
 */
static void scan_directory(const char *dir) {
  char search_path[MAX_PATH];
  WIN32_FIND_DATA find_data;
  HANDLE hFind;

  // Create search pattern for all files in directory
  snprintf(search_path, sizeof(search_path), "%s\\*", dir);

  // Start search
  hFind = FindFirstFile(search_path, &find_data);
  if (hFind == INVALID_HANDLE_VALUE) {
    return;
  }

  do {
    // Skip "." and ".."
    if (strcmp(find_data.cFileName, ".") == 0 ||
        strcmp(find_data.cFileName, "..") == 0) {
      continue;
    }

    // Skip directories
    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }

    // Check file extension
    int is_exec = 0;
    for (int i = 0; i < NUM_EXTENSIONS; i++) {
      const char *ext = EXECUTABLE_EXTENSIONS[i];
      int ext_len = strlen(ext);
      int name_len = strlen(find_data.cFileName);

      if (ext_len == 0) {
        // Handle no extension case - check if file is executable
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s\\%s", dir,
                 find_data.cFileName);
        if (is_executable(full_path)) {
          is_exec = 1;
          break;
        }
      } else if (name_len > ext_len) {
        // Check if filename ends with this extension
        if (_stricmp(find_data.cFileName + name_len - ext_len, ext) == 0) {
          is_exec = 1;
          break;
        }
      }
    }

    if (is_exec) {
      // Remove extension from command name
      char command_name[MAX_PATH];
      strcpy(command_name, find_data.cFileName);

      // Find last dot to remove extension
      char *dot = strrchr(command_name, '.');
      if (dot) {
        *dot = '\0';
      }

      // Add to hash table
      add_external_command(command_name);
    }

  } while (FindNextFile(hFind, &find_data));

  FindClose(hFind);
}

/**
 * Initialize the external commands by scanning PATH
 */
void init_external_commands(void) {
  if (initialized)
    return;

  // Get the PATH environment variable
  char *path_env = getenv("PATH");
  if (!path_env)
    return;

  // Make a copy since strtok modifies the string
  char *path_copy = _strdup(path_env);
  if (!path_copy)
    return;

  // Parse the PATH into directories
  char *context = NULL;
  char *dir = strtok_s(path_copy, ";", &context);

  while (dir) {
    scan_directory(dir);
    dir = strtok_s(NULL, ";", &context);
  }

  free(path_copy);
  initialized = 1;
}

/**
 * Free all allocated memory for commands
 */
void cleanup_external_commands(void) {
  for (int i = 0; i < HASH_TABLE_SIZE; i++) {
    CommandEntry *entry = command_table[i];
    while (entry) {
      CommandEntry *next = entry->next;
      free(entry->name);
      free(entry);
      entry = next;
    }
    command_table[i] = NULL;
  }
  initialized = 0;
}

/**
 * Check if a command exists as an external program
 */
int is_external_command(const char *cmd) {
  if (!initialized || !cmd || cmd[0] == '\0')
    return 0;

  // Normalize to lowercase
  char *normalized = _strdup(cmd);
  if (!normalized)
    return 0;

  for (char *p = normalized; *p; p++) {
    *p = tolower(*p);
  }

  // Calculate hash
  unsigned int hash = hash_string(normalized);

  // Check if command exists
  int found = 0;
  CommandEntry *entry = command_table[hash];
  while (entry) {
    if (strcmp(entry->name, normalized) == 0) {
      found = 1;
      break;
    }
    entry = entry->next;
  }

  free(normalized);
  return found;
}

/**
 * Get a list of external commands matching a prefix
 */
char **get_external_command_matches(const char *prefix, int *count) {
  *count = 0;
  if (!initialized || !prefix)
    return NULL;

  // Normalize prefix to lowercase
  char *normalized_prefix = _strdup(prefix);
  if (!normalized_prefix)
    return NULL;

  for (char *p = normalized_prefix; *p; p++) {
    *p = tolower(*p);
  }

  // First count matching commands
  int match_count = 0;
  for (int i = 0; i < HASH_TABLE_SIZE; i++) {
    CommandEntry *entry = command_table[i];
    while (entry) {
      if (strncmp(entry->name, normalized_prefix, strlen(normalized_prefix)) ==
          0) {
        match_count++;
      }
      entry = entry->next;
    }
  }

  if (match_count == 0) {
    free(normalized_prefix);
    return NULL;
  }

  // Allocate array for matches
  char **matches = (char **)malloc(match_count * sizeof(char *));
  if (!matches) {
    free(normalized_prefix);
    return NULL;
  }

  // Fill array with matching commands
  int index = 0;
  for (int i = 0; i < HASH_TABLE_SIZE; i++) {
    CommandEntry *entry = command_table[i];
    while (entry) {
      if (strncmp(entry->name, normalized_prefix, strlen(normalized_prefix)) ==
          0) {
        matches[index++] = _strdup(entry->name);
      }
      entry = entry->next;
    }
  }

  free(normalized_prefix);
  *count = match_count;
  return matches;
}

/**
 * Rescan the PATH to refresh the commands list
 */
void refresh_external_commands(void) {
  // Clean up the existing commands
  cleanup_external_commands();

  // Reinitialize
  init_external_commands();
}
