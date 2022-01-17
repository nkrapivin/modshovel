#pragma once

/* Targeted at GameMaker Studio 2.3.3, newer versions may have less or more variables, I don't really care. */

namespace LMS {
	struct GMBuiltinVariable {
		const char* name; // name of the variable or nullptr if it's the end of the table.
		unsigned int arrayLength; // is it an array variable that has a length? 0 if NOT an array, length if it is.
		bool isSelf; // does it belong to a CInstance or is it a global builtin like `fps`, `working_directory`, `view_xport` etc
	};

	extern GMBuiltinVariable BuiltinVariables[];
}

#ifdef GMBuiltinVariables_Implementation
#define END() { (nullptr), (0U), (false) }
#endif /* GMBuiltinVariables_Implementation */
