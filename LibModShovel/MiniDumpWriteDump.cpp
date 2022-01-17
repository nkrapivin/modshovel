#include "pch.h"
#include "LibModShovel.h"

/* just so the game loads the dll... */

extern "C" {
    LMSAPI BOOL WINAPI MiniDumpWriteDump(
        HANDLE process,
        DWORD processId,
        HANDLE file,
        UINT dumpType,
        PVOID exceptionParam,
        PVOID userStreamParam,
        PVOID callbackParam
    ) {
        // whatever
        SetLastError(ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
}
