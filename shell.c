/**
 * shell.c
 * Implementation of core shell functionality
 */

#include "shell.h"
#include "builtins.h"
#include "line_reader.h"

/**
 * Launch an external program
 */
int lsh_launch(char **args) {
    // Construct command line string for CreateProcess
    char command[1024] = "";
    for (int i = 0; args[i] != NULL; i++) {
        strcat(command, args[i]);
        strcat(command, " ");
    }
    
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // Create a new process
    if (!CreateProcess(NULL, command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "lsh: failed to execute %s\n", args[0]);
        return 1;
    }
    
    // Wait for the process to finish
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    
    return 1;
}

/**
 * Execute a command
 */
int lsh_execute(char **args) {
    int i;
    
    if (args[0] == NULL) {
        return 1;
    }
    
    // Check for builtin commands
    for (i = 0; i < lsh_num_builtins(); i++) {
        if (strcmp(args[0], builtin_str[i]) == 0) {
            return (*builtin_func[i])(args);
        }
    }
    
    // If not a builtin, launch external program
    return lsh_launch(args);
}

/**
 * Main shell loop
 */
void lsh_loop(void) {
    char *line;
    char **args;
    int status;
    char cwd[1024];
    char prompt_path[1024];
    char username[256] = "Elden Lord";
    
    do {
        // Get current directory for the prompt
        if (_getcwd(cwd, sizeof(cwd)) == NULL) {
            perror("lsh");
            strcpy(prompt_path, "unknown_path"); // Fallback in case of error
        } else {
            // Find the last directory in the path
            char *last_dir = strrchr(cwd, '\\');
            
            if (last_dir != NULL) {
                char last_dir_name[256];
                strcpy(last_dir_name, last_dir + 1); // Save the last directory name
                
                *last_dir = '\0';  // Temporarily terminate string at last backslash
                char *parent_dir = strrchr(cwd, '\\');
                
                if (parent_dir != NULL) {
                    // We have at least two levels deep
                    snprintf(prompt_path, sizeof(prompt_path), "%s in %s\\%s", username, parent_dir + 1, last_dir_name);
                } else {
                    // We're at top level (like C:)
                    snprintf(prompt_path, sizeof(prompt_path), "%s in %s", username, last_dir_name);
                }
            } else {
                // No backslash found (rare case)
                snprintf(prompt_path, sizeof(prompt_path), "%s in %s", username, cwd);
            }
        }
        
        // Print prompt with username and shortened directory
        printf("%s> ", prompt_path);
        
        line = lsh_read_line();
        
        // Add command to history if not empty
        if (line[0] != '\0') {
            lsh_add_to_history(line);
        }
        
        args = lsh_split_line(line);
        status = lsh_execute(args);
        
        free(line);
        free(args);
    } while (status);
}
