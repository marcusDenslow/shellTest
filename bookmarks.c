/**
 * bookmarks.c
 * Implementation of directory bookmarks functionality
 */

#include "bookmarks.h"

// Global variables for bookmark storage
BookmarkEntry *bookmarks = NULL;
int bookmark_count = 0;
int bookmark_capacity = 0;

// Path to the bookmarks file
char bookmarks_file_path[MAX_PATH];

/**
 * Initialize the bookmark system
 */
void init_bookmarks(void) {
  // Set initial capacity
  bookmark_capacity = 10;
  bookmarks =
      (BookmarkEntry *)malloc(bookmark_capacity * sizeof(BookmarkEntry));

  if (!bookmarks) {
    fprintf(stderr, "lsh: allocation error in init_bookmarks\n");
    return;
  }

  // Determine bookmarks file location - in user's home directory
  char *home_dir = getenv("USERPROFILE");
  if (home_dir) {
    snprintf(bookmarks_file_path, MAX_PATH, "%s\\.lsh_bookmarks", home_dir);
  } else {
    // Fallback to current directory if USERPROFILE not available
    strcpy(bookmarks_file_path, ".lsh_bookmarks");
  }

  // Load bookmarks from file
  load_bookmarks();

  // Print debug info about loaded bookmarks
  printf("Loaded %d bookmarks\n", bookmark_count);
}

/**
 * Clean up the bookmark system
 */
void cleanup_bookmarks(void) {
  if (!bookmarks)
    return;

  // Free all bookmark entries
  for (int i = 0; i < bookmark_count; i++) {
    free(bookmarks[i].name);
    free(bookmarks[i].path);
  }

  free(bookmarks);
  bookmarks = NULL;
  bookmark_count = 0;
  bookmark_capacity = 0;
}

/**
 * Load bookmarks from file
 */
int load_bookmarks(void) {
  FILE *file = fopen(bookmarks_file_path, "r");
  if (!file) {
    // File doesn't exist yet - not an error
    return 1;
  }

  // Clear existing bookmarks
  for (int i = 0; i < bookmark_count; i++) {
    free(bookmarks[i].name);
    free(bookmarks[i].path);
  }
  bookmark_count = 0;

  char line[1024];
  int line_number = 0;

  while (fgets(line, sizeof(line), file)) {
    line_number++;

    // Remove newline
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }

    // Skip empty lines and comments
    if (len == 0 || line[0] == '#')
      continue;

    // Find equal sign separator
    char *separator = strchr(line, '=');
    if (!separator) {
      fprintf(stderr, "lsh: warning: invalid bookmark format in line %d\n",
              line_number);
      continue;
    }

    // Split into name and path
    *separator = '\0';
    char *name = line;
    char *path = separator + 1;

    // Trim whitespace from name
    char *end = name + strlen(name) - 1;
    while (end > name && isspace(*end))
      end--;
    *(end + 1) = '\0';

    // Add the bookmark
    add_bookmark(name, path);
  }

  fclose(file);
  return 1;
}

/**
 * Save bookmarks to file
 */
int save_bookmarks(void) {
  FILE *file = fopen(bookmarks_file_path, "w");
  if (!file) {
    fprintf(stderr, "lsh: error: could not save bookmarks to %s\n",
            bookmarks_file_path);
    return 0;
  }

  // Write header comment
  fprintf(file, "# LSH bookmarks file\n");
  fprintf(file, "# Format: bookmark_name=directory_path\n\n");

  // Write each bookmark
  for (int i = 0; i < bookmark_count; i++) {
    fprintf(file, "%s=%s\n", bookmarks[i].name, bookmarks[i].path);
  }

  fclose(file);
  return 1;
}

/**
 * Add a new bookmark
 */
