/**
 * tab_complete.c
 * Implementation of tab completion functionality with tuned command hierarchy
 */

#include "tab_complete.h"
#include "aliases.h" // Added for alias support
#include "bookmarks.h"
#include "builtins.h"        // Added to access builtin_str[]
#include "filters.h"         // Added for filter commands
#include "structured_data.h" // Added for table header information

// Define field types as constants
#define FIELD_TYPE_NAME 0
#define FIELD_TYPE_SIZE 1
#define FIELD_TYPE_TYPE 2
#define FIELD_TYPE_DATE 3
#define FIELD_TYPE_PID 4
#define FIELD_TYPE_MEMORY 5
#define FIELD_TYPE_THREADS 6
#define FIELD_TYPE_ANY 7

// Define argument types as constants
#define ARG_TYPE_FIELD 0
#define ARG_TYPE_OPERATOR 1
#define ARG_TYPE_VALUE 2
#define ARG_TYPE_DIRECTION 3
#define ARG_TYPE_PATTERN 4

#define MAX_REGISTERED_COMMANDS 50

static CommandArgInfo command_registry[MAX_REGISTERED_COMMANDS];
static int command_count = 0;

// Define these struct types:
typedef struct {
  char *command;           // Command name ("where", "sort-by", etc.)
  int *arg_types;          // Array of argument types
  int **valid_field_types; // Valid field types for each argument position
  int arg_count;           // Number of arguments
} CommandDefinition;

typedef struct {
  char *command;      // Source command ("ls", "ps", etc.)
  int *field_types;   // Array of field types
  char **field_names; // Array of field names
  int field_count;    // Number of fields
} CommandFields;

// Global arrays for command definitions and field definitions
static CommandDefinition command_defs[10]; // Allow up to 10 command definitions
static int command_def_count = 0;
static CommandFields field_defs[10]; // Allow up to 10 field source definitions
static int field_def_count = 0;

/**
 * Initialize the command hierarchy definitions
 * This is called when the shell starts
 */
void init_command_hierarchy() {
  // Initialize field definitions for each source command

  // ls/dir command fields
  static int ls_field_types[] = {FIELD_TYPE_NAME, FIELD_TYPE_SIZE,
                                 FIELD_TYPE_TYPE, FIELD_TYPE_DATE};
  static char *ls_field_names[] = {"Name", "Size", "Type", "Last Modified"};
  field_defs[field_def_count].command = "ls";
  field_defs[field_def_count].field_types = ls_field_types;
  field_defs[field_def_count].field_names = ls_field_names;
  field_defs[field_def_count].field_count =
      sizeof(ls_field_types) / sizeof(ls_field_types[0]);
  field_def_count++;

  // Use same field definitions for dir command
  field_defs[field_def_count].command = "dir";
  field_defs[field_def_count].field_types = ls_field_types;
  field_defs[field_def_count].field_names = ls_field_names;
  field_defs[field_def_count].field_count =
      sizeof(ls_field_types) / sizeof(ls_field_types[0]);
  field_def_count++;

  // ps command fields
  static int ps_field_types[] = {FIELD_TYPE_PID, FIELD_TYPE_NAME,
                                 FIELD_TYPE_MEMORY, FIELD_TYPE_THREADS};
  static char *ps_field_names[] = {"PID", "Name", "Memory", "Threads"};
  field_defs[field_def_count].command = "ps";
  field_defs[field_def_count].field_types = ps_field_types;
  field_defs[field_def_count].field_names = ps_field_names;
  field_defs[field_def_count].field_count =
      sizeof(ps_field_types) / sizeof(ps_field_types[0]);
  field_def_count++;

  // Initialize command definitions

  // where command definition - EXCLUDE Name field as specified
  static int where_arg_types[] = {ARG_TYPE_FIELD, ARG_TYPE_OPERATOR,
                                  ARG_TYPE_VALUE};
  // Define valid field types for 'where' command (Size, Type, Date) - NO Name
  static int where_valid_fields_pos0[] = {
      FIELD_TYPE_SIZE,   FIELD_TYPE_TYPE,    FIELD_TYPE_DATE, FIELD_TYPE_PID,
      FIELD_TYPE_MEMORY, FIELD_TYPE_THREADS, FIELD_TYPE_ANY};
  static int *where_field_types[] = {
      where_valid_fields_pos0, NULL,
      NULL // Position 0 only needs field validation
  };
  command_defs[command_def_count].command = "where";
  command_defs[command_def_count].arg_types = where_arg_types;
  command_defs[command_def_count].valid_field_types = where_field_types;
  command_defs[command_def_count].arg_count =
      sizeof(where_arg_types) / sizeof(where_arg_types[0]);
  command_def_count++;

  // sort-by command definition
  static int sort_by_arg_types[] = {ARG_TYPE_FIELD, ARG_TYPE_DIRECTION};
  // Define valid field types for sort-by command
  static int sort_by_valid_fields_pos0[] = {
      FIELD_TYPE_NAME, FIELD_TYPE_SIZE,   FIELD_TYPE_TYPE,    FIELD_TYPE_DATE,
      FIELD_TYPE_PID,  FIELD_TYPE_MEMORY, FIELD_TYPE_THREADS, FIELD_TYPE_ANY};
  static int *sort_by_field_types[] = {
      sort_by_valid_fields_pos0, NULL // Position 0 only needs field validation
  };
  command_defs[command_def_count].command = "sort-by";
  command_defs[command_def_count].arg_types = sort_by_arg_types;
  command_defs[command_def_count].valid_field_types = sort_by_field_types;
  command_defs[command_def_count].arg_count =
      sizeof(sort_by_arg_types) / sizeof(sort_by_arg_types[0]);
  command_def_count++;

  // contains command definition - ONLY Name fields
  static int contains_arg_types[] = {ARG_TYPE_FIELD, ARG_TYPE_PATTERN};
  // Define valid field types for contains command - only name fields
  static int contains_valid_fields_pos0[] = {FIELD_TYPE_NAME, FIELD_TYPE_ANY};
  static int *contains_field_types[] = {
      contains_valid_fields_pos0, NULL // Position 0 only needs field validation
  };
  command_defs[command_def_count].command = "contains";
  command_defs[command_def_count].arg_types = contains_arg_types;
  command_defs[command_def_count].valid_field_types = contains_field_types;
  command_defs[command_def_count].arg_count =
      sizeof(contains_arg_types) / sizeof(contains_arg_types[0]);
  command_def_count++;

  // select command definition - can take multiple fields
  static int select_arg_types[] = {ARG_TYPE_FIELD, ARG_TYPE_FIELD,
                                   ARG_TYPE_FIELD, ARG_TYPE_FIELD};
  // Define valid field types for select command - any field is valid
  static int select_valid_fields_pos0[] = {
      FIELD_TYPE_NAME, FIELD_TYPE_SIZE,   FIELD_TYPE_TYPE,    FIELD_TYPE_DATE,
      FIELD_TYPE_PID,  FIELD_TYPE_MEMORY, FIELD_TYPE_THREADS, FIELD_TYPE_ANY};
  static int *select_field_types[] = {
      select_valid_fields_pos0, select_valid_fields_pos0,
      select_valid_fields_pos0, select_valid_fields_pos0};
  command_defs[command_def_count].command = "select";
  command_defs[command_def_count].arg_types = select_arg_types;
  command_defs[command_def_count].valid_field_types = select_field_types;
  command_defs[command_def_count].arg_count =
      sizeof(select_arg_types) / sizeof(select_arg_types[0]);
  command_def_count++;

  // limit command definition
  static int limit_arg_types[] = {
      ARG_TYPE_VALUE // Simple number value
  };
  command_defs[command_def_count].command = "limit";
  command_defs[command_def_count].arg_types = limit_arg_types;
  command_defs[command_def_count].valid_field_types =
      NULL; // No field validation needed
  command_defs[command_def_count].arg_count =
      sizeof(limit_arg_types) / sizeof(limit_arg_types[0]);
  command_def_count++;
}

