#pragma once

/* These were taken from the GMStudio 2.3.3's Asset Compiler, sorry yoyo */

namespace LMS {
	struct GMConstantReal {
		const char* name;
		double value;
	};

	extern GMConstantReal Constants[];
}

#ifdef GMConstants_Implementation
#define smethod_23(constName, constValue) { (constName), (constValue) }
#define END() { (nullptr), (0.0) }
#endif /* GMConstants_Implementation */
