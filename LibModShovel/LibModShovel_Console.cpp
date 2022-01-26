#include "pch.h"
#include "LibModShovel.h"
#include "LibModShovel_Console.h"

/*
* LibModShovel by nkrapivindev
* Class: LMS::Console
* Purpose: Redirects all possible and impossible outputs into the console.
*/

HANDLE LMS::Console::prevConIn{};
HANDLE LMS::Console::prevConOut{};
HANDLE LMS::Console::prevConErr{};

HANDLE LMS::Console::conIn{};
HANDLE LMS::Console::conOut{};

FILE* LMS::Console::oldStdout{};
FILE* LMS::Console::oldStdin{};
FILE* LMS::Console::oldStderr{};

bool LMS::Console::Init() {
	// already initialized the console? then just return true.
	if (conOut) {
		return true;
	}
	// variables:
	DWORD dwInmode{ 0 };
	DWORD dwOutmode{ 0 };
	// allocation:
	if (!AllocConsole()) return false;
	// reopen printf stuff
	if (_wfreopen_s(&oldStdin, L"CONIN$", L"r", stdin)) return false;
	if (_wfreopen_s(&oldStdout, L"CONOUT$", L"w", stdout)) return false;
	if (_wfreopen_s(&oldStderr, L"CONOUT$", L"w", stderr)) return false;
	// reopen winapi stuff
	conOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!conOut || conOut == INVALID_HANDLE_VALUE) return false;
	conIn = CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (!conIn || conIn == INVALID_HANDLE_VALUE) return false;
	// reset handles
	if (!SetStdHandleEx(STD_INPUT_HANDLE, conIn, &prevConIn)) return false;
	if (!SetStdHandleEx(STD_OUTPUT_HANDLE, conOut, &prevConOut)) return false;
	if (!SetStdHandleEx(STD_ERROR_HANDLE, conOut, &prevConErr)) return false;
	// fix utf8 output
	if (!SetConsoleOutputCP(CP_UTF8)) return false;
	if (!SetConsoleCP(CP_UTF8)) return false;
	// fetch console mode
	if (!GetConsoleMode(conOut, &dwOutmode)) return false;
	if (!GetConsoleMode(conIn, &dwInmode)) return false;
	// enable vt100 stuff for lua
	if (!SetConsoleMode(conOut, dwOutmode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) return false;
	// if (!SetConsoleMode(conIn, dwInmode | ENABLE_VIRTUAL_TERMINAL_INPUT)) return false; // breaks backspaces...
	// set console window title
	if (!SetConsoleTitleW(L"LibModShovel (build " TEXT(__TIMESTAMP__) L"): Debug/Lua print() Console Window")) return false;
	// reset narrow streams
	std::cout.clear();
	std::cerr.clear();
	std::cin.clear();
	// reset wide streams
	std::wcout.clear();
	std::wcerr.clear();
	std::wcin.clear();
	// print something as a test.
	std::cout
		<< "LibModShovel (build " << __TIMESTAMP__ << ") by nkrapivindev"   << std::endl
		<< "Please don't cheat in speedruns/leaderboards with this tool :(" << std::endl;
	//
	return true;
}

bool LMS::Console::Quit() {
	// already initialized the console? return false.
	if (!conOut) {
		return false;
	}
	//
	std::cout << "Destroying the LibModShovel console..." << std::endl;
	// 
	SetStdHandleEx(STD_INPUT_HANDLE, prevConIn, nullptr);
	prevConIn = nullptr;
	SetStdHandleEx(STD_OUTPUT_HANDLE, prevConOut, nullptr);
	prevConOut = nullptr;
	SetStdHandleEx(STD_ERROR_HANDLE, prevConErr, nullptr);
	prevConErr = nullptr;
	//
	CloseHandle(conIn);
	conIn = nullptr;
	CloseHandle(conOut);
	conOut = nullptr;
	//
	return FreeConsole();
}
