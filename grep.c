/**
 * grep.c
 * Implementation of text searching in files
 */

#include "grep.h"
#include "builtins.h"
#include <stdio.h>
#include <string.h>

// Define color codes for highlighting matches
#define COLOR_MATCH FOREGROUND_RED | FOREGROUND_INTENSITY

// Function declarations
static void search_file(const char *filename, const char *pattern, int line_numbers, int ignore_case, int recursive);
static void search_directory(const char *directory, const char *pattern, int line_numbers, int ignore_case, int recursive);
static int is_text_file(const char *filename);

/**
 * Command handler for the "grep" command
 * Usage: grep [options] pattern [file/directory]
 * Options:
 *   -n, --line-numbers  Show line numbers
 *   -i, --ignore-case   Ignore case distinctions
 *   -r, --recursive     Search directories recursively
 */
int lsh_grep(char **args) {
    if (args[1] == NULL) {
        printf("Usage: grep [options] pattern [file/directory]\n");
        printf("Options:\n");
        printf("  -n, --line-numbers  Show line numbers\n");
        printf("  -i, --ignore-case   Ignore case distinctions\n");
        printf("  -r, --recursive     Search directories recursively\n");
        return 1;
    }
    
    // Parse options
    int arg_index = 1;
    int line_numbers = 0;
    int ignore_case = 0;
    int recursive = 0;
    
    // Process options
    while (args[arg_index] != NULL && args[arg_index][0] == '-') {
        if (strcmp(args[arg_index], "-n") == 0 || strcmp(args[arg_index], "--line-numbers") == 0) {
            line_numbers = 1;
        } else if (strcmp(args[arg_index], "-i") == 0 || strcmp(args[arg_index], "--ignore-case") == 0) {
            ignore_case = 1;
        } else if (strcmp(args[arg_index], "-r") == 0 || strcmp(args[arg_index], "--recursive") == 0) {
            recursive = 1;
        } else {
            printf("grep: unknown option: %s\n", args[arg_index]);
            return 1;
        }
        arg_index++;
    }
    
    // After options, we need a pattern at minimum
    if (args[arg_index] == NULL) {
        printf("grep: missing pattern\n");
        return 1;
    }
    
    // Get pattern
    const char *pattern = args[arg_index++];
    
    // Check if a file/directory was specified
    if (args[arg_index] == NULL) {
        // No file specified, search current directory
        search_directory(".", pattern, line_numbers, ignore_case, recursive);
    } else {
        // Process each specified file/directory
        while (args[arg_index] != NULL) {
            // Get file attributes to check if it's a directory
            DWORD attr = GetFileAttributes(args[arg_index]);
            
            if (attr == INVALID_FILE_ATTRIBUTES) {
                printf("grep: %s: No such file or directory\n", args[arg_index]);
            } else if (attr & FILE_ATTRIBUTE_DIRECTORY) {
                // It's a directory
                search_directory(args[arg_index], pattern, line_numbers, ignore_case, recursive);
            } else {
                // It's a file
                search_file(args[arg_index], pattern, line_numbers, ignore_case, 0);
            }
            
            arg_index++;
        }
    }
    
    return 1;
}

/**
 * Search a file for a pattern
 */
static void search_file(const char *filename, const char *pattern, int line_numbers, int ignore_case, int recursive) {
    // Skip non-text files for safety
    if (!is_text_file(filename)) {
        return;
    }
    
    FILE *file = fopen(filename, "r");
    if (!file) {
        printf("grep: %s: Cannot open file\n", filename);
        return;
    }
    
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD originalAttrs;
    
    // Get original console attributes
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    originalAttrs = consoleInfo.wAttributes;
    
    char line[4096];
    int line_num = 0;
    int found_match = 0;
    
    // Read the file line by line
    while (fgets(line, sizeof(line), file) != NULL) {
        line_num++;
        
        // Remove newline character if present
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        
        // Search for pattern in the line
        char *match = NULL;
        
        if (ignore_case) {
            // Case insensitive search
            char *line_lower = _strdup(line);
            char *pattern_lower = _strdup(pattern);
            
            if (line_lower && pattern_lower) {
                // Convert to lowercase
                for (char *p = line_lower; *p; p++) *p = tolower(*p);
                for (char *p = pattern_lower; *p; p++) *p = tolower(*p);
                
                // Find match
                match = strstr(line_lower, pattern_lower);
                
                // Calculate offset in original line if match found
                if (match) {
                    match = line + (match - line_lower);
                }
                
                free(line_lower);
                free(pattern_lower);
            }
        } else {
            // Case sensitive search
            match = strstr(line, pattern);
        }
        
        // If pattern found in the line
        if (match) {
            found_match = 1;
            
            // Print filename if in recursive mode
            if (recursive) {
                printf("%s:", filename);
            }
            
            // Print line number if requested
            if (line_numbers) {
                printf("%d:", line_num);
            }
            
            // Calculate match position
            int match_pos = match - line;
            int pattern_len = strlen(pattern);
            
            // Print part before match
            if (match_pos > 0) {
                printf("%.*s", match_pos, line);
            }
            
            // Print match in color
            SetConsoleTextAttribute(hConsole, COLOR_MATCH);
            printf("%.*s", pattern_len, match);
            SetConsoleTextAttribute(hConsole, originalAttrs);
            
            // Print part after match
            if (match_pos + pattern_len < len) {
                printf("%s", line + match_pos + pattern_len);
            }
            
            printf("\n");
        }
    }
    
    fclose(file);
    
    if (!found_match && !recursive) {
        printf("No matches found in %s\n", filename);
    }
}

