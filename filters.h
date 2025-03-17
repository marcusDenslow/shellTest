/**
 * filters.h
 * Declarations for filter commands (where, sort-by, etc.)
 */

#ifndef FILTERS_H
#define FILTERS_H

#include "structured_data.h"

/**
 * Filter a table based on a condition (e.g., where size > 10kb)
 * 
 * @param input The input table
 * @param args Command arguments
 * @return Filtered table or NULL on error
 */
TableData* lsh_where(TableData *input, char **args);

#endif // FILTERS_H
