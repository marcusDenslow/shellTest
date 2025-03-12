/**
 * builtins.c
 * Implementation of all built-in shell commands
 */

#include "builtins.h"
#include "common.h"

// History command implementation
#define HISTORY_SIZE 10

// New struct to hold history entries with timestamps
typedef struct {
    char *command;
    SYSTEMTIME timestamp;
} HistoryEntry;

static HistoryEntry command_history[HISTORY_SIZE] = {0};
static int history_count = 0;  // Total number of commands entered
static int history_index = 0;  // Current index in the circular buffer

// Array of built-in command names
char *builtin_str[] = {
  "cd",
  "help",
  "exit",
  "ls",
  "dir",
  "clear",
  "cls",
  "mkdir",
  "rmdir",
  "del",
  "rm",
  "touch",
  "pwd",
  "cat",
  "history", // Added history command
};

// Array of built-in command function pointers
int (*builtin_func[]) (char **) = {
  &lsh_cd,
  &lsh_help,
  &lsh_exit,
  &lsh_dir,
  &lsh_dir,
  &lsh_clear,
  &lsh_clear,
  &lsh_mkdir,
  &lsh_rmdir,
  &lsh_del,
  &lsh_del,
  &lsh_touch,
  &lsh_pwd,
  &lsh_cat,
  &lsh_history, // Added history function pointer
};

// Return the number of built-in commands
int lsh_num_builtins() {
  return sizeof(builtin_str) / sizeof(char *);
}

// Add function to store commands in history with timestamps
void lsh_add_to_history(const char *command) {
    if (command == NULL || command[0] == '\0') {
        return;  // Don't add empty commands
    }
    
    // Free the string at the current index if it exists
    if (command_history[history_index].command != NULL) {
        free(command_history[history_index].command);
    }
    
    // Save the current command
    command_history[history_index].command = _strdup(command);
    
    // Save the current time
    GetLocalTime(&command_history[history_index].timestamp);
    
    // Update index and count
    history_index = (history_index + 1) % HISTORY_SIZE;
    history_count++;
}

// Implement the history command with timestamps
int lsh_history(char **args) {
    if (history_count == 0) {
        printf("No commands in history\n");
        return 1;
    }
    
    // Calculate how many commands to display
    int num_to_display = (history_count < HISTORY_SIZE) ? history_count : HISTORY_SIZE;
    
    // Calculate starting index
    int start_idx;
    if (history_count <= HISTORY_SIZE) {
        // Haven't filled the buffer yet
        start_idx = 0;
    } else {
        // Buffer is full, start from the oldest command
        start_idx = history_index;  // Next slot to overwrite contains oldest command
    }
    
    // Display the commands with timestamps
    for (int i = 0; i < num_to_display; i++) {
        int idx = (start_idx + i) % HISTORY_SIZE;
        SYSTEMTIME *ts = &command_history[idx].timestamp;
        printf("[%02d:%02d:%02d] %s\n", 
               ts->wHour, ts->wMinute, ts->wSecond, 
               command_history[idx].command);
    }
    
    return 1;
}

// Print current working directory
int lsh_pwd(char **args) {
  char cwd[1024];

  if (_getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("lsh: pwd");
    return 1;
  }

  printf("\n%s\n\n", cwd);
  return 1;
}

/**
 * Determine file type from extension
 */
typedef enum {
    FILE_TYPE_TEXT,
    FILE_TYPE_C,
    FILE_TYPE_CPP,
    FILE_TYPE_H,
    FILE_TYPE_PY,
    FILE_TYPE_JS,
    FILE_TYPE_HTML,
    FILE_TYPE_CSS,
    FILE_TYPE_MD,
    FILE_TYPE_JSON
} FileType;

/**
 * Get file type from filename
 */