/**
 * Find a command definition by name
 */
CommandDefinition *find_command_def(const char *command) {
  for (int i = 0; i < command_def_count; i++) {
    if (strcasecmp(command_defs[i].command, command) == 0) {
      return &command_defs[i];
    }
  }
  return NULL;
}

/**
 * Find field definitions for a source command
 */
CommandFields *find_field_def(const char *command) {
  for (int i = 0; i < field_def_count; i++) {
    if (strcasecmp(field_defs[i].command, command) == 0) {
      return &field_defs[i];
    }
  }
  return NULL;
}

/**
 * Get context-aware suggestions for commands after a pipe
 * This returns filter commands that haven't been used yet
 */
char **get_pipe_suggestions(const char *cmd_before_pipe, char **used_commands,
                            int used_count, int *num_suggestions) {
  *num_suggestions = 0;

  // Default pipe suggestions are all filter commands
  char **suggestions = (char **)malloc(filter_count * sizeof(char *));
  if (!suggestions) {
    fprintf(stderr, "lsh: allocation error in pipe suggestions\n");
    return NULL;
  }

  // Add filter commands that haven't been used yet - ensure lowercase
  for (int i = 0; i < filter_count; i++) {
    // Check if this command has already been used
    int already_used = 0;
    for (int j = 0; j < used_count; j++) {
      if (strcasecmp(filter_str[i], used_commands[j]) == 0) {
        already_used = 1;
        break;
      }
    }

    if (!already_used) {
      // Convert to lowercase to ensure consistency
      char *cmd = _strdup(filter_str[i]);
      if (cmd) {
        for (char *p = cmd; *p; p++) {
          *p = tolower(*p);
        }
        suggestions[*num_suggestions] = cmd;
        (*num_suggestions)++;
      }
    }
  }

  return suggestions;
}

/**
 * Get field suggestions for a specific filter command and source command
 */
char **get_field_suggestions(const char *cmd_before_pipe,
                             const char *filter_cmd, int arg_position,
                             int *num_suggestions) {
  *num_suggestions = 0;

  // Find the field definitions for the source command
  CommandFields *fields = find_field_def(cmd_before_pipe);
  if (!fields) {
    // Use default fields if command not specifically defined
    static char *default_fields[] = {"Name", "Size", "Type", "Date"};
    int default_count = sizeof(default_fields) / sizeof(default_fields[0]);

    char **suggestions = (char **)malloc(default_count * sizeof(char *));
    if (!suggestions) {
      fprintf(stderr, "lsh: allocation error in field suggestions\n");
      return NULL;
    }

    for (int i = 0; i < default_count; i++) {
      suggestions[*num_suggestions] = _strdup(default_fields[i]);
      (*num_suggestions)++;
    }

    return suggestions;
  }

  // Find the command definition to check valid field types
  CommandDefinition *cmd_def = find_command_def(filter_cmd);
  if (!cmd_def || arg_position >= cmd_def->arg_count ||
      cmd_def->arg_types[arg_position] != ARG_TYPE_FIELD) {
    // Not a field position or unknown command
    return NULL;
  }

  // Check if this command has field type restrictions for this position
  int *valid_types = NULL;
  int valid_count = 0;

  if (cmd_def->valid_field_types && cmd_def->valid_field_types[arg_position]) {
    valid_types = cmd_def->valid_field_types[arg_position];
    // Count valid types
    while (valid_types[valid_count] != FIELD_TYPE_ANY) {
      valid_count++;
    }
  }

  // Allocate suggestions array for maximum possible size
  char **suggestions = (char **)malloc(fields->field_count * sizeof(char *));
  if (!suggestions) {
    fprintf(stderr, "lsh: allocation error in field suggestions\n");
    return NULL;
  }

  // Add field suggestions that match the allowed types
  for (int i = 0; i < fields->field_count; i++) {
    if (!valid_types) {
      // No restrictions, add all fields
      suggestions[*num_suggestions] = _strdup(fields->field_names[i]);
      (*num_suggestions)++;
    } else {
      // Check if this field's type is in the valid types list
      int field_type = fields->field_types[i];
      for (int j = 0; j < valid_count; j++) {
        if (field_type == valid_types[j]) {
          suggestions[*num_suggestions] = _strdup(fields->field_names[i]);
          (*num_suggestions)++;
          break;
        }
      }
    }
  }

  return suggestions;
}

/**
 * Get operator suggestions based on the field type
 */
char **get_operator_suggestions(const char *field, const char *cmd_before_pipe,
                                int *num_suggestions) {
  *num_suggestions = 0;

  // Find the field type
  int field_type = FIELD_TYPE_ANY;
  CommandFields *fields = find_field_def(cmd_before_pipe);

  if (fields) {
    for (int i = 0; i < fields->field_count; i++) {
      if (strcasecmp(fields->field_names[i], field) == 0) {
        field_type = fields->field_types[i];
        break;
      }
    }
  }

  // Define operators based on field type
  const char **operators;
  int operator_count;

  if (field_type == FIELD_TYPE_SIZE || field_type == FIELD_TYPE_MEMORY ||
      field_type == FIELD_TYPE_PID) {
    // Numeric fields use all operators
    static const char *numeric_ops[] = {">", "<", "==", ">=", "<="};
    operators = numeric_ops;
    operator_count = sizeof(numeric_ops) / sizeof(numeric_ops[0]);
  } else if (field_type == FIELD_TYPE_NAME || field_type == FIELD_TYPE_TYPE) {
    // String fields primarily use equality
    static const char *string_ops[] = {"=="};
    operators = string_ops;
    operator_count = sizeof(string_ops) / sizeof(string_ops[0]);
  } else {
    // Default operators
    static const char *default_ops[] = {">", "<", "==", ">=", "<="};
    operators = default_ops;
    operator_count = sizeof(default_ops) / sizeof(default_ops[0]);
  }

  // Create suggestions
  char **suggestions = (char **)malloc(operator_count * sizeof(char *));
  if (!suggestions) {
    fprintf(stderr, "lsh: allocation error in operator suggestions\n");
    return NULL;
  }

  for (int i = 0; i < operator_count; i++) {
    suggestions[*num_suggestions] = _strdup(operators[i]);
    (*num_suggestions)++;
  }

  return suggestions;
}

/**
 * Get value suggestions based on the field and operator
 */