/**
 * Search a directory for files containing a pattern
 */
static void search_directory(const char *directory, const char *pattern, int line_numbers, int ignore_case, int recursive) {
    char search_path[MAX_PATH];
    WIN32_FIND_DATA findData;
    HANDLE hFind;
    
    // Prepare search pattern for all files in directory
    snprintf(search_path, sizeof(search_path), "%s\\*", directory);
    
    // Start finding files
    hFind = FindFirstFile(search_path, &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        printf("grep: %s: Cannot access directory\n", directory);
        return;
    }
    
    do {
        // Skip "." and ".." directories
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }
        
        // Build full path
        char full_path[MAX_PATH];
        snprintf(full_path, sizeof(full_path), "%s\\%s", directory, findData.cFileName);
        
        if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // It's a directory - recursively search if recursive flag is set
            if (recursive) {
                search_directory(full_path, pattern, line_numbers, ignore_case, recursive);
            }
        } else {
            // It's a file - search it
            search_file(full_path, pattern, line_numbers, ignore_case, recursive);
        }
    } while (FindNextFile(hFind, &findData));
    
    FindClose(hFind);
}

/**
 * Check if a file is likely to be a text file
 * This helps avoid reading binary files
 */
static int is_text_file(const char *filename) {
    // Check file extension
    const char *ext = strrchr(filename, '.');
    if (ext) {
        // Skip common binary file extensions
        static const char *binary_exts[] = {
            ".exe", ".dll", ".obj", ".bin", ".dat", ".png", ".jpg", ".jpeg", 
            ".gif", ".bmp", ".zip", ".rar", ".7z", ".gz", ".mp3", ".mp4", 
            ".avi", ".mov", ".pdf", ".doc", ".docx", ".xls", ".xlsx"
        };
        
        for (int i = 0; i < sizeof(binary_exts) / sizeof(binary_exts[0]); i++) {
            if (_stricmp(ext, binary_exts[i]) == 0) {
                return 0; // Binary file
            }
        }
        
        // Check for common text file extensions
        static const char *text_exts[] = {
            ".txt", ".c", ".cpp", ".h", ".hpp", ".cs", ".js", ".py", ".html", 
            ".css", ".xml", ".json", ".md", ".log", ".sh", ".bat", ".cmd", 
            ".ini", ".conf", ".cfg"
        };
        
        for (int i = 0; i < sizeof(text_exts) / sizeof(text_exts[0]); i++) {
            if (_stricmp(ext, text_exts[i]) == 0) {
                return 1; // Text file
            }
        }
    }
    
    // If we can't determine by extension, check the first few bytes
    FILE *file = fopen(filename, "rb");
    if (!file) {
        return 0; // Can't open, assume it's not text
    }
    
    // Read the first 512 bytes
    unsigned char buffer[512];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
    fclose(file);
    
    if (bytes_read == 0) {
        return 1; // Empty file, consider it text
    }
    
    // Count non-printable, non-whitespace characters
    int binary_chars = 0;
    for (size_t i = 0; i < bytes_read; i++) {
        if (buffer[i] < 32 && buffer[i] != '\t' && buffer[i] != '\n' && buffer[i] != '\r') {
            binary_chars++;
        }
    }
    
    // If more than 10% of characters are binary, consider it a binary file
    return (binary_chars < bytes_read / 10);
}
