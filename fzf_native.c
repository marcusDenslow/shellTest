/**
 * fzf_native.c
 * Wrapper for the native fzf command-line fuzzy finder
 */

#include "fzf_native.h"
#include "common.h"
#include "line_reader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Check if native fzf is installed on the system
 *
 * @return 1 if installed, 0 if not
 */
int is_fzf_installed(void) {
  // Try to run fzf --version to check if it's installed
  FILE *fp = _popen("fzf --version 2>nul", "r");
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
 * Display instructions for installing fzf
 */
void show_fzf_install_instructions(void) {
  printf("\nfzf is not installed on this system. To use this feature, install "
         "fzf:\n\n");
  printf("Installation options:\n");
  printf("1. Using Git:\n");
  printf("   git clone --depth 1 https://github.com/junegunn/fzf.git ~/.fzf\n");
  printf("   ~/.fzf/install\n\n");
  printf("2. Using Chocolatey (Windows):\n");
  printf("   choco install fzf\n\n");
  printf("3. Using Scoop (Windows):\n");
  printf("   scoop install fzf\n\n");
  printf("4. Download prebuilt binary from: "
         "https://github.com/junegunn/fzf/releases\n\n");
  printf("After installation, restart your shell.\n");
}

/**
 * Run fzf with files from the current directory
 *
 * @param preview Enable preview window if 1
 * @param args Additional arguments for fzf
 * @return Selected filename or NULL if canceled
 */
char *run_native_fzf_files(int preview, char **args) {
  // Check if fzf is installed
  if (!is_fzf_installed()) {
    show_fzf_install_instructions();
    return NULL;
  }

  // Build the command
  char command[1024] = "dir /b /a-d | fzf";

  // Add proper keybindings for both navigation and search toggle
  strcat(command, " --bind=\"ctrl-j:down,ctrl-k:up,/:toggle-search\"");

  // Add preview if requested
  if (preview) {
    strcat(command, " --preview=\"cat {}\"");
  }

  // Add any additional arguments
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
  }

  // Redirect output to a temporary file to capture selection
  char tempfile[MAX_PATH];
  GetTempPath(MAX_PATH, tempfile);
  strcat(tempfile, "fzf_result.txt");

  // Append output redirection to the command
  strcat(command, " > ");
  strcat(command, tempfile);

  // Run the command
  int result = system(command);

  // Check if user canceled (fzf returns non-zero)
  if (result != 0) {
    remove(tempfile); // Clean up temp file even on cancel
    return NULL;
  }

  // Read the selected filename from the temporary file
  FILE *fp = fopen(tempfile, "r");
  if (!fp) {
    remove(tempfile);
    return NULL;
  }

  // Read the selected line
  char *selected = (char *)malloc(MAX_PATH);
  if (!selected) {
    fclose(fp);
    remove(tempfile);
    return NULL;
  }

  if (fgets(selected, MAX_PATH, fp) == NULL) {
    fclose(fp);
    remove(tempfile);
    free(selected);
    return NULL;
  }

  // Remove newline if present
  size_t len = strlen(selected);
  if (len > 0 && selected[len - 1] == '\n') {
    selected[len - 1] = '\0';
  }

  // Close the file and delete it
  fclose(fp);
  remove(tempfile);

  return selected;
}

/**
 * Run fzf with all files and directories from the current directory
 *
 * @param recursive Search recursively if 1
 * @param args Additional arguments for fzf
 * @return Selected path or NULL if canceled
 */
char *run_native_fzf_all(int recursive, char **args) {
  // Check if fzf is installed
  if (!is_fzf_installed()) {
    show_fzf_install_instructions();
    return NULL;
  }

  // Build the command - note: using 'dir /b /a' instead of 'find' for Windows
  char command[1024];

  if (recursive) {
    strcpy(command, "dir /b /s /a | fzf");
  } else {
    strcpy(command, "dir /b /a | fzf");
  }

  // Add proper keybindings for both navigation and search toggle
  strcat(command, " --bind=\"ctrl-j:down,ctrl-k:up,/:toggle-search\"");

  // Add preview window showing file info or directory contents
  strcat(command, " --preview=\"if exist {}\\NUL (dir /b {}) else (type {} "
                  "2>NUL || echo Binary file)\"");

  // Add any additional arguments
  if (args && args[1] != NULL) {
    int i = 1;
    while (args[i] != NULL) {
      if (strcmp(args[i], "-r") != 0 && strcmp(args[i], "--recursive") != 0) {
        strcat(command, " ");

        // If the argument contains spaces, quote it
        if (strchr(args[i], ' ') != NULL) {
          strcat(command, "\"");
          strcat(command, args[i]);
          strcat(command, "\"");
        } else {
          strcat(command, args[i]);
        }
      }
      i++;
    }
  }

  // Redirect output to a temporary file to capture selection
  char tempfile[MAX_PATH];
  GetTempPath(MAX_PATH, tempfile);
  strcat(tempfile, "fzf_result.txt");

  // Append output redirection to the command
  strcat(command, " > ");
  strcat(command, tempfile);

  // Run the command
  int result = system(command);

  // Check if user canceled (fzf returns non-zero)
  if (result != 0) {
    remove(tempfile); // Clean up temp file even on cancel
    return NULL;
  }

  // Read the selected path from the temporary file
  FILE *fp = fopen(tempfile, "r");
  if (!fp) {
    remove(tempfile);
    return NULL;
  }

  // Read the selected line
  char *selected = (char *)malloc(MAX_PATH);
  if (!selected) {
    fclose(fp);
    remove(tempfile);
    return NULL;
  }

  if (fgets(selected, MAX_PATH, fp) == NULL) {
    fclose(fp);
    remove(tempfile);
    free(selected);
    return NULL;
  }

  // Remove newline if present
  size_t len = strlen(selected);
  if (len > 0 && selected[len - 1] == '\n') {
    selected[len - 1] = '\0';
  }

  // Close the file and delete it
  fclose(fp);
  remove(tempfile);

  return selected;
}