char **get_value_suggestions(const char *field, const char *operator,
                             const char * cmd_before_pipe,
                             int *num_suggestions) {
  *num_suggestions = 0;

  // Find the field type
  int field_type = FIELD_TYPE_ANY;
  CommandFields *fields = find_field_def(cmd_before_pipe);

  if (fields) {
    for (int i = 0; i < fields->field_count; i++) {
      if (strcasecmp(fields->field_names[i], field) == 0) {
        field_type = fields->field_types[i];
        break;
      }
    }
  }

  // Define values based on field type
  const char **values;
  int value_count;

  if (field_type == FIELD_TYPE_SIZE || field_type == FIELD_TYPE_MEMORY) {
    // Size field values
    static const char *size_values[] = {"1KB",  "10KB",  "100KB", "1MB",
                                        "10MB", "100MB", "1GB"};
    values = size_values;
    value_count = sizeof(size_values) / sizeof(size_values[0]);
  } else if (field_type == FIELD_TYPE_TYPE) {
    // Type field values
    static const char *type_values[] = {"File", "Directory"};
    values = type_values;
    value_count = sizeof(type_values) / sizeof(type_values[0]);
  } else if (field_type == FIELD_TYPE_PID || field_type == FIELD_TYPE_THREADS) {
    // Numeric field values
    static const char *numeric_values[] = {"0", "1", "5", "10", "100", "1000"};
    values = numeric_values;
    value_count = sizeof(numeric_values) / sizeof(numeric_values[0]);
  } else {
    // Default empty values
    static const char *empty_values[] = {""};
    values = empty_values;
    value_count = 0;
  }

  // Create suggestions
  char **suggestions =
      (char **)malloc((value_count > 0 ? value_count : 1) * sizeof(char *));
  if (!suggestions) {
    fprintf(stderr, "lsh: allocation error in value suggestions\n");
    return NULL;
  }

  for (int i = 0; i < value_count; i++) {
    suggestions[*num_suggestions] = _strdup(values[i]);
    (*num_suggestions)++;
  }

  return suggestions;
}

/**
 * Get direction suggestions (asc/desc)
 */
char **get_direction_suggestions(int *num_suggestions) {
  *num_suggestions = 2;

  char **suggestions = (char **)malloc(2 * sizeof(char *));
  if (!suggestions) {
    fprintf(stderr, "lsh: allocation error in direction suggestions\n");
    *num_suggestions = 0;
    return NULL;
  }

  suggestions[0] = _strdup("asc");
  suggestions[1] = _strdup("desc");

  return suggestions;
}

/**
 * Parse command tokens from a command line
 */
void parse_command_tokens(const char *line, int position, char ***tokens,
                          int *token_count) {
  *token_count = 0;
  *tokens = NULL;

  if (!line || position <= 0) {
    return;
  }

  // Make a copy of the line up to the cursor position
  char *partial_line = (char *)malloc((position + 1) * sizeof(char));
  if (!partial_line) {
    return;
  }

  strncpy(partial_line, line, position);
  partial_line[position] = '\0';

  // Count potential tokens
  int max_tokens = 0;
  const char *p = partial_line;
  int in_token = 0;

  while (*p) {
    if (*p == '|') {
      if (in_token) {
        max_tokens++;
        in_token = 0;
      }
      // Count pipe as a token
      max_tokens++;
    } else if (isspace(*p)) {
      if (in_token) {
        max_tokens++;
        in_token = 0;
      }
    } else if (!in_token) {
      in_token = 1;
    }
    p++;
  }

  // Add one for any potential token at cursor
  if (in_token) {
    max_tokens++;
  }

  // Allocate token array
  *tokens = (char **)malloc(max_tokens * sizeof(char *));
  if (!(*tokens)) {
    free(partial_line);
    return;
  }

  // Extract tokens
  p = partial_line;
  in_token = 0;
  int token_start = 0;

  while (*p) {
    if (*p == '|') {
      if (in_token) {
        // End the current token
        int len = p - partial_line - token_start;
        char *token = (char *)malloc((len + 1) * sizeof(char));
        strncpy(token, partial_line + token_start, len);
        token[len] = '\0';
        (*tokens)[*token_count] = token;
        (*token_count)++;
        in_token = 0;
      }

      // Add pipe as a token
      char *pipe_token = (char *)malloc(2 * sizeof(char));
      pipe_token[0] = '|';
      pipe_token[1] = '\0';
      (*tokens)[*token_count] = pipe_token;
      (*token_count)++;
    } else if (isspace(*p)) {
      if (in_token) {
        // End the current token
        int len = p - partial_line - token_start;
        char *token = (char *)malloc((len + 1) * sizeof(char));
        strncpy(token, partial_line + token_start, len);
        token[len] = '\0';
        (*tokens)[*token_count] = token;
        (*token_count)++;
        in_token = 0;
      }
    } else if (!in_token) {
      // Start a new token
      token_start = p - partial_line;
      in_token = 1;
    }
    p++;
  }

  // Handle any final token
  if (in_token) {
    int len = p - partial_line - token_start;
    char *token = (char *)malloc((len + 1) * sizeof(char));
    strncpy(token, partial_line + token_start, len);
    token[len] = '\0';
    (*tokens)[*token_count] = token;
    (*token_count)++;
  }

  free(partial_line);
}

/**
 * Parse command line to get context for suggestions with proper hierarchy
 * awareness
 */
void parse_command_context(const char *line, int position,
                           CommandContext *ctx) {
  // Static initialization of command hierarchy
  static int hierarchy_initialized = 0;
  if (!hierarchy_initialized) {
    init_command_hierarchy();
    hierarchy_initialized = 1;
  }

  // Initialize context
  ctx->is_after_pipe = 0;
  ctx->is_filter_command = 0;
  ctx->cmd_before_pipe[0] = '\0';
  ctx->current_token[0] = '\0';
  ctx->token_position = 0;
  ctx->token_index = 0;
  ctx->filter_arg_index = 0;
  ctx->has_current_field = 0;
  ctx->has_current_operator = 0;
  ctx->current_field[0] = '\0';
  ctx->current_operator[0] = '\0';

  // Check for empty line
  if (!line || position <= 0) {
    return;
  }

  // Extract tokens from the command line
  char **tokens;
  int token_count;
  parse_command_tokens(line, position, &tokens, &token_count);

  if (!tokens || token_count == 0) {
    return;
  }

  // Find the last pipe and command before it
  int last_pipe_idx = -1;
  for (int i = 0; i < token_count; i++) {
    if (strcmp(tokens[i], "|") == 0) {
      last_pipe_idx = i;

      // Get command before pipe
      if (i > 0 && tokens[i - 1][0] != '|') {
        strncpy(ctx->cmd_before_pipe, tokens[i - 1],
                sizeof(ctx->cmd_before_pipe) - 1);
        ctx->cmd_before_pipe[sizeof(ctx->cmd_before_pipe) - 1] = '\0';
      }
    }
  }

  // Determine if cursor is right after a pipe
  if (last_pipe_idx >= 0 && last_pipe_idx == token_count - 1) {
    ctx->is_after_pipe = 1;
  }

  // If not after pipe, check if in filter command
  if (!ctx->is_after_pipe && last_pipe_idx >= 0 &&
      last_pipe_idx < token_count - 1) {
    // Check if token after pipe is a filter command
    for (int i = 0; i < filter_count; i++) {
      if (strcasecmp(tokens[last_pipe_idx + 1], filter_str[i]) == 0) {
        ctx->is_filter_command = 1;
        strncpy(ctx->filter_command, filter_str[i],
                sizeof(ctx->filter_command) - 1);
        ctx->filter_command[sizeof(ctx->filter_command) - 1] = '\0';

        // Ensure filter command is lowercase
        for (char *p = ctx->filter_command; *p; p++) {
          *p = tolower(*p);
        }

        // Count arguments after filter command
        int arg_count = 0;
        for (int j = last_pipe_idx + 2; j < token_count; j++) {
          if (tokens[j][0] != '|') {
            arg_count++;

            // Store field and operator if available
            if (arg_count == 1) {
              ctx->has_current_field = 1;
              strncpy(ctx->current_field, tokens[j],
                      sizeof(ctx->current_field) - 1);
              ctx->current_field[sizeof(ctx->current_field) - 1] = '\0';
            } else if (arg_count == 2) {
              ctx->has_current_operator = 1;
              strncpy(ctx->current_operator, tokens[j],
                      sizeof(ctx->current_operator) - 1);
              ctx->current_operator[sizeof(ctx->current_operator) - 1] = '\0';
            }
          }
        }

        ctx->filter_arg_index = arg_count;
        break;
      }
    }
  }

  // Extract the current token and its position
  if (position > 0) {
    int token_start = position - 1;
    while (token_start >= 0 && !isspace(line[token_start]) &&
           line[token_start] != '|') {
      token_start--;
    }
    token_start++; // Move past space or pipe

    ctx->token_position = position - token_start;
    if (token_start < position) {
      strncpy(ctx->current_token, line + token_start, ctx->token_position);
      ctx->current_token[ctx->token_position] = '\0';
    }
  }

  // Clean up
  if (tokens) {
    for (int i = 0; i < token_count; i++) {
      free(tokens[i]);
    }
    free(tokens);
  }
}

