/**
 * git_integration.c
 * Implementation of Git repository detection and information
 */

#include "git_integration.h"
#include <stdio.h>
#include <string.h>

/**
 * Check if the current directory is in a Git repository and get branch info
 */
int get_git_branch(char *branch_name, size_t buffer_size, int *is_dirty) {
    char git_dir[1024] = "";
    char cmd[1024] = "";
    FILE *fp;
    int status = 0;
    
    // Initialize output parameters
    if (branch_name && buffer_size > 0) {
        branch_name[0] = '\0';
    }
    if (is_dirty) {
        *is_dirty = 0;
    }
    
    // First check if .git directory exists (faster than running git command)
    strcpy(git_dir, ".git");
    DWORD attr = GetFileAttributes(git_dir);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        // Try running git command to handle the case of being in a subdirectory
        // of a git repo
        snprintf(cmd, sizeof(cmd), "git rev-parse --is-inside-work-tree 2>nul");
        fp = _popen(cmd, "r");
        if (!fp) {
            return 0;
        }
        
        char output[16];
        if (fgets(output, sizeof(output), fp) == NULL || strcmp(output, "true\n") != 0) {
            _pclose(fp);
            return 0;
        }
        _pclose(fp);
    }
    
    // We're in a git repo, get the branch name
    snprintf(cmd, sizeof(cmd), "git branch --show-current 2>nul");
    fp = _popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    
    // Read the branch name
    if (fgets(branch_name, buffer_size, fp)) {
        // Remove newline
        size_t len = strlen(branch_name);
        if (len > 0 && branch_name[len - 1] == '\n') {
            branch_name[len - 1] = '\0';
        }
        status = 1;
    }
    
    _pclose(fp);
    
    // If branch name is empty, we might be in a detached HEAD state
    if (status && strlen(branch_name) == 0) {
        // Get the current commit hash instead
        snprintf(cmd, sizeof(cmd), "git rev-parse --short HEAD 2>nul");
        fp = _popen(cmd, "r");
        if (fp) {
            if (fgets(branch_name, buffer_size, fp)) {
                // Remove newline
                size_t len = strlen(branch_name);
                if (len > 0 && branch_name[len - 1] == '\n') {
                    branch_name[len - 1] = '\0';
                }
                // Format for detached HEAD state
                char temp[buffer_size];
                snprintf(temp, buffer_size, "detached:%s", branch_name);
                strncpy(branch_name, temp, buffer_size - 1);
                branch_name[buffer_size - 1] = '\0';
            }
            _pclose(fp);
        }
    }
    
    // Check if repo has uncommitted changes
    if (is_dirty && status) {
        snprintf(cmd, sizeof(cmd), "git status --porcelain 2>nul");
        fp = _popen(cmd, "r");
        if (fp) {
            // If there's any output, the repo has changes
            char dirty_check[10];
            *is_dirty = (fgets(dirty_check, sizeof(dirty_check), fp) != NULL);
            _pclose(fp);
        }
    }
    
    return status;
}

/**
 * Get the name of the Git repository from its directory
 * 
 * @param repo_name Buffer to store repository name if found
 * @param buffer_size Size of the repo_name buffer
 * @return 1 if successful, 0 otherwise
 */
int get_git_repo_name(char *repo_name, size_t buffer_size) {
    char cmd[1024] = "";
    FILE *fp;
    int status = 0;
    
    // Initialize output parameter
    if (repo_name && buffer_size > 0) {
        repo_name[0] = '\0';
    }
    
    // Get the path to the root of the Git repository
    snprintf(cmd, sizeof(cmd), "git rev-parse --show-toplevel 2>nul");
    fp = _popen(cmd, "r");
    if (!fp) {
        return 0;
    }
    
    char git_dir[1024] = "";
    if (fgets(git_dir, sizeof(git_dir), fp)) {
        // Remove newline
        size_t len = strlen(git_dir);
        if (len > 0 && git_dir[len - 1] == '\n') {
            git_dir[len - 1] = '\0';
        }
        
        // First try backslash (Windows style)
        char *last_sep = strrchr(git_dir, '\\');
        
        // If not found, try forward slash (Git/Unix style)
        if (!last_sep) {
            last_sep = strrchr(git_dir, '/');
        }
        
        if (last_sep) {
            strncpy(repo_name, last_sep + 1, buffer_size - 1);
            repo_name[buffer_size - 1] = '\0';
            status = 1;
        }
    }
    
    _pclose(fp);
    return status;
}
