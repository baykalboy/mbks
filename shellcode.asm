[BITS 32]
start:
    xor eax, eax             
    mov eax, [fs:eax + 30h]  
    mov eax, [eax + 0Ch]     
    mov esi, [eax + 14h]    
    lodsd                    
    xchg eax, esi           
    lodsd                   
    mov ebx, [eax + 10h]     
    mov edx, [ebx + 3Ch] 
    add edx, ebx         
    mov edx, [edx + 78h] 
    add edx, ebx         
    mov esi, [edx + 20h] 
    add esi, ebx         
    xor ecx, ecx         

find_function_loop:
    mov edi, [esi + ecx * 4]    
    add edi, ebx                 
    cmp dword [edi], 0x50746547
    jnz next_function           
    cmp dword [edi + 4], 0x41636F72
    jnz next_function           
    cmp dword [edi + 8], 0x65726464
    jz find_function_finished    
next_function:
    inc ecx                      
    jmp find_function_loop       

find_function_finished:
    mov edx, [ebx + 3Ch]
    add edx, ebx
    mov edx, [edx + 78h]
    add edx, ebx
    mov edi, [edx + 24h] 
    add edi, ebx           
    mov cx, [edi + ecx * 2] 
    mov edi, [edx + 1Ch] 
    add edi, ebx          
    mov eax, [edi + ecx * 4] 
    add eax, ebx             
    mov ebp, eax 

    sub esp, 0x1000
    test [esp], eax
    sub esp, 0x1000
    test [esp], eax
    sub esp, 0x1000
    mov esi, esp             

    ; --- 1. CreateToolhelp32Snapshot ---
    push 0x00000000          
    push 0x746f6873          
    push 0x70616e53          
    push 0x3233706c          
    push 0x65686c6f          
    push 0x6f546574          
    push 0x61657243          
    push esp                 
    push ebx                 
    call ebp                 
    add esp, 28              
    push 0
    push 2
    call eax                 
    mov dword [esi], eax     

    ; --- 2. Process32First ---
    push 0x00007473          
    push 0x72694632          
    push 0x33737365          
    push 0x636f7250          
    push esp
    push ebx
    call ebp
    add esp, 16
    mov dword [esi + 4], eax 

    ; --- 3. Process32Next ---
    push 0x00000074          
    push 0x78654e32          
    push 0x33737365          
    push 0x636f7250          
    push esp
    push ebx
    call ebp
    add esp, 16
    mov dword [esi + 8], eax 

    ; --- 4. OpenProcess ---
    push 0x00737365          
    push 0x636f7250          
    push 0x6e65704f          
    push esp
    push ebx
    call ebp
    add esp, 12
    mov dword [esi + 12], eax 

    ; --- 5. IsWow64Process ---
    push 0x00007373          
    push 0x65636f72          
    push 0x50343677          
    push 0x6f577349          
    push esp
    push ebx
    call ebp
    add esp, 16
    mov dword [esi + 16], eax 

    mov dword [esi + 0x20], 0x128 
    lea edi, [esi + 0x160]        
    mov byte [edi], 0             

    lea eax, [esi + 0x20]    
    push eax
    mov eax, [esi]           
    push eax
    mov eax, [esi + 4]       
    call eax                 
    test eax, eax
    jz finish_process        

process_loop:
    lea ecx, [esi + 0x44]
copy_name:
    mov al, byte [ecx]
    test al, al
    jz check_bitness         
    mov byte [edi], al
    inc ecx
    inc edi
    jmp copy_name

check_bitness:
    mov eax, [esi + 0x28]    
    push eax
    push 0
    push 0x1000
    mov eax, [esi + 12]      
    call eax
    test eax, eax
    jz append_newline        

    sub esp, 4               
    mov ecx, esp             
    push ecx
    push eax                 
    mov eax, [esi + 16]      
    call eax
    pop eax                  
    test eax, eax
    jnz process_x86          

process_x64:
    mov dword [edi], 0x36785b20   
    mov dword [edi + 4], 0x00005d34 
    add edi, 6
    jmp append_newline

process_x86:
    mov dword [edi], 0x38785b20   
    mov dword [edi + 4], 0x00005d36 
    add edi, 6

append_newline:
    mov word [edi], 0x0A0D
    add edi, 2
    mov byte [edi], 0        

    lea eax, [esi + 0x20]
    push eax
    mov eax, [esi]
    push eax
    mov eax, [esi + 8]       
    call eax
    test eax, eax
    jnz process_loop         

finish_process:
    ; --- 1. Находим CreateFileA ---
    push 0x0041656c          ; "leA\0"
    push 0x69466574          ; "teFi"
    push 0x61657243          ; "Crea"
    mov ecx, esp
    push ecx
    push ebx                 
    call ebp                 
    add esp, 12              
    mov [esi + 0x10], eax    

    ; --- 2. Находим WriteFile ---
    push 0x00000065          ; "e\0\0\0"
    push 0x6c694665          ; "eFil"
    push 0x74697257          ; "Writ"
    mov ecx, esp
    push ecx
    push ebx
    call ebp
    add esp, 12
    mov [esi + 0x14], eax    

    ; --- 3. Находим CloseHandle ---
    push 0x00656c64          ; "dle\0"
    push 0x6e614865          ; "eHan"
    push 0x736f6c43          ; "Clos"
    mov ecx, esp
    push ecx
    push ebx
    call ebp
    add esp, 12
    mov [esi + 0x18], eax    

    ; --- 4. Создаем файл "proc.txt" ---
    push 0x00000000          ; "\0\0\0\0"
    push 0x7478742e          ; ".txt"
    push 0x636f7270          ; "proc"
    mov ecx, esp             

    push 0                   
    push 0x80                
    push 2                   
    push 0                   
    push 0                   
    push 0x40000000          
    push ecx                 
    mov eax, [esi + 0x10]
    call eax                 
    add esp, 12              
    mov [esi], eax           

    ; --- 5. Записываем данные в файл ---
    lea ecx, [esi + 0x160]
    mov edx, edi
    sub edx, ecx             

    push 0                   
    lea eax, [esi + 0x4]     
    push eax
    push edx                 
    push ecx                 
    push dword [esi]         
    mov eax, [esi + 0x14]
    call eax                 

    ; --- 6. Закрываем файл ---
    push dword [esi]
    mov eax, [esi + 0x18]
    call eax                 

    ; --- 7. Выход ---
    push 0x00737365          ; "ess\0"
    push 0x636f7250          ; "Proc"
    push 0x74697845          ; "Exit"
    mov ecx, esp
    push ecx
    push ebx
    call ebp                 
    
    push 0
    call eax