/**
 * Find matching files/directories or commands for tab completion
 */
char **find_matches(const char *partial_text, int is_first_word,
                    int *num_matches) {
  char cwd[1024];
  char search_dir[1024] = "";
  char search_pattern[256] = "";
  char **matches = NULL;
  int matches_capacity = 10;
  *num_matches = 0;

  if (!partial_text) {
    return NULL;
  }

  // Allocate initial array for matches
  matches = (char **)malloc(sizeof(char *) * matches_capacity);
  if (!matches) {
    fprintf(stderr, "lsh: allocation error in tab completion\n");
    return NULL;
  }

  // If it's the first word, try to match against aliases and commands first
  if (is_first_word) {
    // First check aliases
    int alias_match_count = 0;
    char **alias_matches = get_alias_names(&alias_match_count);

    if (alias_matches && alias_match_count > 0) {
      for (int i = 0; i < alias_match_count; i++) {
        // Check if alias starts with the partial_text (case insensitive)
        if (_strnicmp(alias_matches[i], partial_text, strlen(partial_text)) ==
            0) {
          // Add alias to matches
          if (*num_matches >= matches_capacity) {
            matches_capacity *= 2;
            matches =
                (char **)realloc(matches, sizeof(char *) * matches_capacity);
            if (!matches) {
              fprintf(stderr, "lsh: allocation error in tab completion\n");
              for (int j = 0; j < alias_match_count; j++) {
                free(alias_matches[j]);
              }
              free(alias_matches);
              return NULL;
            }
          }

          matches[*num_matches] = _strdup(alias_matches[i]);
          (*num_matches)++;
        }
      }

      // Clean up alias matches
      for (int i = 0; i < alias_match_count; i++) {
        free(alias_matches[i]);
      }
      free(alias_matches);
    }

    // Then check built-in commands
    for (int i = 0; i < lsh_num_builtins(); i++) {
      // Check if command starts with the partial_text (case insensitive)
      if (_strnicmp(builtin_str[i], partial_text, strlen(partial_text)) == 0) {
        // Add command to matches
        if (*num_matches >= matches_capacity) {
          matches_capacity *= 2;
          matches =
              (char **)realloc(matches, sizeof(char *) * matches_capacity);
          if (!matches) {
            fprintf(stderr, "lsh: allocation error in tab completion\n");
            return NULL;
          }
        }

        matches[*num_matches] = _strdup(builtin_str[i]);
        (*num_matches)++;
      }
    }

    // If we found matches, return them
    if (*num_matches > 0) {
      return matches;
    }

    // Otherwise, fall back to file/directory matching
  }

  // Parse the partial path to separate directory and pattern
  char *last_slash = strrchr(partial_text, '\\');
  if (last_slash) {
    // There's a directory part
    int dir_len = last_slash - partial_text + 1;
    strncpy(search_dir, partial_text, dir_len);
    search_dir[dir_len] = '\0';
    strcpy(search_pattern, last_slash + 1);
  } else {
    // No directory specified, use current directory
    _getcwd(cwd, sizeof(cwd));
    strcpy(search_dir, cwd);
    strcat(search_dir, "\\");
    strcpy(search_pattern, partial_text);
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
    if (strcmp(findData.cFileName, ".") == 0 ||
        strcmp(findData.cFileName, "..") == 0) {
      continue;
    }

    // Check if file matches our pattern (case insensitive)
    if (_strnicmp(findData.cFileName, search_pattern, strlen(search_pattern)) ==
        0) {
      // Add to matches
      if (*num_matches >= matches_capacity) {
        matches_capacity *= 2;
        matches = (char **)realloc(matches, sizeof(char *) * matches_capacity);
        if (!matches) {
          fprintf(stderr, "lsh: allocation error in tab completion\n");
          FindClose(hFind);
          return NULL;
        }
      }

      // Just copy the filename without adding backslash for directories
      matches[*num_matches] = _strdup(findData.cFileName);
      (*num_matches)++;
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);

  return matches;
}

/**
 * Find context-aware suggestions based on the command hierarchy
 */
char **find_context_suggestions(const char *line, int position,
                                int *num_suggestions) {
  CommandContext ctx;
  parse_command_context(line, position, &ctx);

  *num_suggestions = 0;

  // Get used commands to avoid suggesting them again
  char **tokens;
  int token_count;
  parse_command_tokens(line, position, &tokens, &token_count);

  char **used_commands = NULL;
  int used_count = 0;

  if (tokens) {
    // Count filter commands already used
    for (int i = 0; i < token_count; i++) {
      for (int j = 0; j < filter_count; j++) {
        if (strcasecmp(tokens[i], filter_str[j]) == 0) {
          used_count++;
        }
      }
    }

    // Collect used filter commands
    if (used_count > 0) {
      used_commands = (char **)malloc(used_count * sizeof(char *));
      if (used_commands) {
        used_count = 0;

        for (int i = 0; i < token_count; i++) {
          for (int j = 0; j < filter_count; j++) {
            if (strcasecmp(tokens[i], filter_str[j]) == 0) {
              used_commands[used_count++] = tokens[i];
            }
          }
        }
      }
    }
  }

  // Check if we're after a pipe
  if (ctx.is_after_pipe) {
    char **suggestions = get_pipe_suggestions(
        ctx.cmd_before_pipe, used_commands, used_count, num_suggestions);

    // Clean up
    if (tokens) {
      for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
      }
      free(tokens);
    }
    if (used_commands) {
      free(used_commands);
    }

    return suggestions;
  }

  // Clean up tokens as we don't need them anymore
  if (tokens) {
    for (int i = 0; i < token_count; i++) {
      free(tokens[i]);
    }
    free(tokens);
  }
  if (used_commands) {
    free(used_commands);
  }

  // Check if we're in a filter command
  if (ctx.is_filter_command) {
    // Find command definition
    CommandDefinition *cmd_def = find_command_def(ctx.filter_command);
    if (!cmd_def || ctx.filter_arg_index >= cmd_def->arg_count) {
      return NULL;
    }

    // Determine suggestion type based on command definition
    int arg_type = cmd_def->arg_types[ctx.filter_arg_index];

    switch (arg_type) {
    case ARG_TYPE_FIELD:
      return get_field_suggestions(ctx.cmd_before_pipe, ctx.filter_command,
                                   ctx.filter_arg_index, num_suggestions);

    case ARG_TYPE_OPERATOR:
      return get_operator_suggestions(ctx.current_field, ctx.cmd_before_pipe,
                                      num_suggestions);

    case ARG_TYPE_VALUE:
      return get_value_suggestions(ctx.current_field, ctx.current_operator,
                                   ctx.cmd_before_pipe, num_suggestions);

    case ARG_TYPE_DIRECTION:
      return get_direction_suggestions(num_suggestions);

    case ARG_TYPE_PATTERN:
      // Pattern is free-form text, so no specific suggestions
      return NULL;
    }
  }

  // Default - suggest based on partial text
  return find_matches(ctx.current_token, ctx.token_index == 0, num_suggestions);
}

