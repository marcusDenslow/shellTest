/**
 * aliases.c
 * Implementation of shell alias functionality
 */

#include "aliases.h"
#include "builtins.h"

// Global variables for alias storage
AliasEntry *aliases = NULL;
int alias_count = 0;
int alias_capacity = 0;

// Path to the aliases file
char aliases_file_path[MAX_PATH];

/**
 * Initialize the alias system
 */
void init_aliases(void) {
    // Set initial capacity
    alias_capacity = 10;
    aliases = (AliasEntry*)malloc(alias_capacity * sizeof(AliasEntry));
    
    if (!aliases) {
        fprintf(stderr, "lsh: allocation error in init_aliases\n");
        return;
    }
    
    // Determine aliases file location - in user's home directory
    char *home_dir = getenv("USERPROFILE");
    if (home_dir) {
        snprintf(aliases_file_path, MAX_PATH, "%s\\.lsh_aliases", home_dir);
    } else {
        // Fallback to current directory if USERPROFILE not available
        strcpy(aliases_file_path, ".lsh_aliases");
    }
    
    // Load aliases from file
    load_aliases();
}

/**
 * Clean up the alias system
 */
void cleanup_aliases(void) {
    if (!aliases) return;
    
    // Free all alias entries
    for (int i = 0; i < alias_count; i++) {
        free(aliases[i].name);
        free(aliases[i].command);
    }
    
    free(aliases);
    aliases = NULL;
    alias_count = 0;
    alias_capacity = 0;
}

/**
 * Load aliases from file
 */
int load_aliases(void) {
    FILE *file = fopen(aliases_file_path, "r");
    if (!file) {
        // File doesn't exist yet - not an error
        return 1;
    }
    
    // Clear existing aliases
    for (int i = 0; i < alias_count; i++) {
        free(aliases[i].name);
        free(aliases[i].command);
    }
    alias_count = 0;
    
    char line[1024];
    int line_number = 0;
    
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        
        // Remove newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }
        
        // Skip empty lines and comments
        if (len == 0 || line[0] == '#') continue;
        
        // Find equal sign separator
        char *separator = strchr(line, '=');
        if (!separator) {
            fprintf(stderr, "lsh: warning: invalid alias format in line %d\n", line_number);
            continue;
        }
        
        // Split into name and command
        *separator = '\0';
        char *name = line;
        char *command = separator + 1;
        
        // Trim whitespace from name
        char *end = name + strlen(name) - 1;
        while (end > name && isspace(*end)) end--;
        *(end + 1) = '\0';
        
        // Add the alias
        add_alias(name, command);
    }
    
    fclose(file);
    return 1;
}

/**
 * Save aliases to file
 */
int save_aliases(void) {
    FILE *file = fopen(aliases_file_path, "w");
    if (!file) {
        fprintf(stderr, "lsh: error: could not save aliases to %s\n", aliases_file_path);
        return 0;
    }
    
    // Write header comment
    fprintf(file, "# LSH aliases file\n");
    fprintf(file, "# Format: alias_name=command with arguments\n\n");
    
    // Write each alias
    for (int i = 0; i < alias_count; i++) {
        fprintf(file, "%s=%s\n", aliases[i].name, aliases[i].command);
    }
    
    fclose(file);
    return 1;
}

/**
 * Add a new alias
 */
int add_alias(const char *name, const char *command) {
    if (!name || !command) return 0;
    
    // Check if we need to resize the array
    if (alias_count >= alias_capacity) {
        alias_capacity *= 2;
        AliasEntry *new_aliases = (AliasEntry*)realloc(aliases, alias_capacity * sizeof(AliasEntry));
        if (!new_aliases) {
            fprintf(stderr, "lsh: allocation error in add_alias\n");
            return 0;
        }
        aliases = new_aliases;
    }
    
    // Check if the alias already exists
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            // Update existing alias
            free(aliases[i].command);
            aliases[i].command = _strdup(command);
            return 1;
        }
    }
    
    // Add new alias
    aliases[alias_count].name = _strdup(name);
    aliases[alias_count].command = _strdup(command);
    alias_count++;
    
    return 1;
}

/**
 * Remove an alias
 */
int remove_alias(const char *name) {
    if (!name) return 0;
    
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            // Free memory
            free(aliases[i].name);
            free(aliases[i].command);
            
            // Shift remaining aliases
            for (int j = i; j < alias_count - 1; j++) {
                aliases[j] = aliases[j + 1];
            }
            
            alias_count--;
            return 1;
        }
    }
    
    return 0; // Alias not found
}

/**
 * Find an alias by name
 */
AliasEntry* find_alias(const char *name) {
    if (!name) return NULL;
    
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return &aliases[i];
        }
    }
    
    return NULL;
}

/**
 * Expand a command line by replacing aliases
 */
