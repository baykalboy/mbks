#include <windows.h>
#include <stdio.h>

int main(int argc, char* argv[]) {

    if (argc < 3) {
        return 1;
    }

    char* filename = argv[1];
    char* action = argv[2];


    if (strcmp(action, "read") == 0) {
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

    else if (strcmp(action, "write") == 0) {
        if (argc < 4) {
            printf("[-] ERROR: No text provided for writing!\n");
            return 1;
        }
        char* data = argv[3];

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