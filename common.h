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


#ifndef CP_UTF8
#define CP_UTF8 65001
#endif

// Key codes
#define KEY_TAB 9
#define KEY_BACKSPACE 8
#define KEY_ENTER 13
#define KEY_ESC 27

#endif // COMMON_H
