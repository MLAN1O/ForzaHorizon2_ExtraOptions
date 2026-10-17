#include "azimuth_proxy.h"
#include "support.h"
#include "winmm_exports.h"

static INIT_ONCE initialized = INIT_ONCE_STATIC_INIT;
static HMODULE system_winmm;
static FARPROC exports[ARRAY_COUNT(winmm_names)];
static wchar_t directory[PATH_CAPACITY], path[PATH_CAPACITY];
static wchar_t temp_path[1024];

/* Copies the system library to a private unique name and returns it in
   temp_path. Explicit system paths are unreliable once this proxy owns the
   plain name: on Windows the load can alias this module, and Wine resolves
   it to nothing distinct. A copy under another name always maps the real
   system library. The copy is reused across runs; at ~1 MB it is negligible
   next to the shader and pipeline caches. */
static int temp_system_copy(void) {
    wchar_t temp_directory[1024];
    DWORD count = GetTempPathW(ARRAY_COUNT(temp_directory), temp_directory);
    if (!count || count >= ARRAY_COUNT(temp_directory) ||
        !path_join(temp_path, temp_directory, L"fh2_winmm_sys.dll")) {
        return 0;
    }
    if (!CopyFileW(path, temp_path, FALSE) &&
        GetFileAttributesW(temp_path) == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return 1;
}

static BOOL CALLBACK load_system_winmm(PINIT_ONCE once, PVOID parameter, PVOID *context) {
    (void)once;
    (void)parameter;
    (void)context;
    UINT length = GetSystemDirectoryW(directory, ARRAY_COUNT(directory));
    if (!length || length >= ARRAY_COUNT(directory) || !path_join(path, directory, L"winmm.dll")) {
        return FALSE;
    }
    system_winmm = LoadLibraryExW(path, NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    HMODULE self = NULL;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       (LPCWSTR)(uintptr_t)load_system_winmm, &self);
    if ((!system_winmm || system_winmm == self) && temp_system_copy()) {
        system_winmm = LoadLibraryExW(temp_path, NULL, 0);
    }
    if (!system_winmm || system_winmm == self) {
        return FALSE;
    }
    for (unsigned index = 0; index < ARRAY_COUNT(exports); ++index) {
        exports[index] = GetProcAddress(system_winmm, winmm_names[index]);
    }
    return TRUE;
}

FARPROC winmm_resolve_export(unsigned index, void *caller) {
    DWORD error = GetLastError();
    if (!InitOnceExecuteOnce(&initialized, load_system_winmm, NULL, NULL) ||
        index >= ARRAY_COUNT(exports) || !exports[index]) {
        RaiseException(0xc0000139u, EXCEPTION_NONCONTINUABLE, 0, NULL);
        return NULL;
    }
    if (index == WINMM_TIME_BEGIN_INDEX) {
        azimuth_proxy_start(caller);
    }
    SetLastError(error);
    return exports[index];
}
