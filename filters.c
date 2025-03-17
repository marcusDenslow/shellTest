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

/**
 * Sort a table based on a column (e.g., sort-by size desc)
 */
TableData* lsh_sort_by(TableData *input, char **args) {
    if (!input || !args || !args[0]) {
        fprintf(stderr, "lsh: sort-by: missing arguments\n");
        fprintf(stderr, "Usage: ... | sort-by FIELD [asc|desc]\n");
        fprintf(stderr, "  e.g.: ls | sort-by size desc\n");
        return NULL;
    }
    
    // Parse sort field and direction
    char *field = args[0];
    int descending = 0;
    
    // Check if direction is specified
    if (args[1] != NULL) {
        if (strcasecmp(args[1], "desc") == 0 || 
            strcasecmp(args[1], "descending") == 0) {
            descending = 1;
        }
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
        fprintf(stderr, "lsh: sort-by: unknown field '%s'\n", field);
        fprintf(stderr, "Available fields: ");
        for (int i = 0; i < input->header_count; i++) {
            fprintf(stderr, "%s%s", i > 0 ? ", " : "", input->headers[i]);
        }
        fprintf(stderr, "\n");
        return NULL;
    }
    
    // Create a copy of the table
    TableData *result = create_table(input->headers, input->header_count);
    if (!result) {
        return NULL;
    }
    
    // Copy all rows to the new table
    for (int i = 0; i < input->row_count; i++) {
        DataValue *row_copy = (DataValue*)malloc(input->header_count * sizeof(DataValue));
        if (!row_copy) {
            fprintf(stderr, "lsh: allocation error in sort_by\n");
            free_table(result);
            return NULL;
        }
        
        for (int j = 0; j < input->header_count; j++) {
            row_copy[j] = copy_data_value(&input->rows[i][j]);
        }
        
        add_table_row(result, row_copy);
    }
    
    // Now sort the rows based on the specified column
    // Bubble sort for simplicity (not efficient for large datasets)
    for (int i = 0; i < result->row_count - 1; i++) {
        for (int j = 0; j < result->row_count - i - 1; j++) {
            int compare_result = 0;
            
            // Compare based on data type
            if (result->rows[j][field_idx].type == TYPE_STRING ||
                result->rows[j][field_idx].type == TYPE_SIZE) {
                // Handle special case for sizes (KB, MB, etc.)
                if (strcasecmp(result->headers[field_idx], "Size") == 0 ||
                    strcasecmp(result->headers[field_idx], "Memory") == 0) {
                    long size1 = extract_size_bytes(result->rows[j][field_idx].value.str_val);
                    long size2 = extract_size_bytes(result->rows[j+1][field_idx].value.str_val);
                    compare_result = (size1 > size2) ? 1 : (size1 < size2) ? -1 : 0;
                } else {
                    // Regular string comparison
                    compare_result = strcasecmp(
                        result->rows[j][field_idx].value.str_val,
                        result->rows[j+1][field_idx].value.str_val
                    );
                }
            } else if (result->rows[j][field_idx].type == TYPE_INT) {
                int val1 = result->rows[j][field_idx].value.int_val;
                int val2 = result->rows[j+1][field_idx].value.int_val;
                compare_result = (val1 > val2) ? 1 : (val1 < val2) ? -1 : 0;
            } else if (result->rows[j][field_idx].type == TYPE_FLOAT) {
                float val1 = result->rows[j][field_idx].value.float_val;
                float val2 = result->rows[j+1][field_idx].value.float_val;
                compare_result = (val1 > val2) ? 1 : (val1 < val2) ? -1 : 0;
            }
            
            // If descending, invert comparison result
            if (descending) {
                compare_result = -compare_result;
            }
            
            // Swap if needed
            if (compare_result > 0) {
                DataValue *temp = result->rows[j];
                result->rows[j] = result->rows[j+1];
                result->rows[j+1] = temp;
            }
        }
    }
    
    return result;
}

