/**
 * Modified image_display.c
 * Improved terminal image display functionality
 */

#include "common.h"
#include "builtins.h"
#include <windows.h>
#include <stdint.h>

// Basic base64 encoding implementation
static const char base64_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char* base64_encode(const unsigned char* data, size_t input_length) {
    size_t output_length = 4 * ((input_length + 2) / 3);
    char* encoded_data = (char*)malloc(output_length + 1);
    
    if (encoded_data == NULL) return NULL;
    
    for (size_t i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;
        
        uint32_t triple = (octet_a << 16) + (octet_b << 8) + octet_c;
        
        encoded_data[j++] = base64_chars[(triple >> 18) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 12) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 6) & 0x3F];
        encoded_data[j++] = base64_chars[triple & 0x3F];
    }
    
    // Padding
    size_t mod_table[] = {0, 2, 1};
    for (size_t i = 0; i < mod_table[input_length % 3]; i++)
        encoded_data[output_length - 1 - i] = '=';
    
    encoded_data[output_length] = '\0';
    return encoded_data;
}

/**
 * Improved check for terminal graphics capability
 * Now supports more terminals and has a force option
 */
BOOL is_terminal_graphics_capable() {
    // Check environment variables that would indicate Windows Terminal
    char* wt_session = getenv("WT_SESSION");
    char* term_program = getenv("TERM_PROGRAM");
    char* term = getenv("TERM");
    
    // Windows Terminal
    if (wt_session != NULL) return TRUE;
    
    // iTerm
    if (term_program != NULL && strstr(term_program, "iTerm")) return TRUE;
    
    // Additional terminal checks for common terminals that might support images
    if (term != NULL) {
        if (strstr(term, "xterm") || 
            strstr(term, "konsole") || 
            strstr(term, "vscode") ||
            strstr(term, "alacritty") ||
            strstr(term, "kitty")) {
            return TRUE;
        }
    }
    
    // Additional Windows Terminal detection
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut != INVALID_HANDLE_VALUE) {
        DWORD consoleMode = 0;
        if (GetConsoleMode(hOut, &consoleMode)) {
            // Check for VT processing support
            if (consoleMode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
                return TRUE;
            }
        }
    }

    // Default to no support
    return FALSE;
}

/**
 * Display an image in the terminal - improved version
 */
int lsh_img(char **args) {
    BOOL force_mode = FALSE;
    char *file_path = NULL;
    
    // Parse arguments
    if (args[1] == NULL) {
        fprintf(stderr, "lsh: expected image file path\n");
        fprintf(stderr, "Usage: img <file_path> or img -f <file_path> to force display\n");
        return 1;
    }
    
    // Check for force flag
    if (strcmp(args[1], "-f") == 0 || strcmp(args[1], "--force") == 0) {
        force_mode = TRUE;
        
        if (args[2] == NULL) {
            fprintf(stderr, "lsh: expected image file path after -f\n");
            return 1;
        }
        file_path = args[2];
    } else {
        file_path = args[1];
    }
    
    // Check if the terminal supports images (skip if force mode)
    if (!force_mode && !is_terminal_graphics_capable()) {
        fprintf(stderr, "lsh: your terminal doesn't appear to support inline images\n");
        fprintf(stderr, "Try using Windows Terminal or use 'img -f %s' to force display\n", file_path);
        return 1;
    }
    
    // Check if file exists
    DWORD fileAttributes = GetFileAttributes(file_path);
    if (fileAttributes == INVALID_FILE_ATTRIBUTES) {
        fprintf(stderr, "lsh: image file not found: %s\n", file_path);
        return 1;
    }
    
    // Load the image file into memory
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        fprintf(stderr, "lsh: failed to open image: %s\n", file_path);
        return 1;
    }
    
    // Get file size
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (fileSize <= 0 || fileSize > 5 * 1024 * 1024) {  // 5MB limit
        fclose(file);
        fprintf(stderr, "lsh: image file is too large or empty\n");
        return 1;
    }
    
    // Allocate memory for image data
    unsigned char *buffer = (unsigned char*)malloc(fileSize);
    if (!buffer) {
        fclose(file);
        fprintf(stderr, "lsh: memory allocation error\n");
        return 1;
    }
    
    // Read file content
    size_t bytesRead = fread(buffer, 1, fileSize, file);
    fclose(file);
    
    if (bytesRead != fileSize) {
        free(buffer);
        fprintf(stderr, "lsh: error reading image file\n");
        return 1;
    }
    
    // Base64 encode the image data
    char *base64Data = base64_encode(buffer, fileSize);
    free(buffer);
    
    if (!base64Data) {
        fprintf(stderr, "lsh: base64 encoding failed\n");
        return 1;
    }
    
    // Determine image format from file extension
    char *extension = strrchr(file_path, '.');
    if (!extension) {
        free(base64Data);
        fprintf(stderr, "lsh: unknown image format\n");
        return 1;
    }
    
    extension++;  // Skip the dot
    char *format = "png";  // Default format
    
    if (_stricmp(extension, "jpg") == 0 || _stricmp(extension, "jpeg") == 0) {
        format = "jpeg";
    } else if (_stricmp(extension, "gif") == 0) {
        format = "gif";
    } else if (_stricmp(extension, "bmp") == 0) {
        format = "bmp";
    }
    
    // Display information about the image
    printf("\nDisplaying image: %s (%s format, %.1f KB)\n\n", 
           file_path, format, fileSize / 1024.0);

    // Enable VT processing just to be sure
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);
    
    // Try multiple escape sequence styles for maximum compatibility
    
    // Style 1: Standard iTerm2/Windows Terminal format with all parameters
    printf("\033]1337;File=name=%s;inline=1;width=auto;height=auto;preserveAspectRatio=1:", file_path);
    printf("%s", base64Data);
    printf("\007\n");
    
    // Style 2: Minimal format (some terminals prefer this)
    printf("\033]1337;File=inline=1:");
    printf("%s", base64Data);
    printf("\007\n");
    
    // Style 3: With format specification
    printf("\033]1337;File=inline=1;width=auto;height=auto;preserveAspectRatio=1;format=%s:", format);
    printf("%s", base64Data);
    printf("\007\n");
    
    free(base64Data);
    printf("\nImage display complete.\n");
    return 1;
}
