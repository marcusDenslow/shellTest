/**
 * ripgrep.c
 * Interactive wrapper for the ripgrep (rg) command-line search tool
 */

#include "ripgrep.h"
#include "common.h"
#include "line_reader.h"
#include "shell.h" // Add this for lsh_execute
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Check if ripgrep (rg) is installed on the system
 *
 * @return 1 if installed, 0 if not
 */
int is_rg_installed(void) {
  // Try to run rg --version to check if it's installed
  FILE *fp = _popen("rg --version 2>nul", "r");
  if (fp == NULL) {
    return 0;
  }

  // Read output (if any)
  char buffer[128];
  int has_output = (fgets(buffer, sizeof(buffer), fp) != NULL);

  // Close the process
  _pclose(fp);

  return has_output;
}

/**
 * Display instructions for installing ripgrep
 */
void show_rg_install_instructions(void) {
  printf("\nripgrep (rg) is not installed on this system. To use this feature, "
         "install ripgrep:\n\n");
  printf("Installation options:\n");
  printf("1. Using Chocolatey (Windows):\n");
  printf("   choco install ripgrep\n\n");
  printf("2. Using Scoop (Windows):\n");
  printf("   scoop install ripgrep\n\n");
  printf("3. Download prebuilt binary from: "
         "https://github.com/BurntSushi/ripgrep/releases\n\n");
  printf("After installation, restart your shell.\n");
}

/**
 * Check if a specific editor is available
 *
 * @param editor Name of the editor executable
 * @return 1 if available, 0 if not
 */
int is_editor_available_for_rg(const char *editor) {
  char command[256];
  snprintf(command, sizeof(command), "%s --version >nul 2>&1", editor);
  return (system(command) == 0);
}

/**
 * Open a file at a specific line in the best available editor
 *
 * @param file_path Path to the file to open
 * @param line_number Line number to position cursor at
 * @return 1 if successful, 0 if failed
 */
int rg_open_in_editor(const char *file_path, int line_number) {
  char command[2048] = {0};
  int success = 0;

  // Try to detect available editors (in order of preference)
  if (is_editor_available_for_rg("nvim")) {
    // Neovim is available - construct command with +line_number
    snprintf(command, sizeof(command), "nvim +%d \"%s\"", line_number,
             file_path);
    success = 1;
  } else if (is_editor_available_for_rg("vim")) {
    // Vim is available
    snprintf(command, sizeof(command), "vim +%d \"%s\"", line_number,
             file_path);
    success = 1;
  } else if (is_editor_available_for_rg("nano")) {
    // Try nano if available
    // Note: nano doesn't have perfect line number navigation
    snprintf(command, sizeof(command), "nano +%d \"%s\"", line_number,
             file_path);
    success = 1;
  } else if (is_editor_available_for_rg("code")) {
    // VSCode with line number
    snprintf(command, sizeof(command), "code -g \"%s:%d\" -r", file_path,
             line_number);
    success = 1;
  } else if (is_editor_available_for_rg("notepad")) {
    // Notepad as last resort (no line number support)
    snprintf(command, sizeof(command), "notepad \"%s\"", file_path);
    success = 1;
  }

  if (success) {
    // Clear the screen before launching the editor
    system("cls");

    // Execute the command directly in the current terminal
    int result = system(command);
    success = (result == 0);

    return success;
  } else {
    // No suitable editor found
    printf("No compatible editor (neovim, vim, nano, VSCode) found.\n");
    return 0;
  }
}

/**
 * Parse a ripgrep result line to extract file path and line number
 *
 * @param result_line The result line from ripgrep (format:
 * file:line:column:text)
 * @param file_path Buffer to store extracted file path
 * @param file_path_size Size of file_path buffer
 * @param line_number Pointer to store extracted line number
 * @return 1 if parsing successful, 0 if failed
 */
static int parse_rg_result(const char *result_line, char *file_path,
                           size_t file_path_size, int *line_number) {
  // Ripgrep output format is: file:line:column:text
  const char *first_colon = strchr(result_line, ':');
  if (!first_colon)
    return 0;

  const char *second_colon = strchr(first_colon + 1, ':');
  if (!second_colon)
    return 0;

  // Extract file path
  size_t path_length = first_colon - result_line;
  if (path_length >= file_path_size)
    path_length = file_path_size - 1;
  strncpy(file_path, result_line, path_length);
  file_path[path_length] = '\0';

  // Extract line number
  char line_str[16] = {0};
  size_t line_str_length = second_colon - (first_colon + 1);
  if (line_str_length >= sizeof(line_str))
    line_str_length = sizeof(line_str) - 1;
  strncpy(line_str, first_colon + 1, line_str_length);
  line_str[line_str_length] = '\0';

  *line_number = atoi(line_str);
  return 1;
}