FileType get_file_type(const char *filename) {
    char *ext = strrchr(filename, '.');
    if (ext == NULL) return FILE_TYPE_TEXT;
    
    ext++; // Skip the dot
    
    if (_stricmp(ext, "c") == 0) return FILE_TYPE_C;
    if (_stricmp(ext, "cpp") == 0 || _stricmp(ext, "cc") == 0) return FILE_TYPE_CPP;
    if (_stricmp(ext, "h") == 0 || _stricmp(ext, "hpp") == 0) return FILE_TYPE_H;
    if (_stricmp(ext, "py") == 0) return FILE_TYPE_PY;
    if (_stricmp(ext, "js") == 0) return FILE_TYPE_JS;
    if (_stricmp(ext, "html") == 0 || _stricmp(ext, "htm") == 0) return FILE_TYPE_HTML;
    if (_stricmp(ext, "css") == 0) return FILE_TYPE_CSS;
    if (_stricmp(ext, "md") == 0 || _stricmp(ext, "markdown") == 0) return FILE_TYPE_MD;
    if (_stricmp(ext, "json") == 0) return FILE_TYPE_JSON;
    
    return FILE_TYPE_TEXT;
}

/**
 * Set console text color
 */
void set_color(int color) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTextAttribute(hConsole, color);
}

/**
 * Reset console text color to default
 */
void reset_color() {
    set_color(COLOR_DEFAULT);
}

/**
 * Check if a character is a word boundary character
 */
int is_separator(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '(' || c == ')' || c == '[' || c == ']' ||
           c == '{' || c == '}' || c == '.' || c == ',' ||
           c == ';' || c == ':' || c == '+' || c == '-' ||
           c == '/' || c == '*' || c == '%' || c == '=' ||
           c == '<' || c == '>' || c == '&' || c == '|' ||
           c == '^' || c == '!' || c == '~' || c == '?' ||
           c == '"' || c == '\'' || c == '\\';
}

/**
 * Check if string matches any keyword in the array
 */
int is_keyword(const char *word, const char **keywords, int num_keywords) {
    for (int i = 0; i < num_keywords; i++) {
        if (strcmp(word, keywords[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Print C/C++ file with syntax highlighting
 */
void print_c_file_highlighted(FILE *file) {
    static const char *c_keywords[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do",
        "double", "else", "enum", "extern", "float", "for", "goto", "if",
        "int", "long", "register", "return", "short", "signed", "sizeof", "static",
        "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while",
        "include", "define", "ifdef", "ifndef", "endif", "pragma", "error", "warning"
    };
    static const int c_keywords_count = sizeof(c_keywords) / sizeof(c_keywords[0]);
    
    char line[4096];
    char word[256];
    int in_comment = 0;
    int in_string = 0;
    int in_char = 0;
    int in_preprocessor = 0;
    
    // Process file line by line
    while (fgets(line, sizeof(line), file) != NULL) {
        char *p = line;
        int word_pos = 0;
        
        while (*p) {
            // Already in a multi-line comment
            if (in_comment) {
                set_color(COLOR_COMMENT);
                putchar(*p);
                
                if (*p == '*' && *(p+1) == '/') {
                    putchar(*(p+1));
                    p++;
                    in_comment = 0;
                    reset_color();
                }
            }
            // Line starts with # (preprocessor directive)
            else if (p == line && *p == '#') {
                in_preprocessor = 1;
                set_color(COLOR_PREPROCESSOR);
                putchar(*p);
            }
            // Inside a string literal
            else if (in_string) {
                set_color(COLOR_STRING);
                putchar(*p);
                
                if (*p == '\\' && *(p+1)) {
                    putchar(*(p+1));
                    p++;
                } else if (*p == '"') {
                    in_string = 0;
                    reset_color();
                }
            }
            // Inside a character literal
            else if (in_char) {
                set_color(COLOR_STRING);
                putchar(*p);
                
                if (*p == '\\' && *(p+1)) {
                    putchar(*(p+1));
                    p++;
                } else if (*p == '\'') {
                    in_char = 0;
                    reset_color();
                }
            }
            // Start of a comment
            else if (*p == '/' && *(p+1) == '/') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, c_keywords, c_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                set_color(COLOR_COMMENT);
                putchar(*p);
                putchar(*(p+1));
                p++;
                
                // Print the rest of the line as a comment
                p++;
                while (*p) {
                    putchar(*p++);
                }
                reset_color();
                break;
            }
            // Start of multi-line comment
            else if (*p == '/' && *(p+1) == '*') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, c_keywords, c_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                in_comment = 1;
                set_color(COLOR_COMMENT);
                putchar(*p);
                putchar(*(p+1));
                p++;
            }
            // Start of string literal
            else if (*p == '"') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, c_keywords, c_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                in_string = 1;
                set_color(COLOR_STRING);
                putchar(*p);
            }
            // Start of character literal
            else if (*p == '\'') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, c_keywords, c_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                in_char = 1;
                set_color(COLOR_STRING);
                putchar(*p);
            }
            // Word boundary
            else if (is_separator(*p)) {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    
                    // Check if it's a keyword or a number
                    if (is_keyword(word, c_keywords, c_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        // Regular identifier
                        printf("%s", word);
                    }
                    
                    word_pos = 0;
                }
                
                // Print the separator
                if (in_preprocessor && *p != '\n') {
                    set_color(COLOR_PREPROCESSOR);
                    putchar(*p);
                    reset_color();
                } else {
                    putchar(*p);
                }
                
                // End of preprocessor directive
                if (in_preprocessor && *p == '\n') {
                    in_preprocessor = 0;
                    reset_color();
                }
            }
            // Part of a word
            else {
                if (word_pos < sizeof(word) - 1) {
                    word[word_pos++] = *p;
                }
                // DON'T print the character here - we'll print the whole word when we hit a boundary
            }
            
            p++;
        }
        
        // Handle word at end of line if there is one
        if (word_pos > 0) {
            word[word_pos] = '\0';
            
            if (is_keyword(word, c_keywords, c_keywords_count)) {
                set_color(COLOR_KEYWORD);
                printf("%s", word);
                reset_color();
            } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                set_color(COLOR_NUMBER);
                printf("%s", word);
                reset_color();
            } else {
                printf("%s", word);
            }
        }
    }
    
    reset_color();
}

