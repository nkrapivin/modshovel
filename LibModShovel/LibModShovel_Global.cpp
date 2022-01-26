#include "pch.h"
#include "LibModShovel.h"
#include "LibModShovel_Hooks.h"
#include "LibModShovel_Console.h"
#include "LibModShovel_Lua.h"
#include <array>
#include <string>

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
		SleepEx(2, TRUE);
	}

	return true;
#else
	return false;
#endif
}

/* turns a constant string literal into a utf-8 constant string. */
#define q(_the_quote_string_literal_) ( u8 ## _the_quote_string_literal_ )
void LMS::Global::printRandomQuote() {
	static std::array<std::string, 17 /* set to the amount of quotes below plzkthx */> randomQuotes{
		/* begin. */
		q("libLassebq? Never heard of it."),
		q("Beep boop, game resumed."),
		q("So who is that creature known as Shovelbrine?"),
		q("Hugs are important."),
		q("meow?"),
		q("мяу?"),
		q("It is morally legal to crack and pirate software whose creators support Web3 or NFTs."),
		q("nintendos law firm kinda sux tho"),
		q("Курица и обнимашки - такого комбо в макдаке не купишь!"),
		q("LuaJIT 5.4 wen"),
		q("college is hard."),
		q("Further improvements to overall scripting stability and other minor adjustments have been made to enhance the user experience."),
		q("GameMaker is hilarious"),
		q("NDAs are evil and are a threat to society."),
		q("Code obfuscators are evil and are a threat to society."),
		q("sleep is for the weak."),
		q("The timestamp is 5 hours off, blame Visual Studio D:")
		/* end. */
	};

	/* init libc prng */
	std::srand(static_cast<unsigned>(std::time(nullptr)));
	std::cout << q("- '") << randomQuotes[rand() % randomQuotes.size()] << q("'") << std::endl;
}
#undef q

void LMS::Global::initStage2() {
	/* continue initialization after the engine has loaded... */
	Lua::Init();
}

bool LMS::Global::Init(HMODULE hMe) {
	if (myDllHandle) {
		return throwError("Library has been already initialized.");
	}

	myDllHandle = hMe;
	waitForDebugger();
	if (!Console::Init()) return throwError("Failed to initialize the console window.");
	if (!Hooks::ApplyInitHooks()) return throwError("Failed to apply function hooks.");
	/* very important. */
	printRandomQuote();

	/* flush the output stream before starting the game. */
	std::cout << std::endl;

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
