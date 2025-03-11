/**
 * tab_complete.h
 * Functions for tab completion and path suggestions
 */

#ifndef TAB_COMPLETE_H
#define TAB_COMPLETE_H

#include "common.h"

/**
 * Find matching files/directories for tab completion
 * 
 * @param partial_path The partial path to match
 * @param num_matches Pointer to store number of matches found
 * @return Array of matching strings (must be freed by caller)
 */
char **find_matches(const char *partial_path, int *num_matches);

/**
 * Find the best matching file/directory for current input
 * 
 * @param partial_text The partial text to match
 * @return Best matching string (must be freed by caller)
 */
char* find_best_match(const char* partial_text);

/**
 * Redraw the tab suggestion without flickering
 */
void redraw_tab_suggestion(HANDLE hConsole, COORD promptEndPos, 
                           char *original_line, char *tab_match, char *last_tab_prefix,
                           int tab_index, int tab_num_matches, WORD originalAttributes);

#endif // TAB_COMPLETE_H
