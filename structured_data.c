/**
 * structured_data.c
 * Implementation of structured data operations
 */

#include "structured_data.h"

/**
 * Create a new table with the given headers
 */
TableData* create_table(char **headers, int header_count) {
    TableData *table = (TableData*)malloc(sizeof(TableData));
    if (!table) {
        fprintf(stderr, "lsh: allocation error in create_table\n");
        return NULL;
    }
    
    // Copy headers
    table->headers = (char**)malloc(header_count * sizeof(char*));
    if (!table->headers) {
        fprintf(stderr, "lsh: allocation error in create_table (headers)\n");
        free(table);
        return NULL;
    }
    
    for (int i = 0; i < header_count; i++) {
        table->headers[i] = _strdup(headers[i]);
    }
    
    table->header_count = header_count;
    table->row_count = 0;
    table->row_capacity = 10;  // Initial capacity for 10 rows
    
    // Allocate memory for rows
    table->rows = (DataValue**)malloc(table->row_capacity * sizeof(DataValue*));
    if (!table->rows) {
        fprintf(stderr, "lsh: allocation error in create_table (rows)\n");
        for (int i = 0; i < header_count; i++) {
            free(table->headers[i]);
        }
        free(table->headers);
        free(table);
        return NULL;
    }
    
    return table;
}

/**
 * Add a row to a table
 */
void add_table_row(TableData *table, DataValue *row) {
    if (!table || !row) return;
    
    // Resize if needed
    if (table->row_count >= table->row_capacity) {
        table->row_capacity *= 2;
        table->rows = (DataValue**)realloc(table->rows, table->row_capacity * sizeof(DataValue*));
        if (!table->rows) {
            fprintf(stderr, "lsh: allocation error in add_table_row\n");
            return;
        }
    }
    
    // Add the row
    table->rows[table->row_count] = row;
    table->row_count++;
}

/**
 * Free all memory associated with a table
 */
void free_table(TableData *table) {
    if (!table) return;
    
    // Free headers
    for (int i = 0; i < table->header_count; i++) {
        free(table->headers[i]);
    }
    free(table->headers);
    
    // Free rows
    for (int i = 0; i < table->row_count; i++) {
        for (int j = 0; j < table->header_count; j++) {
            free_data_value(&table->rows[i][j]);
        }
        free(table->rows[i]);
    }
    free(table->rows);
    
    // Free table structure
    free(table);
}

/**
 * Create a copy of a DataValue
 */
DataValue copy_data_value(const DataValue *src) {
    DataValue dest;
    dest.type = src->type;
    
    switch (src->type) {
        case TYPE_STRING:
            dest.value.str_val = _strdup(src->value.str_val);
            break;
        case TYPE_INT:
            dest.value.int_val = src->value.int_val;
            break;
        case TYPE_FLOAT:
            dest.value.float_val = src->value.float_val;
            break;
        case TYPE_SIZE:
            dest.value.size_val = src->value.size_val;
            break;
    }
    
    return dest;
}

/**
 * Free a DataValue
 */
void free_data_value(DataValue *value) {
    if (!value) return;
    
    if (value->type == TYPE_STRING && value->value.str_val) {
        free(value->value.str_val);
        value->value.str_val = NULL;
    }
}

/**
 * Parse human-readable sizes (e.g., "10kb", "2.5MB") into bytes
 */