/**
 * Select specific columns from a table
 */
TableData* lsh_select(TableData *input, char **args) {
    if (!input || !args || !args[0]) {
        fprintf(stderr, "lsh: select: missing arguments\n");
        fprintf(stderr, "Usage: ... | select FIELD1,FIELD2,...\n");
        fprintf(stderr, "  e.g.: ls | select Name,Size\n");
        return NULL;
    }
    
    // Parse fields to select
    char *field_list = args[0];
    char *field_copy = _strdup(field_list); // Make a copy since strtok modifies the string
    if (!field_copy) {
        fprintf(stderr, "lsh: allocation error in select\n");
        return NULL;
    }
    
    // Count how many fields are specified
    int num_fields = 1; // Start with 1 for the first field
    for (char *p = field_list; *p; p++) {
        if (*p == ',') num_fields++;
    }
    
    // Allocate array for field indices
    int *field_indices = (int*)malloc(num_fields * sizeof(int));
    if (!field_indices) {
        fprintf(stderr, "lsh: allocation error in select\n");
        free(field_copy);
        return NULL;
    }
    
    // Parse field names and get their indices
    char *field_token;
    char *saveptr;
    int field_count = 0;
    
    field_token = strtok_s(field_copy, ",", &saveptr);
    while (field_token && field_count < num_fields) {
        // Trim whitespace
        while (isspace(*field_token)) field_token++;
        char *end = field_token + strlen(field_token) - 1;
        while (end > field_token && isspace(*end)) end--;
        *(end + 1) = '\0';
        
        // Find field index
        int found = 0;
        for (int i = 0; i < input->header_count; i++) {
            if (strcasecmp(input->headers[i], field_token) == 0) {
                field_indices[field_count++] = i;
                found = 1;
                break;
            }
        }
        
        if (!found) {
            fprintf(stderr, "lsh: select: unknown field '%s'\n", field_token);
            free(field_indices);
            free(field_copy);
            return NULL;
        }
        
        field_token = strtok_s(NULL, ",", &saveptr);
    }
    
    free(field_copy);
    
    // Create new headers for result table
    char **new_headers = (char**)malloc(field_count * sizeof(char*));
    if (!new_headers) {
        fprintf(stderr, "lsh: allocation error in select\n");
        free(field_indices);
        return NULL;
    }
    
    for (int i = 0; i < field_count; i++) {
        new_headers[i] = _strdup(input->headers[field_indices[i]]);
    }
    
    // Create result table with new headers
    TableData *result = create_table(new_headers, field_count);
    
    // Free temporary header array
    for (int i = 0; i < field_count; i++) {
        free(new_headers[i]);
    }
    free(new_headers);
    
    if (!result) {
        free(field_indices);
        return NULL;
    }
    
    // Copy rows with only the selected columns
    for (int i = 0; i < input->row_count; i++) {
        DataValue *row = (DataValue*)malloc(field_count * sizeof(DataValue));
        if (!row) {
            fprintf(stderr, "lsh: allocation error in select\n");
            free_table(result);
            free(field_indices);
            return NULL;
        }
        
        for (int j = 0; j < field_count; j++) {
            row[j] = copy_data_value(&input->rows[i][field_indices[j]]);
        }
        
        add_table_row(result, row);
    }
    
    free(field_indices);
    return result;
}

/**
 * Case-insensitive substring search (strcasestr equivalent for Windows)
 */
char *my_strcasestr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return (char*)haystack;
    
    size_t haystack_len = strlen(haystack);
    if (haystack_len < needle_len) return NULL;
    
    for (size_t i = 0; i <= haystack_len - needle_len; i++) {
        if (_strnicmp(haystack + i, needle, needle_len) == 0) {
            return (char*)(haystack + i);
        }
    }
    
    return NULL;
}

/**
 * Filter rows where a column contains a substring
 */
