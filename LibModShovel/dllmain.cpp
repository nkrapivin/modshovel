// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"
#include "LibModShovel.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReasonForCall, LPVOID lpReserved) {
    UNREFERENCED_PARAMETER(lpReserved);

    switch (dwReasonForCall) {
        case DLL_PROCESS_ATTACH: {
            return LMS::Global::Init(hModule);
        }

        case DLL_PROCESS_DETACH: {
            return LMS::Global::Quit();
        }

        default: {
            break;
        }
    }

    return TRUE;
}