long parse_size(const char *size_str) {
    char *unit;
    double size = strtod(size_str, &unit);
    
    // Skip whitespace between number and unit
    while (*unit && isspace(*unit)) unit++;
    
    if (strcasecmp(unit, "kb") == 0 || strcasecmp(unit, "k") == 0) {
        return (long)(size * 1024);
    } else if (strcasecmp(unit, "mb") == 0 || strcasecmp(unit, "m") == 0) {
        return (long)(size * 1024 * 1024);
    } else if (strcasecmp(unit, "gb") == 0 || strcasecmp(unit, "g") == 0) {
        return (long)(size * 1024 * 1024 * 1024);
    } else if (strcasecmp(unit, "b") == 0 || *unit == '\0') {
        return (long)size;
    }
    
    // Try to handle cases like "2.5 KB" with a space
    char num_str[32];
    char unit_str[8];
    if (sscanf(size_str, "%31s %7s", num_str, unit_str) == 2) {
        double num = atof(num_str);
        if (strcasecmp(unit_str, "kb") == 0 || strcasecmp(unit_str, "k") == 0) {
            return (long)(num * 1024);
        } else if (strcasecmp(unit_str, "mb") == 0 || strcasecmp(unit_str, "m") == 0) {
            return (long)(num * 1024 * 1024);
        } else if (strcasecmp(unit_str, "gb") == 0 || strcasecmp(unit_str, "g") == 0) {
            return (long)(num * 1024 * 1024 * 1024);
        } else if (strcasecmp(unit_str, "b") == 0) {
            return (long)num;
        }
    }
    
    return (long)size;  // Default to just the number if no unit matches
}

/**
 * Extract size in bytes from a formatted size string (e.g., "10.5 KB")
 */
long extract_size_bytes(const char *size_str) {
    double size_val = 0;
    char unit[8] = "";
    
    if (sscanf(size_str, "%lf %7s", &size_val, unit) == 2) {
        if (strcasecmp(unit, "B") == 0) {
            return (long)size_val;
        } else if (strcasecmp(unit, "KB") == 0) {
            return (long)(size_val * 1024);
        } else if (strcasecmp(unit, "MB") == 0) {
            return (long)(size_val * 1024 * 1024);
        } else if (strcasecmp(unit, "GB") == 0) {
            return (long)(size_val * 1024 * 1024 * 1024);
        }
    }
    
    // Fallback to direct parsing if the format doesn't match
    return parse_size(size_str);
}

/**
 * Filter a table based on a condition
 */