/**
 * Print Python file with syntax highlighting
 */
void print_py_file_highlighted(FILE *file) {
    static const char *py_keywords[] = {
        "and", "as", "assert", "break", "class", "continue", "def", "del",
        "elif", "else", "except", "False", "finally", "for", "from", "global",
        "if", "import", "in", "is", "lambda", "None", "nonlocal", "not",
        "or", "pass", "raise", "return", "True", "try", "while", "with", "yield"
    };
    static const int py_keywords_count = sizeof(py_keywords) / sizeof(py_keywords[0]);
    
    char line[4096];
    char word[256];
    int in_string_single = 0;
    int in_string_double = 0;
    int in_triple_single = 0;
    int in_triple_double = 0;
    
    // Process file line by line
    while (fgets(line, sizeof(line), file) != NULL) {
        char *p = line;
        int word_pos = 0;
        
        while (*p) {
            // Already in a string
            if (in_string_single || in_string_double || in_triple_single || in_triple_double) {
                set_color(COLOR_STRING);
                putchar(*p);
                
                if (in_string_single && *p == '\'') {
                    in_string_single = 0;
                    reset_color();
                } else if (in_string_double && *p == '"') {
                    in_string_double = 0;
                    reset_color();
                } else if (in_triple_single && *p == '\'' && *(p+1) == '\'' && *(p+2) == '\'') {
                    putchar(*(p+1));
                    putchar(*(p+2));
                    p += 2;
                    in_triple_single = 0;
                    reset_color();
                } else if (in_triple_double && *p == '"' && *(p+1) == '"' && *(p+2) == '"') {
                    putchar(*(p+1));
                    putchar(*(p+2));
                    p += 2;
                    in_triple_double = 0;
                    reset_color();
                }
            }
            // Start of a comment
            else if (*p == '#') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, py_keywords, py_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                set_color(COLOR_COMMENT);
                // Print the rest of the line as a comment
                while (*p) {
                    putchar(*p++);
                }
                reset_color();
                break;
            }
            // Start of triple quoted string
            else if (*p == '\'' && *(p+1) == '\'' && *(p+2) == '\'') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, py_keywords, py_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                in_triple_single = 1;
                set_color(COLOR_STRING);
                putchar(*p);
                putchar(*(p+1));
                putchar(*(p+2));
                p += 2;
            }
            else if (*p == '"' && *(p+1) == '"' && *(p+2) == '"') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, py_keywords, py_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                in_triple_double = 1;
                set_color(COLOR_STRING);
                putchar(*p);
                putchar(*(p+1));
                putchar(*(p+2));
                p += 2;
            }
            // Start of single quoted string
            else if (*p == '\'') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, py_keywords, py_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                in_string_single = 1;
                set_color(COLOR_STRING);
                putchar(*p);
            }
            // Start of double quoted string
            else if (*p == '"') {
                // Print any word we've been accumulating
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    if (is_keyword(word, py_keywords, py_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        printf("%s", word);
                    }
                    word_pos = 0;
                }
                
                in_string_double = 1;
                set_color(COLOR_STRING);
                putchar(*p);
            }
            // Word boundary
            else if (is_separator(*p)) {
                if (word_pos > 0) {
                    word[word_pos] = '\0';
                    
                    // Check if it's a keyword or a number
                    if (is_keyword(word, py_keywords, py_keywords_count)) {
                        set_color(COLOR_KEYWORD);
                        printf("%s", word);
                        reset_color();
                    } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                        set_color(COLOR_NUMBER);
                        printf("%s", word);
                        reset_color();
                    } else {
                        // Regular identifier
                        printf("%s", word);
                    }
                    
                    word_pos = 0;
                }
                
                // Print the separator
                putchar(*p);
            }
            // Part of a word
            else {
                if (word_pos < sizeof(word) - 1) {
                    word[word_pos++] = *p;
                }
                // Don't print characters immediately - wait until we have a complete word
            }
            
            p++;
        }
        
        // Handle word at end of line
        if (word_pos > 0) {
            word[word_pos] = '\0';
            
            if (is_keyword(word, py_keywords, py_keywords_count)) {
                set_color(COLOR_KEYWORD);
                printf("%s", word);
                reset_color();
            } else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
                set_color(COLOR_NUMBER);
                printf("%s", word);
                reset_color();
            } else {
                printf("%s", word);
            }
        }
    }
    
    reset_color();
}

