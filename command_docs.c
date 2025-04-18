/**
 * command_docs.c
 * Implementation of command documentation management
 */

#include "command_docs.h"
#include "external_commands.h"
#include <ShlObj.h> // For SHGetFolderPath
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

// Define hashmap size for storing command docs
#define DOC_HASH_TABLE_SIZE 257

// Structure for hash table entry
typedef struct DocEntry {
  CommandDoc doc;
  struct DocEntry *next;
} DocEntry;

// Hash table for command documentation
static DocEntry *doc_table[DOC_HASH_TABLE_SIZE] = {NULL};
static int docs_initialized = 0;

// File paths for documentation
static char docs_dir[MAX_PATH] = "";

/**
 * Simple hash function for strings
 */
static unsigned int hash_doc_string(const char *str) {
  unsigned int hash = 0;
  while (*str) {
    hash = (hash * 31) + (*str++);
  }
  return hash % DOC_HASH_TABLE_SIZE;
}

/**
 * Initialize the command documentation system
 */
void init_command_docs(void) {
  if (docs_initialized)
    return;

  // Create docs directory if it doesn't exist
  char app_data[MAX_PATH];
  if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, app_data))) {
    snprintf(docs_dir, MAX_PATH, "%s\\ShellDocs", app_data);
    _mkdir(docs_dir); // Create if it doesn't exist
  }

  // Add documentation for builtin commands
  // This would be expanded with actual documentation
  // For now, just a simple example
  char *cd_params[] = {"-help", "directory"};
  char *cd_param_descs[] = {"Display help for this command",
                            "Directory to change to"};
  add_command_doc(
      "cd", "Change the current directory",
      "Changes the current working directory to the specified path.\n"
      "If no directory is specified, displays the current directory.",
      cd_params, cd_param_descs, 2, 1);

  char *ls_params[] = {"-l", "-a", "directory"};
  char *ls_param_descs[] = {"Use long listing format", "Show hidden files",
                            "Directory to list"};
  add_command_doc("ls", "List directory contents",
                  "Lists the contents of the specified directory.\n"
                  "If no directory is specified, lists the current directory.",
                  ls_params, ls_param_descs, 3, 1);

  // Similar entries would be added for other builtin commands

  docs_initialized = 1;
}

/**
 * Free memory for a command doc entry
 */
static void free_command_doc(CommandDoc *doc) {
  if (!doc)
    return;

  free(doc->command);
  free(doc->short_desc);
  free(doc->long_desc);

  for (int i = 0; i < doc->param_count; i++) {
    free(doc->parameters[i]);
    free(doc->param_descs[i]);
  }

  free(doc->parameters);
  free(doc->param_descs);
}

/**
 * Clean up the command documentation system
 */
void cleanup_command_docs(void) {
  for (int i = 0; i < DOC_HASH_TABLE_SIZE; i++) {
    DocEntry *entry = doc_table[i];
    while (entry) {
      DocEntry *next = entry->next;
      free_command_doc(&entry->doc);
      free(entry);
      entry = next;
    }
    doc_table[i] = NULL;
  }
  docs_initialized = 0;
}

/**
 * Add documentation for a command
 */