TableData* filter_table(TableData *input, char *field, char *op, char *value) {
    if (!input || !field || !op || !value) {
        return NULL;
    }
    
    // Create a new table with the same headers
    TableData *result = create_table(input->headers, input->header_count);
    if (!result) {
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
        fprintf(stderr, "filter_table: unknown field '%s'\n", field);
        free_table(result);
        return NULL;
    }
    
    // Special handling for size field - parse human-readable sizes
    int is_size_field = (strcasecmp(field, "size") == 0);
    long value_size = is_size_field ? parse_size(value) : 0;
    
    // Filter rows based on condition
    for (int i = 0; i < input->row_count; i++) {
        int include_row = 0;
        
        // Handle based on data type
        if (input->rows[i][field_idx].type == TYPE_STRING) {
            char *row_value = input->rows[i][field_idx].value.str_val;
            
            // Special handling for size field with values like "10.5 KB"
            if (is_size_field) {
                long row_size = extract_size_bytes(row_value);
                
                // Compare sizes
                if (strcmp(op, ">") == 0) {
                    include_row = (row_size > value_size);
                } else if (strcmp(op, "<") == 0) {
                    include_row = (row_size < value_size);
                } else if (strcmp(op, ">=") == 0) {
                    include_row = (row_size >= value_size);
                } else if (strcmp(op, "<=") == 0) {
                    include_row = (row_size <= value_size);
                } else if (strcmp(op, "==") == 0) {
                    include_row = (row_size == value_size);
                }
            } else {
                // String comparison
                int cmp = strcasecmp(row_value, value);
                if (strcmp(op, ">") == 0) {
                    include_row = (cmp > 0);
                } else if (strcmp(op, "<") == 0) {
                    include_row = (cmp < 0);
                } else if (strcmp(op, "==") == 0) {
                    include_row = (cmp == 0);
                } else if (strcmp(op, ">=") == 0) {
                    include_row = (cmp >= 0);
                } else if (strcmp(op, "<=") == 0) {
                    include_row = (cmp <= 0);
                }
            }
        } else if (input->rows[i][field_idx].type == TYPE_INT) {
            int row_value = input->rows[i][field_idx].value.int_val;
            int val = atoi(value);
            
            if (strcmp(op, ">") == 0) {
                include_row = (row_value > val);
            } else if (strcmp(op, "<") == 0) {
                include_row = (row_value < val);
            } else if (strcmp(op, ">=") == 0) {
                include_row = (row_value >= val);
            } else if (strcmp(op, "<=") == 0) {
                include_row = (row_value <= val);
            } else if (strcmp(op, "==") == 0) {
                include_row = (row_value == val);
            }
        } else if (input->rows[i][field_idx].type == TYPE_FLOAT) {
            float row_value = input->rows[i][field_idx].value.float_val;
            float val = (float)atof(value);
            
            if (strcmp(op, ">") == 0) {
                include_row = (row_value > val);
            } else if (strcmp(op, "<") == 0) {
                include_row = (row_value < val);
            } else if (strcmp(op, ">=") == 0) {
                include_row = (row_value >= val);
            } else if (strcmp(op, "<=") == 0) {
                include_row = (row_value <= val);
            } else if (strcmp(op, "==") == 0) {
                include_row = (row_value == val);
            }
        }
        
        if (include_row) {
            // Create a copy of the row for the filtered table
            DataValue *row_copy = (DataValue*)malloc(input->header_count * sizeof(DataValue));
            if (!row_copy) {
                fprintf(stderr, "lsh: allocation error in filter_table\n");
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
 * Print a table to the console with nice formatting
 */
void print_table(TableData *table) {
    if (!table || table->row_count == 0) {
        printf("(empty table)\n");
        return;
    }
    
    // Calculate column widths
    int *col_widths = (int*)malloc(table->header_count * sizeof(int));
    if (!col_widths) {
        fprintf(stderr, "lsh: allocation error in print_table\n");
        return;
    }
    
    // Initialize with header widths
    for (int i = 0; i < table->header_count; i++) {
        col_widths[i] = strlen(table->headers[i]);
    }
    
    // Check cell widths
    for (int i = 0; i < table->row_count; i++) {
        for (int j = 0; j < table->header_count; j++) {
            if (table->rows[i][j].type == TYPE_STRING) {
                int len = strlen(table->rows[i][j].value.str_val);
                if (len > col_widths[j]) {
                    col_widths[j] = len;
                }
            } else if (table->rows[i][j].type == TYPE_INT) {
                char buf[32];
                int len = snprintf(buf, sizeof(buf), "%d", table->rows[i][j].value.int_val);
                if (len > col_widths[j]) {
                    col_widths[j] = len;
                }
            } else if (table->rows[i][j].type == TYPE_FLOAT) {
                char buf[32];
                int len = snprintf(buf, sizeof(buf), "%.2f", table->rows[i][j].value.float_val);
                if (len > col_widths[j]) {
                    col_widths[j] = len;
                }
            }
        }
    }
    
    // Add padding to column widths
    for (int i = 0; i < table->header_count; i++) {
        col_widths[i] += 4;  // 2 spaces on each side
    }
    
    // Print table header
    printf("\n");
    
    // Print top border
    printf("+");
    for (int i = 0; i < table->header_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            printf("-");
        }
        printf("+");
    }
    printf("\n");
    
    // Print header row
    printf("|");
    for (int i = 0; i < table->header_count; i++) {
        printf(" %-*s |", col_widths[i] - 2, table->headers[i]);
    }
    printf("\n");
    
    // Print header/data separator
    printf("+");
    for (int i = 0; i < table->header_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            printf("-");
        }
        printf("+");
    }
    printf("\n");
    
    // Print data rows
    for (int i = 0; i < table->row_count; i++) {
        printf("|");
        for (int j = 0; j < table->header_count; j++) {
            if (table->rows[i][j].type == TYPE_STRING) {
                printf(" %-*s |", col_widths[j] - 2, table->rows[i][j].value.str_val);
            } else if (table->rows[i][j].type == TYPE_INT) {
                printf(" %-*d |", col_widths[j] - 2, table->rows[i][j].value.int_val);
            } else if (table->rows[i][j].type == TYPE_FLOAT) {
                printf(" %-*.2f |", col_widths[j] - 2, table->rows[i][j].value.float_val);
            }
        }
        printf("\n");
    }
    
    // Print bottom border
    printf("+");
    for (int i = 0; i < table->header_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            printf("-");
        }
        printf("+");
    }
    printf("\n\n");
    
    free(col_widths);
}
