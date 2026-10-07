// testdll.c - тестовая PE-DLL для MysticOS 2.
// Экспортирует пару функций. Собирается MinGW -shared.
__declspec(dllexport) int my_add(int a, int b) {
    return a + b;
}

__declspec(dllexport) int my_mul(int a, int b) {
    return a * b;
}

int __stdcall DllMainCRTStartup(void* h, unsigned int reason, void* r) {
    (void)h; (void)reason; (void)r;
    return 1;
}