void add_command_doc(const char *cmd, const char *short_desc,
                     const char *long_desc, char **params, char **param_descs,
                     int param_count, int is_builtin) {
  if (!cmd || !short_desc)
    return;

  // Normalize command to lowercase
  char *normalized = _strdup(cmd);
  if (!normalized)
    return;

  for (char *p = normalized; *p; p++) {
    *p = tolower(*p);
  }

  // Calculate hash
  unsigned int hash = hash_doc_string(normalized);

  // Check if command already exists
  DocEntry *entry = doc_table[hash];
  while (entry) {
    if (strcmp(entry->doc.command, normalized) == 0) {
      // Update existing entry
      free(entry->doc.short_desc);
      free(entry->doc.long_desc);

      for (int i = 0; i < entry->doc.param_count; i++) {
        free(entry->doc.parameters[i]);
        free(entry->doc.param_descs[i]);
      }

      free(entry->doc.parameters);
      free(entry->doc.param_descs);

      entry->doc.short_desc = _strdup(short_desc);
      entry->doc.long_desc = long_desc ? _strdup(long_desc) : NULL;
      entry->doc.param_count = param_count;
      entry->doc.is_builtin = is_builtin;

      if (param_count > 0) {
        entry->doc.parameters = (char **)malloc(param_count * sizeof(char *));
        entry->doc.param_descs = (char **)malloc(param_count * sizeof(char *));

        for (int i = 0; i < param_count; i++) {
          entry->doc.parameters[i] = _strdup(params[i]);
          entry->doc.param_descs[i] = _strdup(param_descs[i]);
        }
      } else {
        entry->doc.parameters = NULL;
        entry->doc.param_descs = NULL;
      }

      free(normalized);
      return;
    }
    entry = entry->next;
  }

  // Create new entry
  DocEntry *new_entry = (DocEntry *)malloc(sizeof(DocEntry));
  if (!new_entry) {
    free(normalized);
    return;
  }

  new_entry->doc.command = normalized;
  new_entry->doc.short_desc = _strdup(short_desc);
  new_entry->doc.long_desc = long_desc ? _strdup(long_desc) : NULL;
  new_entry->doc.param_count = param_count;
  new_entry->doc.is_builtin = is_builtin;

  if (param_count > 0) {
    new_entry->doc.parameters = (char **)malloc(param_count * sizeof(char *));
    new_entry->doc.param_descs = (char **)malloc(param_count * sizeof(char *));

    for (int i = 0; i < param_count; i++) {
      new_entry->doc.parameters[i] = _strdup(params[i]);
      new_entry->doc.param_descs[i] = _strdup(param_descs[i]);
    }
  } else {
    new_entry->doc.parameters = NULL;
    new_entry->doc.param_descs = NULL;
  }

  new_entry->next = doc_table[hash];
  doc_table[hash] = new_entry;
}

/**
 * Get documentation for a command
 */
const CommandDoc *get_command_doc(const char *cmd) {
  if (!docs_initialized || !cmd || cmd[0] == '\0')
    return NULL;

  // Normalize command to lowercase
  char *normalized = _strdup(cmd);
  if (!normalized)
    return NULL;

  for (char *p = normalized; *p; p++) {
    *p = tolower(*p);
  }

  // Calculate hash
  unsigned int hash = hash_doc_string(normalized);

  // Look up command documentation
  DocEntry *entry = doc_table[hash];
  while (entry) {
    if (strcmp(entry->doc.command, normalized) == 0) {
      free(normalized);
      return &entry->doc;
    }
    entry = entry->next;
  }

  // If documentation not found, try to load it
  if (load_command_doc(normalized)) {
    // Now try again
    entry = doc_table[hash];
    while (entry) {
      if (strcmp(entry->doc.command, normalized) == 0) {
        free(normalized);
        return &entry->doc;
      }
      entry = entry->next;
    }
  }

  free(normalized);
  return NULL;
}

/**
 * Extract documentation from command's help output
 */
