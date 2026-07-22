#ifndef SCRIPTBUILDER_METADATA_PASS_HPP
#define SCRIPTBUILDER_METADATA_PASS_HPP

#include "script_text.hpp"
#include "scope_tracker.hpp"
#include "types.hpp"
#include <map>
#include <vector>
#include <string>

// Expects Visibility enum from scriptbuilder.h to be in scope.

BEGIN_AS_NAMESPACE

struct MetadataPass
{
	int flags_;

	// Owned state: pending annotations and resolved maps
	std::vector<PendingAnnotation> pendingAnnotations_;

	std::map<int, std::vector<std::string> > typeMetadataMap;
	std::map<int, std::vector<std::string> > funcMetadataMap;
	std::map<int, std::vector<std::string> > varMetadataMap;

	struct SClassMetadata
	{
		SClassMetadata(const std::string& aName) : className(aName) {}
		std::string className;
		std::map<int, std::vector<std::string> > funcMetadataMap;
		std::map<int, std::vector<std::string> > varMetadataMap;
	};
	std::map<int, SClassMetadata> classMetadataMap;

	MetadataPass(int flags) : flags_(flags) {}

	int HandleMetadata(ScriptText &text, unsigned int bracketPos,
	                   const ScopeTracker &scope);

	void Resolve(asIScriptModule *module, asIScriptEngine *engine);

	std::vector<std::string> GetMetadataForType(int typeId)
	{
		std::map<int,std::vector<std::string> >::iterator it = typeMetadataMap.find(typeId);
		if( it != typeMetadataMap.end() ) return it->second;
		return std::vector<std::string>();
	}

	std::vector<std::string> GetMetadataForFunc(asIScriptFunction *func)
	{
		if( func )
		{
			std::map<int,std::vector<std::string> >::iterator it = funcMetadataMap.find(func->GetId());
			if( it != funcMetadataMap.end() ) return it->second;
		}
		return std::vector<std::string>();
	}

	std::vector<std::string> GetMetadataForVar(int varIdx)
	{
		std::map<int,std::vector<std::string> >::iterator it = varMetadataMap.find(varIdx);
		if( it != varMetadataMap.end() ) return it->second;
		return std::vector<std::string>();
	}

	std::vector<std::string> GetMetadataForTypeProperty(int typeId, int varIdx)
	{
		std::map<int, SClassMetadata>::iterator typeIt = classMetadataMap.find(typeId);
		if( typeIt == classMetadataMap.end() ) return std::vector<std::string>();
		std::map<int, std::vector<std::string> >::iterator propIt = typeIt->second.varMetadataMap.find(varIdx);
		if( propIt == typeIt->second.varMetadataMap.end() ) return std::vector<std::string>();
		return propIt->second;
	}

	std::vector<std::string> GetMetadataForTypeMethod(int typeId, asIScriptFunction *method)
	{
		if( !method ) return std::vector<std::string>();
		std::map<int, SClassMetadata>::iterator typeIt = classMetadataMap.find(typeId);
		if( typeIt == classMetadataMap.end() ) return std::vector<std::string>();
		std::map<int, std::vector<std::string> >::iterator methodIt =
			typeIt->second.funcMetadataMap.find(method->GetId());
		if( methodIt == typeIt->second.funcMetadataMap.end() ) return std::vector<std::string>();
		return methodIt->second;
	}

	void ClearAll()
	{
		pendingAnnotations_.clear();
		typeMetadataMap.clear();
		funcMetadataMap.clear();
		varMetadataMap.clear();
		classMetadataMap.clear();
	}

private:
	std::map<int, SClassMetadata>::iterator FindOrCreateClassMeta(int typeId, const std::string &className)
	{
		std::map<int, SClassMetadata>::iterator it = classMetadataMap.find(typeId);
		if( it == classMetadataMap.end() )
		{
			classMetadataMap.insert(std::map<int, SClassMetadata>::value_type(typeId, SClassMetadata(className)));
			it = classMetadataMap.find(typeId);
		}
		return it;
	}

