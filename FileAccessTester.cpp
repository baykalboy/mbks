#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {
    // Check arguments
    if (argc < 3) {
        printf("Usage:\n");
        printf("  Read:  %s <file_path> read\n", argv[0]);
        printf("  Write: %s <file_path> write \"Text to write\"\n", argv[0]);
        return 1;
    }

    char* filename = argv[1];
    char* action = argv[2];

    // --- READ MODE ---
    if (strcmp(action, "read") == 0) {
        printf("[*] Attempting to open file for reading: %s\n", filename);
        HANDLE hFile = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        
        if (hFile == INVALID_HANDLE_VALUE) {
            printf("[-] ERROR: Failed to open file. Windows Error Code: %lu\n", GetLastError());
            return 1;
        }

        char buffer[1024] = {0};
        DWORD bytesRead;
        if (ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytesRead, NULL)) {
            printf("[+] SUCCESS! Read %lu bytes:\n%s\n", bytesRead, buffer);
        } else {
            printf("[-] ERROR: Failed to read data. Code: %lu\n", GetLastError());
        }
        CloseHandle(hFile);
    }
    // --- WRITE MODE ---
    else if (strcmp(action, "write") == 0) {
        if (argc < 4) {
            printf("[-] ERROR: No text provided for writing!\n");
            return 1;
        }
        char* data = argv[3];

        printf("[*] Attempting to open file for writing: %s\n", filename);
        HANDLE hFile = CreateFileA(filename, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        
        if (hFile == INVALID_HANDLE_VALUE) {
            printf("[-] ERROR: Failed to open file. Windows Error Code: %lu\n", GetLastError());
            return 1;
        }

        DWORD bytesWritten;
        if (WriteFile(hFile, data, (DWORD)strlen(data), &bytesWritten, NULL)) {
            printf("[+] SUCCESS! Wrote %lu bytes.\n", bytesWritten);
        } else {
            printf("[-] ERROR: Failed to write data. Code: %lu\n", GetLastError());
        }
        CloseHandle(hFile);
    }
    else {
        printf("Unknown action.\n");
    }

    return 0;
}