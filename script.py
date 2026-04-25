import idaapi
import idautils
func = ["fread", "fscanf", "read", "fgets", "fopen", "makepath", "gets", "scanf", "sscanf", "snscanf", "malloc", "realloc", "calloc", "strcpy", "strncpy", "strcat", "strncat", "memset", "memcpy", "memmove", "sprintf", "atoi", "atof", "atol"]
print('\n')
for example in Functions():
    if(get_func_name(example) in func):
        for i in CodeRefsTo(example, 1):
            print(get_func_name(example) + " at 0x%08x" % i)