static int extract_docs_from_help(const char *cmd, char **short_desc,
                                  char **long_desc, char ***params,
                                  char ***param_descs, int *param_count) {
  char command_line[MAX_PATH * 2];
  char temp_file[MAX_PATH];
  FILE *output = NULL;
  int result = 0;

  // Generate a temporary file name
  GetTempPath(MAX_PATH, temp_file);
  strcat(temp_file, "\\cmd_help.txt");

  // Try different help flags
  const char *help_flags[] = {"--help", "-h", "-help", "/?", "/help", "/h"};
  const int num_flags = sizeof(help_flags) / sizeof(help_flags[0]);

  for (int i = 0; i < num_flags; i++) {
    // Construct command line to capture help output
    snprintf(command_line, sizeof(command_line), "%s %s > \"%s\" 2>&1", cmd,
             help_flags[i], temp_file);

    // Execute command
    int ret = system(command_line);
    if (ret == 0) {
      // Command succeeded, open output file
      output = fopen(temp_file, "r");
      if (output) {
        // Read first line for short description
        char line[1024] = {0};
        if (fgets(line, sizeof(line), output)) {
          // Remove newline
          line[strcspn(line, "\r\n")] = 0;

          // Skip leading program name if present
          char *desc_start = line;
          if (strstr(line, cmd) == line) {
            desc_start = line + strlen(cmd);
            // Skip any separator
            while (*desc_start && (*desc_start == ':' || *desc_start == '-' ||
                                   isspace(*desc_start))) {
              desc_start++;
            }
          }

          *short_desc = _strdup(desc_start);

          // Read the rest for long description
          char buffer[4096] = {0};
          size_t bytes_read = 0;

          // Reset file position to beginning
          fseek(output, 0, SEEK_SET);

          // Read entire file
          bytes_read = fread(buffer, 1, sizeof(buffer) - 1, output);
          if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            *long_desc = _strdup(buffer);
          } else {
            *long_desc = _strdup("No detailed help available.");
          }

          // Extract parameters (simple approach - could be enhanced)
          // This is a simplistic approach to param extraction
          *param_count = 0;
          *params = NULL;
          *param_descs = NULL;

          // Count potential parameters (lines starting with - or --)
          fseek(output, 0, SEEK_SET);
          int potential_params = 0;
          while (fgets(line, sizeof(line), output)) {
            if (line[0] == '-' || (line[0] == ' ' && line[1] == '-')) {
              potential_params++;
            }
          }

          if (potential_params > 0) {
            *params = (char **)malloc(potential_params * sizeof(char *));
            *param_descs = (char **)malloc(potential_params * sizeof(char *));

            // Reset file again
            fseek(output, 0, SEEK_SET);

            // Extract parameters
            while (fgets(line, sizeof(line), output) &&
                   *param_count < potential_params) {
              // Remove newline
              line[strcspn(line, "\r\n")] = 0;

              // Skip leading spaces
              char *param_start = line;
              while (*param_start && isspace(*param_start)) {
                param_start++;
              }

              // Check if line starts with - or --
              if (*param_start == '-') {
                char *desc_start = param_start;

                // Find end of parameter
                while (*desc_start && !isspace(*desc_start)) {
                  desc_start++;
                }

                // Null-terminate parameter
                if (*desc_start) {
                  *desc_start = '\0';
                  desc_start++;

                  // Skip spaces before description
                  while (*desc_start && isspace(*desc_start)) {
                    desc_start++;
                  }
                }

                // Add parameter and description
                (*params)[*param_count] = _strdup(param_start);
                (*param_descs)[*param_count] =
                    _strdup(desc_start ? desc_start : "");
                (*param_count)++;
              }
            }
          }

          result = 1;
        }
        fclose(output);
      }

      // If we successfully extracted documentation, break the loop
      if (result) {
        break;
      }
    }
  }

  // Clean up temp file
  remove(temp_file);

  return result;
}

/**
 * Try to find and load man page for a command
 * This is a Windows-specific approach trying multiple documentation sources
 */
static int load_man_page(const char *cmd, char **short_desc, char **long_desc,
                         char ***params, char ***param_descs,
                         int *param_count) {
  // 1. Check for man pages in common locations
  const char *man_locations[] = {
      "C:\\msys64\\usr\\share\\man\\man1\\%s.1",
      "C:\\Program Files\\Git\\usr\\share\\man\\man1\\%s.1",
      "C:\\cygwin64\\usr\\share\\man\\man1\\%s.1"};
  const int num_locations = sizeof(man_locations) / sizeof(man_locations[0]);

  for (int i = 0; i < num_locations; i++) {
    char man_path[MAX_PATH];
    snprintf(man_path, sizeof(man_path), man_locations[i], cmd);

    FILE *man_file = fopen(man_path, "r");
    if (man_file) {
      // A very simple man page parser
      char line[1024];
      int in_name_section = 0;
      int in_synopsis = 0;

      // Initialize out params to sensible defaults
      *short_desc = _strdup("No short description available");
      *long_desc =
          _strdup("Documentation found in man page format but not parsed.");
      *param_count = 0;
      *params = NULL;
      *param_descs = NULL;

      // Basic parsing - just to extract name and synopsis
      while (fgets(line, sizeof(line), man_file)) {
        // Remove newline
        line[strcspn(line, "\r\n")] = 0;

        // Look for name section
        if (strstr(line, "NAME")) {
          in_name_section = 1;
          in_synopsis = 0;
          continue;
        } else if (strstr(line, "SYNOPSIS")) {
          in_name_section = 0;
          in_synopsis = 1;
          continue;
        } else if (line[0] == '.') {
          // Section change
          in_name_section = 0;
          in_synopsis = 0;
          continue;
        }

        // Extract short description from NAME section
        if (in_name_section && strlen(line) > 0 && line[0] != '.') {
          // NAME sections often have format: "cmd - description"
          char *desc_start = strstr(line, " - ");
          if (desc_start) {
            desc_start += 3; // Skip " - "
            free(*short_desc);
            *short_desc = _strdup(desc_start);
          }
        }

        // Extract synopsis and parameters
        if (in_synopsis && strlen(line) > 0 && line[0] != '.') {
          // This would be expanded in a real parser
          // Just using as placeholder for now
        }
      }

      fclose(man_file);
      return 1;
    }
  }

  return 0;
}