char* expand_aliases(const char *command) {
    if (!command) return NULL;
    
    // Make a copy of the input command
    char *result = _strdup(command);
    if (!result) return NULL;
    
    // Parse the first word (the command)
    char *cmd_copy = _strdup(result);
    if (!cmd_copy) {
        free(result);
        return NULL;
    }
    
    char *cmd = strtok(cmd_copy, " \t");
    if (!cmd) {
        free(cmd_copy);
        return result; // No command to expand
    }
    
    // Look for an alias match
    AliasEntry *alias = find_alias(cmd);
    if (alias) {
        // Get the arguments part (everything after the command)
        char *args = result + strlen(cmd);
        
        // Allocate space for expanded command + arguments
        char *expanded = (char*)malloc(strlen(alias->command) + strlen(args) + 1);
        if (!expanded) {
            free(cmd_copy);
            return result;
        }
        
        // Build the expanded command
        strcpy(expanded, alias->command);
        strcat(expanded, args);
        
        // Replace the result
        free(result);
        result = expanded;
    }
    
    free(cmd_copy);
    return result;
}

/**
 * Command handler for the "alias" command
 */
int lsh_alias(char **args) {
    // "alias edit" command - open the aliases file in a text editor
    if (args[1] != NULL && strcmp(args[1], "edit") == 0) {
        // Try to determine if neovim or vim is available
        FILE *test_nvim = _popen("nvim --version 2>nul", "r");
        if (test_nvim != NULL) {
            // nvim is available
            _pclose(test_nvim);
            char edit_cmd[1024];
            snprintf(edit_cmd, sizeof(edit_cmd), "nvim %s", aliases_file_path);
            system(edit_cmd);
        } else {
            // Try vim next
            FILE *test_vim = _popen("vim --version 2>nul", "r");
            if (test_vim != NULL) {
                // vim is available
                _pclose(test_vim);
                char edit_cmd[1024];
                snprintf(edit_cmd, sizeof(edit_cmd), "vim %s", aliases_file_path);
                system(edit_cmd);
            } else {
                // Fall back to notepad
                char edit_cmd[1024];
                snprintf(edit_cmd, sizeof(edit_cmd), "notepad %s", aliases_file_path);
                system(edit_cmd);
            }
        }
        
        // Reload aliases after editing
        load_aliases();
        return 1;
    }
    
    // No arguments - list all aliases
    if (args[1] == NULL) {
        if (alias_count == 0) {
            printf("No aliases defined\n");
            printf("Use 'alias name=command' to create an alias\n");
            printf("Use 'alias edit' to edit aliases in a text editor\n");
        } else {
            printf("Current aliases:\n");
            for (int i = 0; i < alias_count; i++) {
                printf("  %s=%s\n", aliases[i].name, aliases[i].command);
            }
            printf("\nUse 'alias edit' to edit aliases in a text editor\n");
        }
        return 1;
    }
    
    // Check if it's an alias definition (contains =)
    char *equals = strchr(args[1], '=');
    if (equals) {
        // Split into name and command
        *equals = '\0';
        char *name = args[1];
        char *command = equals + 1;
        
        // Add the alias
        if (add_alias(name, command)) {
            save_aliases();
            printf("Alias added: %s=%s\n", name, command);
        }
        
        // Restore the equals sign for history consistency
        *equals = '=';
        return 1;
    }
    
    // Look up a specific alias
    AliasEntry *alias = find_alias(args[1]);
    if (alias) {
        printf("%s=%s\n", alias->name, alias->command);
    } else {
        printf("Alias '%s' not found\n", args[1]);
    }
    
    return 1;
}

/**
 * Command handler for the "unalias" command
 */
int lsh_unalias(char **args) {
    if (args[1] == NULL) {
        fprintf(stderr, "lsh: expected argument to \"unalias\"\n");
        return 1;
    }
    
    if (remove_alias(args[1])) {
        save_aliases();
        printf("Alias '%s' removed\n", args[1]);
    } else {
        printf("Alias '%s' not found\n", args[1]);
    }
    
    return 1;
}

/**
 * Command handler for the "aliases" command
 * Simply displays all defined aliases
 */
/**
 * Command handler for the "aliases" command
 * Simply displays all defined aliases
 */
int lsh_aliases(char **args) {
    if (alias_count == 0) {
        printf("No aliases defined\n");
    } else {
        printf("Current aliases:\n");
        printf("\n");
        
        // Calculate maximum name length for alignment
        int max_name_length = 0;
        for (int i = 0; i < alias_count; i++) {
            int len = strlen(aliases[i].name);
            if (len > max_name_length) {
                max_name_length = len;
            }
        }
        
        // Display aliases in a nicely formatted table without coloring
        // (we're removing the color code because it's causing a crash)
        for (int i = 0; i < alias_count; i++) {
            printf("  %-*s = %s\n", max_name_length + 2, aliases[i].name, aliases[i].command);
        }
        
        printf("\n");
    }
    
    return 1;
}

/**
 * Get alias names for tab completion
 */
char** get_alias_names(int *count) {
    if (alias_count == 0) {
        *count = 0;
        return NULL;
    }
    
    char **names = (char**)malloc(alias_count * sizeof(char*));
    if (!names) {
        *count = 0;
        return NULL;
    }
    
    for (int i = 0; i < alias_count; i++) {
        names[i] = _strdup(aliases[i].name);
    }
    
    *count = alias_count;
    return names;
}
