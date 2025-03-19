/**
 * shell.c
 * Implementation of core shell functionality
 */

#include "shell.h"
#include "builtins.h"
#include "line_reader.h"
#include "structured_data.h"
#include "filters.h"
#include "aliases.h"  // Added for alias support
#include <time.h>  // Added for time functions


/**
 * Display a welcome banner with BBQ sauce invention time
 */
void display_welcome_banner(void) {
    // Calculate time since BBQ sauce invention (January 1, 1650)
    // Current time
    time_t now = time(NULL);
    struct tm *now_tm = localtime(&now);
    
    // January 1, 1650
    int bbq_year = 1650;
    int current_year = now_tm->tm_year + 1900;
    
    // Calculate difference in years
    int years = current_year - bbq_year;
    
    // Calculate remaining months, days, etc.
    int months = now_tm->tm_mon;  // 0-11 for Jan-Dec
    int days = now_tm->tm_mday - 1; // Assuming invention was on the 1st
    int hours = now_tm->tm_hour;
    int minutes = now_tm->tm_min;
    int seconds = now_tm->tm_sec;
    
    // Format the time string
    char time_str[256];
    snprintf(time_str, sizeof(time_str), 
             "It's been %d years, %d months, %d days, %d hours, %d minutes, %d seconds since BBQ sauce was invented",
             years, months, days, hours, minutes, seconds);
    
    // Get console width to center the box
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int consoleWidth = 80; // Default width if we can't get actual console info
    
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        consoleWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    }
    
    // Calculate box width based on content
    const char* title = "Welcome to shell!";
    
    // Calculate the minimum box width needed - ensure it fits the time string
    int min_width = strlen(time_str) + 4; // Add padding
    int box_width = min_width;
    
    // Calculate left padding to center the box
    int left_padding = (consoleWidth - box_width - 2) / 2; // -2 for the border chars
    if (left_padding < 0) left_padding = 0;
    
    // Define colors
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD boxColor = FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    WORD textColor = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    WORD originalAttrs;
    
    // Get original console attributes
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    originalAttrs = consoleInfo.wAttributes;
    
    // Clear any previous content (important to remove the game trivia box)
    system("cls");
    
    // Set color for box drawing
    SetConsoleTextAttribute(hConsole, boxColor);
    
    // Top border
    printf("%*s\u250C", left_padding, "");
    for (int i = 0; i < box_width; i++) printf("\u2500");
    printf("\u2510\n");
    
    // Title row
    printf("%*s\u2502", left_padding, "");
    int title_padding = (box_width - strlen(title)) / 2;
    SetConsoleTextAttribute(hConsole, textColor);
    printf("%*s%s%*s", title_padding, "", title, box_width - title_padding - strlen(title), "");
    SetConsoleTextAttribute(hConsole, boxColor);
    printf("\u2502\n");
    
    // Separator
    printf("%*s\u251C", left_padding, "");
    for (int i = 0; i < box_width; i++) printf("\u2500");
    printf("\u2524\n");
    
    // Time since invention row
    printf("%*s\u2502", left_padding, "");
    SetConsoleTextAttribute(hConsole, textColor);
    printf(" %-*s ", box_width - 2, time_str);
    SetConsoleTextAttribute(hConsole, boxColor);
    printf("\u2502\n");
    
    // Bottom border
    printf("%*s\u2514", left_padding, "");
    for (int i = 0; i < box_width; i++) printf("\u2500");
    printf("\u2518\n\n");
    
    // Reset console attributes
    SetConsoleTextAttribute(hConsole, originalAttrs);
}


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
 * Execute a command (updated to handle aliases)
 */
int lsh_execute(char **args) {
    int i;
    
    if (args[0] == NULL) {
        return 1;
    }
    
    // Check if the command is an alias
    AliasEntry *alias = find_alias(args[0]);
    if (alias) {
        // Create expanded command with the alias
        char expanded_cmd[1024] = "";
        strcpy(expanded_cmd, alias->command);
        
        // Add any arguments
        for (i = 1; args[i] != NULL; i++) {
            strcat(expanded_cmd, " ");
            strcat(expanded_cmd, args[i]);
        }
        
        // Parse the expanded command
        char *expanded_copy = _strdup(expanded_cmd);
        char **expanded_args = lsh_split_line(expanded_copy);
        
        // Execute the expanded command
        int status = lsh_execute(expanded_args);
        
        // Clean up
        free(expanded_copy);
        free(expanded_args);
        
        return status;
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
            // Check for commands that can produce structured data
            if (strcmp(args[0], "ls") == 0 || strcmp(args[0], "dir") == 0) {
                result = lsh_dir_structured(args);
                if (!result) {
                    fprintf(stderr, "lsh: error generating structured output for '%s'\n", args[0]);
                    return 1;
                }
            } else if (strcmp(args[0], "ps") == 0) {
                // Add support for ps command to produce structured data
                result = lsh_ps_structured(args);
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
 * Main shell loop (updated for alias initialization)
 */
void lsh_loop(void) {
    char *line;
    char ***commands;
    int status;
    char cwd[1024];
    char prompt_path[1024];
    char username[256] = "Elden Lord";
    
    // Initialize aliases
    init_aliases();
    
    // Display the welcome banner at startup
    display_welcome_banner();
    
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
        printf("%s -> ", prompt_path);
        
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
    
    // Clean up aliases on exit
    cleanup_aliases();
}
