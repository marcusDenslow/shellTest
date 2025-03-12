/**
 * tab_complete.c
 * Implementation of tab completion functionality
 */

#include "tab_complete.h"

/**
 * Find matching files/directories for tab completion
 */
char **find_matches(const char *partial_path, int *num_matches) {
    char cwd[1024];
    char search_dir[1024] = "";
    char search_pattern[256] = "";
    char **matches = NULL;
    int matches_capacity = 10;
    *num_matches = 0;
    
    // Allocate initial array for matches
    matches = (char**)malloc(sizeof(char*) * matches_capacity);
    if (!matches) {
        fprintf(stderr, "lsh: allocation error in tab completion\n");
        return NULL;
    }
    
    // Parse the partial path to separate directory and pattern
    char *last_slash = strrchr(partial_path, '\\');
    if (last_slash) {
        // There's a directory part
        int dir_len = last_slash - partial_path + 1;
        strncpy(search_dir, partial_path, dir_len);
        search_dir[dir_len] = '\0';
        strcpy(search_pattern, last_slash + 1);
    } else {
        // No directory specified, use current directory
        _getcwd(cwd, sizeof(cwd));
        strcpy(search_dir, cwd);
        strcat(search_dir, "\\");
        strcpy(search_pattern, partial_path);
    }
    
    // Prepare for file searching
    char search_path[1024];
    strcpy(search_path, search_dir);
    strcat(search_path, "*");
    
    WIN32_FIND_DATA findData;
    HANDLE hFind = FindFirstFile(search_path, &findData);
    
    if (hFind == INVALID_HANDLE_VALUE) {
        free(matches);
        return NULL;
    }
    
    // Find all matching files/directories
    do {
        // Skip . and .. directories
        if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) {
            continue;
        }
        
        // Check if file matches our pattern
        if (strncmp(findData.cFileName, search_pattern, strlen(search_pattern)) == 0) {
            // Add to matches
            if (*num_matches >= matches_capacity) {
                matches_capacity *= 2;
                matches = (char**)realloc(matches, sizeof(char*) * matches_capacity);
                if (!matches) {
                    fprintf(stderr, "lsh: allocation error in tab completion\n");
                    FindClose(hFind);
                    return NULL;
                }
            }
            
            matches[*num_matches] = _strdup(findData.cFileName);
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                // Append a backslash to directory names
                size_t len = strlen(matches[*num_matches]);
                matches[*num_matches] = (char*)realloc(matches[*num_matches], len + 2);
                strcat(matches[*num_matches], "\\");
            }
            (*num_matches)++;
        }
    } while (FindNextFile(hFind, &findData));
    
    FindClose(hFind);
    
    return matches;
}

/**
 * Find the best match for current input
 */
char* find_best_match(const char* partial_text) {
    // Start from the beginning of the current word
    int len = strlen(partial_text);
    if (len == 0) return NULL;
    
    // Find the start of the current word
    int word_start = len - 1;
    while (word_start >= 0 && partial_text[word_start] != ' ' && partial_text[word_start] != '\\') {
        word_start--;
    }
    word_start++; // Move past the space or backslash
    
    // Extract the current word
    char partial_path[1024] = "";
    strncpy(partial_path, partial_text + word_start, len - word_start);
    partial_path[len - word_start] = '\0';
    
    // Skip if we're not typing a path
    if (strlen(partial_path) == 0) return NULL;
    
    // Find matches
    int num_matches;
    char **matches = find_matches(partial_path, &num_matches);
    
    if (matches && num_matches > 0) {
        // Create the full suggestion by combining the prefix with the matched path
        char* full_suggestion = (char*)malloc(len + strlen(matches[0]) - strlen(partial_path) + 1);
        if (!full_suggestion) {
            for (int i = 0; i < num_matches; i++) {
                free(matches[i]);
            }
            free(matches);
            return NULL;
        }
        
        // Copy the prefix (everything before the current word)
        strncpy(full_suggestion, partial_text, word_start);
        full_suggestion[word_start] = '\0';
        
        // Append the matched path
        strcat(full_suggestion, matches[0]);
        
        // Free matches array
        for (int i = 0; i < num_matches; i++) {
            free(matches[i]);
        }
        free(matches);
        
        return full_suggestion;
    }
    
    return NULL;
}

/**
 * Prepare an entire screen line in a buffer before displaying
 */
void prepare_display_buffer(char *displayBuffer, const char *original_line, 
                           const char *tab_match, const char *last_tab_prefix,
                           int tab_index, int tab_num_matches) {
    displayBuffer[0] = '\0';
    
    // 1. Add the command prefix (e.g., "cd ")
    strcat(displayBuffer, original_line);
    
    // 2. Add matching prefix part
    int prefixLen = strlen(last_tab_prefix);
    char matchPrefix[1024] = "";
    strncpy(matchPrefix, tab_match, prefixLen);
    matchPrefix[prefixLen] = '\0';
    strcat(displayBuffer, matchPrefix);
    
    // 3. Add the remainder of the match
    strcat(displayBuffer, tab_match + prefixLen);
    
    // 4. Add indicator if needed
    if (tab_num_matches > 1) {
        char indicatorBuffer[20];
        sprintf(indicatorBuffer, " (%d/%d)", tab_index + 1, tab_num_matches);
        strcat(displayBuffer, indicatorBuffer);
    }
}

