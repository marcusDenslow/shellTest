/**
 * line_reader.c
 * Implementation of line reading and parsing with context-aware suggestions
 */

#include "line_reader.h"
#include "bookmarks.h" // Added for bookmark support
#include "builtins.h"  // Added for history access
#include "persistent_history.h"
#include "tab_complete.h"

// Define key codes
#define KEY_BACKSPACE 8
#define KEY_TAB 9
#define KEY_ENTER 13
#define KEY_ESCAPE 27

/**
 * Try to complete a bookmark name if the command is 'goto' or 'unbookmark'
 */
int try_bookmark_completion(char *buffer, int position, HANDLE hConsole,
                            COORD promptEndPos, WORD originalAttributes) {
  // Check buffer starts with "goto " or "unbookmark "
  if (position < 5)
    return 0; // Too short for "goto "

  // Check for "goto " prefix
  int is_goto = (strncmp(buffer, "goto ", 5) == 0);

  // Check for "unbookmark " prefix
  int is_unbookmark = 0;
  if (position >= 11) { // Check length for "unbookmark"
    is_unbookmark = (strncmp(buffer, "unbookmark ", 11) == 0);
  }

  if (!is_goto && !is_unbookmark)
    return 0;

  // Let the regular tab completion system handle bookmarks
  // This will enable proper tab cycling
  return 0;
}