int add_bookmark(const char *name, const char *path) {
  if (!name || !path)
    return 0;

  // Check if we need to resize the array
  if (bookmark_count >= bookmark_capacity) {
    bookmark_capacity *= 2;
    BookmarkEntry *new_bookmarks = (BookmarkEntry *)realloc(
        bookmarks, bookmark_capacity * sizeof(BookmarkEntry));
    if (!new_bookmarks) {
      fprintf(stderr, "lsh: allocation error in add_bookmark\n");
      return 0;
    }
    bookmarks = new_bookmarks;
  }

  // Check if the bookmark already exists
  for (int i = 0; i < bookmark_count; i++) {
    if (strcmp(bookmarks[i].name, name) == 0) {
      // Update existing bookmark
      free(bookmarks[i].path);
      bookmarks[i].path = _strdup(path);
      return 1;
    }
  }

  // Add new bookmark
  bookmarks[bookmark_count].name = _strdup(name);
  bookmarks[bookmark_count].path = _strdup(path);
  bookmark_count++;

  return 1;
}

/**
 * Remove a bookmark
 */
int remove_bookmark(const char *name) {
  if (!name)
    return 0;

  for (int i = 0; i < bookmark_count; i++) {
    if (strcmp(bookmarks[i].name, name) == 0) {
      // Free memory
      free(bookmarks[i].name);
      free(bookmarks[i].path);

      // Shift remaining bookmarks
      for (int j = i; j < bookmark_count - 1; j++) {
        bookmarks[j] = bookmarks[j + 1];
      }

      bookmark_count--;
      return 1;
    }
  }

  return 0; // Bookmark not found
}

/**
 * Find a bookmark by name
 */
BookmarkEntry *find_bookmark(const char *name) {
  if (!name)
    return NULL;

  for (int i = 0; i < bookmark_count; i++) {
    if (strcmp(bookmarks[i].name, name) == 0) {
      return &bookmarks[i];
    }
  }

  return NULL;
}

/**
 * Command handler for the "bookmark" command
 * Usage: bookmark [name] - Bookmark the current directory
 */
int lsh_bookmark(char **args) {
  char cwd[MAX_PATH];

  // If no arguments, show usage
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected bookmark name\n");
    fprintf(stderr, "Usage: bookmark <name>\n");
    fprintf(stderr, "  e.g.: bookmark projects\n");
    return 1;
  }

  // Get current directory
  if (_getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("lsh: getcwd");
    return 1;
  }

  // Add the bookmark with the specified name
  if (add_bookmark(args[1], cwd)) {
    printf("Bookmarked current directory as '%s'\n", args[1]);
    save_bookmarks();
  }

  return 1;
}

/**
 * Command handler for the "bookmarks" command
 * Usage: bookmarks [edit] - List all bookmarks or edit them
 */
int lsh_bookmarks(char **args) {
  // "bookmarks edit" command - open the bookmarks file in a text editor
  if (args[1] != NULL && strcmp(args[1], "edit") == 0) {
    // Try to determine if neovim or vim is available
    FILE *test_nvim = _popen("nvim --version 2>nul", "r");
    if (test_nvim != NULL) {
      // nvim is available
      _pclose(test_nvim);
      char edit_cmd[1024];
      snprintf(edit_cmd, sizeof(edit_cmd), "nvim %s", bookmarks_file_path);
      system(edit_cmd);
    } else {
      // Try vim next
      FILE *test_vim = _popen("vim --version 2>nul", "r");
      if (test_vim != NULL) {
        // vim is available
        _pclose(test_vim);
        char edit_cmd[1024];
        snprintf(edit_cmd, sizeof(edit_cmd), "vim %s", bookmarks_file_path);
        system(edit_cmd);
      } else {
        // Fall back to notepad
        char edit_cmd[1024];
        snprintf(edit_cmd, sizeof(edit_cmd), "notepad %s", bookmarks_file_path);
        system(edit_cmd);
      }
    }

    // Reload bookmarks after editing
    load_bookmarks();
    return 1;
  }

  // No arguments - list all bookmarks
  if (bookmark_count == 0) {
    printf("No bookmarks defined\n");
    printf("Use 'bookmark <name>' to bookmark the current directory\n");
  } else {
    printf("\nBookmarks:\n\n");

    // Calculate maximum name length for alignment
    int max_name_length = 0;
    for (int i = 0; i < bookmark_count; i++) {
      int len = strlen(bookmarks[i].name);
      if (len > max_name_length) {
        max_name_length = len;
      }
    }

    // Get handle to console for colors
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    WORD originalAttrs = csbi.wAttributes;

    // Display bookmarks in a nice format
    for (int i = 0; i < bookmark_count; i++) {
      // Print name in green
      SetConsoleTextAttribute(hConsole,
                              FOREGROUND_GREEN | FOREGROUND_INTENSITY);
      printf("  %-*s", max_name_length + 2, bookmarks[i].name);

      // Print path in default color
      SetConsoleTextAttribute(hConsole, originalAttrs);
      printf("%s\n", bookmarks[i].path);
    }

    // Reset color
    SetConsoleTextAttribute(hConsole, originalAttrs);
    printf("\n");
  }

  return 1;
}

