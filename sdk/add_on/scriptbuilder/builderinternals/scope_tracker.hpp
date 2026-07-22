#ifndef SCRIPTBUILDER_SCOPE_TRACKER_HPP
#define SCRIPTBUILDER_SCOPE_TRACKER_HPP

#include "types.hpp"
#include "script_text.hpp"
#include <string>
#include <vector>

BEGIN_AS_NAMESPACE

// ScopeTracker: tracks current class/interface and namespace scope as tokens
// are consumed from a ScriptText cursor.
struct ScopeTracker
{
	StringSpan           currentClass;
	std::string          currentNamespace;
	std::vector<int>     namespaceStack;

	ScopeTracker()
	{
		currentClass.pos = 0;
		currentClass.len = 0;
	}

	// Update scope for class/interface/namespace/closing-brace tokens.
	// Returns true if the token was consumed (caller should continue to next token).
	bool Update(ScriptText &text, const Token &tok)
	{
		// Case 1: class or interface token (only when not already in a class)
		if( (text.Equals(tok, "class") || text.Equals(tok, "interface")) && currentClass.len == 0 )
		{
			Token name = text.Next();
			if( name.type == asTC_IDENTIFIER )
				currentClass = name.span;
			else
				currentClass.len = 0;

			// Skip forward to '{' or ';'
			for (;;)
			{
				Token t = text.Next();
				if( t.type == asTC_UNKNOWN && t.span.len == 0 )
					break;
				if( text.Equals(t, "{") )
					break;
				if( text.Equals(t, ";") )
				{
					// Forward declaration — not entering a class body
					currentClass.len = 0;
					break;
				}
			}
			return true;
		}

		// Case 2: '}' while in a class — exit class scope
		if( text.Equals(tok, "}") && currentClass.len != 0 )
		{
			currentClass.len = 0;
			return true;
		}

		// Case 3: namespace token
		if( text.Equals(tok, "namespace") )
		{
			int levels = 0;
			for (;;)
			{
				Token ident = text.Next();
				if( ident.type == asTC_UNKNOWN && ident.span.len == 0 )
					break;
				if( text.Equals(ident, "{") )
					break;

				if( ident.type == asTC_IDENTIFIER )
				{
					if( !currentNamespace.empty() )
						currentNamespace += "::";
					currentNamespace += text.Substr(ident.span);
					++levels;
				}
			}
			namespaceStack.push_back(levels);
			return true;
		}

		// Case 4: '}' while in a namespace (and not in a class)
		if( text.Equals(tok, "}") && !currentNamespace.empty() && currentClass.len == 0 )
		{
			if( !namespaceStack.empty() )
			{
				int levels = namespaceStack.back();
				namespaceStack.pop_back();

				for( int i = 0; i < levels; ++i )
				{
					std::string::size_type p = currentNamespace.rfind("::");
					if( p != std::string::npos )
						currentNamespace.erase(p);
					else
						currentNamespace.clear();
				}
			}
			return true;
		}

		return false;
	}
};

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_SCOPE_TRACKER_HPP