char *lsh_read_line(void) {
  // Allocate buffer for the input
  int bufsize = LSH_RL_BUFSIZE;
  char *buffer = malloc(bufsize * sizeof(char));
  if (!buffer) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }

  // Initialize buffer as empty string
  buffer[0] = '\0';

  char *suggestion = NULL;
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  WORD originalAttributes;

  // For tab completion cycling
  static int tab_index = 0;
  static char **tab_matches = NULL;
  static int tab_num_matches = 0;
  static char last_tab_prefix[1024] = "";
  static int tab_word_start = 0; // Track where the word being completed starts

  // For history navigation
  static int history_navigation_index =
      -1; // -1 means not in history navigation mode
  static char original_line[LSH_RL_BUFSIZE] =
      ""; // Original input before history navigation

  // Flag to track if we're showing a suggestion
  int showing_suggestion = 0;

  // Flag to track if we're showing a history suggestion (vs command/file
  // suggestion)
  int showing_history_suggestion = 0;

  // Flag to track if we're ready to execute after accepting a suggestion
  int ready_to_execute = 0;

  // Current cursor position in the buffer
  int position = 0;

  // Get original console attributes
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
  originalAttributes = consoleInfo.wAttributes;

  // Save the initial cursor position (after prompt)
  COORD promptEndPos = consoleInfo.dwCursorPosition;

  // Get console width
  int console_width = 80; // Default

  if (GetConsoleScreenBufferInfo(hConsole, &consoleInfo)) {
    console_width = consoleInfo.srWindow.Right - consoleInfo.srWindow.Left + 1;
  }

  // Set up the console for raw input
  HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
  DWORD oldMode;
  GetConsoleMode(hStdin, &oldMode);

  // Disable line input mode
  DWORD newMode = oldMode & ~ENABLE_LINE_INPUT;

  // Also disable echo input - we'll handle echoing ourselves
  newMode &= ~ENABLE_ECHO_INPUT;

  // Must still be processed input for Ctrl+C to work
  newMode |= ENABLE_PROCESSED_INPUT;

  // Apply new console mode
  SetConsoleMode(hStdin, newMode);

  // Main input loop
  int c;
  int done = 0;

  while (!done) {
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
      showing_history_suggestion = 0;
    }

    // Find and display new suggestion only if we're not in tab cycling mode
    if (!tab_matches && !ready_to_execute && position == strlen(buffer)) {
      buffer[position] = '\0'; // Ensure buffer is null-terminated

      // Try to find frequency-based suggestions first
      int freq_suggestions_count = 0;
      char **freq_suggestions = NULL;

      // Only try frequency suggestions if we have at least a few characters
      // typed
      if (strlen(buffer) >= 2) {
        freq_suggestions =
            get_frequency_suggestions(buffer, &freq_suggestions_count);
      }

      if (freq_suggestions && freq_suggestions_count > 0) {
        // Use the highest frequency suggestion
        suggestion = freq_suggestions[0];

        // Get current cursor position
        GetConsoleScreenBufferInfo(hConsole, &consoleInfo);

        // Set text color to gray for suggestion
        SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);

        // Calculate and display the part of suggestion not yet typed
        const char *completion = suggestion + strlen(buffer);
        if (completion && *completion) {
          printf("%s", completion);
          // Reset cursor position
          SetConsoleCursorPosition(hConsole, consoleInfo.dwCursorPosition);
          showing_suggestion = 1;
          showing_history_suggestion = 1; // This is a history-based suggestion
        }

        // Reset color
        SetConsoleTextAttribute(hConsole, originalAttributes);

        // Free all but the first suggestion which is now stored in 'suggestion'
        for (int i = 1; i < freq_suggestions_count; i++) {
          free(freq_suggestions[i]);
        }
        free(freq_suggestions);
      } else {
        // Fall back to context-aware best match
        suggestion = find_context_best_match(buffer, position);

        if (suggestion) {
          // Get current cursor position
          GetConsoleScreenBufferInfo(hConsole, &consoleInfo);

          // Find the start of the current word
          int word_start = position - 1;
          while (word_start >= 0 && buffer[word_start] != ' ' &&
                 buffer[word_start] != '\\' && buffer[word_start] != '|') {
            word_start--;
          }
          word_start++; // Move past the space, backslash, or pipe

          // Extract just the last word from the suggested path
          const char *lastWord = strrchr(suggestion, ' ');
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
          // (case insensitive) and hasn't been completely typed already
          if (_strnicmp(lastWord, currentWord, strlen(currentWord)) == 0 &&
              _stricmp(lastWord, currentWord) != 0) {
            // Set text color to gray for suggestion
            SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
            // Print only the part of the suggestion that hasn't been typed yet
            printf("%s", lastWord + strlen(currentWord));
            // Reset color
            SetConsoleTextAttribute(hConsole, originalAttributes);
            // Reset cursor position
            SetConsoleCursorPosition(hConsole, consoleInfo.dwCursorPosition);
            showing_suggestion = 1;
            showing_history_suggestion =
                0; // This is a command/file suggestion, not history
          }
        }
      }
    }

    // Read a character
    c = _getch();

    // Check for CTRL and SHIFT key states
    SHORT ctrlKeyState = GetKeyState(VK_CONTROL);
    SHORT shiftKeyState = GetKeyState(VK_SHIFT);
    int ctrlPressed = (ctrlKeyState & 0x8000) != 0;
    int shiftPressed = (shiftKeyState & 0x8000) != 0;

    // Early detection for arrow key sequences
    if (c == 0 || c == 224) {
      // Arrow key - get the second byte
      c = _getch();

      switch (c) {
      case 75: // Left arrow
        if (position > 0) {
          // Move cursor left on screen
          CONSOLE_SCREEN_BUFFER_INFO csbi;
          GetConsoleScreenBufferInfo(hConsole, &csbi);
          csbi.dwCursorPosition.X--;
          SetConsoleCursorPosition(hConsole, csbi.dwCursorPosition);

          // Update position in buffer
          position--;
        }
        break;

      case 77: // Right arrow
        if (position < strlen(buffer)) {
          // Move cursor right on screen
          CONSOLE_SCREEN_BUFFER_INFO csbi;
          GetConsoleScreenBufferInfo(hConsole, &csbi);
          csbi.dwCursorPosition.X++;
          SetConsoleCursorPosition(hConsole, csbi.dwCursorPosition);

          // Update position in buffer
          position++;
        }
        break;

      case 72: // Up arrow - history backwards
        if (history_count > 0 || get_history_count() > 0) {
          // Save original input if just starting navigation
          if (history_navigation_index == -1) {
            buffer[position] = '\0';
            strcpy(original_line, buffer);
            history_navigation_index = 0;
          } else {
            // Increase history index if possible
            int in_memory_max = (history_count < HISTORY_SIZE)
                                    ? history_count - 1
                                    : HISTORY_SIZE - 1;
            int persistent_max = get_history_count() - 1;

            if (history_navigation_index <= in_memory_max) {
              // Still within in-memory history
              history_navigation_index++;
            } else if (history_navigation_index - in_memory_max <=
                       persistent_max) {
              // Continue into persistent history
              history_navigation_index++;
            }
          }

          // Hide cursor to prevent flicker
          CONSOLE_CURSOR_INFO cursorInfo;
          GetConsoleCursorInfo(hConsole, &cursorInfo);
          BOOL originalCursorVisible = cursorInfo.bVisible;
          cursorInfo.bVisible = FALSE;
          SetConsoleCursorInfo(hConsole, &cursorInfo);

          // Clear current line
          SetConsoleCursorPosition(hConsole, promptEndPos);
          for (int i = 0; i < strlen(buffer) + 5; i++) {
            putchar(' ');
          }

          // Move back to start position
          SetConsoleCursorPosition(hConsole, promptEndPos);

          // Calculate which history entry to show
          int in_memory_max = (history_count < HISTORY_SIZE) ? history_count - 1
                                                             : HISTORY_SIZE - 1;

          if (history_navigation_index <= in_memory_max) {
            // Use in-memory history
            int history_entry;
            if (history_count <= HISTORY_SIZE) {
              // History buffer isn't full yet
              history_entry = history_count - 1 - history_navigation_index;
            } else {
              // History buffer is full (circular)
              history_entry = (history_index - 1 - history_navigation_index +
                               HISTORY_SIZE) %
                              HISTORY_SIZE;
            }

            // Copy history entry to buffer
            strcpy(buffer, command_history[history_entry].command);
          } else {
            // Use persistent history
            int persistent_index = history_navigation_index - in_memory_max - 1;
            PersistentHistoryEntry *entry =
                get_history_entry(get_history_count() - 1 - persistent_index);
            if (entry && entry->command) {
              strcpy(buffer, entry->command);
            }
          }

          position = strlen(buffer);

          // Display the command
          printf("%s", buffer);

          // Show cursor again
          cursorInfo.bVisible = originalCursorVisible;
          SetConsoleCursorInfo(hConsole, &cursorInfo);
        }
        break;

      case 80: // Down arrow - history forwards
        if (history_navigation_index > 0) {
          // Move forward in history
          history_navigation_index--;

          // Hide cursor to prevent flicker
          CONSOLE_CURSOR_INFO cursorInfo;
          GetConsoleCursorInfo(hConsole, &cursorInfo);
          BOOL originalCursorVisible = cursorInfo.bVisible;
          cursorInfo.bVisible = FALSE;
          SetConsoleCursorInfo(hConsole, &cursorInfo);

          // Clear current line
          SetConsoleCursorPosition(hConsole, promptEndPos);
          for (int i = 0; i < strlen(buffer) + 5; i++) {
            putchar(' ');
          }

          // Move back to start position
          SetConsoleCursorPosition(hConsole, promptEndPos);

          // Calculate which history entry to show
          int in_memory_max = (history_count < HISTORY_SIZE) ? history_count - 1
                                                             : HISTORY_SIZE - 1;

          if (history_navigation_index > in_memory_max) {
            // Still in persistent history
            int persistent_index = history_navigation_index - in_memory_max - 1;
            PersistentHistoryEntry *entry =
                get_history_entry(get_history_count() - 1 - persistent_index);
            if (entry && entry->command) {
              strcpy(buffer, entry->command);
            }
          } else {
            // Back to in-memory history
            int history_entry;
            if (history_count <= HISTORY_SIZE) {
              // History buffer isn't full yet
              history_entry = history_count - 1 - history_navigation_index;
            } else {
              // History buffer is full (circular)
              history_entry = (history_index - 1 - history_navigation_index +
                               HISTORY_SIZE) %
                              HISTORY_SIZE;
            }

            // Copy history entry to buffer
            strcpy(buffer, command_history[history_entry].command);
          }

          position = strlen(buffer);

          // Display the command
          printf("%s", buffer);

          // Show cursor again
          cursorInfo.bVisible = originalCursorVisible;
          SetConsoleCursorInfo(hConsole, &cursorInfo);
        } else if (history_navigation_index == 0) {
          // Return to original input
          history_navigation_index = -1;

          // Hide cursor to prevent flicker
          CONSOLE_CURSOR_INFO cursorInfo;
          GetConsoleCursorInfo(hConsole, &cursorInfo);
          BOOL originalCursorVisible = cursorInfo.bVisible;
          cursorInfo.bVisible = FALSE;
          SetConsoleCursorInfo(hConsole, &cursorInfo);

          // Clear current line
          SetConsoleCursorPosition(hConsole, promptEndPos);
          for (int i = 0; i < strlen(buffer) + 5; i++) {
            putchar(' ');
          }

          // Move back to start position
          SetConsoleCursorPosition(hConsole, promptEndPos);

          // Restore original input
          strcpy(buffer, original_line);
          position = strlen(buffer);

          // Display the original input
          printf("%s", buffer);

          // Show cursor again
          cursorInfo.bVisible = originalCursorVisible;
          SetConsoleCursorInfo(hConsole, &cursorInfo);
        }
        break;
      }

      continue;
    }
    // Check for Shift+Enter (Enter key with shift pressed)
    else if (c == KEY_ENTER && shiftPressed) {
      // Handle Shift+Enter for accepting history suggestion
      if (showing_suggestion && showing_history_suggestion && suggestion) {
        // Copy the suggestion to the buffer
        strcpy(buffer, suggestion);
        position = strlen(buffer);

        // Hide cursor to prevent flicker
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hConsole, &cursorInfo);
        BOOL originalCursorVisible = cursorInfo.bVisible;
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &cursorInfo);

        // Clear the line
        DWORD numCharsWritten;
        COORD clearPos = promptEndPos;
        FillConsoleOutputCharacter(hConsole, ' ', 120, clearPos,
                                   &numCharsWritten);
        FillConsoleOutputAttribute(hConsole, originalAttributes, 120, clearPos,
                                   &numCharsWritten);

        // Move cursor back to beginning
        SetConsoleCursorPosition(hConsole, promptEndPos);

        // Print the full command
        buffer[position] = '\0';
        WriteConsole(hConsole, buffer, strlen(buffer), &numCharsWritten, NULL);

        // Restore cursor visibility
        cursorInfo.bVisible = originalCursorVisible;
        SetConsoleCursorInfo(hConsole, &cursorInfo);

        // Clean up
        free(suggestion);
        suggestion = NULL;
        showing_suggestion = 0;
        showing_history_suggestion = 0;

        // Don't execute yet - just keep the suggestion for editing
        ready_to_execute = 0;

        // Continue editing
        continue;
      } else {
        // If no history suggestion is showing, treat as regular Enter
        putchar('\n'); // Echo newline
        buffer[position] = '\0';

        // Clean up
        if (suggestion) {
          free(suggestion);
          suggestion = NULL;
        }
        showing_suggestion = 0;
        showing_history_suggestion = 0;

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

        // Reset history navigation
        history_navigation_index = -1;

        // Restore original console mode
        SetConsoleMode(hStdin, oldMode);
        return buffer;
      }
    } else if (c == KEY_ENTER) {
      // User pressed Enter - end input

      // If we're ready to execute after accepting a suggestion
      if (ready_to_execute) {
        putchar('\n'); // Echo newline
        buffer[position] = '\0';

        // Clean up
        if (suggestion) {
          free(suggestion);
          suggestion = NULL;
        }
        showing_suggestion = 0;
        showing_history_suggestion = 0;

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

        // Reset history navigation
        history_navigation_index = -1;

        ready_to_execute = 0;

        // Restore original console mode
        SetConsoleMode(hStdin, oldMode);
        return buffer;
      }
      // If we're in tab cycling mode, accept the current suggestion but don't
      // execute
      else if (tab_matches) {
        // Tab cycling is active - accept the current suggestion but don't
        // execute Find the start of the current word
        int word_start = tab_word_start;

        // Insert the selected match into buffer
        char *current_match = tab_matches[tab_index];

        // Check if the match is already fully typed out (case insensitive)
        int already_typed = (_stricmp(buffer + word_start, current_match) == 0);

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

        // If the match was already fully typed, execute immediately
        if (already_typed) {
          putchar('\n'); // Echo newline
          buffer[position] = '\0';

          // Restore original console mode
          SetConsoleMode(hStdin, oldMode);
          return buffer;
        }

        // Hide cursor to prevent flicker
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hConsole, &cursorInfo);
        BOOL originalCursorVisible = cursorInfo.bVisible;
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &cursorInfo);

        // Clear the line
        DWORD numCharsWritten;
        COORD clearPos = promptEndPos;
        FillConsoleOutputCharacter(hConsole, ' ', 120, clearPos,
                                   &numCharsWritten);
        FillConsoleOutputAttribute(hConsole, originalAttributes, 120, clearPos,
                                   &numCharsWritten);

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
      // Just submit the command as typed - ignore suggestions unless explicitly
      // accepted
      else {
        putchar('\n'); // Echo newline
        buffer[position] = '\0';

        // Clean up
        if (suggestion) {
          free(suggestion);
          suggestion = NULL;
        }
        showing_suggestion = 0;
        showing_history_suggestion = 0;

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

        // Reset history navigation
        history_navigation_index = -1;

        // Restore original console mode
        SetConsoleMode(hStdin, oldMode);
        return buffer;
      }
    } else if (c == KEY_BACKSPACE) {
      // User pressed Backspace
      if (position > 0) {
        if (ctrlPressed) {
          // Handle Ctrl+Backspace - delete entire word
          // Find the start of the current word
          int word_start = position - 1;

          // Skip any spaces immediately before the cursor
          while (word_start >= 0 &&
                 (buffer[word_start] == ' ' || buffer[word_start] == '\t')) {
            word_start--;
          }

          // Find the beginning of the word
          while (word_start >= 0 && buffer[word_start] != ' ' &&
                 buffer[word_start] != '\t') {
            word_start--;
          }

          word_start++; // Move past the space or tab

          // Calculate how many characters to delete
          int chars_to_delete = position - word_start;

          if (chars_to_delete > 0) {
            // Get current cursor position
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            GetConsoleScreenBufferInfo(hConsole, &csbi);

            // Move cursor back to start of word
            COORD newPos = csbi.dwCursorPosition;
            newPos.X -= chars_to_delete;
            SetConsoleCursorPosition(hConsole, newPos);

            // Hide cursor during redraw
            CONSOLE_CURSOR_INFO cursorInfo;
            GetConsoleCursorInfo(hConsole, &cursorInfo);
            BOOL originalCursorVisible = cursorInfo.bVisible;
            cursorInfo.bVisible = FALSE;
            SetConsoleCursorInfo(hConsole, &cursorInfo);

            // Shift the buffer contents to remove the word
            memmove(buffer + word_start, buffer + position,
                    strlen(buffer) - position + 1);

            // Calculate length of text after cursor
            int textAfterLen = strlen(buffer + word_start);

            // Print the rest of the line and some spaces to erase old content
            printf("%s", buffer + word_start);
            for (int i = 0; i < chars_to_delete; i++) {
              putchar(' ');
            }

            // Move cursor back to the correct position
            COORD finalPos = newPos;
            finalPos.X += textAfterLen;
            SetConsoleCursorPosition(hConsole, finalPos);

            // Move cursor back to where the word was deleted
            SetConsoleCursorPosition(hConsole, newPos);

            // Update position
            position = word_start;

            // Restore cursor visibility
            cursorInfo.bVisible = originalCursorVisible;
            SetConsoleCursorInfo(hConsole, &cursorInfo);
          }
        }
        // Handle backspace differently when in tab cycling mode
        else if (tab_matches) {
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
        } else if (position == strlen(buffer)) {
          // At end of line, backspace is simpler
          position--;
          printf("\b \b"); // Move back, erase, move back
          buffer[position] = '\0';
        } else {
          // Backspace in middle of line
          // Get current cursor position
          CONSOLE_SCREEN_BUFFER_INFO csbi;
          GetConsoleScreenBufferInfo(hConsole, &csbi);

          // Hide cursor during redraw
          CONSOLE_CURSOR_INFO cursorInfo;
          GetConsoleCursorInfo(hConsole, &cursorInfo);
          BOOL originalCursorVisible = cursorInfo.bVisible;
          cursorInfo.bVisible = FALSE;
          SetConsoleCursorInfo(hConsole, &cursorInfo);

          // Move cursor back one
          COORD newPos = csbi.dwCursorPosition;
          newPos.X--;
          SetConsoleCursorPosition(hConsole, newPos);

          // Shift buffer contents to remove the character
          position--;
          memmove(buffer + position, buffer + position + 1,
                  strlen(buffer) - position);

          // Print rest of line and a space to erase last character
          printf("%s ", buffer + position);

          // Move cursor back to the correct position
          COORD finalPos = newPos;
          finalPos.X += strlen(buffer + position) + 1;
          SetConsoleCursorPosition(hConsole, finalPos);

          // Move cursor back to where the character was deleted
          SetConsoleCursorPosition(hConsole, newPos);

          // Restore cursor visibility
          cursorInfo.bVisible = originalCursorVisible;
          SetConsoleCursorInfo(hConsole, &cursorInfo);
        }

        // Reset execution flag when editing
        ready_to_execute = 0;

        // Exit history navigation mode if active
        if (history_navigation_index >= 0) {
          history_navigation_index = -1;
        }
      }
    } else if (c == KEY_TAB) {
      // User pressed Tab - try bookmark completion first
      buffer[position] = '\0'; // Ensure null termination

      // Try bookmark completion first
      if (try_bookmark_completion(buffer, position, hConsole, promptEndPos,
                                  originalAttributes)) {
        // Update position to end of the completed bookmark
        position = strlen(buffer);
        continue;
      }

      // TAB now only accepts command/file suggestions, not history suggestions
      if (showing_suggestion && !showing_history_suggestion && suggestion) {
        // Accept the command/file suggestion (not history)
        // Find the start of the current word
        int word_start = position - 1;
        while (word_start >= 0 && buffer[word_start] != ' ' &&
               buffer[word_start] != '\\' && buffer[word_start] != '|') {
          word_start--;
        }
        word_start++; // Move past the space, backslash, or pipe

        // Extract the suggestion (either last word or full path)
        const char *lastWord = strrchr(suggestion, ' ');
        if (lastWord) {
          lastWord++; // Move past the space
        } else {
          lastWord = suggestion;
        }

        // Extract current word
        char currentWord[1024] = "";
        strncpy(currentWord, buffer + word_start, position - word_start);
        currentWord[position - word_start] = '\0';

        // Replace the current word with the suggestion
        memmove(buffer + word_start, lastWord, strlen(lastWord) + 1);
        position = word_start + strlen(lastWord);

        // Clear the line
        GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
        SetConsoleCursorPosition(hConsole, promptEndPos);

        // Clear the entire line
        for (int i = 0; i < console_width; i++) {
          putchar(' ');
        }

        // Move back to beginning of line and display buffer
        SetConsoleCursorPosition(hConsole, promptEndPos);
        printf("%s", buffer);

        // Free suggestion
        free(suggestion);
        suggestion = NULL;
        showing_suggestion = 0;

        continue;
      }

      // Original tab completion code for files/directories
      // Find the start of the current word
      int word_start = position;
      while (word_start > 0 && buffer[word_start - 1] != ' ' &&
             buffer[word_start - 1] != '\\' && buffer[word_start - 1] != '|') {
        word_start--;
      }

      // Save word_start for later use
      tab_word_start = word_start;

      // Save original command line up to the word being completed
      char original_input[LSH_RL_BUFSIZE];
      strncpy(original_input, buffer, word_start);
      original_input[word_start] = '\0';

      char partial_path[1024] = "";
      strncpy(partial_path, buffer + word_start, position - word_start);
      partial_path[position - word_start] = '\0';

      // Check if we're continuing to tab through the same prefix (case
      // insensitive)
      if (_stricmp(partial_path, last_tab_prefix) != 0 || tab_matches == NULL) {
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

        // Check if this is the first word (command)
        int is_first_word = (tab_word_start == 0);

        // Find context-aware matches for the new prefix
        tab_matches = find_context_matches(buffer, position, partial_path,
                                           &tab_num_matches);

        // If no matches, don't try frequency-based matches as these are handled
        // by Shift+Enter now

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

        // Always start with the first match
        tab_index = 0;
      } else {
        // Same prefix, cycle to next match
        tab_index = (tab_index + 1) % tab_num_matches;
      }

      // Display the current match
      redraw_tab_suggestion(hConsole, promptEndPos, original_input,
                            tab_matches[tab_index], last_tab_prefix, tab_index,
                            tab_num_matches, originalAttributes);

      // Reset execution flag when using Tab
      ready_to_execute = 0;

      // Exit history navigation mode if active
      if (history_navigation_index >= 0) {
        history_navigation_index = -1;
      }
    } else if (isprint(c)) {
      // Regular printable character
      if (position == strlen(buffer)) {
        // Cursor at end of line - simple append
        buffer[position] = c;
        position++;
        buffer[position] = '\0';
        putchar(c); // Echo character
      } else {
        // Cursor in middle of line - insert character

        // Get current cursor position
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(hConsole, &csbi);

        // Hide cursor during update to prevent flicker
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hConsole, &cursorInfo);
        BOOL originalCursorVisible = cursorInfo.bVisible;
        cursorInfo.bVisible = FALSE;
        SetConsoleCursorInfo(hConsole, &cursorInfo);

        // Make room in the buffer
        memmove(buffer + position + 1, buffer + position,
                strlen(buffer) - position + 1);
        buffer[position] = c;

        // Display the new character and text after it
        printf("%c%s", c, buffer + position + 1);

        // Move cursor back to position right after the inserted char
        position++;
        COORD newPos = csbi.dwCursorPosition;
        newPos.X = promptEndPos.X + position;
        SetConsoleCursorPosition(hConsole, newPos);

        // Restore cursor visibility
        cursorInfo.bVisible = originalCursorVisible;
        SetConsoleCursorInfo(hConsole, &cursorInfo);
      }

      // Reset execution flag when editing
      ready_to_execute = 0;

      // Exit history navigation mode if active
      if (history_navigation_index >= 0) {
        history_navigation_index = -1;
      }

      // Resize buffer if needed
      if (position >= bufsize - 1) {
        bufsize += LSH_RL_BUFSIZE;
        buffer = realloc(buffer, bufsize);
        if (!buffer) {
          fprintf(stderr, "lsh: allocation error\n");
          SetConsoleMode(hStdin, oldMode);
          exit(EXIT_FAILURE);
        }
      }
    }

    buffer[position] = '\0'; // Ensure null termination
  }

  // This should never be reached, but just in case
  buffer[position] = '\0';

  // Restore original console mode
  SetConsoleMode(hStdin, oldMode);

  return buffer;
}

