/**
 * main.c
 * Entry point for the LSH shell program
 */

#include "command_docs.h"
#include "common.h"
#include "external_commands.h"
#include "shell.h"

/**
 * Main entry point
 */

int main(int argc, char **argv) {
  // Set console output code page to UTF-8 (65001)
  // This enables proper display of UTF-8 box characters
  UINT oldCP = GetConsoleOutputCP();
  SetConsoleOutputCP(65001);

  // Initialize external commands by scanning PATH
  init_external_commands();

  // Add some common commands that might not be in PATH but often exist
  add_external_command("git");
  add_external_command("npm");
  add_external_command("node");
  add_external_command("python");
  add_external_command("python3");
  add_external_command("pip");
  add_external_command("lazygit");
  add_external_command("neofetch");
  add_external_command("docker");
  add_external_command("kubectl");
  add_external_command("vim");
  add_external_command("nvim");
  add_external_command("code"); // VSCode

  // Initialize command documentation system
  init_command_docs();

  // Pre-load documentation for commonly used external commands
  // This will make their documentation available immediately
  load_command_doc("git");
  load_command_doc("npm");
  load_command_doc("python");
  load_command_doc("lazygit");
  load_command_doc("neofetch");

  // Start the shell loop
  lsh_loop();

  // Clean up command documentation
  cleanup_command_docs();

  // Clean up external commands
  cleanup_external_commands();

  // Restore the original code page (optional cleanup)
  SetConsoleOutputCP(oldCP);

  return EXIT_SUCCESS;
}