	int ExtractMetadata(std::string &code, asIScriptEngine *engine,
	                    int pos, std::vector<std::string> &metadata);
	int ExtractDeclaration(std::string &code, asIScriptEngine *engine,
	                       int pos, std::string &name,
	                       std::string &declaration, int &type);
};

// --- Inline implementations ---

inline int MetadataPass::HandleMetadata(ScriptText &text, unsigned int bracketPos,
                                        const ScopeTracker &scope)
{
	if( !(flags_ & kProcessMetadata) ) return -1;

	std::string name, declaration;
	std::vector<std::string> metadata;
	declaration.reserve(100);

	unsigned int metaPos = ExtractMetadata(text.code, text.engine, bracketPos, metadata);

	int type;
	ExtractDeclaration(text.code, text.engine, metaPos, name, declaration, type);

	if( type > 0 )
	{
		PendingAnnotation pa;
		pa.target = static_cast<AnnotationTarget>(type);
		pa.name = name;
		pa.declaration = declaration;
		pa.parentClass = text.Substr(scope.currentClass);
		pa.nameSpace = scope.currentNamespace;
		pa.visibility = visUndefined;
		pa.metadata = metadata;
		pendingAnnotations_.push_back(pa);
	}

	return (int)metaPos;
}

inline int MetadataPass::ExtractMetadata(std::string &code, asIScriptEngine *engine,
                                         int pos, std::vector<std::string> &metadata)
{
	metadata.clear();

	// Extract all metadata. They can be separated by whitespace and comments
	for (;;)
	{
		std::string metadataString = "";

		// Overwrite the metadata with space characters to allow compilation
		code[pos] = ' ';

		// Skip opening brackets
		pos += 1;

		int level = 1;
		asUINT len = 0;
		while (level > 0 && pos < (int)code.size())
		{
			asETokenClass t = engine->ParseToken(&code[pos], code.size() - pos, &len);
			if (t == asTC_KEYWORD)
			{
				if (code[pos] == '[')
					level++;
				else if (code[pos] == ']')
					level--;
			}

			// Copy the metadata to our buffer
			if (level > 0)
				metadataString.append(&code[pos], len);

			// Overwrite the metadata with space characters to allow compilation
			if (t != asTC_WHITESPACE)
			{
				for( int n = 0; n < (int)len; n++ )
					if( code[pos + n] != '\n' ) code[pos + n] = ' ';
			}

			pos += len;
		}

		metadata.push_back(metadataString);

		// Check for more metadata. Possibly separated by comments
		asETokenClass t = engine->ParseToken(&code[pos], code.size() - pos, &len);
		while (t == asTC_COMMENT || t == asTC_WHITESPACE)
		{
			pos += len;
			t = engine->ParseToken(&code[pos], code.size() - pos, &len);
		}

		if (code[pos] != '[')
			break;
	}

	return pos;
}

