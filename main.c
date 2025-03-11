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
    // Start the shell loop
    lsh_loop();
    
    return EXIT_SUCCESS;
}