/**
 * Run ripgrep with interactive filtering
 *
 * @param args Command arguments or NULL for interactive mode
 * @return Selected filename/line or NULL if canceled
 */
char *run_interactive_ripgrep(char **args) {
  // Check if ripgrep is installed
  if (!is_rg_installed()) {
    show_rg_install_instructions();
    return NULL;
  }

  // Create temporary files for the search results
  char temp_rg_results[MAX_PATH];
  char temp_fzf_output[MAX_PATH];
  GetTempPath(MAX_PATH, temp_rg_results);
  GetTempPath(MAX_PATH, temp_fzf_output);
  strcat(temp_rg_results, "rg_results.txt");
  strcat(temp_fzf_output, "rg_selected.txt");

  // Build the initial command
  char command[4096] = {0};

  // Base ripgrep command with sensible defaults
  strcpy(command,
         "rg --line-number --column --no-heading --color=always --smart-case");

  // Add any user-provided arguments
  if (args && args[1] != NULL) {
    int i = 1;
    while (args[i] != NULL) {
      strcat(command, " ");

      // If the argument contains spaces, quote it
      if (strchr(args[i], ' ') != NULL) {
        strcat(command, "\"");
        strcat(command, args[i]);
        strcat(command, "\"");
      } else {
        strcat(command, args[i]);
      }
      i++;
    }
  } else {
    // If no search pattern provided, use empty string to search all files
    // Will be filtered by fzf as user types
    strcat(command, " \"\"");
  }

  // Pipe to fzf for interactive selection with preview
  strcat(command, " | fzf --ansi");

  // Add keybindings for navigation
  strcat(command, " --bind=\"ctrl-j:down,ctrl-k:up,/:toggle-search\"");

  // Add preview window showing file content with line highlighted
  strcat(command, " --preview=\"bat --color=always --style=numbers "
                  "--highlight-line={2} {1}\"");
  strcat(command, " --preview-window=+{2}-10");

  // Add output redirection
  strcat(command, " > ");
  strcat(command, temp_fzf_output);

  // Run the command
  printf("Starting interactive ripgrep search...\n");
  int result = system(command);

  // Check if user canceled (fzf returns non-zero)
  if (result != 0) {
    remove(temp_fzf_output);
    return NULL;
  }

  // Read the selected result from the temporary file
  FILE *fp = fopen(temp_fzf_output, "r");
  if (!fp) {
    remove(temp_fzf_output);
    return NULL;
  }

  // Read the selected line
  char *selected = (char *)malloc(MAX_PATH * 2);
  if (!selected) {
    fclose(fp);
    remove(temp_fzf_output);
    return NULL;
  }

  if (fgets(selected, MAX_PATH * 2, fp) == NULL) {
    fclose(fp);
    remove(temp_fzf_output);
    free(selected);
    return NULL;
  }

  // Remove newline if present
  size_t len = strlen(selected);
  if (len > 0 && selected[len - 1] == '\n') {
    selected[len - 1] = '\0';
  }

  // Close and delete the temporary file
  fclose(fp);
  remove(temp_fzf_output);

  return selected;
}

/**
 * Run ripgrep with interactive editing capabilities for a custom Neovim-like
 * experience
 */