inline int MetadataPass::ExtractDeclaration(std::string &code, asIScriptEngine *engine,
                                            int pos, std::string &name,
                                            std::string &declaration, int &type)
{
	declaration = "";
	type = 0;

	int start = pos;

	std::string token;
	asUINT len = 0;
	asETokenClass t = asTC_WHITESPACE;

	// Skip white spaces, comments, and leading decorators
	do
	{
		pos += len;
		t = engine->ParseToken(&code[pos], code.size() - pos, &len);
		token.assign(&code[pos], len);
	} while ( t == asTC_WHITESPACE || t == asTC_COMMENT ||
	          token == "private" || token == "protected" ||
	          token == "shared" || token == "external" ||
	          token == "final" || token == "abstract" );

	// We're expecting, either a class, interface, function, or variable declaration
	if( t == asTC_KEYWORD || t == asTC_IDENTIFIER )
	{
		token.assign(&code[pos], len);
		if( token == "interface" || token == "class" || token == "enum" )
		{
			// Skip white spaces and comments
			do
			{
				pos += len;
				t = engine->ParseToken(&code[pos], code.size() - pos, &len);
			} while ( t == asTC_WHITESPACE || t == asTC_COMMENT );

			if( t == asTC_IDENTIFIER )
			{
				type = AT_TYPE;
				declaration.assign(&code[pos], len);
				pos += len;
				return pos;
			}
		}
		else
		{
			// For function declarations, store everything up to the start of the
			// statement block, except for succeeding decorators (final, override, etc)

			// For variable declaration store just the name as there can only be one

			// We'll only know if the declaration is a variable or function declaration
			// when we see the statement block, or absense of a statement block.
			bool hasParenthesis = false;
			int nestedParenthesis = 0;
			declaration.append(&code[pos], len);
			pos += len;
			for(; pos < (int)code.size();)
			{
				t = engine->ParseToken(&code[pos], code.size() - pos, &len);
				token.assign(&code[pos], len);
				if (t == asTC_KEYWORD)
				{
					if (token == "{" && nestedParenthesis == 0)
					{
						if (hasParenthesis)
						{
							// We've found the end of a function signature
							type = AT_FUNC;
						}
						else
						{
							// We've found a virtual property. Just keep the name
							declaration = name;
							type = AT_VIRTPROP;
						}
						return pos;
					}
					if ((token == "=" && !hasParenthesis) || token == ";")
					{
						if (hasParenthesis)
						{
							// The declaration is ambigous. It can be a variable with initialization, or a function prototype
							type = AT_FUNC_OR_VAR;
						}
						else
						{
							// Substitute the declaration with just the name
							declaration = name;
							type = AT_VAR;
						}
						return pos;
					}
					else if (token == "(")
					{
						nestedParenthesis++;

						// This is the first parenthesis we encounter. If the parenthesis isn't followed
						// by a statement block, then this is a variable declaration, in which case we
						// should only store the type and name of the variable, not the initialization parameters.
						hasParenthesis = true;
					}
					else if (token == ")")
					{
						nestedParenthesis--;
					}
				}
				else if( t == asTC_IDENTIFIER )
				{
					// If a parenthesis is already found then the name is already known so it must not be overwritten
					if( !hasParenthesis )
						name = token;
				}

				// Skip trailing decorators
				if( !hasParenthesis || nestedParenthesis > 0 || t != asTC_IDENTIFIER || (token != "final" && token != "override" && token != "delete" && token != "property"))
					declaration += token;

				pos += len;
			}
		}
	}

	return start;
}