#include "bookmarks.h" // Added for bookmark support

/**
 * Find context-aware matches based on the current command line
 * Enhanced to use the command registry for better suggestions
 */
char **find_context_matches(const char *buffer, int position,
                            const char *partial_text, int *num_matches) {
  // Initialize command registry if not done already
  init_command_registry();

  CommandContext ctx;
  parse_command_context(buffer, position, &ctx);

  *num_matches = 0;

  // Extract command name and determine if we're in argument mode
  char cmd[64] = "";
  int is_argument = 0;
  int cmd_end = 0;

  // Find the first word (the command)
  while (cmd_end < position && !isspace(buffer[cmd_end])) {
    cmd_end++;
  }

  // Extract the command
  if (cmd_end > 0 && cmd_end < sizeof(cmd)) {
    strncpy(cmd, buffer, cmd_end);
    cmd[cmd_end] = '\0';
  }

  // Check if we're past the command and at least one space
  if (cmd_end > 0 && cmd_end < position && isspace(buffer[cmd_end])) {
    is_argument = 1;
  }

  // If we're in argument mode, handle context-specific suggestions
  if (is_argument) {
    // Get the argument type for this command
    ArgumentType arg_type = get_command_arg_type(cmd);

    // Handle based on argument type
    switch (arg_type) {
    case ARG_TYPE_BOOKMARK: {
      int bookmark_count;
      char **bookmark_names = get_bookmark_names(&bookmark_count);

      if (bookmark_names && bookmark_count > 0) {
        // Filter bookmark names by prefix if any
        int match_count = 0;
        char **matches = (char **)malloc(bookmark_count * sizeof(char *));

        if (!matches) {
          for (int i = 0; i < bookmark_count; i++) {
            free(bookmark_names[i]);
          }
          free(bookmark_names);
          *num_matches = 0;
          return NULL;
        }

        for (int i = 0; i < bookmark_count; i++) {
          if (partial_text[0] == '\0' ||
              _strnicmp(bookmark_names[i], partial_text,
                        strlen(partial_text)) == 0) {
            matches[match_count++] = _strdup(bookmark_names[i]);
          }
        }

        // Free the original bookmark names
        for (int i = 0; i < bookmark_count; i++) {
          free(bookmark_names[i]);
        }
        free(bookmark_names);

        *num_matches = match_count;
        return matches;
      }
    }
      return NULL;

    case ARG_TYPE_DIRECTORY:
      return find_directory_matches(partial_text, num_matches);

    case ARG_TYPE_FILE:
      return find_file_matches(partial_text, num_matches);

    case ARG_TYPE_ALIAS: {
      int alias_count;
      char **alias_names = get_alias_names(&alias_count);

      if (alias_names && alias_count > 0) {
        // Filter alias names by prefix if any
        int match_count = 0;
        char **matches = (char **)malloc(alias_count * sizeof(char *));

        if (!matches) {
          for (int i = 0; i < alias_count; i++) {
            free(alias_names[i]);
          }
          free(alias_names);
          *num_matches = 0;
          return NULL;
        }

        for (int i = 0; i < alias_count; i++) {
          if (partial_text[0] == '\0' || _strnicmp(alias_names[i], partial_text,
                                                   strlen(partial_text)) == 0) {
            matches[match_count++] = _strdup(alias_names[i]);
          }
        }

        // Free the original alias names
        for (int i = 0; i < alias_count; i++) {
          free(alias_names[i]);
        }
        free(alias_names);

        *num_matches = match_count;
        return matches;
      }
    }
      return NULL;

    case ARG_TYPE_BOTH:
    case ARG_TYPE_ANY:
    default:
      // Default for other commands - suggest both files and directories
      return find_matches(partial_text, 0, num_matches);
    }
  }

  // Check if we're after a pipe
  if (ctx.is_after_pipe) {
    char **used_commands = NULL;
    int used_count = 0;

    // Get used commands to avoid suggesting them again
    char **tokens;
    int token_count;
    parse_command_tokens(buffer, position, &tokens, &token_count);

    // Collect used filter commands
    if (tokens) {
      // Count filter commands already used
      for (int i = 0; i < token_count; i++) {
        for (int j = 0; j < filter_count; j++) {
          if (strcasecmp(tokens[i], filter_str[j]) == 0) {
            used_count++;
          }
        }
      }

      // Collect used filter commands
      if (used_count > 0) {
        used_commands = (char **)malloc(used_count * sizeof(char *));
        if (used_commands) {
          used_count = 0;

          for (int i = 0; i < token_count; i++) {
            for (int j = 0; j < filter_count; j++) {
              if (strcasecmp(tokens[i], filter_str[j]) == 0) {
                used_commands[used_count++] = tokens[i];
              }
            }
          }
        }
      }

      // Clean up tokens
      for (int i = 0; i < token_count; i++) {
        free(tokens[i]);
      }
      free(tokens);
    }

    char **suggestions = get_pipe_suggestions(
        ctx.cmd_before_pipe, used_commands, used_count, num_matches);

    // Clean up
    if (used_commands) {
      free(used_commands);
    }

    return suggestions;
  }

  // Check if we're in a filter command
  if (ctx.is_filter_command) {
    // Find command definition
    CommandDefinition *cmd_def = find_command_def(ctx.filter_command);
    if (!cmd_def || ctx.filter_arg_index >= cmd_def->arg_count) {
      return NULL;
    }

    // Determine suggestion type based on command definition
    int arg_type = cmd_def->arg_types[ctx.filter_arg_index];

    switch (arg_type) {
    case ARG_TYPE_FIELD:
      return get_field_suggestions(ctx.cmd_before_pipe, ctx.filter_command,
                                   ctx.filter_arg_index, num_matches);

    case ARG_TYPE_OPERATOR:
      return get_operator_suggestions(ctx.current_field, ctx.cmd_before_pipe,
                                      num_matches);

    case ARG_TYPE_VALUE:
      return get_value_suggestions(ctx.current_field, ctx.current_operator,
                                   ctx.cmd_before_pipe, num_matches);

    case ARG_TYPE_DIRECTION:
      return get_direction_suggestions(num_matches);

    case ARG_TYPE_PATTERN:
      // Pattern is free-form text, so no specific suggestions
      return NULL;
    }
  }

  // Default - suggest based on partial text - commands only if at beginning of
  // line
  return find_matches(partial_text, position == strlen(partial_text),
                      num_matches);
}