/**
 * Print HTML/XML file with syntax highlighting
 */
void print_html_file_highlighted(FILE *file) {
    char line[4096];
    int in_tag = 0;
    int in_attribute = 0;
    int in_string = 0;
    int in_comment = 0;
    
    // Process file line by line
    while (fgets(line, sizeof(line), file) != NULL) {
        char *p = line;
        
        while (*p) {
            // Inside a comment
            if (in_comment) {
                set_color(COLOR_COMMENT);
                putchar(*p);
                
                if (*p == '-' && *(p+1) == '-' && *(p+2) == '>') {
                    putchar(*(p+1));
                    putchar(*(p+2));
                    p += 2;
                    in_comment = 0;
                    reset_color();
                }
            }
            // Start of a comment
            else if (*p == '<' && *(p+1) == '!' && *(p+2) == '-' && *(p+3) == '-') {
                in_comment = 1;
                set_color(COLOR_COMMENT);
                putchar(*p);
                putchar(*(p+1));
                putchar(*(p+2));
                putchar(*(p+3));
                p += 3;
            }
            // Inside a string
            else if (in_string) {
                set_color(COLOR_STRING);
                putchar(*p);
                
                if ((*p == '"' || *p == '\'') && *(p-1) != '\\') {
                    in_string = 0;
                    if (in_tag) set_color(COLOR_KEYWORD);
                    else reset_color();
                }
            }
            // Start of a tag
            else if (*p == '<' && *(p+1) != '!') {
                in_tag = 1;
                in_attribute = 0;
                set_color(COLOR_KEYWORD);
                putchar(*p);
            }
            // End of a tag
            else if (*p == '>' && in_tag) {
                in_tag = 0;
                in_attribute = 0;
                putchar(*p);
                reset_color();
            }
            // Inside a tag, start of attribute
            else if (in_tag && *p == ' ' && !in_attribute) {
                in_attribute = 1;
                putchar(*p);
                set_color(COLOR_IDENTIFIER);
            }
            // Start of string inside tag
            else if (in_tag && (*p == '"' || *p == '\'')) {
                in_string = 1;
                putchar(*p);
            }
            // Equals sign in attribute
            else if (in_tag && in_attribute && *p == '=') {
                putchar(*p);
                set_color(COLOR_KEYWORD);
            }
            // Just print the character
            else {
                putchar(*p);
            }
            
            p++;
        }
    }
    
    reset_color();
}

