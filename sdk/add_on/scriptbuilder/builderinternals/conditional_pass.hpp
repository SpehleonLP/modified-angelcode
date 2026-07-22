#ifndef SCRIPTBUILDER_CONDITIONAL_PASS_HPP
#define SCRIPTBUILDER_CONDITIONAL_PASS_HPP

#include "script_text.hpp"
#include <set>
#include <string>

BEGIN_AS_NAMESPACE

struct ConditionalPass
{
	int flags_;
	std::set<std::string> definedWords;

	ConditionalPass(int flags) : flags_(flags) {}

	int Exclude(ScriptText &text);
	int ExcludeCode(std::string &code, asIScriptEngine *engine, int pos, bool *hitElse);
};

inline int ConditionalPass::Exclude(ScriptText &text)
{
	if( !(flags_ & kProcessConditionals) ) return 0;

	std::string &code = text.code;
	asIScriptEngine *eng = text.engine;
	unsigned int pos = 0;
	int nested = 0;
	while( pos < code.size() )
	{
		asUINT len = 0;
		asETokenClass t = eng->ParseToken(&code[pos], code.size() - pos, &len);
		if( t == asTC_UNKNOWN && code[pos] == '#' && (pos + 1 < code.size()) )
		{
			int start = pos++;

			// Is this an #if directive?
			t = eng->ParseToken(&code[pos], code.size() - pos, &len);

			std::string token;
			token.assign(&code[pos], len);

			pos += len;

			if( token == "if" )
			{
				t = eng->ParseToken(&code[pos], code.size() - pos, &len);
				if( t == asTC_WHITESPACE )
				{
					pos += len;
					t = eng->ParseToken(&code[pos], code.size() - pos, &len);
				}

				if( t == asTC_IDENTIFIER )
				{
					std::string word;
					word.assign(&code[pos], len);

					// Overwrite the #if directive with space characters to avoid compiler error
					pos += len;
					text.Blank(start, pos-start);

					// Has this identifier been defined by the application or not?
					if( definedWords.find(word) == definedWords.end() )
					{
						// Exclude all the code until #else or #endif
						bool hitElse = false;
						pos = ExcludeCode(code, eng, pos, &hitElse);
						if( hitElse )
							nested++; // the included #else block still needs its #endif cleaned up
					}
					else
					{
						nested++;
					}
				}
			}
			else if( token == "else" )
			{
				// #else after a true #if — exclude the rest until #endif
				if( nested > 0 )
				{
					text.Blank(start, pos-start);
					pos = ExcludeCode(code, eng, pos, 0);
					nested--;
				}
			}
			else if( token == "endif" )
			{
				// Only remove the #endif if there was a matching #if
				if( nested > 0 )
				{
					text.Blank(start, pos-start);
					nested--;
				}
			}
		}
		else
			pos += len;
	}

	return 0;
}

inline int ConditionalPass::ExcludeCode(std::string &code, asIScriptEngine *engine, int pos, bool *hitElse)
{
	if( hitElse ) *hitElse = false;

	asUINT len = 0;
	int nested = 0;
	while( pos < (int)code.size() )
	{
		engine->ParseToken(&code[pos], code.size() - pos, &len);
		if( code[pos] == '#' )
		{
			code[pos] = ' ';
			pos++;

			// Is it an #if, #else, or #endif directive?
			engine->ParseToken(&code[pos], code.size() - pos, &len);
			std::string token;
			token.assign(&code[pos], len);
			// blank code[pos..pos+len), preserving newlines
			for( int n = 0; n < (int)len; n++ )
				if( code[pos + n] != '\n' ) code[pos + n] = ' ';

			if( token == "if" )
			{
				nested++;
			}
			else if( token == "else" )
			{
				if( nested == 0 )
				{
					pos += len;
					if( hitElse ) *hitElse = true;
					break;
				}
			}
			else if( token == "endif" )
			{
				if( nested-- == 0 )
				{
					pos += len;
					break;
				}
			}
		}
		else if( code[pos] != '\n' )
		{
			// blank code[pos..pos+len), preserving newlines
			for( int n = 0; n < (int)len; n++ )
				if( code[pos + n] != '\n' ) code[pos + n] = ' ';
		}
		pos += len;
	}

	return pos;
}

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_CONDITIONAL_PASS_HPP
