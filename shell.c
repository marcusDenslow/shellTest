/**
 * shell.c
 * Implementation of core shell functionality
 */

#include "shell.h"
#include "builtins.h"
#include "line_reader.h"
#include "structured_data.h"
#include "filters.h"

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
 * Execute a pipeline of commands
 */
int lsh_execute_piped(char ***commands) {
    int i;
    TableData *result = NULL;
    
    // Execute each command in the pipeline
    for (i = 0; commands[i] != NULL; i++) {
        char **args = commands[i];
        
        if (args[0] == NULL) {
            continue;
        }
        
        // First command in pipeline
        if (i == 0) {
            // Only 'ls' and 'dir' can produce structured data for now
            if (strcmp(args[0], "ls") == 0 || strcmp(args[0], "dir") == 0) {
                result = lsh_dir_structured(args);
                if (!result) {
                    fprintf(stderr, "lsh: error generating structured output for '%s'\n", args[0]);
                    return 1;
                }
            } else {
                fprintf(stderr, "lsh: command '%s' does not support piping\n", args[0]);
                return 1;
            }
        } else {
            // Handle piped commands (filters)
            if (result == NULL) {
                fprintf(stderr, "lsh: no data to pipe\n");
                return 1;
            }
            
            // Search for matching filter
            int found = 0;
            for (int j = 0; j < filter_count; j++) {
                if (strcmp(args[0], filter_str[j]) == 0) {
                    // Run the filter
                    TableData *filtered = (*filter_func[j])(result, args + 1);
                    
                    // Clean up previous result
                    free_table(result);
                    
                    // Use the filtered result for next stage or output
                    result = filtered;
                    found = 1;
                    break;
                }
            }
            
            if (!found) {
                fprintf(stderr, "lsh: filter '%s' not supported\n", args[0]);
                free_table(result);
                result = NULL;
                return 1;
            }
        }
    }
    
    // Print the final result
    if (result != NULL) {
        print_table(result);
        free_table(result);
    }
    
    return 1;
}

/**
 * Free memory for a command array from lsh_split_commands
 */
void free_commands(char ***commands) {
    if (!commands) return;
    
    for (int i = 0; commands[i] != NULL; i++) {
        // Note: We don't free the token strings since they're
        // just pointers into the original command string
        free(commands[i]);
    }
    free(commands);
}

/**
 * Main shell loop
 */
void lsh_loop(void) {
    char *line;
    char ***commands;
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
        
        // Split and execute commands
        commands = lsh_split_commands(line);
        
        // Check if there are any pipes (more than one command)
        int pipe_count = 0;
        while (commands[pipe_count] != NULL) pipe_count++;
        
        if (pipe_count > 1) {
            // Execute piped commands
            status = lsh_execute_piped(commands);
        } else if (pipe_count == 1) {
            // Execute single command the normal way
            status = lsh_execute(commands[0]);
        } else {
            // No commands (empty line)
            status = 1;
        }
        
        // Clean up
        free(line);
        free_commands(commands);
        
    } while (status);
}