/**
 * Print a file with language-specific syntax highlighting
 */
void print_file_with_highlighting(FILE *file, FileType type) {
    switch (type) {
        case FILE_TYPE_C:
        case FILE_TYPE_CPP:
        case FILE_TYPE_H:
            print_c_file_highlighted(file);
            break;
        case FILE_TYPE_PY:
            print_py_file_highlighted(file);
            break;
        case FILE_TYPE_HTML:
            print_html_file_highlighted(file);
            break;
        default:
            // For unsupported file types, just print the content
            char buffer[4096];
            size_t bytes_read;
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                fwrite(buffer, 1, bytes_read, stdout);
            }
            break;
    }
}

/**
 * Display file contents with optional syntax highlighting
 */
int lsh_cat(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "lsh: expected file argument to \"cat\"\n");
        return 1;
    }
    
    // Check for flags (e.g., -n for line numbers, -h for highlighting)
    int use_highlighting = 1; // Default to using highlighting
    int start_index = 1;
    
    if (args[1][0] == '-') {
        if (strcmp(args[1], "-p") == 0 || strcmp(args[1], "--plain") == 0) {
            use_highlighting = 0;
            start_index = 2;
        }
        
        if (start_index >= 2 && args[start_index] == NULL) {
            fprintf(stderr, "lsh: expected file argument after %s\n", args[1]);
            return 1;
        }
    }
    
    // Process each file argument
    int i = start_index;
    int success = 1;
    
    while (args[i] != NULL) {
        // Print filename and blank line before content
        printf("\n--- %s ---\n\n", args[i]);
        
        // Open the file in binary mode to avoid automatic CRLF conversion
        FILE *file = fopen(args[i], "rb");
        
        if (file == NULL) {
            fprintf(stderr, "lsh: cannot open '%s': ", args[i]);
            perror("");
            success = 0;
            i++;
            continue;
        }
        
        // Determine file type and print with appropriate highlighting
        if (use_highlighting) {
            FileType type = get_file_type(args[i]);
            
            // Reopen as text file for line-by-line processing when highlighting
            fclose(file);
            file = fopen(args[i], "r");
            
            if (file == NULL) {
                fprintf(stderr, "lsh: cannot reopen '%s' as text: ", args[i]);
                perror("");
                success = 0;
                i++;
                continue;
            }
            
            print_file_with_highlighting(file, type);
        } else {
            // Just print the file without highlighting
            char buffer[4096];
            size_t bytes_read;
            
            while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
                fwrite(buffer, 1, bytes_read, stdout);
            }
        }
        
        // Check for read errors
        if (ferror(file)) {
            fprintf(stderr, "lsh: error reading from '%s': ", args[i]);
            perror("");
            success = 0;
        }
        
        // Close the file
        fclose(file);
        
        // Print blank line after content
        printf("\n\n");
        
        i++;
    }
    
    // Make sure we reset the console color
    reset_color();
    
    return success;
}

// Delete files
int lsh_del(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file argument to \"del\"\n");
    return 1;
  }
  
  // Handle multiple files
  int i = 1;
  int success = 1;
  
  while (args[i] != NULL) {
    // Try deleting each file
    if (DeleteFile(args[i]) == 0) {
      // DeleteFile returns 0 on failure, non-zero on success
      DWORD error = GetLastError();
      fprintf(stderr, "lsh: failed to delete '%s': ", args[i]);
      
      switch (error) {
        case ERROR_FILE_NOT_FOUND:
          fprintf(stderr, "file not found\n");
          break;
        case ERROR_ACCESS_DENIED:
          fprintf(stderr, "access denied\n");
          break;
        default:
          fprintf(stderr, "error code %lu\n", error);
          break;
      }
      
      success = 0;
    } else {
      printf("Deleted '%s'\n", args[i]);
    }
    
    i++;
  }
  
  return success;
}

