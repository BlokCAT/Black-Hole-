// Provide __imp_ symbols expected by MinGW-built GLFW3
// These point to UCRT implementations

char* strncpy(char* dst, const char* src, unsigned long long n);
unsigned long long strspn(const char* s, const char* accept);
char* strtok(char* str, const char* delim);
float fminf(float x, float y);
float fmaxf(float x, float y);

void* __imp_strncpy = (void*)&strncpy;
void* __imp_strspn = (void*)&strspn;
void* __imp_strtok = (void*)&strtok;
void* __imp_fminf = (void*)&fminf;
void* __imp_fmaxf = (void*)&fmaxf;