/**
 * Find directory matches for the given partial path
 * Used for cd command tab completion
 */
char **find_directory_matches(const char *partial_text, int *num_matches) {
  char cwd[1024];
  char search_dir[1024] = "";
  char search_pattern[256] = "";
  char **matches = NULL;
  int matches_capacity = 10;
  *num_matches = 0;

  if (!partial_text) {
    return NULL;
  }

  // Allocate initial array for matches
  matches = (char **)malloc(sizeof(char *) * matches_capacity);
  if (!matches) {
    fprintf(stderr, "lsh: allocation error in directory tab completion\n");
    return NULL;
  }

  // Parse the partial path to separate directory and pattern
  char *last_slash = strrchr(partial_text, '\\');
  if (last_slash) {
    // There's a directory part
    int dir_len = last_slash - partial_text + 1;
    strncpy(search_dir, partial_text, dir_len);
    search_dir[dir_len] = '\0';
    strcpy(search_pattern, last_slash + 1);
  } else {
    // No directory specified, use current directory
    _getcwd(cwd, sizeof(cwd));
    strcpy(search_dir, cwd);
    strcat(search_dir, "\\");
    strcpy(search_pattern, partial_text);
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

  // Find all matching directories
  do {
    // Skip . and .. directories
    if (strcmp(findData.cFileName, ".") == 0 ||
        strcmp(findData.cFileName, "..") == 0) {
      continue;
    }

    // Only include directories
    if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      // Check if directory matches our pattern (case insensitive)
      if (_strnicmp(findData.cFileName, search_pattern,
                    strlen(search_pattern)) == 0) {
        // Add to matches
        if (*num_matches >= matches_capacity) {
          matches_capacity *= 2;
          matches =
              (char **)realloc(matches, sizeof(char *) * matches_capacity);
          if (!matches) {
            fprintf(stderr,
                    "lsh: allocation error in directory tab completion\n");
            FindClose(hFind);
            return NULL;
          }
        }

        // Just copy the filename
        matches[*num_matches] = _strdup(findData.cFileName);
        (*num_matches)++;
      }
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);

  return matches;
}

/**
 * Find file matches for the given partial path
 * Used for cat command tab completion
 */
char **find_file_matches(const char *partial_text, int *num_matches) {
  char cwd[1024];
  char search_dir[1024] = "";
  char search_pattern[256] = "";
  char **matches = NULL;
  int matches_capacity = 10;
  *num_matches = 0;

  if (!partial_text) {
    return NULL;
  }

  // Allocate initial array for matches
  matches = (char **)malloc(sizeof(char *) * matches_capacity);
  if (!matches) {
    fprintf(stderr, "lsh: allocation error in file tab completion\n");
    return NULL;
  }

  // Parse the partial path to separate directory and pattern
  char *last_slash = strrchr(partial_text, '\\');
  if (last_slash) {
    // There's a directory part
    int dir_len = last_slash - partial_text + 1;
    strncpy(search_dir, partial_text, dir_len);
    search_dir[dir_len] = '\0';
    strcpy(search_pattern, last_slash + 1);
  } else {
    // No directory specified, use current directory
    _getcwd(cwd, sizeof(cwd));
    strcpy(search_dir, cwd);
    strcat(search_dir, "\\");
    strcpy(search_pattern, partial_text);
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

  // Find all matching files (prioritize files over directories)
  do {
    // Skip . and .. directories
    if (strcmp(findData.cFileName, ".") == 0 ||
        strcmp(findData.cFileName, "..") == 0) {
      continue;
    }

    // Check if file matches our pattern (case insensitive)
    if (_strnicmp(findData.cFileName, search_pattern, strlen(search_pattern)) ==
        0) {
      // Add to matches - prioritize files
      if (*num_matches >= matches_capacity) {
        matches_capacity *= 2;
        matches = (char **)realloc(matches, sizeof(char *) * matches_capacity);
        if (!matches) {
          fprintf(stderr, "lsh: allocation error in file tab completion\n");
          FindClose(hFind);
          return NULL;
        }
      }

      // Prioritize files by adding them first
      if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        matches[*num_matches] = _strdup(findData.cFileName);
        (*num_matches)++;
      }
    }
  } while (FindNextFile(hFind, &findData));

  FindClose(hFind);

  // If we didn't find any files, run the search again to include directories as
  // fallback
  if (*num_matches == 0) {
    hFind = FindFirstFile(search_path, &findData);

    if (hFind != INVALID_HANDLE_VALUE) {
      do {
        // Skip . and .. directories
        if (strcmp(findData.cFileName, ".") == 0 ||
            strcmp(findData.cFileName, "..") == 0) {
          continue;
        }

        // This time include directories too
        if (_strnicmp(findData.cFileName, search_pattern,
                      strlen(search_pattern)) == 0) {
          if (*num_matches >= matches_capacity) {
            matches_capacity *= 2;
            matches =
                (char **)realloc(matches, sizeof(char *) * matches_capacity);
            if (!matches) {
              fprintf(stderr, "lsh: allocation error in file tab completion\n");
              FindClose(hFind);
              return NULL;
            }
          }

          matches[*num_matches] = _strdup(findData.cFileName);
          (*num_matches)++;
        }
      } while (FindNextFile(hFind, &findData));

      FindClose(hFind);
    }
  }

  return matches;
}

/**
 * Find the best match for current input
 */
char *find_best_match(const char *partial_text) {
  // Start from the beginning of the current word
  int len = strlen(partial_text);
  if (len == 0)
    return NULL;

  // Find the start of the current word
  int word_start = len - 1;
  while (word_start >= 0 && partial_text[word_start] != ' ' &&
         partial_text[word_start] != '\\') {
    word_start--;
  }
  word_start++; // Move past the space or backslash

  // Extract the current word
  char partial_path[1024] = "";
  strncpy(partial_path, partial_text + word_start, len - word_start);
  partial_path[len - word_start] = '\0';

  // Skip if we're not typing a path
  if (strlen(partial_path) == 0)
    return NULL;

  // Check if this is the first word (command)
  int is_first_word = (word_start == 0);

  // Find matches
  int num_matches;
  char **matches = find_matches(partial_path, is_first_word, &num_matches);

  if (matches && num_matches > 0) {
    // Create the full suggestion by combining the prefix with the matched path
    char *full_suggestion =
        (char *)malloc(len + strlen(matches[0]) - strlen(partial_path) + 1);
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
 * Find context-aware best match for current input using the command registry
 */
char *find_context_best_match(const char *buffer, int position) {
  // If empty buffer, nothing to suggest
  if (position == 0)
    return NULL;

  // Initialize command registry if needed
  init_command_registry();

  // Check if we've already typed a command and are now typing an argument
  int is_argument = 0;
  char cmd[64] = "";
  int cmd_end = 0;

  // Find the first word (the command)
  while (cmd_end < position && !isspace(buffer[cmd_end])) {
    cmd_end++;
  }

  // Extract the command
  if (cmd_end > 0 && cmd_end < sizeof(cmd)) {
    strncpy(cmd, buffer, cmd_end);
    cmd[cmd_end] = '\0';
  }

  // Check if we're past the command and at least one space
  if (cmd_end > 0 && cmd_end < position && isspace(buffer[cmd_end])) {
    is_argument = 1;
  }

  // Find the start of the current word
  int word_start = position - 1;
  while (word_start >= 0 && !isspace(buffer[word_start]) &&
         buffer[word_start] != '\\' && buffer[word_start] != '|') {
    word_start--;
  }
  word_start++; // Move past the space, backslash, or pipe

  // Extract the current partial word
  char partial_word[1024] = "";
  strncpy(partial_word, buffer + word_start, position - word_start);
  partial_word[position - word_start] = '\0';

  // If we're in argument mode, handle differently based on command
  if (is_argument) {
    // Get argument type for the command
    ArgumentType arg_type = get_command_arg_type(cmd);

    // Handle based on argument type
    switch (arg_type) {
    case ARG_TYPE_BOOKMARK: {
      int bookmark_count;
      char **bookmark_names = get_bookmark_names(&bookmark_count);
      char *best_match = NULL;

      if (bookmark_names && bookmark_count > 0) {
        // Find the best matching bookmark
        for (int i = 0; i < bookmark_count; i++) {
          if (_strnicmp(bookmark_names[i], partial_word,
                        strlen(partial_word)) == 0) {
            // Create full suggestion with command and matched bookmark
            best_match =
                (char *)malloc(strlen(buffer) + strlen(bookmark_names[i]) + 1);
            if (best_match) {
              // Copy everything up to the current word
              strncpy(best_match, buffer, word_start);
              best_match[word_start] = '\0';
              // Add the matched bookmark name
              strcat(best_match, bookmark_names[i]);
            }
            break;
          }
        }

        // Free bookmark names
        for (int i = 0; i < bookmark_count; i++) {
          free(bookmark_names[i]);
        }
        free(bookmark_names);

        return best_match;
      }
    } break;

    case ARG_TYPE_DIRECTORY: {
      // Custom directory-only search
      WIN32_FIND_DATA findData;
      HANDLE hFind;
      char search_path[1024];
      char *best_match = NULL;

      // Parse the partial path to separate directory and pattern
      char search_dir[1024] = "";
      char search_pattern[256] = "";

      char *last_slash = strrchr(partial_word, '\\');
      if (last_slash) {
        // There's a directory part
        int dir_len = last_slash - partial_word + 1;
        strncpy(search_dir, partial_word, dir_len);
        search_dir[dir_len] = '\0';
        strcpy(search_pattern, last_slash + 1);
      } else {
        // No directory specified, use current directory
        char cwd[1024];
        _getcwd(cwd, sizeof(cwd));
        strcpy(search_dir, cwd);
        strcat(search_dir, "\\");
        strcpy(search_pattern, partial_word);
      }

      // Prepare search path
      strcpy(search_path, search_dir);
      strcat(search_path, "*");

      hFind = FindFirstFile(search_path, &findData);
      if (hFind != INVALID_HANDLE_VALUE) {
        do {
          // Skip . and .. directories
          if (strcmp(findData.cFileName, ".") == 0 ||
              strcmp(findData.cFileName, "..") == 0) {
            continue;
          }

          // Only include directories
          if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (_strnicmp(findData.cFileName, search_pattern,
                          strlen(search_pattern)) == 0) {
              // We found a matching directory
              best_match = (char *)malloc(strlen(buffer) +
                                          strlen(findData.cFileName) + 1);
              if (best_match) {
                strncpy(best_match, buffer, word_start);
                best_match[word_start] = '\0';
                strcat(best_match, findData.cFileName);
                break;
              }
            }
          }
        } while (FindNextFile(hFind, &findData));

        FindClose(hFind);
        if (best_match) {
          return best_match;
        }
      }
    } break;

    case ARG_TYPE_FILE: {
      // Search for files
      WIN32_FIND_DATA findData;
      HANDLE hFind;
      char search_path[1024];
      char *best_match = NULL;

      // Parse the partial path
      char search_dir[1024] = "";
      char search_pattern[256] = "";

      char *last_slash = strrchr(partial_word, '\\');
      if (last_slash) {
        int dir_len = last_slash - partial_word + 1;
        strncpy(search_dir, partial_word, dir_len);
        search_dir[dir_len] = '\0';
        strcpy(search_pattern, last_slash + 1);
      } else {
        char cwd[1024];
        _getcwd(cwd, sizeof(cwd));
        strcpy(search_dir, cwd);
        strcat(search_dir, "\\");
        strcpy(search_pattern, partial_word);
      }

      strcpy(search_path, search_dir);
      strcat(search_path, "*");

      hFind = FindFirstFile(search_path, &findData);
      if (hFind != INVALID_HANDLE_VALUE) {
        do {
          // Skip . and .. directories
          if (strcmp(findData.cFileName, ".") == 0 ||
              strcmp(findData.cFileName, "..") == 0) {
            continue;
          }

          // Prioritize files over directories
          if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            if (_strnicmp(findData.cFileName, search_pattern,
                          strlen(search_pattern)) == 0) {
              best_match = (char *)malloc(strlen(buffer) +
                                          strlen(findData.cFileName) + 1);
              if (best_match) {
                strncpy(best_match, buffer, word_start);
                best_match[word_start] = '\0';
                strcat(best_match, findData.cFileName);
                break;
              }
            }
          }
        } while (FindNextFile(hFind, &findData));

        FindClose(hFind);
        if (best_match) {
          return best_match;
        }
      }
    } break;

    case ARG_TYPE_ALIAS: {
      int alias_count;
      char **alias_names = get_alias_names(&alias_count);
      char *best_match = NULL;

      if (alias_names && alias_count > 0) {
        // Find the best matching alias
        for (int i = 0; i < alias_count; i++) {
          if (_strnicmp(alias_names[i], partial_word, strlen(partial_word)) ==
              0) {
            // Create full suggestion
            best_match =
                (char *)malloc(strlen(buffer) + strlen(alias_names[i]) + 1);
            if (best_match) {
              // Copy everything up to the current word
              strncpy(best_match, buffer, word_start);
              best_match[word_start] = '\0';
              // Add the matched alias name
              strcat(best_match, alias_names[i]);
            }
            break;
          }
        }

        // Free alias names
        for (int i = 0; i < alias_count; i++) {
          free(alias_names[i]);
        }
        free(alias_names);

        if (best_match)
          return best_match;
      }
    } break;

    case ARG_TYPE_BOTH:
    case ARG_TYPE_ANY:
    default:
      // Fall back to general matching
      break;
    }
  }

  // If we're not in argument mode or didn't find a context-specific match,
  // fall back to the original match finding logic
  return find_best_match(buffer);
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
                           char *original_line, char *tab_match,
                           char *last_tab_prefix, int tab_index,
                           int tab_num_matches, WORD originalAttributes) {
  CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
  DWORD numCharsWritten;
  char displayBuffer[2048] = "";

  // Prepare the entire line in memory before displaying anything
  prepare_display_buffer(displayBuffer, original_line, tab_match,
                         last_tab_prefix, tab_index, tab_num_matches);

  // Calculate where to position the cursor after displaying
  int prefixLen = strlen(last_tab_prefix);

  // Hide cursor during redraw to prevent flicker
  CONSOLE_CURSOR_INFO cursorInfo;
  GetConsoleCursorInfo(hConsole, &cursorInfo);
  BOOL originalCursorVisible = cursorInfo.bVisible;
  cursorInfo.bVisible = FALSE;
  SetConsoleCursorInfo(hConsole, &cursorInfo);

  // Clear the entire line with one operation
  COORD clearPos = promptEndPos;
  FillConsoleOutputCharacter(hConsole, ' ', 120, clearPos, &numCharsWritten);
  FillConsoleOutputAttribute(hConsole, originalAttributes, 120, clearPos,
                             &numCharsWritten);

  // Move cursor to the beginning of line
  SetConsoleCursorPosition(hConsole, promptEndPos);

  // Prepare matchPrefix for display
  char matchPrefix[1024] = "";
  strncpy(matchPrefix, tab_match, prefixLen);
  matchPrefix[prefixLen] = '\0';

  // Write the command prefix and matching part with normal attributes
  WriteConsole(hConsole, original_line, strlen(original_line), &numCharsWritten,
               NULL);
  WriteConsole(hConsole, matchPrefix, strlen(matchPrefix), &numCharsWritten,
               NULL);

  // Get current cursor position
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);

  // Write the suggestion part with gray attributes
  SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);
  WriteConsole(hConsole, tab_match + prefixLen, strlen(tab_match + prefixLen),
               &numCharsWritten, NULL);

  // Save cursor position at end of suggestion
  GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
  COORD endOfSuggestionPos = consoleInfo.dwCursorPosition;

  // Write the indicator with gray attributes
  if (tab_num_matches > 1) {
    char indicatorBuffer[20];
    sprintf(indicatorBuffer, " (%d/%d)", tab_index + 1, tab_num_matches);
    WriteConsole(hConsole, indicatorBuffer, strlen(indicatorBuffer),
                 &numCharsWritten, NULL);
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
void display_suggestion_atomically(HANDLE hConsole, COORD promptEndPos,
                                   const char *buffer, const char *suggestion,
                                   int position, WORD originalAttributes) {
  if (!suggestion)
    return;

  // Find the start of the current word
  int word_start = position - 1;
  while (word_start >= 0 && buffer[word_start] != ' ' &&
         buffer[word_start] != '\\' && buffer[word_start] != '|') {
    word_start--;
  }
  word_start++; // Move past the space, backslash, or pipe

  // Check if we're in "goto" or "unbookmark" command
  char cmd[64] = "";
  int is_bookmark_cmd = 0;

  // Extract the command (first word)
  int cmd_end = 0;
  while (cmd_end < word_start && !isspace(buffer[cmd_end])) {
    cmd_end++;
  }

  if (cmd_end < sizeof(cmd)) {
    strncpy(cmd, buffer, cmd_end);
    cmd[cmd_end] = '\0';

    if (strcasecmp(cmd, "goto") == 0 || strcasecmp(cmd, "unbookmark") == 0) {
      is_bookmark_cmd = 1;
    }
  }

  // Extract just the last word from what we've typed so far
  char currentWord[1024] = "";
  strncpy(currentWord, buffer + word_start, position - word_start);
  currentWord[position - word_start] = '\0';

  // Get the suggested completion
  const char *completionText = NULL;

  if (is_bookmark_cmd) {
    // For bookmark commands, the suggestion is the full bookmark name
    completionText = suggestion;
  } else {
    // For other cases, extract the last word from the suggestion
    const char *lastWord = strrchr(suggestion, ' ');
    if (lastWord) {
      lastWord++; // Move past the space
    } else {
      lastWord = suggestion;
    }
    completionText = lastWord;
  }

  // Only display if suggestion starts with what we're typing (case insensitive)
  if (_strnicmp(completionText, currentWord, strlen(currentWord)) != 0) {
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
  strcpy(suggestionText, completionText + strlen(currentWord));

  // Set text color to gray for suggestion
  SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY);

  // Write the suggestion in one operation
  DWORD numCharsWritten;
  WriteConsole(hConsole, suggestionText, strlen(suggestionText),
               &numCharsWritten, NULL);

  // Reset color
  SetConsoleTextAttribute(hConsole, originalAttributes);

  // Reset cursor position
  SetConsoleCursorPosition(hConsole, consoleInfo.dwCursorPosition);

  // Restore cursor visibility
  cursorInfo.bVisible = originalCursorVisible;
  SetConsoleCursorInfo(hConsole, &cursorInfo);
}

void register_command(const char *cmd, ArgumentType type, const char *desc) {
  if (command_count < MAX_REGISTERED_COMMANDS) {
    command_registry[command_count].command = _strdup(cmd);
    command_registry[command_count].arg_type = type;
    command_registry[command_count].description = desc ? _strdup(desc) : NULL;
    command_count++;
  }
}

/**
 * Initialize command registry with all known commands
 */
void init_command_registry(void) {
  // Only initialize once
  static int initialized = 0;
  if (initialized)
    return;
  initialized = 1;

  // Existing commands with specific argument types
  register_command("goto", ARG_TYPE_BOOKMARK,
                   "Change to a bookmarked directory");
  register_command("unbookmark", ARG_TYPE_BOOKMARK, "Remove a bookmark");
  register_command("cd", ARG_TYPE_DIRECTORY, "Change directory");
  register_command("cat", ARG_TYPE_FILE, "Display file contents");

  // New commands to add
  register_command("rmdir", ARG_TYPE_DIRECTORY, "Remove directory");
  register_command("del", ARG_TYPE_FILE, "Delete files");
  register_command("rm", ARG_TYPE_FILE, "Remove files");
  register_command("copy", ARG_TYPE_BOTH, "Copy files or directories");
  register_command("cp", ARG_TYPE_BOTH, "Copy files or directories");
  register_command("move", ARG_TYPE_BOTH, "Move files or directories");
  register_command("mv", ARG_TYPE_BOTH, "Move files or directories");
  register_command("unalias", ARG_TYPE_ALIAS, "Remove an alias");

  // Add more commands as needed
}

/**
 * Get the argument type for a command
 */
ArgumentType get_command_arg_type(const char *cmd) {
  for (int i = 0; i < command_count; i++) {
    if (strcasecmp(command_registry[i].command, cmd) == 0) {
      return command_registry[i].arg_type;
    }
  }
  return ARG_TYPE_ANY; // Default to any
}
