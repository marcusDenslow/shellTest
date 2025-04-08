/**
 * tab_complete.h
 * Functions for tab completion and path suggestions
 */

#ifndef TAB_COMPLETE_H
#define TAB_COMPLETE_H

#include "common.h"

/**
 * Command context structure
 */
typedef struct {
  int is_after_pipe;         // Flag if cursor is right after a pipe
  int is_filter_command;     // Flag if current command is a filter command
  char cmd_before_pipe[64];  // Command before the pipe
  char filter_command[64];   // Current filter command
  char current_token[1024];  // Current token being typed
  int token_position;        // Position within current token
  int token_index;           // Index of current token in command
  int filter_arg_index;      // Argument index for filter command
  int has_current_field;     // Flag if current field is set
  int has_current_operator;  // Flag if current operator is set
  char current_field[64];    // Current field if known
  char current_operator[16]; // Current operator if known
} CommandContext;

typedef enum {
  ARG_TYPE_ANY,       // Any argument
  ARG_TYPE_FILE,      // Files only
  ARG_TYPE_DIRECTORY, // Directories only
  ARG_TYPE_BOOKMARK,  // Bookmark names
  ARG_TYPE_ALIAS,     // Alias names
  ARG_TYPE_BOTH,      // Both files and directories
  ARG_TYPE_FAVORITE_CITY,
  ARG_TYPE_THEME,
} ArgumentType;

/**
 * Structure for command argument type information
 */
typedef struct {
  char *command;         // Command name
  ArgumentType arg_type; // Expected argument type
  char *description;     // Optional description
} CommandArgInfo;

// Function declarations for command registry
void init_command_registry(void);
ArgumentType get_command_arg_type(const char *cmd);
void register_command(const char *cmd, ArgumentType type, const char *desc);

/**
 * Parse command context from a command line
 *
 * @param line Command line to parse
 * @param position Cursor position in the line
 * @param ctx Pointer to store the parsed context
 */
void parse_command_context(const char *line, int position, CommandContext *ctx);

/**
 * Find matching files/directories or commands for tab completion
 *
 * @param partial_text The partial text to match
 * @param is_first_word Flag indicating if this is the first word (command)
 * @param num_matches Pointer to store number of matches found
 * @return Array of matching strings (must be freed by caller)
 */
char **find_matches(const char *partial_text, int is_first_word,
                    int *num_matches);

/**
 * Find directory matches for the given partial path
 * Used for cd command tab completion
 *
 * @param partial_text The partial text to match
 * @param num_matches Pointer to store number of matches found
 * @return Array of matching directory names (must be freed by caller)
 */
char **find_directory_matches(const char *partial_text, int *num_matches);

/**
 * Find file matches for the given partial path
 * Used for cat command tab completion
 *
 * @param partial_text The partial text to match
 * @param num_matches Pointer to store number of matches found
 * @return Array of matching file names (must be freed by caller)
 */
char **find_file_matches(const char *partial_text, int *num_matches);

/**
 * Find context-aware matches based on the current command line
 *
 * @param buffer Current command buffer
 * @param position Cursor position in the buffer
 * @param partial_text Partial text to match
 * @param num_matches Pointer to store number of matches found
 * @return Array of matching strings (must be freed by caller)
 */
char **find_context_matches(const char *buffer, int position,
                            const char *partial_text, int *num_matches);

/**
 * Find context-aware suggestions based on the command hierarchy
 *
 * @param line Command line
 * @param position Cursor position in the line
 * @param num_suggestions Pointer to store number of suggestions found
 * @return Array of suggestion strings (must be freed by caller)
 */
char **find_context_suggestions(const char *line, int position,
                                int *num_suggestions);

/**
 * Find the best matching file/directory for current input
 *
 * @param partial_text The partial text to match
 * @return Best matching string (must be freed by caller)
 */
char *find_best_match(const char *partial_text);

/**
 * Find context-aware best match for current input
 *
 * @param buffer Current command buffer
 * @param position Cursor position in the buffer
 * @return Best matching string (must be freed by caller)
 */
char *find_context_best_match(const char *buffer, int position);

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
                           char *original_line, char *tab_match,
                           char *last_tab_prefix, int tab_index,
                           int tab_num_matches, WORD originalAttributes);

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

void display_suggestion_atomically(HANDLE hConsole, COORD promptEndPos,
                                   const char *buffer, const char *suggestion,
                                   int position, WORD originalAttributes,
                                   int is_history_suggestion);

#endif // TAB_COMPLETE_H
