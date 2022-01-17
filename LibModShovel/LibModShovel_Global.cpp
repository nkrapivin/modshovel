#include "pch.h"
#include "LibModShovel.h"
#include "LibModShovel_Hooks.h"
#include "LibModShovel_Console.h"
#include "LibModShovel_Lua.h"

/*
* LibModShovel by nkrapivindev
* Class:   LMS::Global
* Purpose: Global initialization stuff of LMS
*/

HMODULE LMS::Global::myDllHandle{};

bool LMS::Global::throwError(const std::string_view strview) {
	MessageBoxA(GetActiveWindow(), strview.data(), "LibModShovel: Fatal Error", MB_ICONERROR | MB_OK);
	std::abort();
	return false;
}

bool LMS::Global::waitForDebugger() {
#ifdef _DEBUG
	while (!IsDebuggerPresent()) {
		Sleep(2);
	}

	return true;
#else
	return false;
#endif
}

void LMS::Global::initStage2() {
	/* continue initialization after the engine has loaded... */
	Lua::Init();
}

bool LMS::Global::Init(HMODULE hMe) {
	if (myDllHandle) {
		return throwError("Library has been already initialized.");
	}

	myDllHandle = hMe;
	//waitForDebugger();
	if (!Console::Init()) return throwError("Failed to initialize the console window.");
	if (!Hooks::ApplyInitHooks()) return throwError("Failed to apply function hooks.");

	/* wait until gamemaker will load .win, initialization will continue in initStage2() */
	return true;
}

bool LMS::Global::Quit() {
	if (!myDllHandle) {
		return throwError("Library was not initialized.");
	}

	Console::Quit();
	return true;
}