// Create directory
int lsh_mkdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"mkdir\"\n");
    return 1;
  }

  if (_mkdir(args[1]) != 0) {
    perror("lsh: mkdir");
  }

  return 1;
}

// Remove directory
int lsh_rmdir(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"rmdir\"\n");
    return 1;
  }

  if (_rmdir(args[1]) != 0) {
    perror("lsh: rmdir");
  }

  return 1;
}

// Touch a file (create or update timestamp)
int lsh_touch(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected file argument to \"touch\"\n");
    return 1;
  }
  
  // Handle multiple files
  int i = 1;
  int success = 1;
  
  while (args[i] != NULL) {
    // Check if file exists
    HANDLE hFile = CreateFile(
      args[i],                       // filename
      FILE_WRITE_ATTRIBUTES,         // access mode
      FILE_SHARE_READ | FILE_SHARE_WRITE, // share mode
      NULL,                          // security attributes
      OPEN_ALWAYS,                   // create if doesn't exist, open if does
      FILE_ATTRIBUTE_NORMAL,         // file attributes
      NULL                           // template file
    );
    
    if (hFile == INVALID_HANDLE_VALUE) {
      // Failed to create/open file
      DWORD error = GetLastError();
      fprintf(stderr, "lsh: failed to touch '%s': error code %lu\n", args[i], error);
      success = 0;
    } else {
      // File was created or opened successfully
      // Get current system time
      SYSTEMTIME st;
      FILETIME ft;
      GetSystemTime(&st);
      SystemTimeToFileTime(&st, &ft);
      
      // Update file times
      if (!SetFileTime(hFile, &ft, &ft, &ft)) {
        fprintf(stderr, "lsh: failed to update timestamps for '%s'\n", args[i]);
        success = 0;
      }
      
      // Close file handle
      CloseHandle(hFile);
      
      printf("Created/updated '%s'\n", args[i]);
    }
    
    i++;
  }
  
  return success;
}

// Change directory
int lsh_cd(char **args) {
  if (args[1] == NULL) {
    fprintf(stderr, "lsh: expected argument to \"cd\"\n");
  } else {
    if (_chdir(args[1]) != 0) {  // Use _chdir for Windows
      perror("lsh");
    }
  }
  return 1;
}

// Clear the screen
int lsh_clear(char **args) {
  // Get the handle to the console
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
  if (hConsole == INVALID_HANDLE_VALUE) {
    perror("lsh: failed to get console handle");
    return 1;
  }
    
  // Get console information
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    perror("lsh: failed to get console info");
    return 1;
  }
    
  // Calculate total cells in console
  DWORD cellCount = csbi.dwSize.X * csbi.dwSize.Y;
    
  // Fill the entire buffer with spaces
  DWORD count;
  COORD homeCoords = {0, 0};
    
  if (!FillConsoleOutputCharacter(hConsole, ' ', cellCount, homeCoords, &count)) {
    perror("lsh: failed to fill console");
    return 1;
  }
    
  // Fill the entire buffer with the current attributes
  if (!FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cellCount, homeCoords, &count)) {
    perror("lsh: failed to set console attributes");
    return 1;
  }
    
  // Move the cursor to home position
  SetConsoleCursorPosition(hConsole, homeCoords);
    
  return 1;
}