/**
 * Run fzf with command history
 *
 * @return Selected command or NULL if canceled
 */
char *run_native_fzf_history(void) {
  // Check if fzf is installed
  if (!is_fzf_installed()) {
    show_fzf_install_instructions();
    return NULL;
  }

  // Create a temporary file with command history
  char tempfile_in[MAX_PATH];
  GetTempPath(MAX_PATH, tempfile_in);
  strcat(tempfile_in, "fzf_history.txt");

  FILE *fp_in = fopen(tempfile_in, "w");
  if (!fp_in) {
    return NULL;
  }

  // Write history to temporary file
  int num_to_display =
      (history_count < HISTORY_SIZE) ? history_count : HISTORY_SIZE;
  int start_idx;

  if (history_count <= HISTORY_SIZE) {
    // Haven't filled the buffer yet
    start_idx = 0;
  } else {
    // History buffer is full, start from the oldest command
    start_idx = history_index; // Next slot to overwrite contains oldest command
  }

  for (int i = 0; i < num_to_display; i++) {
    int idx = (start_idx + i) % HISTORY_SIZE;
    if (command_history[idx].command) {
      fprintf(fp_in, "%s\n", command_history[idx].command);
    }
  }

  fclose(fp_in);

  // Create a temporary file for the result
  char tempfile_out[MAX_PATH];
  GetTempPath(MAX_PATH, tempfile_out);
  strcat(tempfile_out, "fzf_result.txt");

  // Build the command
  char command[1024];
  sprintf(command,
          "type \"%s\" | fzf --tac --no-sort "
          "--bind=\"ctrl-j:down,ctrl-k:up,/:toggle-search\" > \"%s\"",
          tempfile_in, tempfile_out);

  // Run the command
  int result = system(command);

  // Delete the input file
  remove(tempfile_in);

  // Check if user canceled
  if (result != 0) {
    remove(tempfile_out); // Clean up temp file even on cancel
    return NULL;
  }

  // Read the selected command from the output file
  FILE *fp_out = fopen(tempfile_out, "r");
  if (!fp_out) {
    remove(tempfile_out);
    return NULL;
  }

  // Read the selected line
  char *selected = (char *)malloc(MAX_PATH);
  if (!selected) {
    fclose(fp_out);
    remove(tempfile_out);
    return NULL;
  }

  if (fgets(selected, MAX_PATH, fp_out) == NULL) {
    fclose(fp_out);
    remove(tempfile_out);
    free(selected);
    return NULL;
  }

  // Remove newline if present
  size_t len = strlen(selected);
  if (len > 0 && selected[len - 1] == '\n') {
    selected[len - 1] = '\0';
  }

  // Close and delete the output file
  fclose(fp_out);
  remove(tempfile_out);

  return selected;
}

/**
 * Check if a specific editor is available
 *
 * @param editor Name of the editor executable
 * @return 1 if available, 0 if not
 */
int is_editor_available(const char *editor) {
  char command[256];
  snprintf(command, sizeof(command), "%s --version >nul 2>&1", editor);
  return (system(command) == 0);
}

/**
 * Open a file in the best available editor in the current terminal
 *
 * @param file_path Path to the file to open
 * @param line_number Optional line number to jump to (0 to ignore)
 * @return 1 if successful, 0 if failed
 */