/**
 * Split a line into tokens
 */
char **lsh_split_line(char *line) {
  int bufsize = LSH_TOK_BUFSIZE;
  int position = 0;
  char **tokens = malloc(bufsize * sizeof(char *));
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
      tokens = realloc(tokens, bufsize * sizeof(char *));
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

/**
 * Split a line into commands separated by pipes
 */
char ***lsh_split_commands(char *line) {
  int bufsize = 8; // Initial number of commands
  int cmd_count = 0;
  char ***commands = malloc(bufsize * sizeof(char **));

  if (!commands) {
    fprintf(stderr, "lsh: allocation error\n");
    exit(EXIT_FAILURE);
  }

  // Make a copy of the line since strtok modifies the string
  char *line_copy = _strdup(line);
  if (!line_copy) {
    fprintf(stderr, "lsh: allocation error\n");
    free(commands);
    exit(EXIT_FAILURE);
  }

  // Split by pipe symbol
  char *saveptr;
  char *cmd_str = strtok_s(line_copy, "|", &saveptr);

  while (cmd_str != NULL) {
    // Trim leading whitespace
    while (isspace(*cmd_str))
      cmd_str++;

    // Make a copy of the cmd_str for splitting
    char *cmd_copy = _strdup(cmd_str);
    if (!cmd_copy) {
      fprintf(stderr, "lsh: allocation error\n");
      free(line_copy);
      // Free previously allocated commands
      for (int i = 0; i < cmd_count; i++) {
        free(commands[i]);
      }
      free(commands);
      exit(EXIT_FAILURE);
    }

    // Split the command into tokens
    commands[cmd_count] = lsh_split_line(cmd_copy);
    cmd_count++;

    // Resize if needed
    if (cmd_count >= bufsize) {
      bufsize += 8;
      commands = realloc(commands, bufsize * sizeof(char **));
      if (!commands) {
        fprintf(stderr, "lsh: allocation error\n");
        free(line_copy);
        exit(EXIT_FAILURE);
      }
    }

    // The copy of cmd_str is now owned by lsh_split_line
    cmd_str = strtok_s(NULL, "|", &saveptr);
  }

  // Add NULL terminator
  commands[cmd_count] = NULL;

  free(line_copy);
  return commands;
}
