/**
 * ps_command.c
 * Implementation of the 'ps' command for listing running processes
 * Includes support for piping and filtering
 */

#include "builtins.h"
#include "common.h"
#include <psapi.h>
#include <tlhelp32.h>

/**
 * Function to generate structured data for running processes 
 * This enables piping and filtering of process information
 * 
 * @param args Command arguments
 * @return TableData structure with process information
 */
TableData* lsh_ps_structured(char **args) {
    HANDLE hSnapshot;
    PROCESSENTRY32 pe32;
    int processCount = 0;
    
    // Define our table headers
    char *headers[] = {"PID", "Name", "Memory", "Threads"};
    int header_count = 4;
    
    // Create our table
    TableData *table = create_table(headers, header_count);
    if (!table) {
        fprintf(stderr, "lsh: allocation error in ps_structured\n");
        return NULL;
    }
    
    // Create a snapshot of all processes
    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "lsh: failed to create process snapshot\n");
        free_table(table);
        return NULL;
    }
    
    // Set the size of the structure before using it
    pe32.dwSize = sizeof(PROCESSENTRY32);
    
    // Retrieve information about the first process
    if (!Process32First(hSnapshot, &pe32)) {
        fprintf(stderr, "lsh: failed to get process information\n");
        CloseHandle(hSnapshot);
        free_table(table);
        return NULL;
    }
    
    // Walk through the snapshot of processes
    do {
        // Check if this is likely a user process by using heuristics
        BOOL isUserProcess = FALSE;
        
        // Common system process executables to exclude
        static const char *systemProcesses[] = {
            "svchost.exe", "csrss.exe", "smss.exe", "wininit.exe", 
            "services.exe", "lsass.exe", "winlogon.exe",
            "spoolsv.exe", "dwm.exe", "taskhost.exe", "taskhostw.exe",
            "conhost.exe", "system", "registry", "dllhost.exe",
            "msdtc.exe", "sqlservr.exe", "w3wp.exe", "inetinfo.exe"
        };
        
        BOOL isSystemProcess = FALSE;
        for (int i = 0; i < sizeof(systemProcesses) / sizeof(systemProcesses[0]); i++) {
            if (_stricmp(pe32.szExeFile, systemProcesses[i]) == 0) {
                isSystemProcess = TRUE;
                break;
            }
        }
        
        // User processes are those that are not system processes
        isUserProcess = !isSystemProcess;
        
        // Get additional process info
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe32.th32ProcessID);
        SIZE_T memoryUsage = 0;
        
        if (hProcess != NULL) {
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                memoryUsage = pmc.WorkingSetSize;
            }
            CloseHandle(hProcess);
        }
        
        // Include if not a system process or if it has a significant memory footprint
        if (!isSystemProcess || memoryUsage > 5 * 1024 * 1024) {  // > 5MB is likely a user app
            // Create a new row for this process
            DataValue *row = (DataValue*)malloc(header_count * sizeof(DataValue));
            if (!row) {
                fprintf(stderr, "lsh: allocation error in ps_structured\n");
                free_table(table);
                CloseHandle(hSnapshot);
                return NULL;
            }
            
            // Set PID (as a string for compatibility)
            char pidStr[20];
            sprintf(pidStr, "%lu", pe32.th32ProcessID);
            row[0].type = TYPE_STRING;
            row[0].value.str_val = _strdup(pidStr);
            
            // Set process name
            row[1].type = TYPE_STRING;
            row[1].value.str_val = _strdup(pe32.szExeFile);
            
            // Format memory usage string (important for filtering)
            char memoryString[32];
            if (memoryUsage < 1024) {
                sprintf(memoryString, "%llu B", (unsigned long long)memoryUsage);
            } else if (memoryUsage < 1024 * 1024) {
                sprintf(memoryString, "%.1f KB", memoryUsage / 1024.0);
            } else {
                // Format as MB for consistency in filtering
                sprintf(memoryString, "%.1f MB", memoryUsage / (1024.0 * 1024.0));
            }
            
            row[2].type = TYPE_SIZE;  // Use the special SIZE type for filtering
            row[2].value.str_val = _strdup(memoryString);
            
            // Set thread count
            char threadStr[20];
            sprintf(threadStr, "%lu", pe32.cntThreads);
            row[3].type = TYPE_STRING;
            row[3].value.str_val = _strdup(threadStr);
            
            // Store whether this is a user process
            row[0].is_highlighted = isUserProcess;
            
            // Add the row to the table
            add_table_row(table, row);
            processCount++;
        }
        
    } while (Process32Next(hSnapshot, &pe32));
    
    // Clean up the snapshot handle
    CloseHandle(hSnapshot);
    
    return table;
}