/**
 * Automatically discover and load documentation for a command
 */
int load_command_doc(const char *cmd) {
  // Don't try to load for empty command
  if (!cmd || cmd[0] == '\0')
    return 0;

  // Check if we can find the command in PATH
  if (!is_external_command(cmd)) {
    return 0;
  }

  // Variables to hold extracted documentation
  char *short_desc = NULL;
  char *long_desc = NULL;
  char **params = NULL;
  char **param_descs = NULL;
  int param_count = 0;

  int found_docs = 0;

  // First try: Check if we have a cached documentation file
  char doc_path[MAX_PATH];
  snprintf(doc_path, sizeof(doc_path), "%s\\%s.txt", docs_dir, cmd);

  FILE *doc_file = fopen(doc_path, "r");
  if (doc_file) {
    // Parse the cached documentation file
    char line[1024];

    // Read short description
    if (fgets(line, sizeof(line), doc_file)) {
      line[strcspn(line, "\r\n")] = 0;
      short_desc = _strdup(line);

      // Read long description length
      if (fgets(line, sizeof(line), doc_file)) {
        int long_desc_len = atoi(line);
        if (long_desc_len > 0) {
          long_desc = (char *)malloc(long_desc_len + 1);
          if (long_desc) {
            size_t bytes_read = fread(long_desc, 1, long_desc_len, doc_file);
            long_desc[bytes_read] = '\0';
          }
        }

        // Read parameter count
        if (fgets(line, sizeof(line), doc_file)) {
          param_count = atoi(line);
          if (param_count > 0) {
            params = (char **)malloc(param_count * sizeof(char *));
            param_descs = (char **)malloc(param_count * sizeof(char *));

            for (int i = 0; i < param_count && params && param_descs; i++) {
              // Read parameter
              if (fgets(line, sizeof(line), doc_file)) {
                line[strcspn(line, "\r\n")] = 0;
                params[i] = _strdup(line);

                // Read parameter description
                if (fgets(line, sizeof(line), doc_file)) {
                  line[strcspn(line, "\r\n")] = 0;
                  param_descs[i] = _strdup(line);
                } else {
                  param_descs[i] = _strdup("");
                }
              } else {
                params[i] = _strdup("");
                param_descs[i] = _strdup("");
              }
            }
          }
        }
      }
    }

    fclose(doc_file);
    found_docs = 1;
  }

  // Second try: Look for man page
  if (!found_docs) {
    found_docs = load_man_page(cmd, &short_desc, &long_desc, &params,
                               &param_descs, &param_count);
  }

  // Third try: Extract from command's help output
  if (!found_docs) {
    found_docs = extract_docs_from_help(cmd, &short_desc, &long_desc, &params,
                                        &param_descs, &param_count);

    // If found, cache the documentation for future use
    if (found_docs) {
      FILE *cache_file = fopen(doc_path, "w");
      if (cache_file) {
        // Write short description
        fprintf(cache_file, "%s\n", short_desc);

        // Write long description
        fprintf(cache_file, "%d\n", (int)strlen(long_desc));
        fwrite(long_desc, 1, strlen(long_desc), cache_file);
        fprintf(cache_file, "\n");

        // Write parameter count
        fprintf(cache_file, "%d\n", param_count);

        // Write parameters and descriptions
        for (int i = 0; i < param_count; i++) {
          fprintf(cache_file, "%s\n", params[i]);
          fprintf(cache_file, "%s\n", param_descs[i]);
        }

        fclose(cache_file);
      }
    }
  }

  // If we found documentation, add it to the command docs
  if (found_docs) {
    add_command_doc(cmd, short_desc, long_desc, params, param_descs,
                    param_count, 0);
  } else {
    // No documentation found, add minimal entry
    short_desc = _strdup("External command");
    long_desc = _strdup("No documentation available for this command.");
    add_command_doc(cmd, short_desc, long_desc, NULL, NULL, 0, 0);
  }

  // Clean up
  free(short_desc);
  free(long_desc);

  if (params && param_descs) {
    for (int i = 0; i < param_count; i++) {
      free(params[i]);
      free(param_descs[i]);
    }
    free(params);
    free(param_descs);
  }

  return found_docs;
}

