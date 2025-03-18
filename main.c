/**
 * main.c
 * Entry point for the LSH shell program
 */

#include "common.h"
#include "shell.h"

/**
 * Main entry point
 */
int main(int argc, char **argv) {
    // Set console output code page to UTF-8 (65001)
    // This enables proper display of UTF-8 box characters
    UINT oldCP = GetConsoleOutputCP();
    SetConsoleOutputCP(65001);
    
    // We don't need to modify console input mode here
    // The line_reader.c will handle this temporarily during input
    
    // Start the shell loop
    lsh_loop();
    
    // Restore the original code page (optional cleanup)
    SetConsoleOutputCP(oldCP);
    
    return EXIT_SUCCESS;
}
