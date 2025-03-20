/**
 * git_integration.h
 * Functions for Git repository detection and information
 */

#ifndef GIT_INTEGRATION_H
#define GIT_INTEGRATION_H

#include "common.h"

/**
 * Check if the current directory is in a Git repository and get branch info
 * 
 * @param branch_name Buffer to store branch name if found
 * @param buffer_size Size of the branch_name buffer
 * @param is_dirty Pointer to store dirty status flag (1 if repo has changes)
 * @return 1 if in a Git repo, 0 otherwise
 */
int get_git_branch(char *branch_name, size_t buffer_size, int *is_dirty);

#endif // GIT_INTEGRATION_H
