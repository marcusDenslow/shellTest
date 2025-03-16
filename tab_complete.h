/**
 * tab_complete.h
 * Functions for tab completion and path suggestions
 */

#ifndef TAB_COMPLETE_H
#define TAB_COMPLETE_H

#include "common.h"

/**
 * Find matching files/directories or commands for tab completion
 * 
 * @param partial_text The partial text to match
 * @param is_first_word Flag indicating if this is the first word (command)
 * @param num_matches Pointer to store number of matches found
 * @return Array of matching strings (must be freed by caller)
 */
char **find_matches(const char *partial_text, int is_first_word, int *num_matches);

/**
 * Find the best matching file/directory for current input
 * 
 * @param partial_text The partial text to match
 * @return Best matching string (must be freed by caller)
 */
char* find_best_match(const char* partial_text);

/**
 * Prepare an entire screen line in a buffer before displaying
 * 
 * @param displayBuffer Buffer to store the prepared display string
 * @param original_line The original command line
 * @param tab_match The tab match to display
 * @param last_tab_prefix The prefix typed by the user
 * @param tab_index The current index in tab matches
 * @param tab_num_matches The total number of matches
 */
void prepare_display_buffer(char *displayBuffer, const char *original_line, 
                           const char *tab_match, const char *last_tab_prefix,
                           int tab_index, int tab_num_matches);

/**
 * Redraw the tab suggestion without flickering
 */
void redraw_tab_suggestion(HANDLE hConsole, COORD promptEndPos, 
                           char *original_line, char *tab_match, char *last_tab_prefix,
                           int tab_index, int tab_num_matches, WORD originalAttributes);

/**
 * Display suggestion in one operation to prevent visible "typing"
 * 
 * @param hConsole Handle to the console
 * @param promptEndPos Position at the end of prompt
 * @param buffer Current input buffer
 * @param suggestion Suggestion to display
 * @param position Current cursor position in buffer
 * @param originalAttributes Original console text attributes
 */
void display_suggestion_atomically(HANDLE hConsole, COORD promptEndPos, const char *buffer, 
                                  const char *suggestion, int position, WORD originalAttributes);

#endif // TAB_COMPLETE_H
