/**
 * line_reader.c
 * Implementation of line reading and parsing
 */

#include "line_reader.h"
#include "tab_complete.h"

/**
 * Read a line of input from the user with tab completion
 */
char *lsh_read_line(void) {
    int bufsize = LSH_RL_BUFSIZE;
    int position = 0;
    char *buffer = malloc(sizeof(char) * bufsize);
    int c;
    char *suggestion = NULL;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD originalAttributes;
    
    // For tab completion cycling
    static int tab_index = 0;
    static char **tab_matches = NULL;
    static int tab_num_matches = 0;
    static char last_tab_prefix[1024] = "";
    static int tab_word_start = 0;  // Track where the word being completed starts
    
    // Flag to track if we're showing a suggestion
    int showing_suggestion = 0;
    
    // Flag to track if we're ready to execute after accepting a suggestion
    int ready_to_execute = 0;
    
    // Variables to track the prompt and original line
    static char original_line[LSH_RL_BUFSIZE];
    COORD promptEndPos;
    
    // Get original console attributes
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    originalAttributes = consoleInfo.wAttributes;
    
    // Save prompt end position for reference
    promptEndPos = consoleInfo.dwCursorPosition;
    
    if (!buffer) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    
    buffer[0] = '\0';  // Initialize empty string
    
    while (1) {
        // Clear any previous suggestion from screen if not in tab cycling
        if (suggestion && !tab_matches) {
            // Get current cursor position
            GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
            // Calculate suggestion length
            int suggestionLen = strlen(suggestion) - strlen(buffer);
            if (suggestionLen > 0) {
                // Set cursor to current position
                SetConsoleCursorPosition(hConsole, consoleInfo.dwCursorPosition);
                // Clear the suggestion by printing spaces
                for (int i = 0; i < suggestionLen; i++) {
                    putchar(' ');
                }
                // Reset cursor position
                SetConsoleCursorPosition(hConsole, consoleInfo.dwCursorPosition);
            }
            free(suggestion);
            suggestion = NULL;
            showing_suggestion = 0;
        }
        
        // Find and display new suggestion only if we're not in tab cycling mode
        if (!tab_matches && !ready_to_execute) {
            buffer[position] = '\0';  // Ensure buffer is null-terminated
            suggestion = find_best_match(buffer);
            if (suggestion) {
                // Get current cursor position
                GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
                // Calculate the start of the current word
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
                
                // Only display the suggestion if it starts with what we're typing
                if (strncmp(lastWord, currentWord, strlen(currentWord)) == 0) {
                    // Set text color to gray for suggestion
                    SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
                    // Print only the part of the suggestion that hasn't been typed yet
                    printf("%s", lastWord + strlen(currentWord));
                    // Reset color
                    SetConsoleTextAttribute(hConsole, originalAttributes);
                    // Reset cursor position
                    SetConsoleCursorPosition(hConsole, consoleInfo.dwCursorPosition);
                    showing_suggestion = 1;
                }
            }
        }
        
        c = _getch();  // Get character without echo
        
        if (c == KEY_ENTER) {
            // If we're ready to execute after accepting a suggestion
            if (ready_to_execute) {
                putchar('\n');  // Echo newline
                buffer[position] = '\0';
                
                // Clean up
                if (suggestion) {
                    free(suggestion);
                    suggestion = NULL;
                }
                showing_suggestion = 0;
                
                // Clean up tab completion resources
                if (tab_matches) {
                    for (int i = 0; i < tab_num_matches; i++) {
                        free(tab_matches[i]);
                    }
                    free(tab_matches);
                    tab_matches = NULL;
                    tab_num_matches = 0;
                    tab_index = 0;
                    last_tab_prefix[0] = '\0';
                }
                
                ready_to_execute = 0;
                return buffer;
            }
            // If we're in tab cycling mode, accept the current suggestion
            else if (tab_matches) {
                // Tab cycling is active - accept the current suggestion but don't execute
                // Find the start of the current word
                int word_start = tab_word_start;
                
                // Insert the selected match into buffer
                char *current_match = tab_matches[tab_index];
                
                // Replace partial path with current match
                strcpy(buffer + word_start, current_match);
                position = word_start + strlen(current_match);
                
                // Clean up tab completion resources
                for (int i = 0; i < tab_num_matches; i++) {
                    free(tab_matches[i]);
                }
                free(tab_matches);
                tab_matches = NULL;
                tab_num_matches = 0;
                tab_index = 0;
                last_tab_prefix[0] = '\0';
                
                // Hide cursor to prevent flicker
                CONSOLE_CURSOR_INFO cursorInfo;
                GetConsoleCursorInfo(hConsole, &cursorInfo);
                BOOL originalCursorVisible = cursorInfo.bVisible;
                cursorInfo.bVisible = FALSE;
                SetConsoleCursorInfo(hConsole, &cursorInfo);
                
                // Clear the line
                DWORD numCharsWritten;
                COORD clearPos = promptEndPos;
                FillConsoleOutputCharacter(hConsole, ' ', 120, clearPos, &numCharsWritten);
                FillConsoleOutputAttribute(hConsole, originalAttributes, 120, clearPos, &numCharsWritten);
                
                // Move cursor back to beginning
                SetConsoleCursorPosition(hConsole, promptEndPos);
                
                // Print the full command with the accepted match
                buffer[position] = '\0';
                WriteConsole(hConsole, buffer, strlen(buffer), &numCharsWritten, NULL);
                
                // Restore cursor visibility
                cursorInfo.bVisible = originalCursorVisible;
                SetConsoleCursorInfo(hConsole, &cursorInfo);
                
                // Set flag to execute on next Enter
                ready_to_execute = 1;
                
                // Continue editing - don't submit yet
                continue;
            }
            // Otherwise, if we have a suggestion showing, accept it
            else if (showing_suggestion && suggestion) {
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
                
                // Get current cursor position
                GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
                
                // Print the remainder of the suggestion in normal color
                printf("%s", lastWord + strlen(currentWord));
                
                // Update buffer with the suggestion
                // Keep the prefix (everything before the current word)
                char tempBuffer[1024] = "";
                strncpy(tempBuffer, buffer, word_start);
                tempBuffer[word_start] = '\0';
                
                // Add the completed word
                strcat(tempBuffer, lastWord);
                
                // Copy back to buffer
                strcpy(buffer, tempBuffer);
                position = strlen(buffer);
                
                // Set flag to execute on next Enter
                ready_to_execute = 1;
                
                // Continue editing - don't submit yet
                free(suggestion);
                suggestion = NULL;
                showing_suggestion = 0;
                continue;
            }
            // No tab cycling or suggestion - submit the command
            else {
                putchar('\n');  // Echo newline
                buffer[position] = '\0';
                
                // Clean up
                if (suggestion) free(suggestion);
                
                // Clean up tab completion resources
                if (tab_matches) {
                    for (int i = 0; i < tab_num_matches; i++) {
                        free(tab_matches[i]);
                    }
                    free(tab_matches);
                    tab_matches = NULL;
                    tab_num_matches = 0;
                    tab_index = 0;
                    last_tab_prefix[0] = '\0';
                }
                
                return buffer;
            }
        } else if (c == KEY_BACKSPACE) {
            // User pressed Backspace
            if (position > 0) {
                // Handle backspace differently when in tab cycling mode
                if (tab_matches) {
                    // If in tab cycling mode, immediately revert to original input
                    // Clear the line and redraw with just the original input
                    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
                    SetConsoleCursorPosition(hConsole, promptEndPos);
                    
                    // Clear the entire line
                    for (int i = 0; i < 80; i++) {
                        putchar(' ');
                    }
                    
                    // Move cursor back to beginning of line
                    SetConsoleCursorPosition(hConsole, promptEndPos);
                    
                    // Restore original buffer and position (what the user typed)
                    buffer[tab_word_start] = '\0';
                    strcat(buffer, last_tab_prefix);
                    position = tab_word_start + strlen(last_tab_prefix);
                    
                    // Print the original input
                    printf("%s", buffer);
                    
                    // Reset tab cycling
                    for (int i = 0; i < tab_num_matches; i++) {
                        free(tab_matches[i]);
                    }
                    free(tab_matches);
                    tab_matches = NULL;
                    tab_num_matches = 0;
                    tab_index = 0;
                    last_tab_prefix[0] = '\0';
                } else {
                    // Standard backspace behavior when not in tab cycling mode
                    position--;
                    // Move cursor back, print space, move cursor back again
                    printf("\b \b");
                    buffer[position] = '\0';
                }
                
                // Reset execution flag when editing
                ready_to_execute = 0;
            }
        } else if (c == KEY_TAB) {
            // User pressed Tab - activate completion
            buffer[position] = '\0';  // Ensure null termination
            
            // Find the start of the current word
            int word_start = position;
            while (word_start > 0 && buffer[word_start - 1] != ' ' && buffer[word_start - 1] != '\\') {
                word_start--;
            }
            
            // Save word_start for later use
            tab_word_start = word_start;
            
            // Save original command line up to the word being completed
            strncpy(original_line, buffer, word_start);
            original_line[word_start] = '\0';
            
            char partial_path[1024] = "";
            strncpy(partial_path, buffer + word_start, position - word_start);
            partial_path[position - word_start] = '\0';
            
            // Check if we're continuing to tab through the same prefix
            if (strcmp(partial_path, last_tab_prefix) != 0 || tab_matches == NULL) {
                // New prefix or first tab press, find all matches
                // Clean up previous matches if any
                if (tab_matches) {
                    for (int i = 0; i < tab_num_matches; i++) {
                        free(tab_matches[i]);
                    }
                    free(tab_matches);
                }
                
                // Store the current prefix
                strcpy(last_tab_prefix, partial_path);
                
                // Reset tab index
                tab_index = 0;
                
                // Find matches for the new prefix
                tab_matches = find_matches(partial_path, &tab_num_matches);
                
                // If no matches, don't do anything
                if (!tab_matches || tab_num_matches == 0) {
                    if (tab_matches) {
                        free(tab_matches);
                        tab_matches = NULL;
                    }
                    tab_num_matches = 0;
                    last_tab_prefix[0] = '\0';
                    continue;
                }
                
                // Display the first match with our helper function to avoid flickering
                if (tab_matches && tab_num_matches > 0) {
                    redraw_tab_suggestion(hConsole, promptEndPos, original_line, 
                                          tab_matches[tab_index], last_tab_prefix,
                                          tab_index, tab_num_matches, originalAttributes);
                    continue;
                }
            } else {
                // Same prefix, cycle to next match
                tab_index = (tab_index + 1) % tab_num_matches;
                
                // Redraw with the next match
                redraw_tab_suggestion(hConsole, promptEndPos, original_line, 
                                      tab_matches[tab_index], last_tab_prefix,
                                      tab_index, tab_num_matches, originalAttributes);
                continue;
            }
            
            if (tab_matches && tab_num_matches > 0) {
                // Create a buffer for what we want to display in one go
                char displayBuffer[2048] = "";
                
                // Same prefix, cycle to next match
                tab_index = (tab_index + 1) % tab_num_matches;
                
                // Build the command display in our buffer instead of printing directly
                // First the original command prefix
                strcat(displayBuffer, original_line);
                
                // Get prefix length for later use (what user typed after the command)
                int prefixLen = strlen(last_tab_prefix);
                
                // Matching prefix part in normal color
                char matchPrefix[1024] = "";
                strncpy(matchPrefix, tab_matches[tab_index], prefixLen);
                matchPrefix[prefixLen] = '\0';
                strcat(displayBuffer, matchPrefix);
                
                // The rest of the match
                strcat(displayBuffer, tab_matches[tab_index] + prefixLen);
                
                // Add indicator if needed
                char indicatorBuffer[20] = "";
                if (tab_num_matches > 1) {
                    sprintf(indicatorBuffer, " (%d/%d)", tab_index + 1, tab_num_matches);
                }
                
                // Get the cursor position where it should end up (after suggestion, before indicator)
                // Remove unused variable
                // int cursorPos = strlen(displayBuffer);
                
                // Add the indicator to the display buffer
                strcat(displayBuffer, indicatorBuffer);
                
                // Clear the line and print everything at once to avoid cursor flicker
                
                // First, FillConsoleOutputCharacter to clear the line
                DWORD numCharsWritten;
                FillConsoleOutputCharacter(hConsole, ' ', 80, promptEndPos, &numCharsWritten);
                
                // Move cursor to beginning of line
                SetConsoleCursorPosition(hConsole, promptEndPos);
                
                // Print command and normal part of suggestion
                printf("%s%s", original_line, matchPrefix);
                
                // Print rest of suggestion in gray
                SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
                printf("%s", tab_matches[tab_index] + prefixLen);
                
                // Save cursor position (end of suggestion)
                GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
                COORD endOfSuggestionPos = consoleInfo.dwCursorPosition;
                
                // Print indicator in gray
                if (tab_num_matches > 1) {
                    printf("%s", indicatorBuffer);
                }
                
                // Reset text color
                SetConsoleTextAttribute(hConsole, originalAttributes);
                
                // Move cursor back to end of suggestion
                SetConsoleCursorPosition(hConsole, endOfSuggestionPos);
                
                // Reset execution flag when using Tab
                ready_to_execute = 0;
            }
        } else if (isprint(c)) {
            // Regular printable character
            
            // Special handling when tab cycling is active but user hasn't accepted a suggestion
            if (tab_matches) {
                // Hide cursor temporarily
                CONSOLE_CURSOR_INFO cursorInfo;
                GetConsoleCursorInfo(hConsole, &cursorInfo);
                BOOL originalCursorVisible = cursorInfo.bVisible;
                cursorInfo.bVisible = FALSE;
                SetConsoleCursorInfo(hConsole, &cursorInfo);
                
                // Restore original input (what user typed before tab)
                buffer[tab_word_start] = '\0';
                strcat(buffer, last_tab_prefix);
                position = tab_word_start + strlen(last_tab_prefix);
                
                // Clear the entire line
                DWORD numCharsWritten;
                FillConsoleOutputCharacter(hConsole, ' ', 120, promptEndPos, &numCharsWritten);
                FillConsoleOutputAttribute(hConsole, originalAttributes, 120, promptEndPos, &numCharsWritten);
                
                // Move cursor to beginning of line
                SetConsoleCursorPosition(hConsole, promptEndPos);
                
                // Redraw the original input
                WriteConsole(hConsole, buffer, strlen(buffer), &numCharsWritten, NULL);
                
                // Now add the new character
                buffer[position] = c;
                position++;
                WriteConsole(hConsole, &c, 1, &numCharsWritten, NULL);
                
                // Clean up tab completion resources
                for (int i = 0; i < tab_num_matches; i++) {
                    free(tab_matches[i]);
                }
                free(tab_matches);
                tab_matches = NULL;
                tab_num_matches = 0;
                tab_index = 0;
                last_tab_prefix[0] = '\0';
                
                // Show cursor again
                cursorInfo.bVisible = originalCursorVisible;
                SetConsoleCursorInfo(hConsole, &cursorInfo);
            } else {
                // Standard character handling when not in tab cycling mode
                putchar(c);  // Echo character
                buffer[position] = c;
                position++;
            }
            
            // Reset execution flag when editing
            ready_to_execute = 0;
            
            // Resize buffer if needed
            if (position >= bufsize) {
                bufsize += LSH_RL_BUFSIZE;
                buffer = realloc(buffer, bufsize);
                if (!buffer) {
                    fprintf(stderr, "lsh: allocation error\n");
                    exit(EXIT_FAILURE);
                }
            }
        }
        
        buffer[position] = '\0';  // Ensure null termination
    }
}

/**
 * Split a line into tokens
 */
char **lsh_split_line(char *line) {
    int bufsize = LSH_TOK_BUFSIZE;
    int position = 0;
    char **tokens = malloc(bufsize * sizeof(char*));
    char *token;
    
    if (!tokens) {
        fprintf(stderr, "lsh: allocation error\n");
        exit(EXIT_FAILURE);
    }
    
    token = strtok(line, LSH_TOK_DELIM);
    while (token != NULL) {
        tokens[position] = token;
        position++;
        
        if (position >= bufsize) {
            bufsize += LSH_TOK_BUFSIZE;
            tokens = realloc(tokens, bufsize * sizeof(char*));
            if (!tokens) {
                fprintf(stderr, "lsh: allocation error\n");
                exit(EXIT_FAILURE);
            }
        }
        
        token = strtok(NULL, LSH_TOK_DELIM);
    }
    
    tokens[position] = NULL;
    return tokens;
}