void run_ripgrep_interactive_session(void) {
  // Check if ripgrep and fzf are installed
  if (!is_rg_installed()) {
    show_rg_install_instructions();
    return;
  }

  // Initialize the search query
  char search_query[256] = "";
  char last_query[256] = "";

  // Create a temporary file for results
  char temp_results[MAX_PATH];
  GetTempPath(MAX_PATH, temp_results);
  strcat(temp_results, "ripgrep_results.txt");

  // Save original console mode to restore later
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD originalMode;
  GetConsoleMode(hStdin, &originalMode);

  // Set console mode for raw input
  DWORD newMode = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT;
  SetConsoleMode(hStdin, newMode);

  // Clear screen and show initial UI
  system("cls");
  printf("--- Interactive Ripgrep Search ---\n");
  printf(
      "Type to search | Ctrl+N/P: navigate | Enter: open | Ctrl+C: exit\n\n");
  printf("Search: ");

  int running = 1;
  int selected_index = 0;
  int results_count = 0;
  char **results = NULL;

  // Main interaction loop
  while (running) {
    // Display the current search query
    printf("\rSearch: %s", search_query);
    fflush(stdout);

    // Check if the query has changed and we need to run ripgrep again
    if (strcmp(search_query, last_query) != 0) {
      // Free previous results if any
      if (results) {
        for (int i = 0; i < results_count; i++) {
          free(results[i]);
        }
        free(results);
        results = NULL;
        results_count = 0;
      }

      // Execute ripgrep with the current query if not empty
      if (strlen(search_query) > 0) {
        // Build ripgrep command
        char command[4096];
        snprintf(command, sizeof(command),
                 "rg --line-number --column --no-heading --color=never "
                 "--smart-case \"%s\" > %s",
                 search_query, temp_results);

        system(command);

        // Read results from the temp file
        FILE *fp = fopen(temp_results, "r");
        if (fp) {
          // Count lines first
          char line[1024];
          results_count = 0;

          while (fgets(line, sizeof(line), fp) != NULL) {
            results_count++;
          }

          // Allocate memory for results
          rewind(fp);
          results = (char **)malloc(results_count * sizeof(char *));

          if (results) {
            int i = 0;
            while (i < results_count && fgets(line, sizeof(line), fp) != NULL) {
              // Remove newline
              size_t len = strlen(line);
              if (len > 0 && line[len - 1] == '\n') {
                line[len - 1] = '\0';
              }

              results[i] = _strdup(line);
              i++;
            }
          }

          fclose(fp);
        }
      }

      // Update last query
      strcpy(last_query, search_query);

      // Reset selection to first result
      selected_index = 0;

      // Clear the results display area
      printf("\n\033[J"); // Clear from cursor to end of screen

      // Display results count
      printf("\nFound %d matches\n\n", results_count);

      // Display results with selection
      int max_display = 10; // Show at most 10 results at a time
      for (int i = 0; i < results_count && i < max_display; i++) {
        if (i == selected_index) {
          printf("\033[7m> %s\033[0m\n",
                 results[i]); // Invert colors for selection
        } else {
          printf("  %s\n", results[i]);
        }
      }
    }

    // Process user input
    int c = _getch();

    if (c == 3) { // Ctrl+C
      running = 0;
    } else if (c == 13) { // Enter
      // Open the selected file if there are results
      if (results_count > 0 && selected_index < results_count) {
        char file_path[MAX_PATH];
        int line_number;

        if (parse_rg_result(results[selected_index], file_path,
                            sizeof(file_path), &line_number)) {
          rg_open_in_editor(file_path, line_number);

          // After returning from editor, refresh the UI
          system("cls");
          printf("--- Interactive Ripgrep Search ---\n");
          printf("Type to search | Ctrl+N/P: navigate | Enter: open | Ctrl+C: "
                 "exit\n\n");
          printf("Search: %s\n\n", search_query);

          // Re-display results count
          printf("Found %d matches\n\n", results_count);

          // Re-display results with selection
          int max_display = 10;
          for (int i = 0; i < results_count && i < max_display; i++) {
            if (i == selected_index) {
              printf("\033[7m> %s\033[0m\n", results[i]);
            } else {
              printf("  %s\n", results[i]);
            }
          }
        }
      }
    } else if (c == 14) { // Ctrl+N
      if (results_count > 0) {
        selected_index = (selected_index + 1) % results_count;

        // Redraw results with new selection
        printf("\033[%dA",
               results_count > 10
                   ? 10
                   : results_count); // Move cursor up to results start

        int max_display = 10;
        for (int i = 0; i < results_count && i < max_display; i++) {
          if (i == selected_index) {
            printf("\033[2K\033[7m> %s\033[0m\n",
                   results[i]); // Clear line and invert colors
          } else {
            printf("\033[2K  %s\n", results[i]); // Clear line
          }
        }
      }
    } else if (c == 16) { // Ctrl+P
      if (results_count > 0) {
        selected_index = (selected_index - 1 + results_count) % results_count;

        // Redraw results with new selection
        printf("\033[%dA",
               results_count > 10
                   ? 10
                   : results_count); // Move cursor up to results start

        int max_display = 10;
        for (int i = 0; i < results_count && i < max_display; i++) {
          if (i == selected_index) {
            printf("\033[2K\033[7m> %s\033[0m\n",
                   results[i]); // Clear line and invert colors
          } else {
            printf("\033[2K  %s\n", results[i]); // Clear line
          }
        }
      }
    } else if (c == 8) { // Backspace
      // Remove last character from search query
      size_t len = strlen(search_query);
      if (len > 0) {
        search_query[len - 1] = '\0';
      }
    } else if (isprint(c)) { // Printable character
      // Add character to search query
      size_t len = strlen(search_query);
      if (len < sizeof(search_query) - 1) {
        search_query[len] = c;
        search_query[len + 1] = '\0';
      }
    }
  }

  // Clean up
  if (results) {
    for (int i = 0; i < results_count; i++) {
      free(results[i]);
    }
    free(results);
  }

  remove(temp_results);

  // Restore original console mode
  SetConsoleMode(hStdin, originalMode);

  // Final screen clear
  system("cls");
}