/**
 * Redraw tab suggestion without flickering
 */
void redraw_tab_suggestion(HANDLE hConsole, COORD promptEndPos, 
                           char *original_line, char *tab_match, char *last_tab_prefix,
                           int tab_index, int tab_num_matches, WORD originalAttributes) {
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    DWORD numCharsWritten;
    char displayBuffer[2048] = "";
    
    // Prepare the entire line in memory before displaying anything
    prepare_display_buffer(displayBuffer, original_line, tab_match, last_tab_prefix, 
                           tab_index, tab_num_matches);
    
    // Calculate where to position the cursor after displaying
    int prefixLen = strlen(last_tab_prefix);
    int cursorPos = strlen(original_line) + strlen(tab_match);
    
    // Hide cursor during redraw to prevent flicker
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    BOOL originalCursorVisible = cursorInfo.bVisible;
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    
    // Clear the entire line with one operation
    COORD clearPos = promptEndPos;
    FillConsoleOutputCharacter(hConsole, ' ', 120, clearPos, &numCharsWritten);
    FillConsoleOutputAttribute(hConsole, originalAttributes, 120, clearPos, &numCharsWritten);
    
    // Move cursor to the beginning of line
    SetConsoleCursorPosition(hConsole, promptEndPos);
    
    // Write the command prefix and matching part with normal attributes
    WriteConsole(hConsole, original_line, strlen(original_line), &numCharsWritten, NULL);
    WriteConsole(hConsole, matchPrefix, strlen(matchPrefix), &numCharsWritten, NULL);
    
    // Get current cursor position 
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    
    // Write the suggestion part with gray attributes
    SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
    WriteConsole(hConsole, tab_match + prefixLen, strlen(tab_match + prefixLen), &numCharsWritten, NULL);
    
    // Save cursor position at end of suggestion
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    COORD endOfSuggestionPos = consoleInfo.dwCursorPosition;
    
    // Write the indicator with gray attributes
    if (tab_num_matches > 1) {
        char indicatorBuffer[20];
        sprintf(indicatorBuffer, " (%d/%d)", tab_index + 1, tab_num_matches);
        WriteConsole(hConsole, indicatorBuffer, strlen(indicatorBuffer), &numCharsWritten, NULL);
    }
    
    // Reset text attributes
    SetConsoleTextAttribute(hConsole, originalAttributes);
    
    // Move cursor to end of suggestion (before indicator)
    SetConsoleCursorPosition(hConsole, endOfSuggestionPos);
    
    // Restore cursor visibility
    cursorInfo.bVisible = originalCursorVisible;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}

/**
 * Display suggestion in one operation to prevent visible "typing"
 */
void display_suggestion_atomically(HANDLE hConsole, COORD promptEndPos, const char *buffer, 
                                  const char *suggestion, int position, WORD originalAttributes) {
    if (!suggestion) return;
    
    // Find the start of the current word
    int word_start = position - 1;
    while (word_start >= 0 && buffer[word_start] != ' ' && buffer[word_start] != '\\') {
        word_start--;
    }
    word_start++; // Move past the space or backslash
    
    // Extract just the last word from the suggested path
    char *lastWord = strrchr(suggestion, ' ');
    if (lastWord) {
        lastWord++; // Move past the space
    } else {
        lastWord = suggestion;
    }
    
    // Extract just the last word from what we've typed so far
    char currentWord[1024] = "";
    strncpy(currentWord, buffer + word_start, position - word_start);
    currentWord[position - word_start] = '\0';
    
    // Only display if suggestion starts with what we're typing
    if (strncmp(lastWord, currentWord, strlen(currentWord)) != 0) {
        return;
    }
    
    // Get current cursor position
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    
    // Hide cursor during display
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    BOOL originalCursorVisible = cursorInfo.bVisible;
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
    
    // Prepare the suggestion text (only the part not yet typed)
    char suggestionText[1024] = "";
    strcpy(suggestionText, lastWord + strlen(currentWord));
    
    // Set text color to gray for suggestion
    SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
    
    // Write the suggestion in one operation
    DWORD numCharsWritten;
    WriteConsole(hConsole, suggestionText, strlen(suggestionText), &numCharsWritten, NULL);
    
    // Reset color
    SetConsoleTextAttribute(hConsole, originalAttributes);
    
    // Reset cursor position
    SetConsoleCursorPosition(hConsole, consoleInfo.dwCursorPosition);
    
    // Restore cursor visibility
    cursorInfo.bVisible = originalCursorVisible;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}