/**
 * Get documentation for a specific parameter of a command
 */
const char *get_param_doc(const char *cmd, const char *param) {
  const CommandDoc *doc = get_command_doc(cmd);
  if (!doc)
    return NULL;

  for (int i = 0; i < doc->param_count; i++) {
    if (strcmp(doc->parameters[i], param) == 0) {
      return doc->param_descs[i];
    }
  }

  return NULL;
}

/**
 * Get list of commands with documentation matching a search term
 */
char **search_command_docs(const char *search_term, int *count) {
  *count = 0;
  if (!docs_initialized || !search_term)
    return NULL;

  // First count matching commands
  int match_count = 0;
  for (int i = 0; i < DOC_HASH_TABLE_SIZE; i++) {
    DocEntry *entry = doc_table[i];
    while (entry) {
      if (strstr(entry->doc.command, search_term) ||
          (entry->doc.short_desc &&
           strstr(entry->doc.short_desc, search_term))) {
        match_count++;
      }
      entry = entry->next;
    }
  }

  if (match_count == 0) {
    return NULL;
  }

  // Allocate array for matches
  char **matches = (char **)malloc(match_count * sizeof(char *));
  if (!matches) {
    return NULL;
  }

  // Fill array with matching commands
  int index = 0;
  for (int i = 0; i < DOC_HASH_TABLE_SIZE; i++) {
    DocEntry *entry = doc_table[i];
    while (entry) {
      if (strstr(entry->doc.command, search_term) ||
          (entry->doc.short_desc &&
           strstr(entry->doc.short_desc, search_term))) {
        matches[index++] = _strdup(entry->doc.command);
      }
      entry = entry->next;
    }
  }

  *count = match_count;
  return matches;
}

/**
 * Get list of parameters for a command
 */
const char **get_command_params(const char *cmd, int *count) {
  const CommandDoc *doc = get_command_doc(cmd);
  if (!doc) {
    *count = 0;
    return NULL;
  }

  *count = doc->param_count;
  return (const char **)doc->parameters;
}

/**
 * Display command help using built-in documentation
 */
/**
 * Display command help using built-in documentation
 */
int display_command_help(const char *cmd) {
  const CommandDoc *doc = get_command_doc(cmd);
  if (!doc) {
    return 0;
  }

  // Get handle to console for colored output
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  WORD originalAttrs = csbi.wAttributes;

  // Display command name and short description
  SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
  printf("\nCOMMAND: %s\n", doc->command);
  SetConsoleTextAttribute(hConsole, originalAttrs);

  printf("%-12s%s\n\n", "Description:", doc->short_desc);

  // Display long description if available
  if (doc->long_desc && strlen(doc->long_desc) > 0) {
    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    printf("DETAILS:\n");
    SetConsoleTextAttribute(hConsole, originalAttrs);

    printf("%s\n\n", doc->long_desc);
  }

  // Display parameters if available
  if (doc->param_count > 0) {
    SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    printf("PARAMETERS:\n");
    SetConsoleTextAttribute(hConsole, originalAttrs);

    for (int i = 0; i < doc->param_count; i++) {
      // Use FOREGROUND_BLUE | FOREGROUND_GREEN instead of FOREGROUND_CYAN
      SetConsoleTextAttribute(hConsole, FOREGROUND_BLUE | FOREGROUND_GREEN |
                                            FOREGROUND_INTENSITY);
      printf("  %-15s", doc->parameters[i]);
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s\n", doc->param_descs[i]);
    }
    printf("\n");
  }

  return 1;
}
