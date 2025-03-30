/**
 * fzf.h
 * Header for fuzzy file finder functionality
 */

#ifndef FZF_H
#define FZF_H

#include "builtins.h"
#include "common.h"

/**
 * Command handler for the "fzf" command
 *
 * @param args Command arguments
 * @return 1 to continue shell, 0 to exit
 */
int lsh_fzf(char **args);

#endif // FZF_H
