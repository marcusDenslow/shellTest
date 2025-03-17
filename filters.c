/**
 * filters.c
 * Implementation of filter commands (where, sort-by, etc.)
 */

#include "filters.h"

/**
 * Filter a table based on a condition (e.g., where size > 10kb)
 */
TableData* lsh_where(TableData *input, char **args) {
    if (!input || !args || !args[0]) {
        fprintf(stderr, "lsh: where: missing arguments\n");
        fprintf(stderr, "Usage: ... | where FIELD OPERATOR VALUE\n");
        fprintf(stderr, "  e.g.: ls | where size > 10kb\n");
        return NULL;
    }
    
    // Parse filter condition (field, operator, value)
    char *field = args[0];
    char *op = NULL;
    char *value = NULL;
    
    // Find operator and value
    if (args[1] != NULL) {
        if (strcmp(args[1], ">") == 0 || 
            strcmp(args[1], "<") == 0 || 
            strcmp(args[1], "==") == 0 || 
            strcmp(args[1], ">=") == 0 ||
            strcmp(args[1], "<=") == 0) {
            op = args[1];
            value = args[2];
        }
    }
    
    if (!op || !value) {
        fprintf(stderr, "lsh: where: invalid filter condition\n");
        fprintf(stderr, "Usage: ... | where FIELD OPERATOR VALUE\n");
        fprintf(stderr, "  e.g.: ls | where size > 10kb\n");
        return NULL;
    }
    
    // Find field index
    int field_idx = -1;
    for (int i = 0; i < input->header_count; i++) {
        if (strcasecmp(input->headers[i], field) == 0) {
            field_idx = i;
            break;
        }
    }
    
    if (field_idx == -1) {
        fprintf(stderr, "lsh: where: unknown field '%s'\n", field);
        fprintf(stderr, "Available fields: ");
        for (int i = 0; i < input->header_count; i++) {
            fprintf(stderr, "%s%s", i > 0 ? ", " : "", input->headers[i]);
        }
        fprintf(stderr, "\n");
        return NULL;
    }
    
    // Create new filtered table
    return filter_table(input, field, op, value);
}
