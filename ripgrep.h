#ifndef RIPGREP_H
#define RIPGREP_H

#include "builtins.h"
#include "common.h"
#include "fzf_native.h"

int is_rg_installed(void);

void show_fzf_install_instructions(void);

char *run_interactive_ripgrep(char **args);

int lsh_grep(char **args);

int rg_open_in_editor(const char *file_path, int line_number);

#endif // RIPGREP_H
