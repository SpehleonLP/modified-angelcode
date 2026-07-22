#ifndef SCRIPTBUILDER_DIRECTIVE_PASS_HPP
#define SCRIPTBUILDER_DIRECTIVE_PASS_HPP

#include "script_text.hpp"
#include "types.hpp"
#include <string>
#include <vector>

// Callback typedefs (INCLUDECALLBACK_t, PRAGMACALLBACK_t) are defined in scriptbuilder.h,
// which is included before this file via scriptbuilder.cpp.

BEGIN_AS_NAMESPACE

class CScriptBuilder;  // forward declaration

struct DirectivePass
{
	int flags_;
	INCLUDECALLBACK_t          includeCb_;
	void                      *includeParam_;
	PRAGMACALLBACK_t           pragmaCb_;
	void                      *pragmaParam_;
	std::vector<std::string>   pendingIncludes_;

	DirectivePass(int flags)
		: flags_(flags), includeCb_(0), includeParam_(0), pragmaCb_(0), pragmaParam_(0) {}

	int HandleDirective(ScriptText &text, unsigned int hashPos,
	                    const char *sectionName, int flags,
	                    CScriptBuilder *builder);

	int ResolveIncludes(const char *sectionName, int flags,
	                    CScriptBuilder *builder);
};

inline int DirectivePass::HandleDirective(ScriptText &text, unsigned int hashPos,
                                          const char *sectionName, int flags,
                                          CScriptBuilder *builder)
{
	std::string &code = text.code;
	asIScriptEngine *eng = text.engine;

	unsigned int ppPos = text.pos;
	asUINT ppLen = 0;
	asETokenClass ppT = eng->ParseToken(&code[ppPos], code.size() - ppPos, &ppLen);
	if( ppT == asTC_IDENTIFIER )
	{
		std::string ppToken;
		ppToken.assign(&code[ppPos], ppLen);
		if( ppToken == "include" )
		{
			ppPos += ppLen;
			ppT = eng->ParseToken(&code[ppPos], code.size() - ppPos, &ppLen);
			if( ppT == asTC_WHITESPACE )
			{
				ppPos += ppLen;
				ppT = eng->ParseToken(&code[ppPos], code.size() - ppPos, &ppLen);
			}

			if( ppT == asTC_VALUE && ppLen > 2 && (code[ppPos] == '"' || code[ppPos] == '\'') )
			{
				// Get the include file
				std::string includefile;
				includefile.assign(&code[ppPos + 1], ppLen - 2);
				ppPos += ppLen;

				// Make sure the includeFile doesn't contain any line breaks
				size_t p = includefile.find('\n');
				if( p != std::string::npos )
				{
					std::string str = "Invalid file name for #include; it contains a line-break: '" + includefile.substr(0, p) + "'";
					eng->WriteMessage(sectionName, text.line, 0, asMSGTYPE_ERROR, str.c_str());
				}
				else
				{
					// Store it for later processing (only if includes enabled)
					if( flags_ & kProcessIncludes )
						pendingIncludes_.push_back(includefile);

					// Overwrite the include directive with space characters to avoid compiler error
					text.Blank(hashPos, ppPos - hashPos);
				}
			}
			text.pos = ppPos;
		}
		else if( ppToken == "pragma" )
		{
			// Read until the end of the line
			ppPos += ppLen;
			for( ; ppPos < code.size() && code[ppPos] != '\n'; ppPos++ );

			// Call the pragma callback
			std::string pragmaText(&code[hashPos + 7], ppPos - hashPos - 7);
			int r = -1;
			if( (flags_ & kProcessPragmas) && pragmaCb_ )
				r = pragmaCb_(pragmaText, *builder, pragmaParam_);
			else if( !(flags_ & kProcessPragmas) )
				r = 0;  // pragma processing disabled, silently skip
			else
				r = -1;

			if( r < 0 )
			{
				eng->WriteMessage(sectionName, text.line, 0, asMSGTYPE_ERROR, "Invalid #pragma directive");
				return r;
			}

			// Overwrite the pragma directive with space characters to avoid compiler error
			text.Blank(hashPos, ppPos - hashPos);
			text.pos = ppPos;
		}
		else
		{
			// No matching preprocessor directive; skip
		}
	}
	else
	{
		// Check for lines starting with #!, e.g. shebang interpreter directive
		if( code[ppPos] == '!' )
		{
			// Read until the end of the line
			ppPos += ppLen;
			for( ; ppPos < code.size() && code[ppPos] != '\n'; ppPos++ );

			// Overwrite the directive with space characters to avoid compiler error
			text.Blank(hashPos, ppPos - hashPos);
			text.pos = ppPos;
		}
	}

	return 0;
}

inline int DirectivePass::ResolveIncludes(const char *sectionName, int flags,
                                          CScriptBuilder *builder)
{
	if( !(flags_ & kProcessIncludes) ) return 0;

	if( pendingIncludes_.size() > 0 )
	{
		// If the callback has been set, then call it for each included file
		if( includeCb_ )
		{
			for( int n = 0; n < (int)pendingIncludes_.size(); n++ )
			{
				int r = includeCb_(pendingIncludes_[n].c_str(), sectionName, builder, includeParam_);
				if( r < 0 )
					return r;
			}
		}
		else
		{
			// By default we try to load the included file from the relative directory of the current file

			// Determine the path of the current script so that we can resolve relative paths for includes
			std::string path = sectionName;
			size_t posOfSlash = path.find_last_of("/\\");
			if( posOfSlash != std::string::npos )
				path.resize(posOfSlash+1);
			else
				path = "";

			// Load the included scripts
			for( int n = 0; n < (int)pendingIncludes_.size(); n++ )
			{
				// If the include is a relative path, then prepend the path of the originating script
				if( pendingIncludes_[n].find_first_of("/\\") != 0 &&
					pendingIncludes_[n].find_first_of(":") == std::string::npos )
				{
					pendingIncludes_[n] = path + pendingIncludes_[n];
				}

				// Include the script section (goes to pendingSections_ for deferred processing)
				int r = builder->AddSectionFromFile(pendingIncludes_[n].c_str(), flags);
				if( r < 0 )
				{
					std::string msg = "While including from '" + std::string(sectionName) + "'";
					builder->GetEngine()->WriteMessage(sectionName, 0, 0, asMSGTYPE_INFORMATION, msg.c_str());
					return r;
				}
			}
		}
	}

	return 0;
}

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_DIRECTIVE_PASS_HPP