int open_in_best_editor(const char *file_path, int line_number) {
  char command[2048] = {0};
  int success = 0;

  // Try to detect available editors (in order of preference)
  if (is_editor_available("nvim")) {
    // Neovim is available - construct command with +line_number
    if (line_number > 0) {
      snprintf(command, sizeof(command), "nvim +%d \"%s\"", line_number,
               file_path);
    } else {
      snprintf(command, sizeof(command), "nvim \"%s\"", file_path);
    }
    success = 1;
  } else if (is_editor_available("vim")) {
    // Vim is available
    if (line_number > 0) {
      snprintf(command, sizeof(command), "vim +%d \"%s\"", line_number,
               file_path);
    } else {
      snprintf(command, sizeof(command), "vim \"%s\"", file_path);
    }
    success = 1;
  } else if (is_editor_available("nano")) {
    // Try nano if available
    snprintf(command, sizeof(command), "nano \"%s\"", file_path);
    success = 1;
  } else if (is_editor_available("code")) {
    // VSCode in the terminal if possible
    if (line_number > 0) {
      snprintf(command, sizeof(command), "code -g \"%s:%d\" -r", file_path,
               line_number);
    } else {
      snprintf(command, sizeof(command), "code \"%s\" -r", file_path);
    }
    success = 1;
  } else if (is_editor_available("notepad")) {
    // Notepad as a last resort (will still open in a new window)
    snprintf(command, sizeof(command), "notepad \"%s\"", file_path);
    success = 1;
  }

  if (success) {
    // Clear the screen before launching the editor
    system("cls");

    // Execute the command directly in the current terminal
    system(command);

    // Return directly to prompt without requiring key press
    return 1;
  } else {
    // No suitable editor found
    printf("No compatible editor (neovim, vim, nano or VSCode) found.\n");
    return 0;
  }
}

/**
 * Command handler for the fzf command
 *
 * @param args Command arguments
 * @return 1 to continue shell execution, 0 to exit shell
 */
int lsh_fzf_native(char **args) {
  // Check if fzf is installed
  if (!is_fzf_installed()) {
    show_fzf_install_instructions();
    return 1;
  }

  // Parse options
  int recursive = 0;
  int mode = 0;    // 0 = all, 1 = files only, 2 = history
  int no_open = 0; // Don't open files automatically if set
  int arg_index = 1;

  if (args[arg_index] && strcmp(args[arg_index], "--help") == 0) {
    printf("Usage: fzf [options] [pattern]\n");
    printf("Interactive fuzzy finder.\n");
    printf("Options:\n");
    printf("  -r, --recursive     Search directories recursively\n");
    printf("  -f, --files         Search only files (not directories)\n");
    printf("  -h, --history       Search command history\n");
    printf("  --no-open           Don't automatically open selected files\n");
    printf("\nControls:\n");
    printf("  Ctrl+j/Ctrl+k       Move down/up (vim-style navigation)\n");
    printf("  Type directly       To search (default mode)\n");
    printf("  /                   Toggle search mode (allows searching for 'j' "
           "and 'k')\n");
    printf("  Enter               Select item (and open file)\n");
    printf("  Ctrl+C/Esc          Cancel\n");
    printf("  ?                   Toggle preview window\n");
    return 1;
  }

  // Process options
  while (args[arg_index] != NULL && args[arg_index][0] == '-') {
    if (strcmp(args[arg_index], "-r") == 0 ||
        strcmp(args[arg_index], "--recursive") == 0) {
      recursive = 1;
      arg_index++;
    } else if (strcmp(args[arg_index], "-f") == 0 ||
               strcmp(args[arg_index], "--files") == 0) {
      mode = 1; // Files only
      arg_index++;
    } else if (strcmp(args[arg_index], "-h") == 0 ||
               strcmp(args[arg_index], "--history") == 0) {
      mode = 2; // History
      arg_index++;
    } else if (strcmp(args[arg_index], "--no-open") == 0) {
      no_open = 1;
      arg_index++;
    } else {
      break; // Unknown option, treat as pattern
    }
  }

  // Run fzf based on the selected mode
  char *result = NULL;

  if (mode == 2) {
    // History mode
    result = run_native_fzf_history();
  } else if (mode == 1) {
    // Files only mode
    result = run_native_fzf_files(1, args);
  } else {
    // All files and directories
    result = run_native_fzf_all(recursive, args);
  }

  // Handle the result
  if (result) {
    if (mode == 2) {
      // Execute the selected command from history
      printf("Executing: %s\n", result);

      // Parse and execute the command
      char *cmd_copy = _strdup(result);
      if (cmd_copy) {
        char **cmd_args = lsh_split_line(cmd_copy);
        lsh_execute(cmd_args);

        // Clean up
        free(cmd_args);
        free(cmd_copy);
      }
    } else {
      // File or directory selected

      // Check if it's a regular file (not a directory)
      WIN32_FIND_DATA findData;
      HANDLE hFind = FindFirstFile(result, &findData);

      if (hFind != INVALID_HANDLE_VALUE) {
        BOOL is_directory =
            (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        FindClose(hFind);

        if (is_directory) {
          // It's a directory, just print it
          printf("Selected directory: %s\n", result);

          // Optionally change to that directory
          printf("Do you want to change to this directory? (y/n): ");
          char response = getchar();
          // Clear input buffer
          while (getchar() != '\n')
            ;

          if (response == 'y' || response == 'Y') {
            _chdir(result);
            printf("Changed directory to: %s\n", result);
          }
        } else {
          // It's a file
          printf("Selected file: %s\n", result);

          // Open in editor if allowed
          if (!no_open) {
            int success = open_in_best_editor(result, 0);
            if (success) {
              printf("File opened in editor.\n");
            }
          }
        }
      } else {
        // Could not determine file type, just print the path
        printf("Selected: %s\n", result);
      }
    }

    free(result);
  } else {
    printf("No selection made.\n");
  }

  return 1;
}
