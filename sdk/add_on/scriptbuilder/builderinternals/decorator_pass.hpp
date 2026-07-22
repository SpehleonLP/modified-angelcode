#ifndef SCRIPTBUILDER_DECORATOR_PASS_HPP
#define SCRIPTBUILDER_DECORATOR_PASS_HPP

#include "script_text.hpp"
#include "scope_tracker.hpp"
#include "types.hpp"
#include <map>
#include <vector>

// Expects Visibility enum from scriptbuilder.h to be in scope.

BEGIN_AS_NAMESPACE

struct DecoratorPass
{
	bool hadDecorator_;
	int  pendingVis_;
	int  errors_;

	// Owned state: visibility annotations and resolved maps
	std::vector<PendingAnnotation>  pendingAnnotations_;
	std::map<int, Visibility>       typeVisibility_;
	std::map<int, Visibility>       funcVisibility_;

	DecoratorPass(int /*flags*/)
		: hadDecorator_(false), pendingVis_(0), errors_(0) {}

	bool HandleDecorator(ScriptText &text, const Token &tok, int flags,
	                     const char *sectionName, bool inClass);

	bool HandleClassDecl(ScriptText &text, const Token &tok,
	                     ScopeTracker &scope, int flags);

	void HandleOtherDecl(ScriptText &text, const Token &tok,
	                     const ScopeTracker &scope, int flags);

	void Resolve(asIScriptModule *module)
	{
		for( size_t n = 0; n < pendingAnnotations_.size(); n++ )
		{
			PendingAnnotation &a = pendingAnnotations_[n];
			Visibility vis = static_cast<Visibility>(a.visibility);
			if( vis == visUndefined ) continue;

			module->SetDefaultNamespace(a.nameSpace.c_str());
			if( a.target == AT_TYPE )
			{
				int typeId = module->GetTypeIdByDecl(a.declaration.c_str());
				if( typeId >= 0 )
					typeVisibility_[typeId] = vis;
			}
			else if( a.target == AT_FUNC )
			{
				asIScriptFunction *func = module->GetFunctionByDecl(a.declaration.c_str());
				if( func )
					funcVisibility_[func->GetId()] = vis;
			}
		}
		module->SetDefaultNamespace("");
	}

	Visibility GetVisibility(asITypeInfo *type) const
	{
		if( !type ) return visUndefined;
		std::map<int, Visibility>::const_iterator it = typeVisibility_.find(type->GetTypeId());
		return (it != typeVisibility_.end()) ? it->second : visUndefined;
	}

	Visibility GetVisibility(asIScriptFunction *func) const
	{
		if( !func ) return visUndefined;
		std::map<int, Visibility>::const_iterator it = funcVisibility_.find(func->GetId());
		return (it != funcVisibility_.end()) ? it->second : visUndefined;
	}

	void ClearAll()
	{
		pendingAnnotations_.clear();
		typeVisibility_.clear();
		funcVisibility_.clear();
		hadDecorator_ = false;
		pendingVis_ = 0;
		errors_ = 0;
	}
};

inline bool DecoratorPass::HandleDecorator(ScriptText &text, const Token &tok, int flags,
                                           const char *sectionName, bool inClass)
{
	// Skip possible decorators before class and interface declarations
	if( text.Equals(tok, "shared") || text.Equals(tok, "abstract") ||
		text.Equals(tok, "mixin") || text.Equals(tok, "external") )
	{
		if( text.Equals(tok, "shared") && !(flags & kAllowShared) && !(flags & kInterpretAccessModifiers) )
		{
			text.engine->WriteMessage(sectionName, text.line, 0,
				asMSGTYPE_ERROR, "Keyword 'shared' is not allowed in this section");
			errors_++;
		}
		if( text.Equals(tok, "external") && !(flags & kAllowExternal) )
		{
			text.engine->WriteMessage(sectionName, text.line, 0,
				asMSGTYPE_ERROR, "Keyword 'external' is not allowed in this section");
			errors_++;
		}
		hadDecorator_ = true;
		return true;
	}

	// Handle access modifiers when kInterpretAccessModifiers is set
	if( (flags & kInterpretAccessModifiers) && !inClass &&
		(text.Equals(tok, "public") || text.Equals(tok, "private") || text.Equals(tok, "protected")) )
	{
		// Record the visibility
		if( text.Equals(tok, "public") )
			pendingVis_ = visPublic;
		else if( text.Equals(tok, "private") )
			pendingVis_ = visPrivate;
		else
			pendingVis_ = visProtected;

		// In-place overwrite with "shared" + padding spaces to preserve offsets.
		memcpy(&(text.code[tok.span.pos]), "shared      ", tok.span.len);

		hadDecorator_ = true;
		return true;
	}

	return false;
}