inline void MetadataPass::Resolve(asIScriptModule *module, asIScriptEngine *engine)
{
	for( size_t n = 0; n < pendingAnnotations_.size(); n++ )
	{
		PendingAnnotation &a = pendingAnnotations_[n];
		module->SetDefaultNamespace(a.nameSpace.c_str());

		if( a.target == AT_TYPE )
		{
			int typeId = module->GetTypeIdByDecl(a.declaration.c_str());
			if( typeId >= 0 )
			{
				if( !a.metadata.empty() )
					typeMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(typeId, a.metadata));
			}
		}
		else if( a.target == AT_FUNC )
		{
			if( a.parentClass.empty() )
			{
				asIScriptFunction *func = module->GetFunctionByDecl(a.declaration.c_str());
				if( func )
				{
					if( !a.metadata.empty() )
						funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
				}
			}
			else
			{
				int typeId = module->GetTypeIdByDecl(a.parentClass.c_str());
				if( typeId > 0 )
				{
					std::map<int, SClassMetadata>::iterator it = FindOrCreateClassMeta(typeId, a.parentClass);
					asITypeInfo *type = engine->GetTypeInfoById(typeId);
					asIScriptFunction *func = type->GetMethodByDecl(a.declaration.c_str());
					if( func && !a.metadata.empty() )
						it->second.funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
				}
			}
		}
		else if( a.target == AT_VIRTPROP )
		{
			if( a.parentClass.empty() )
			{
				asIScriptFunction *func = module->GetFunctionByName(("get_" + a.declaration).c_str());
				if( func && !a.metadata.empty() )
					funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
				func = module->GetFunctionByName(("set_" + a.declaration).c_str());
				if( func && !a.metadata.empty() )
					funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
			}
			else
			{
				int typeId = module->GetTypeIdByDecl(a.parentClass.c_str());
				if( typeId > 0 )
				{
					std::map<int, SClassMetadata>::iterator it = FindOrCreateClassMeta(typeId, a.parentClass);
					asITypeInfo *type = engine->GetTypeInfoById(typeId);
					asIScriptFunction *func = type->GetMethodByName(("get_" + a.declaration).c_str());
					if( func && !a.metadata.empty() )
						it->second.funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
					func = type->GetMethodByName(("set_" + a.declaration).c_str());
					if( func && !a.metadata.empty() )
						it->second.funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
				}
			}
		}
		else if( a.target == AT_VAR )
		{
			if( a.parentClass.empty() )
			{
				int varIdx = module->GetGlobalVarIndexByName(a.declaration.c_str());
				if( varIdx >= 0 && !a.metadata.empty() )
					varMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(varIdx, a.metadata));
			}
			else
			{
				int typeId = module->GetTypeIdByDecl(a.parentClass.c_str());
				if( typeId > 0 )
				{
					std::map<int, SClassMetadata>::iterator it = FindOrCreateClassMeta(typeId, a.parentClass);
					asITypeInfo *objectType = engine->GetTypeInfoById(typeId);
					int idx = -1;
					for( asUINT i = 0; i < (asUINT)objectType->GetPropertyCount(); ++i )
					{
						const char *propName;
						objectType->GetProperty(i, &propName);
						if( a.declaration == propName )
						{
							idx = i;
							break;
						}
					}
					if( idx >= 0 && !a.metadata.empty() )
						it->second.varMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(idx, a.metadata));
				}
			}
		}
		else if( a.target == AT_FUNC_OR_VAR )
		{
			if( a.parentClass.empty() )
			{
				int varIdx = module->GetGlobalVarIndexByName(a.name.c_str());
				if( varIdx >= 0 )
				{
					if( !a.metadata.empty() )
						varMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(varIdx, a.metadata));
				}
				else
				{
					asIScriptFunction *func = module->GetFunctionByDecl(a.declaration.c_str());
					if( func && !a.metadata.empty() )
						funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
				}
			}
			else
			{
				int typeId = module->GetTypeIdByDecl(a.parentClass.c_str());
				if( typeId > 0 )
				{
					std::map<int, SClassMetadata>::iterator it = FindOrCreateClassMeta(typeId, a.parentClass);
					asITypeInfo *objectType = engine->GetTypeInfoById(typeId);
					int idx = -1;
					for( asUINT i = 0; i < (asUINT)objectType->GetPropertyCount(); ++i )
					{
						const char *propName;
						objectType->GetProperty(i, &propName);
						if( a.name == propName )
						{
							idx = i;
							break;
						}
					}
					if( idx >= 0 )
					{
						if( !a.metadata.empty() )
							it->second.varMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(idx, a.metadata));
					}
					else
					{
						asITypeInfo *type = engine->GetTypeInfoById(typeId);
						asIScriptFunction *func = type->GetMethodByDecl(a.declaration.c_str());
						if( func && !a.metadata.empty() )
							it->second.funcMetadataMap.insert(std::map<int, std::vector<std::string> >::value_type(func->GetId(), a.metadata));
					}
				}
			}
		}
	}
	module->SetDefaultNamespace("");
}

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_METADATA_PASS_HPP
