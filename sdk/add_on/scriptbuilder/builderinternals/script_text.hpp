#ifndef SCRIPTBUILDER_SCRIPT_TEXT_HPP
#define SCRIPTBUILDER_SCRIPT_TEXT_HPP

#include "types.hpp"
#include <string>
#include <string.h>

BEGIN_AS_NAMESPACE

struct ScriptText
{
	std::string    &code;
	asIScriptEngine *engine;
	unsigned int    pos;
	int             line;

	ScriptText(std::string &c, asIScriptEngine *e)
		: code(c), engine(e), pos(0), line(1) {}

	// Return next meaningful token (skip whitespace/comments). Advances cursor.
	Token Next()
	{
		Token tok;
		for (;;)
		{
			if( pos >= code.size() )
			{
				tok.type = asTC_UNKNOWN;
				tok.span.pos = pos;
				tok.span.len = 0;
				return tok;
			}

			asUINT len = 0;
			tok.type = engine->ParseToken(&code[pos], code.size() - pos, &len);
			tok.span.pos = pos;
			tok.span.len = len;

			if( tok.type == asTC_WHITESPACE || tok.type == asTC_COMMENT )
			{
				for( unsigned int i = pos; i < pos + len; ++i )
					if( code[i] == '\n' ) ++line;
				pos += len;
				continue;
			}

			pos += len;
			return tok;
		}
	}

	// Return next meaningful token without advancing cursor.
	Token Peek()
	{
		unsigned int savedPos = pos;
		int savedLine = line;
		Token tok = Next();
		pos = savedPos;
		line = savedLine;
		return tok;
	}

	// Skip matched block: pos must be ON the opening delimiter.
	// Returns position after the matching close.
	unsigned int SkipBlock(char open, char close)
	{
		int depth = 1;
		pos += 1;

		while( pos < code.size() && depth > 0 )
		{
			asUINT len = 0;
			asETokenClass tc = engine->ParseToken(&code[pos], code.size() - pos, &len);

			if( tc == asTC_WHITESPACE || tc == asTC_COMMENT )
			{
				for( unsigned int i = pos; i < pos + len; ++i )
					if( code[i] == '\n' ) ++line;
			}
			else if( tc == asTC_KEYWORD )
			{
				if( len == 1 && code[pos] == open )  ++depth;
				else if( len == 1 && code[pos] == close ) --depth;
			}

			pos += len;
		}

		return pos;
	}

	// Skip tokens until ';' or end of a '{...}' block.
	unsigned int SkipStatement()
	{
		for (;;)
		{
			if( pos >= code.size() )
				return pos;

			Token tok = Next();

			if( tok.type == asTC_UNKNOWN && tok.span.len == 0 )
				return pos;

			if( tok.span.len == 1 && code[tok.span.pos] == ';' )
				return pos;

			if( tok.span.len == 1 && code[tok.span.pos] == '{' )
			{
				pos = tok.span.pos;
				return SkipBlock('{', '}');
			}
		}
	}

	// Overwrite code[start..start+len) with spaces, preserving newlines.
	void Blank(unsigned int start, unsigned int len)
	{
		for( unsigned int i = start; i < start + len && i < code.size(); ++i )
			if( code[i] != '\n' ) code[i] = ' ';
	}

	// Insert text at pos, shift everything after.
	void Insert(unsigned int p, const char *text, unsigned int textLen)
	{
		code.insert(p, text, textLen);
		if( pos >= p ) pos += textLen;
	}

	// Compare token text against C string without allocating.
	bool Equals(const Token &tok, const char *str) const
	{
		unsigned int slen = (unsigned int)strlen(str);
		if( tok.span.len != slen ) return false;
		return strncmp(&code[tok.span.pos], str, slen) == 0;
	}

	// Extract substring as std::string.
	std::string Substr(StringSpan span) const
	{
		return code.substr(span.pos, span.len);
	}
};

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_SCRIPT_TEXT_HPP