TableData* lsh_contains(TableData *input, char **args) {
    if (!input || !args || !args[0] || !args[1]) {
        fprintf(stderr, "lsh: contains: missing arguments\n");
        fprintf(stderr, "Usage: ... | contains FIELD VALUE\n");
        fprintf(stderr, "  e.g.: ls | contains Name .exe\n");
        return NULL;
    }
    
    // Parse arguments
    char *field = args[0];
    char *value = args[1];
    
    // Find field index
    int field_idx = -1;
    for (int i = 0; i < input->header_count; i++) {
        if (strcasecmp(input->headers[i], field) == 0) {
            field_idx = i;
            break;
        }
    }
    
    if (field_idx == -1) {
        fprintf(stderr, "lsh: contains: unknown field '%s'\n", field);
        fprintf(stderr, "Available fields: ");
        for (int i = 0; i < input->header_count; i++) {
            fprintf(stderr, "%s%s", i > 0 ? ", " : "", input->headers[i]);
        }
        fprintf(stderr, "\n");
        return NULL;
    }
    
    // Create result table with same headers
    TableData *result = create_table(input->headers, input->header_count);
    if (!result) {
        return NULL;
    }
    
    // Filter rows based on whether the specified column contains the substring
    for (int i = 0; i < input->row_count; i++) {
        int include_row = 0;
        
        // Only string fields can be checked with contains
        if (input->rows[i][field_idx].type == TYPE_STRING || 
            input->rows[i][field_idx].type == TYPE_SIZE) {
            char *cell_value = input->rows[i][field_idx].value.str_val;
            
            // Case-insensitive substring check
            char *found = my_strcasestr(cell_value, value);
            include_row = (found != NULL);
        }
        
        if (include_row) {
            // Create a copy of the row for the result table
            DataValue *row_copy = (DataValue*)malloc(input->header_count * sizeof(DataValue));
            if (!row_copy) {
                fprintf(stderr, "lsh: allocation error in contains\n");
                free_table(result);
                return NULL;
            }
            
            for (int j = 0; j < input->header_count; j++) {
                row_copy[j] = copy_data_value(&input->rows[i][j]);
            }
            
            add_table_row(result, row_copy);
        }
    }
    
    return result;
}

/**
 * Limit the number of rows displayed
 */
TableData* lsh_limit(TableData *input, char **args) {
    if (!input || !args || !args[0]) {
        fprintf(stderr, "lsh: limit: missing arguments\n");
        fprintf(stderr, "Usage: ... | limit N\n");
        fprintf(stderr, "  e.g.: ls | sort-by Size desc | limit 5\n");
        return NULL;
    }
    
    // Parse limit
    int limit = atoi(args[0]);
    if (limit <= 0) {
        fprintf(stderr, "lsh: limit: invalid limit '%s', must be a positive number\n", args[0]);
        return NULL;
    }
    
    // Create result table with same headers
    TableData *result = create_table(input->headers, input->header_count);
    if (!result) {
        return NULL;
    }
    
    // Copy only the specified number of rows
    int rows_to_copy = (limit < input->row_count) ? limit : input->row_count;
    
    for (int i = 0; i < rows_to_copy; i++) {
        DataValue *row_copy = (DataValue*)malloc(input->header_count * sizeof(DataValue));
        if (!row_copy) {
            fprintf(stderr, "lsh: allocation error in limit\n");
            free_table(result);
            return NULL;
        }
        
        for (int j = 0; j < input->header_count; j++) {
            row_copy[j] = copy_data_value(&input->rows[i][j]);
        }
        
        add_table_row(result, row_copy);
    }
    
    return result;
}

// Define the filter arrays here
char *filter_str[] = {
    "where",
    "sort-by",
    "select",
    "contains",
    "limit"
};

TableData* (*filter_func[]) (TableData*, char**) = {
    &lsh_where,
    &lsh_sort_by,
    &lsh_select,
    &lsh_contains,
    &lsh_limit
};

int filter_count = sizeof(filter_str) / sizeof(char*);
