/**
 * command_docs.h
 * Functions for retrieving and managing command documentation
 */

#ifndef COMMAND_DOCS_H
#define COMMAND_DOCS_H

#include "common.h"

// Structure to hold command documentation
typedef struct {
  char *command;      // Command name
  char *short_desc;   // Short description
  char *long_desc;    // Long description
  char **parameters;  // Array of parameter names
  char **param_descs; // Array of parameter descriptions
  int param_count;    // Number of parameters
  int is_builtin;     // Whether this is a builtin command
} CommandDoc;

/**
 * Initialize the command documentation system
 * This should be called during shell startup
 */
void init_command_docs(void);

/**
 * Clean up the command documentation system
 * This should be called during shell shutdown
 */
void cleanup_command_docs(void);

/**
 * Get documentation for a command
 *
 * @param cmd The command to get documentation for
 * @return Pointer to CommandDoc structure or NULL if not found
 */
const CommandDoc *get_command_doc(const char *cmd);

/**
 * Add documentation for a command
 *
 * @param cmd The command name
 * @param short_desc Short description
 * @param long_desc Long description
 * @param params Array of parameter strings
 * @param param_descs Array of parameter descriptions
 * @param param_count Number of parameters
 * @param is_builtin Whether this is a builtin command
 */
void add_command_doc(const char *cmd, const char *short_desc,
                     const char *long_desc, char **params, char **param_descs,
                     int param_count, int is_builtin);

/**
 * Automatically discover and load documentation for a command
 *
 * @param cmd The command to load documentation for
 * @return 1 if documentation was found, 0 otherwise
 */
int load_command_doc(const char *cmd);

/**
 * Get documentation for a specific parameter of a command
 *
 * @param cmd The command name
 * @param param The parameter name
 * @return Parameter description or NULL if not found
 */
const char *get_param_doc(const char *cmd, const char *param);

/**
 * Get list of commands with documentation matching a search term
 *
 * @param search_term The term to search for in command names and descriptions
 * @param count Pointer to store the number of results
 * @return Array of command names (caller must free)
 */
char **search_command_docs(const char *search_term, int *count);

/**
 * Get list of parameters for a command
 *
 * @param cmd The command name
 * @param count Pointer to store the number of parameters
 * @return Array of parameter names (directly from CommandDoc - do not free)
 */
const char **get_command_params(const char *cmd, int *count);

/**
 * Display command help using built-in documentation
 *
 * @param cmd The command to show help for
 * @return 1 if help was displayed, 0 if no documentation found
 */
int display_command_help(const char *cmd);

#endif // COMMAND_DOCS_H
