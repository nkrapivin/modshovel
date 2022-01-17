#include "pch.h"
#include "LibModShovel.h"
#include "LibModShovel_Console.h"

#include <cstdio>
#include <iostream>

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

bool LMS::Console::Init() {
	// check:
	if (conOut) return Global::throwError("Console has already been initialized.");
	// variables:
	DWORD dwInmode{ 0 };
	DWORD dwOutmode{ 0 };
	FILE* f{ nullptr };
	// allocation:
	if (!AllocConsole()) return false;
	// reopen printf stuff
	if (_wfreopen_s(&f, L"CONIN$", L"r", stdin)) return false;
	if (_wfreopen_s(&f, L"CONOUT$", L"w", stdout)) return false;
	if (_wfreopen_s(&f, L"CONOUT$", L"w", stderr)) return false;
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
	if (!SetConsoleMode(conIn, dwInmode | ENABLE_VIRTUAL_TERMINAL_INPUT)) return false;
	// set console window title
	if (!SetConsoleTitleW(L"LibModShovel: Debug Console Window, all Lua print()s will appear here.")) return false;
	// reset narrow streams
	std::cout.clear();
	std::cerr.clear();
	std::cin.clear();
	// reset wide streams
	std::wcout.clear();
	std::wcerr.clear();
	std::wcin.clear();
	// print something as a test.
	std::cout << "LibModShovel by nkrapivindev" << std::endl;
	std::cout << "Version: " << __TIMESTAMP__ << std::endl;
	std::cout << "Please don't cheat in speedruns/leaderboards with this tool :(" << std::endl;
	std::cout << std::endl;
	//
	return true;
}

bool LMS::Console::Quit() {
	// do nothing if we haven't initialized the console.
	if (!conOut) {
		return false;
	}
	//
	std::cout << "Destroying the LibModShovel console..." << std::endl;
	// 
	SetStdHandle(STD_INPUT_HANDLE, prevConIn);
	//CloseHandle(prevConIn);
	prevConIn = nullptr;
	SetStdHandle(STD_OUTPUT_HANDLE, prevConOut);
	//CloseHandle(prevConOut);
	prevConOut = nullptr;
	SetStdHandle(STD_ERROR_HANDLE, prevConErr);
	//CloseHandle(prevConErr);
	prevConErr = nullptr;
	//
	CloseHandle(conIn);
	conIn = nullptr;
	CloseHandle(conOut);
	conOut = nullptr;
	//
	fclose(stdout);
	fclose(stdin);
	fclose(stderr);
	// we're done here
	return FreeConsole();
}