/**
 * Command handler for the ripgrep command
 *
 * @param args Command arguments
 * @return 1 to continue shell execution, 0 to exit shell
 */
int lsh_ripgrep(char **args) {
  // Check if ripgrep is installed
  if (!is_rg_installed()) {
    printf("Ripgrep (rg) is not installed. Falling back to custom "
           "implementation.\n");
    printf("For better performance, consider installing ripgrep:\n");
    show_rg_install_instructions();
    printf("\nRunning with custom implementation...\n\n");

    // Fall back to our custom implementation
    run_ripgrep_interactive_session();
    return 1;
  }

  // Also check if fzf is installed for the interactive UI
  int fzf_available = is_fzf_installed(); // Reuse from fzf_native.h

  // Check for help flag
  if (args[1] &&
      (strcmp(args[1], "--help") == 0 || strcmp(args[1], "-h") == 0)) {
    printf("Usage: ripgrep [pattern] [options]\n");
    printf("Interactive code search using ripgrep (rg) with fzf.\n\n");
    printf("If called without arguments, launches fzf with ripgrep for "
           "interactive searching.\n\n");
    printf("Options:\n");
    printf("  -t, --type [TYPE]    Only search files matching TYPE (e.g., -t "
           "cpp)\n");
    printf("  -i, --ignore-case    Case insensitive search\n");
    printf("  -w, --word-regexp    Only match whole words\n");
    printf("  -e, --regexp         Treat pattern as a regular expression\n");
    printf("  -f, --fixed-strings  Treat pattern as a literal string\n");
    printf("  -g, --glob [GLOB]    Include/exclude files matching the glob\n");
    return 1;
  }

  // If no arguments provided, use fzf with ripgrep
  if (!args[1]) {
    if (!fzf_available) {
      printf("fzf is not installed. Falling back to custom implementation.\n");
      show_fzf_install_instructions();
      printf("\nRunning with custom implementation...\n\n");

      // Fall back to custom implementation if fzf is not available
      run_ripgrep_interactive_session();
      return 1;
    }

    // Create a temporary file for the fzf preview script
    char preview_script[MAX_PATH];
    GetTempPath(MAX_PATH, preview_script);
    strcat(preview_script, "fzf_preview.bat");

    // Create the preview script as a batch file with highlighting support
    FILE *preview_fp = fopen(preview_script, "w");
    if (preview_fp) {
      // Write a smarter batch script that highlights searched content
      fprintf(preview_fp, "@echo off\n");
      fprintf(preview_fp, "set file=%%~1\n");
      fprintf(preview_fp, "set line=%%~2\n");
      fprintf(preview_fp, "set query=%%~3\n");
      fprintf(preview_fp, "if \"%%query%%\"==\"\" (\n");
      // If no query, just show the file with line highlighting
      fprintf(preview_fp, "  bat --color=always --highlight-line %%line%% "
                          "\"%%file%%\" 2>nul || type \"%%file%%\"\n");
      fprintf(preview_fp, ") else (\n");
      // If query provided, highlight both the line and the search term
      fprintf(preview_fp,
              "  bat --color=always --highlight-line %%line%% \"%%file%%\" "
              "2>nul | findstr /i \"%%query%%\" >nul\n");
      fprintf(preview_fp, "  if %%errorlevel%% equ 0 (\n");
      fprintf(preview_fp,
              "    rg --color=always --context 3 --line-number \"%%query%%\" "
              "\"%%file%%\" 2>nul || bat --color=always --highlight-line "
              "%%line%% \"%%file%%\" 2>nul || type \"%%file%%\"\n");
      fprintf(preview_fp, "  ) else (\n");
      fprintf(preview_fp, "    bat --color=always --highlight-line %%line%% "
                          "\"%%file%%\" 2>nul || type \"%%file%%\"\n");
      fprintf(preview_fp, "  )\n");
      fprintf(preview_fp, ")\n");
      fclose(preview_fp);
    }

    // Use fzf with ripgrep to provide an interactive UI similar to Neovim
    char command[4096] = {0};

    // Use ripgrep with an empty pattern to search all text files
    // and pipe to fzf for interactive filtering - now with fullscreen mode
    snprintf(
        command, sizeof(command),
        "cls && rg --line-number --column --no-heading --color=always \"\" | "
        "fzf --ansi --delimiter : "
        "--preview \"%s {1} {2} {{q}}\" "
        "--preview-window=right:60%%:wrap "
        "--bind \"ctrl-j:down,ctrl-k:up,enter:accept\" "
        "--border "
        "--height=100%% > %s",
        preview_script, "rg_selection.txt");

    // Execute the command
    int result = system(command);

    // Check if user selected something (fzf returns non-zero on cancel)
    if (result == 0) {
      // Read the selected file/line from the temporary file
      FILE *fp = fopen("rg_selection.txt", "r");
      if (fp) {
        char selected[1024];
        if (fgets(selected, sizeof(selected), fp)) {
          // Remove newline if present
          size_t len = strlen(selected);
          if (len > 0 && selected[len - 1] == '\n') {
            selected[len - 1] = '\0';
          }

          // Parse the selection to extract file path and line number
          char file_path[MAX_PATH];
          int line_number;

          if (parse_rg_result(selected, file_path, sizeof(file_path),
                              &line_number)) {
            printf("Opening %s at line %d\n", file_path, line_number);
            rg_open_in_editor(file_path, line_number);
          }
        }
        fclose(fp);
        remove("rg_selection.txt"); // Clean up
      }
    }

    // Remove the preview script
    remove(preview_script);

    return 1;
  }

  // If options are provided but no pattern, run regular ripgrep
  if (args[1][0] == '-') {
    // Build command to pass to system
    char command[4096] = "rg";

    // Add all arguments
    for (int i = 1; args[i] != NULL; i++) {
      strcat(command, " ");

      // If argument contains spaces, quote it
      if (strchr(args[i], ' ') != NULL) {
        strcat(command, "\"");
        strcat(command, args[i]);
        strcat(command, "\"");
      } else {
        strcat(command, args[i]);
      }
    }

    // Execute standard ripgrep command
    system(command);
    return 1;
  }

  // If a pattern is provided, run ripgrep with fzf for selection
  if (fzf_available) {
    // Create a temporary file for the fzf preview script
    char preview_script[MAX_PATH];
    GetTempPath(MAX_PATH, preview_script);
    strcat(preview_script, "fzf_preview.bat");

    // Create the preview script as a batch file with highlighting
    FILE *preview_fp = fopen(preview_script, "w");
    if (preview_fp) {
      // Write a smarter batch script that highlights searched content
      fprintf(preview_fp, "@echo off\n");
      fprintf(preview_fp, "set file=%%~1\n");
      fprintf(preview_fp, "set line=%%~2\n");
      fprintf(preview_fp, "set search_term=%s\n",
              args[1]); // Use the search term provided
      // Show file with both line highlighting and search term highlighting
      fprintf(preview_fp,
              "rg --color=always --context 3 --line-number \"%s\" \"%%file%%\" "
              "2>nul || ",
              args[1]);
      fprintf(preview_fp, "bat --color=always --highlight-line %%line%% "
                          "\"%%file%%\" 2>nul || type \"%%file%%\"\n");
      fclose(preview_fp);
    }

    // Build command with fzf for better interaction
    char command[4096] = {0};

    // Now with better UI but using compatible options
    snprintf(
        command, sizeof(command),
        "cls && rg --line-number --column --no-heading --color=always \"%s\" | "
        "fzf --ansi --delimiter : "
        "--preview \"%s {1} {2}\" "
        "--preview-window=right:60%%:wrap "
        "--bind \"ctrl-j:down,ctrl-k:up,enter:accept\" "
        "--border "
        "--height=100%% > %s",
        args[1], preview_script, "rg_selection.txt");

    // Execute the command
    int result = system(command);

    // Check if user selected something
    if (result == 0) {
      // Read the selected file/line from the temporary file
      FILE *fp = fopen("rg_selection.txt", "r");
      if (fp) {
        char selected[1024];
        if (fgets(selected, sizeof(selected), fp)) {
          // Remove newline if present
          size_t len = strlen(selected);
          if (len > 0 && selected[len - 1] == '\n') {
            selected[len - 1] = '\0';
          }

          // Parse the selection to extract file path and line number
          char file_path[MAX_PATH];
          int line_number;

          if (parse_rg_result(selected, file_path, sizeof(file_path),
                              &line_number)) {
            printf("Opening %s at line %d\n", file_path, line_number);
            rg_open_in_editor(file_path, line_number);
          }
        }
        fclose(fp);
        remove("rg_selection.txt"); // Clean up
      }
    }

    // Remove the preview script
    remove(preview_script);
  } else {
    // If fzf is not available, fall back to custom implementation
    printf("fzf is not installed. Falling back to custom implementation.\n");
    run_ripgrep_interactive_session();
  }

  return 1;
}