// List directory contents with creation time and file type
int lsh_dir(char **args) {
  char cwd[1024];
  WIN32_FIND_DATA findData;
  HANDLE hFind;
  
  // Get current directory
  if (_getcwd(cwd, sizeof(cwd)) == NULL) {
    perror("lsh");
    return 1;
  }
  
  // Prepare search pattern for all files
  char searchPath[1024];
  strcpy(searchPath, cwd);
  strcat(searchPath, "\\*");
  
  // Find first file
  hFind = FindFirstFile(searchPath, &findData);
  
  if (hFind == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "lsh: Failed to list directory contents\n");
    return 1;
  }
  
  // Print current directory header
  printf("Contents of directory: %s\n\n", cwd);
  
  // Define column widths for better alignment
  const int timeColWidth = 20;
  const int sizeColWidth = 15;
  const int typeColWidth = 10;
  const int nameColWidth = 30;
  
  // Print table header with ASCII characters
  printf("+%.*s+%.*s+%.*s+%.*s+\n", 
         timeColWidth, "--------------------", 
         sizeColWidth, "---------------", 
         typeColWidth, "----------", 
         nameColWidth, "------------------------------");
         
  printf("| %-*s | %-*s | %-*s | %-*s |\n", 
         timeColWidth-2, "Created", 
         sizeColWidth-2, "Size", 
         typeColWidth-2, "Type", 
         nameColWidth-2, "Name");
         
  printf("+%.*s+%.*s+%.*s+%.*s+\n", 
         timeColWidth, "--------------------", 
         sizeColWidth, "---------------", 
         typeColWidth, "----------", 
         nameColWidth, "------------------------------");
  
  // List all files
  do {
    // Skip . and .. directories for cleaner output
    if (strcmp(findData.cFileName, ".") != 0 && strcmp(findData.cFileName, "..") != 0) {
      // Convert file creation time to system time
      SYSTEMTIME fileTime;
      FileTimeToSystemTime(&findData.ftCreationTime, &fileTime);
      
      // Format creation time as string
      char timeString[32];
      sprintf(timeString, "%04d-%02d-%02d %02d:%02d:%02d", 
              fileTime.wYear, fileTime.wMonth, fileTime.wDay,
              fileTime.wHour, fileTime.wMinute, fileTime.wSecond);
      
      // Check if it's a directory
      const char* fileType = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "Directory" : "File";
      
      // Format size (only for files)
      char sizeString[32];
      if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        strcpy(sizeString, "-");
      } else {
        // Format size with commas for better readability
        if (findData.nFileSizeLow < 1024) {
          sprintf(sizeString, "%lu B", findData.nFileSizeLow);
        } else if (findData.nFileSizeLow < 1024 * 1024) {
          sprintf(sizeString, "%.1f KB", findData.nFileSizeLow / 1024.0);
        } else {
          sprintf(sizeString, "%.1f MB", findData.nFileSizeLow / (1024.0 * 1024.0));
        }
      }
      
      // Truncate filename if too long
      char truncName[nameColWidth];
      strncpy(truncName, findData.cFileName, nameColWidth-3);
      truncName[nameColWidth-3] = '\0';
      if (strlen(findData.cFileName) > nameColWidth-3) {
        strcat(truncName, "...");
      }
      
      // Print formatted line with table borders
      printf("| %-*s | %-*s | %-*s | %-*s |\n", 
             timeColWidth-2, timeString, 
             sizeColWidth-2, sizeString, 
             typeColWidth-2, fileType, 
             nameColWidth-2, truncName);
    }
  } while (FindNextFile(hFind, &findData));
  
  // Print table footer
  printf("+%.*s+%.*s+%.*s+%.*s+\n", 
         timeColWidth, "--------------------", 
         sizeColWidth, "---------------", 
         typeColWidth, "----------", 
         nameColWidth, "------------------------------");
  
  // Close find handle
  FindClose(hFind);
  
  return 1;
}

// Display help
int lsh_help(char **args) {
  int i;
  printf("Marcus Denslow's LSH\n");
  printf("Type program names and arguments, and hit enter.\n");
  printf("The following are built in:\n");

  for (i = 0; i < lsh_num_builtins(); i++) {
    printf("  %s\n", builtin_str[i]);
  }

  printf("Use the command for information on other programs.\n");
  return 1;
}

// Exit the shell
int lsh_exit(char **args) {
  return 0;
}