inline bool DecoratorPass::HandleClassDecl(ScriptText &text, const Token &tok,
                                           ScopeTracker &scope, int flags)
{
	// Insert "shared " if kInterpretAccessModifiers and no decorator preceded this
	if( (flags & kInterpretAccessModifiers) && !hadDecorator_ )
	{
		text.Insert(tok.span.pos, "shared ", 7);
		pendingVis_ = 0;
	}

	// Capture the visibility we recorded for this declaration
	int capturedVis = pendingVis_;

	// Build a corrected token for ScopeTracker (span may have shifted
	// due to "shared " insertion). text.pos is already past the
	// class/interface keyword thanks to Insert adjusting the cursor.
	Token classTok;
	classTok.type = tok.type;
	classTok.span.len = tok.span.len;
	classTok.span.pos = ((flags & kInterpretAccessModifiers) && !hadDecorator_)
		? tok.span.pos + 7 : tok.span.pos;

	// Let ScopeTracker read the class name and skip to '{'
	scope.Update(text, classTok);

	// Store visibility for this type (resolved after Build)
	if( (flags & kInterpretAccessModifiers) && scope.currentClass.len != 0 )
	{
		PendingAnnotation pa;
		pa.target = AT_TYPE;
		pa.declaration = text.Substr(scope.currentClass);
		pa.visibility = capturedVis;
		pendingAnnotations_.push_back(pa);
	}

	hadDecorator_ = false;
	pendingVis_ = 0;
	return true;
}

inline void DecoratorPass::HandleOtherDecl(ScriptText &text, const Token &tok,
                                           const ScopeTracker &scope, int flags)
{
	if( !(flags & kInterpretAccessModifiers) || scope.currentClass.len != 0 )
	{
		hadDecorator_ = false;
		pendingVis_ = 0;
		return;
	}

	int capturedVis = pendingVis_;

	// Insert "shared " if no decorator preceded this declaration
	unsigned int declPos = tok.span.pos;
	if( !hadDecorator_ )
	{
		text.Insert(tok.span.pos, "shared ", 7);
		capturedVis = 0;
		declPos = tok.span.pos + 7;
	}

	// Re-read the current token after possible insert
	text.pos = (declPos);
	Token declTok = text.Next();
	std::string token = text.Substr(declTok.span);

	// Determine the declaration name
	if( token == "enum" || token == "funcdef" )
	{
		if( token == "enum" )
		{
			// Scan forward to find the enum name
			unsigned int scanPos = text.pos;
			asUINT scanLen = 0;
			asETokenClass scanT;
			do
			{
				scanT = text.engine->ParseToken(&text.code[scanPos], text.code.size() - scanPos, &scanLen);
				if( scanT == asTC_COMMENT || scanT == asTC_WHITESPACE )
					scanPos += scanLen;
				else
					break;
			} while( scanPos < text.code.size() );

			if( scanT == asTC_IDENTIFIER )
			{
				PendingAnnotation pa;
				pa.target = AT_TYPE;
				pa.declaration = text.code.substr(scanPos, scanLen);
				pa.visibility = capturedVis;
				pendingAnnotations_.push_back(pa);
			}
		}
		else // funcdef
		{
			// For funcdef, find the identifier before "("
			unsigned int scanPos = text.pos;
			asUINT scanLen = 0;
			std::string lastIdent;
			while( scanPos < text.code.size() )
			{
				asETokenClass scanT = text.engine->ParseToken(&text.code[scanPos], text.code.size() - scanPos, &scanLen);
				if( scanT == asTC_IDENTIFIER )
					lastIdent = text.code.substr(scanPos, scanLen);
				else if( scanT == asTC_KEYWORD && text.code[scanPos] == '(' )
					break;
				scanPos += scanLen;
			}
			if( !lastIdent.empty() )
			{
				PendingAnnotation pa;
				pa.target = AT_TYPE;
				pa.declaration = lastIdent;
				pa.visibility = capturedVis;
				pendingAnnotations_.push_back(pa);
			}
		}
	}
	else
	{
		// For functions: capture the declaration text up to and including ")"
		// For global vars (has ; before (), we skip
		unsigned int scanPos = declPos;
		asUINT scanLen = 0;
		bool isFunction = false;
		bool isOutOfClass = false;
		int parenDepth = 0;
		unsigned int declEnd = 0;
		while( scanPos < text.code.size() )
		{
			asETokenClass scanT = text.engine->ParseToken(&text.code[scanPos], text.code.size() - scanPos, &scanLen);
			if( scanT == asTC_KEYWORD && text.code[scanPos] == '(' )
			{
				isFunction = true;
				parenDepth++;
			}
			else if( scanT == asTC_KEYWORD && text.code[scanPos] == ')' )
			{
				parenDepth--;
				if( parenDepth == 0 )
				{
					declEnd = scanPos + scanLen;
					break;
				}
			}
			else if( scanT == asTC_KEYWORD && text.code[scanPos] == ';' && !isFunction )
			{
				break;
			}
			else if( scanT == asTC_KEYWORD && scanLen == 2 && parenDepth == 0 &&
			         text.code[scanPos] == ':' && text.code[scanPos+1] == ':' )
			{
				// Out-of-class method definition (Class::Method) — handled by splice pass.
				isOutOfClass = true;
			}
			scanPos += scanLen;
		}
		if( isFunction && declEnd > 0 && !isOutOfClass )
		{
			// Extract declaration text (skip leading "shared ")
			std::string funcDecl = text.code.substr(declPos, declEnd - declPos);
			PendingAnnotation pa;
			pa.target = AT_FUNC;
			pa.declaration = funcDecl;
			pa.visibility = capturedVis;
			pendingAnnotations_.push_back(pa);
		}
	}

	hadDecorator_ = false;
	pendingVis_ = 0;
}

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_DECORATOR_PASS_HPP
