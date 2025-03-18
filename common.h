/**
 * common.h
 * Common includes, defines and macros used throughout the shell
 */

#ifndef COMMON_H
#define COMMON_H

// Standard library includes
#include <fileapi.h>
#include <minwinbase.h>
#include <timezoneapi.h>
#include <windows.h>
#include <process.h>  // For _spawn functions
#include <direct.h>   // For *chdir and *getcwd
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>    // For _getch
#include <ctype.h>    // For isprint
#include <winerror.h>
#include <winnt.h>
#include <fcntl.h>    // For _O_BINARY
#include <io.h>       // For _setmode, _fileno

// Common defines
#define LSH_RL_BUFSIZE 1024
#define LSH_TOK_BUFSIZE 64
#define LSH_TOK_DELIM " \t\r\n\a"

// Console and input defines
#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

// Virtual Terminal defines
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
#define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif

#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif

// Key code definitions used by the line reader
#define KEY_BACKSPACE 8
#define KEY_TAB 9
#define KEY_ENTER 13
#define KEY_ESCAPE 27

// Special keys when reading with getch()
#define RAW_KEY_UP 72
#define RAW_KEY_DOWN 80
#define RAW_KEY_LEFT 75
#define RAW_KEY_RIGHT 77

#endif // COMMON_H
