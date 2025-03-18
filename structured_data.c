/**
 * structured_data.c
 * Implementation of structured data operations
 */

#include "structured_data.h"
#include "builtins.h"  // For set_color and reset_color functions

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
    dest.is_highlighted = src->is_highlighted;  // Make sure we preserve the highlighting flag
    
    switch (src->type) {
        case TYPE_STRING:
        case TYPE_SIZE:
            dest.value.str_val = _strdup(src->value.str_val);
            break;
        case TYPE_INT:
            dest.value.int_val = src->value.int_val;
            break;
        case TYPE_FLOAT:
            dest.value.float_val = src->value.float_val;
            break;
    }
    
    return dest;
}

/**
 * Free a DataValue
 */
void free_data_value(DataValue *value) {
    if (!value) return;
    
    if ((value->type == TYPE_STRING || value->type == TYPE_SIZE) && value->value.str_val) {
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
    
    // Handle case insensitive units
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
    
    // Try the format with float + space + unit (e.g., "10.5 MB")
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
    // Modified to handle both "size" and "Memory" columns
    int is_size_field = (strcasecmp(field, "size") == 0 || strcasecmp(field, "Memory") == 0);
    long value_size = is_size_field ? parse_size(value) : 0;
    
    // Filter rows based on condition
    for (int i = 0; i < input->row_count; i++) {
        int include_row = 0;
        
        // Handle based on data type
        if (input->rows[i][field_idx].type == TYPE_STRING || 
            input->rows[i][field_idx].type == TYPE_SIZE) {
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
 * Print a table to the console with nice formatting using Unicode box characters
 * Ultra-optimized version with correct color handling (only text in green)
 * This maintains the batch-printing speed while fixing the colors
 */
void print_table(TableData *table) {
    if (!table || table->row_count == 0) {
        printf("(empty table)\n");
        return;
    }
    
    // Get handle to console for output
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    
    // Get console screen info
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int consoleHeight = 0;
    WORD originalAttributes;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        consoleHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
        originalAttributes = csbi.wAttributes;
    } else {
        consoleHeight = 25; // Common default height
        originalAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE; // Default white
    }
    
    // Calculate listing height
    int listingHeight = 6 + table->row_count;
    int needBottomHeader = (listingHeight > consoleHeight);
    
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
            if (table->rows[i][j].type == TYPE_STRING || table->rows[i][j].type == TYPE_SIZE) {
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
    
    // Calculate total table width
    int totalWidth = 1; // Start with 1 for the left border
    for (int i = 0; i < table->header_count; i++) {
        totalWidth += col_widths[i] + 1; // Add column width and right border
    }
    
    // Create a buffer big enough for one line of the table
    char *lineBuffer = (char*)malloc(totalWidth * 4 + 1); // Unicode chars can be up to 4 bytes
    if (!lineBuffer) {
        fprintf(stderr, "lsh: allocation error in print_table\n");
        free(col_widths);
        return;
    }
    
    // Begin output with a newline
    printf("\n");
    
    // ---- Print top border ----
    lineBuffer[0] = '\0';
    strcat(lineBuffer, "\u250C"); // Top-left corner ┌
    for (int i = 0; i < table->header_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            strcat(lineBuffer, "\u2500"); // Horizontal line ─
        }
        if (i < table->header_count - 1) {
            strcat(lineBuffer, "\u252C"); // Top T-junction ┬
        }
    }
    strcat(lineBuffer, "\u2510"); // Top-right corner ┐
    printf("%s\n", lineBuffer);
    
    // ---- Print header row ----
    lineBuffer[0] = '\0';
    strcat(lineBuffer, "\u2502"); // Vertical line │
    for (int i = 0; i < table->header_count; i++) {
        char cellBuffer[256];
        snprintf(cellBuffer, sizeof(cellBuffer), " %-*s ", col_widths[i] - 2, table->headers[i]);
        strcat(lineBuffer, cellBuffer);
        strcat(lineBuffer, "\u2502"); // Vertical line │
    }
    printf("%s\n", lineBuffer);
    
    // ---- Print header/data separator ----
    lineBuffer[0] = '\0';
    strcat(lineBuffer, "\u251C"); // Left T-junction ├
    for (int i = 0; i < table->header_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            strcat(lineBuffer, "\u2500"); // Horizontal line ─
        }
        if (i < table->header_count - 1) {
            strcat(lineBuffer, "\u253C"); // Cross junction ┼
        }
    }
    strcat(lineBuffer, "\u2524"); // Right T-junction ┤
    printf("%s\n", lineBuffer);
    
    // ---- Pre-build the row template with borders in normal color ----
    char *rowTemplate = (char*)malloc(totalWidth * 4 + 1);
    if (!rowTemplate) {
        fprintf(stderr, "lsh: allocation error in print_table\n");
        free(lineBuffer);
        free(col_widths);
        return;
    }
    
    // Build a template for each row with borders in default color
    int templatePos = 0;
    rowTemplate[templatePos++] = '\u2502'; // Left border
    rowTemplate[templatePos] = '\0';
    
    for (int j = 0; j < table->header_count; j++) {
        // Add placeholder for cell content (we'll format and print these separately in green)
        char *placeholder = "%-*s";
        strcat(rowTemplate, placeholder);
        templatePos += strlen(placeholder);
        
        // Add right border
        rowTemplate[templatePos++] = '\u2502';
        rowTemplate[templatePos] = '\0';
    }
    
    // Now for each row, we'll print the borders in normal color and content in green
    for (int i = 0; i < table->row_count; i++) {
        // Prepare all cell contents in advance
        char **cellContents = (char**)malloc(table->header_count * sizeof(char*));
        if (!cellContents) {
            fprintf(stderr, "lsh: allocation error in print_table\n");
            free(rowTemplate);
            free(lineBuffer);
            free(col_widths);
            return;
        }
        
        for (int j = 0; j < table->header_count; j++) {
            cellContents[j] = (char*)malloc(col_widths[j] + 1);
            if (!cellContents[j]) {
                // Handle allocation failure
                for (int k = 0; k < j; k++) {
                    free(cellContents[k]);
                }
                free(cellContents);
                free(rowTemplate);
                free(lineBuffer);
                free(col_widths);
                return;
            }
            
            // Format the cell content
            if (table->rows[i][j].type == TYPE_STRING || table->rows[i][j].type == TYPE_SIZE) {
                sprintf(cellContents[j], " %-*s ", col_widths[j] - 2, table->rows[i][j].value.str_val);
            } else if (table->rows[i][j].type == TYPE_INT) {
                sprintf(cellContents[j], " %-*d ", col_widths[j] - 2, table->rows[i][j].value.int_val);
            } else if (table->rows[i][j].type == TYPE_FLOAT) {
                sprintf(cellContents[j], " %-*.2f ", col_widths[j] - 2, table->rows[i][j].value.float_val);
            }
        }
        
        // Print the left border in normal color
        printf("\u2502");
        
        // For each cell, print cell in green and border in normal
        for (int j = 0; j < table->header_count; j++) {
            // Print cell content in green
            SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_INTENSITY);
            printf("%s", cellContents[j]);
            
            // Print border in normal color
            SetConsoleTextAttribute(hConsole, originalAttributes);
            if (j < table->header_count - 1) {
                printf("\u2502");
            } else {
                printf("\u2502\n");
            }
        }
        
        // Clean up cell contents
        for (int j = 0; j < table->header_count; j++) {
            free(cellContents[j]);
        }
        free(cellContents);
    }
    
    // Reset to original color (just to be safe)
    SetConsoleTextAttribute(hConsole, originalAttributes);
    
    // ---- Print bottom border ----
    lineBuffer[0] = '\0';
    strcat(lineBuffer, "\u2514"); // Bottom-left corner └
    for (int i = 0; i < table->header_count; i++) {
        for (int j = 0; j < col_widths[i]; j++) {
            strcat(lineBuffer, "\u2500"); // Horizontal line ─
        }
        if (i < table->header_count - 1) {
            strcat(lineBuffer, "\u2534"); // Bottom T-junction ┴
        }
    }
    strcat(lineBuffer, "\u2518"); // Bottom-right corner ┘
    printf("%s\n\n", lineBuffer);
    
    // Clean up
    free(rowTemplate);
    free(lineBuffer);
    free(col_widths);
}