/**
 * Command handler for the "goto" command
 * Usage: goto <bookmark> - Change to the bookmarked directory
 */
int lsh_goto(char **args) {
  // Check for bookmark name
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected bookmark name\n");
    fprintf(stderr, "Usage: goto <bookmark>\n");
    fprintf(stderr, "  e.g.: goto projects\n");
    return 1;
  }

  // Find the bookmark
  BookmarkEntry *bookmark = find_bookmark(args[1]);
  if (bookmark) {
    // Change to the bookmarked directory
    if (_chdir(bookmark->path) != 0) {
      perror("lsh: chdir");
      printf("The directory '%s' no longer exists.\n", bookmark->path);

      // Ask if user wants to remove the invalid bookmark
      char response[10];
      printf("Would you like to remove this bookmark? (y/n): ");
      if (fgets(response, sizeof(response), stdin) &&
          (response[0] == 'y' || response[0] == 'Y')) {
        remove_bookmark(args[1]);
        save_bookmarks();
        printf("Bookmark '%s' removed.\n", args[1]);
      }
    } else {
      printf("Changed directory to '%s' (%s)\n", args[1], bookmark->path);
    }
  } else {
    printf("Bookmark '%s' not found\n", args[1]);

    // List available bookmarks if any
    if (bookmark_count > 0) {
      printf("Available bookmarks: ");
      for (int i = 0; i < bookmark_count; i++) {
        printf("%s%s", i > 0 ? ", " : "", bookmarks[i].name);
      }
      printf("\n");
    }
  }

  return 1;
}

/**
 * Command handler for the "unbookmark" command
 * Usage: unbookmark <name> - Remove a bookmark
 */
int lsh_unbookmark(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected bookmark name\n");
    fprintf(stderr, "Usage: unbookmark <name>\n");
    fprintf(stderr, "  e.g.: unbookmark projects\n");
    return 1;
  }

  if (remove_bookmark(args[1])) {
    save_bookmarks();
    printf("Bookmark '%s' removed\n", args[1]);
  } else {
    printf("Bookmark '%s' not found\n", args[1]);
  }

  return 1;
}

/**
 * Get bookmark names for tab completion
 */
char **get_bookmark_names(int *count) {
  if (bookmark_count == 0) {
    *count = 0;
    return NULL;
  }

  char **names = (char **)malloc(bookmark_count * sizeof(char *));
  if (!names) {
    *count = 0;
    return NULL;
  }

  for (int i = 0; i < bookmark_count; i++) {
    names[i] = _strdup(bookmarks[i].name);
  }

  *count = bookmark_count;
  return names;
}

/**
 * Find a matching bookmark by partial name
 */
char *find_matching_bookmark(const char *partial_name) {
  // Easy case - if we have no bookmarks
  if (bookmark_count == 0) {
    return NULL;
  }

  // Empty prefix - return first bookmark
  if (!partial_name || partial_name[0] == '\0') {
    return _strdup(bookmarks[0].name);
  }

  // Find a matching bookmark
  for (int i = 0; i < bookmark_count; i++) {
    if (_strnicmp(bookmarks[i].name, partial_name, strlen(partial_name)) == 0) {
      return _strdup(bookmarks[i].name);
    }
  }

  return NULL;
}
