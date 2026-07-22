#ifndef SCRIPTBUILDER_SPLICE_PASS_HPP
#define SCRIPTBUILDER_SPLICE_PASS_HPP

#include "types.hpp"
#include "script_text.hpp"
#include <string>
#include <vector>
#include <set>

BEGIN_AS_NAMESPACE

// These using-declarations are scoped inside BEGIN_AS_NAMESPACE.
// This header is only included by scriptbuilder.cpp (never by user code).
using std::string;
using std::vector;
using std::set;

namespace splice_detail {

inline int CountLines(const string &code, int pos)
{
	int line = 1;
	for( int i = 0; i < pos && i < (int)code.size(); i++ )
		if( code[i] == '\n' ) line++;
	return line;
}

inline vector<string> Tokenize(asIScriptEngine *eng, const string &code)
{
	vector<string> tokens;
	int pos = 0;
	while( pos < (int)code.size() )
	{
		asUINT len = 0;
		asETokenClass t = eng->ParseToken(&code[pos], code.size() - pos, &len);
		if( t != asTC_WHITESPACE && t != asTC_COMMENT )
			tokens.push_back(code.substr(pos, len));
		pos += len;
	}
	return tokens;
}

inline int SkipWs(const string &code, asIScriptEngine *eng, int pos,
                  asETokenClass *outType, asUINT *outLen)
{
	while( pos < (int)code.size() )
	{
		asETokenClass t = eng->ParseToken(&code[pos], code.size() - pos, outLen);
		if( t != asTC_WHITESPACE && t != asTC_COMMENT )
		{
			*outType = t;
			return pos;
		}
		pos += *outLen;
	}
	*outType = asTC_UNKNOWN;
	*outLen = 0;
	return pos;
}

// Skip a matched block (braces or parens). pos must be ON the opener.
inline int SkipBlock(string &code, asIScriptEngine *eng, int pos, char open, char close)
{
	ScriptText t(code, eng);
	t.pos = pos;
	t.SkipBlock(open, close);
	return t.pos;
}

// Strip default argument values from a tokenized declaration.
// Removes '= expr' sequences inside the parenthesized parameter list.
// Handles nested parens in default expressions (e.g. "= max(1, 2)").
// Returns true if any defaults were present.
inline bool StripDefaults(vector<string> &tokens)
{
	bool hadDefaults = false;
	int parenDepth = 0;
	bool inDefault = false;
	int defaultDepth = 0;

	vector<string> result;
	for( int i = 0; i < (int)tokens.size(); i++ )
	{
		const string &tok = tokens[i];

		if( !inDefault )
		{
			if( tok == "(" ) parenDepth++;
			else if( tok == ")" ) parenDepth--;

			if( parenDepth == 1 && tok == "=" )
			{
				inDefault = true;
				hadDefaults = true;
				defaultDepth = 0;
				continue;
			}

			result.push_back(tok);
		}
		else
		{
			// Inside a default value expression — skip tokens
			if( tok == "(" )
			{
				defaultDepth++;
				continue;
			}
			if( tok == ")" && defaultDepth > 0 )
			{
				defaultDepth--;
				continue;
			}
			if( defaultDepth == 0 && (tok == "," || tok == ")") )
			{
				// End of default expression — this delimiter belongs
				// to the parameter list, not the default value
				inDefault = false;
				if( tok == ")" ) parenDepth--;
				result.push_back(tok);
				continue;
			}
			// else skip this default-value token
		}
	}
	tokens.swap(result);
	return hadDefaults;
}

} // namespace splice_detail

struct SplicePass
{
	int flags_;
	vector<SourceMapping> sourceMap_;

	SplicePass(int flags) : flags_(flags) {}

	int Extract(vector<SectionEntry> &sections, asIScriptEngine *engine);
	int Splice(vector<SectionEntry> &sections, asIScriptEngine *engine);
	void RemapDebugInfo(asIScriptModule *module);

	const vector<SourceMapping>& GetSourceMap() const { return sourceMap_; }

	void ClearAll()
	{
		defs_.clear();
		sourceMap_.clear();
	}

private:
	struct OutOfClassDef
	{
		string className;
		string declTokens;
		string body;
		int    sectionIdx;
		string sourceName;
		int    sourceLine;
	};
	vector<OutOfClassDef> defs_;
};

inline int SplicePass::Extract(vector<SectionEntry> &sections, asIScriptEngine *engine)
{
	if( !(flags_ & kProcessSplice) ) return 0;

	int errors = 0;
	defs_.clear();

	for( int secIdx = 0; secIdx < (int)sections.size(); secIdx++ )
	{
		string &code = sections[secIdx].code;
		const string &secName = sections[secIdx].name;
		int pos = 0;

		while( pos < (int)code.size() )
		{
			asUINT len = 0;
			asETokenClass t;
			pos = splice_detail::SkipWs(code, engine, pos, &t, &len);
			if( pos >= (int)code.size() ) break;

			if( t == asTC_KEYWORD )
			{
				string tok;
				tok.assign(&code[pos], len);

				if( tok == "{" )
				{
					pos = splice_detail::SkipBlock(code, engine, pos, '{', '}');
					continue;
				}

				if( tok == ";" || tok == "}" )
				{
					pos += len;
					continue;
				}

				if( tok == "class" || tok == "interface" || tok == "enum" || tok == "namespace" )
				{
					pos += len;
					while( pos < (int)code.size() )
					{
						asUINT len2 = 0;
						asETokenClass t2 = engine->ParseToken(&code[pos], code.size() - pos, &len2);
						if( t2 == asTC_KEYWORD && code[pos] == '{' )
						{
							pos = splice_detail::SkipBlock(code, engine, pos, '{', '}');
							break;
						}
						else if( t2 == asTC_KEYWORD && code[pos] == ';' )
						{
							pos += len2;
							break;
						}
						pos += len2;
					}
					continue;
				}
			}

			int stmtStart = pos;

			// Scan for :: to identify potential out-of-class method definition
			int colonColonPos = -1;
			int stmtBoundary = pos + len;
			{
				int tmpPos = pos;
				int braceLevel = 0;
				while( tmpPos < (int)code.size() )
				{
					asUINT tmpLen = 0;
					asETokenClass tmpT = engine->ParseToken(&code[tmpPos], code.size() - tmpPos, &tmpLen);

					if( tmpT == asTC_WHITESPACE || tmpT == asTC_COMMENT )
					{
						tmpPos += tmpLen;
						continue;
					}

					if( tmpT == asTC_KEYWORD )
					{
						string tmpTok;
						tmpTok.assign(&code[tmpPos], tmpLen);

						if( tmpTok == "{" ) braceLevel++;
						else if( tmpTok == "}" ) braceLevel--;
						else if( tmpTok == ";" && braceLevel == 0 ) { stmtBoundary = tmpPos + tmpLen; break; }
						else if( tmpTok == "::" && braceLevel == 0 )
						{
							colonColonPos = tmpPos;
							break;
						}
					}

					if( braceLevel < 0 ) { stmtBoundary = tmpPos + tmpLen; break; }
					tmpPos += tmpLen;
				}
				if( colonColonPos < 0 && tmpPos >= (int)code.size() )
					stmtBoundary = tmpPos;
			}

			if( colonColonPos < 0 )
			{
				pos = stmtBoundary;
				continue;
			}

			// Found :: — find method name (next identifier after ::)
			int afterColonColon = colonColonPos + 2;
			int methodNamePos = -1;
			int methodNameLen = 0;
			{
				asUINT tmpLen = 0;
				asETokenClass tmpT;
				int tmpPos = splice_detail::SkipWs(code, engine, afterColonColon, &tmpT, &tmpLen);
				if( tmpT == asTC_IDENTIFIER )
				{
					methodNamePos = tmpPos;
					methodNameLen = tmpLen;
				}
			}

			if( methodNamePos < 0 )
			{
				pos += len;
				continue;
			}

			// Check if next non-ws token after method name is (
			int afterMethodName = methodNamePos + methodNameLen;
			{
				asUINT tmpLen = 0;
				asETokenClass tmpT;
				int tmpPos = splice_detail::SkipWs(code, engine, afterMethodName, &tmpT, &tmpLen);
				if( !(tmpT == asTC_KEYWORD && code[tmpPos] == '(') )
				{
					pos = afterColonColon;
					continue;
				}
			}

			// Find the class name: the last identifier before ::
			int classNamePos = -1;
			int classNameLen = 0;
			{
				int tmpPos = stmtStart;
				while( tmpPos < colonColonPos )
				{
					asUINT tmpLen = 0;
					asETokenClass tmpT = engine->ParseToken(&code[tmpPos], code.size() - tmpPos, &tmpLen);
					if( tmpT == asTC_IDENTIFIER )
					{
						classNamePos = tmpPos;
						classNameLen = tmpLen;
					}
					tmpPos += tmpLen;
				}
			}

			if( classNamePos < 0 )
			{
				pos += len;
				continue;
			}

			string className;
			className.assign(&code[classNamePos], classNameLen);

			// Return type is everything from stmtStart to classNamePos,
			// skipping "shared" which the decorator pass may have inserted.
			string returnType;
			{
				int tmpPos = stmtStart;
				while( tmpPos < classNamePos )
				{
					asUINT tmpLen = 0;
					asETokenClass tmpT = engine->ParseToken(&code[tmpPos], code.size() - tmpPos, &tmpLen);
					if( tmpT != asTC_WHITESPACE && tmpT != asTC_COMMENT )
					{
						if( !(tmpT == asTC_IDENTIFIER && tmpLen == 6 &&
						      code.compare(tmpPos, 6, "shared") == 0) )
						{
							if( !returnType.empty() )
								returnType += " ";
							returnType.append(&code[tmpPos], tmpLen);
						}
					}
					tmpPos += tmpLen;
				}
			}

			string methodName;
			methodName.assign(&code[methodNamePos], methodNameLen);

			// Find the params: ( to matching )
			int parenStart = -1;
			{
				asUINT tmpLen = 0;
				asETokenClass tmpT;
				int tmpPos = splice_detail::SkipWs(code, engine, afterMethodName, &tmpT, &tmpLen);
				if( tmpT == asTC_KEYWORD && code[tmpPos] == '(' )
					parenStart = tmpPos;
			}

			if( parenStart < 0 )
			{
				pos += len;
				continue;
			}

			int parenEnd = splice_detail::SkipBlock(code, engine, parenStart, '(', ')');

			// Get params text (between '(' and ')')
			string paramsText;
			paramsText.assign(&code[parenStart + 1], parenEnd - 1 - (parenStart + 1));

			// Check for optional "const" and then "{"
			bool isConst = false;
			int bodyStart = -1;
			{
				asUINT tmpLen = 0;
				asETokenClass tmpT;
				int tmpPos = splice_detail::SkipWs(code, engine, parenEnd, &tmpT, &tmpLen);
				if( tmpT == asTC_KEYWORD && tmpLen == 5 &&
				    code.compare(tmpPos, 5, "const") == 0 )
				{
					isConst = true;
					tmpPos = splice_detail::SkipWs(code, engine, tmpPos + tmpLen, &tmpT, &tmpLen);
				}
				if( tmpT == asTC_KEYWORD && code[tmpPos] == '{' )
					bodyStart = tmpPos;
			}

			if( bodyStart < 0 )
			{
				pos += len;
				continue;
			}

			int bodyEnd = splice_detail::SkipBlock(code, engine, bodyStart, '{', '}');

			// Build the declTokens
			string declTokens = returnType + " " + methodName + "(" + paramsText + ")";
			if( isConst )
				declTokens += " const";

			string body;
			body.assign(&code[bodyStart], bodyEnd - bodyStart);

			int sourceLine = splice_detail::CountLines(code, bodyStart);

			OutOfClassDef def;
			def.className = className;
			def.declTokens = declTokens;
			def.body = body;
			def.sectionIdx = secIdx;
			def.sourceName = secName;
			def.sourceLine = sourceLine;
			defs_.push_back(def);

			ScriptText(code, engine).Blank(stmtStart, bodyEnd - stmtStart);

			pos = bodyEnd;
			continue;
		}
	}

	return errors;
}

inline int SplicePass::Splice(vector<SectionEntry> &sections, asIScriptEngine *engine)
{
	if( !(flags_ & kProcessSplice) ) return 0;

	int errors = 0;
	set<string> splicedSignatures;

	for( int defIdx = 0; defIdx < (int)defs_.size(); defIdx++ )
	{
		OutOfClassDef &def = defs_[defIdx];
		vector<string> defTokens = splice_detail::Tokenize(engine, def.declTokens);
		bool defHadDefaults = splice_detail::StripDefaults(defTokens);

		string sigKey = def.className + '\0';
		for( int ti = 0; ti < (int)defTokens.size(); ti++ )
		{
			if( ti > 0 ) sigKey += " ";
			sigKey += defTokens[ti];
		}

		if( splicedSignatures.find(sigKey) != splicedSignatures.end() )
		{
			string methodName;
			if( defTokens.size() >= 2 )
				methodName = defTokens[1];
			else
				methodName = def.declTokens;
			string msg = "Duplicate out-of-class definition for '" + def.className + "::" +
				methodName + "'";
			engine->WriteMessage(def.sourceName.c_str(), def.sourceLine, 0, asMSGTYPE_ERROR, msg.c_str());
			errors++;
			continue;
		}

		bool classFound = false;
		bool declFound = false;

		for( int secIdx = 0; secIdx < (int)sections.size(); secIdx++ )
		{
			string &code = sections[secIdx].code;

			int searchPos = 0;
			while( searchPos < (int)code.size() )
			{
				asUINT slen = 0;
				asETokenClass st = engine->ParseToken(&code[searchPos], code.size() - searchPos, &slen);

				if( st == asTC_KEYWORD )
				{
					string stok;
					stok.assign(&code[searchPos], slen);
					if( stok == "class" )
					{
						int classIdPos = -1;
						int classIdLen = 0;
						{
							asUINT tmpLen = 0;
							asETokenClass tmpT;
							int tmpPos = splice_detail::SkipWs(code, engine, searchPos + slen, &tmpT, &tmpLen);
							if( tmpT == asTC_IDENTIFIER )
							{
								classIdPos = tmpPos;
								classIdLen = tmpLen;
							}
						}

						if( classIdPos >= 0 )
						{
							string foundName;
							foundName.assign(&code[classIdPos], classIdLen);

							if( foundName == def.className )
							{
								// Find the opening { of the class body
								int classBodyStart = -1;
								{
									int tmpPos = classIdPos + classIdLen;
									while( tmpPos < (int)code.size() )
									{
										asUINT tmpLen = 0;
										asETokenClass tmpT = engine->ParseToken(&code[tmpPos], code.size() - tmpPos, &tmpLen);
										if( tmpT == asTC_KEYWORD && code[tmpPos] == '{' )
										{
											classBodyStart = tmpPos;
											break;
										}
										if( tmpT == asTC_KEYWORD && code[tmpPos] == ';' )
											break;
										tmpPos += tmpLen;
									}
								}

								if( classBodyStart < 0 )
								{
									searchPos += slen;
									continue;
								}

								classFound = true;

								int classBodyEnd = splice_detail::SkipBlock(code, engine, classBodyStart, '{', '}') - 1;
								if( classBodyEnd <= classBodyStart )
								{
									searchPos += slen;
									continue;
								}

								// Scan inside the class body for ;-terminated declarations
								int scanPos2 = classBodyStart + 1;
								while( scanPos2 < classBodyEnd )
								{
									asUINT dLen = 0;
									asETokenClass dT;
									int declStart = splice_detail::SkipWs(code, engine, scanPos2, &dT, &dLen);
									if( declStart >= classBodyEnd ) break;

									int stmtEnd = declStart;
									int stmtTerminator = -1;
									while( stmtEnd < classBodyEnd )
									{
										asUINT tmpLen = 0;
										asETokenClass tmpT = engine->ParseToken(&code[stmtEnd], code.size() - stmtEnd, &tmpLen);

										if( tmpT == asTC_KEYWORD )
										{
											if( code[stmtEnd] == '{' )
											{
												stmtEnd = splice_detail::SkipBlock(code, engine, stmtEnd, '{', '}');
												break;
											}
											else if( code[stmtEnd] == ';' )
											{
												stmtTerminator = stmtEnd;
												stmtEnd += tmpLen;
												break;
											}
										}

										stmtEnd += tmpLen;
									}

									if( stmtTerminator < 0 )
									{
										scanPos2 = stmtEnd;
										continue;
									}

									string inClassDecl;
									inClassDecl.assign(&code[declStart], stmtTerminator - declStart);

									vector<string> inClassTokens = splice_detail::Tokenize(engine, inClassDecl);

									bool hasParen = false;
									for( int ti = 0; ti < (int)inClassTokens.size(); ti++ )
									{
										if( inClassTokens[ti] == "(" )
										{
											hasParen = true;
											break;
										}
									}

									if( !hasParen )
									{
										scanPos2 = stmtEnd;
										continue;
									}

									bool declHadDefaults = splice_detail::StripDefaults(inClassTokens);

									bool match = (inClassTokens.size() == defTokens.size());
									if( match )
									{
										for( int ti = 0; ti < (int)inClassTokens.size(); ti++ )
										{
											if( inClassTokens[ti] != defTokens[ti] )
											{
												match = false;
												break;
											}
										}
									}

									if( match && defHadDefaults )
									{
										string methodName2;
										if( defTokens.size() >= 2 )
											methodName2 = defTokens[1];
										else
											methodName2 = def.declTokens;
										string msg2 = "Default arguments must be in the declaration, not the out-of-class definition of '" +
											def.className + "::" + methodName2 + "'";
										engine->WriteMessage(def.sourceName.c_str(), def.sourceLine, 0, asMSGTYPE_ERROR, msg2.c_str());
										errors++;
										declFound = true;
										break;
									}

									if( match )
									{
										string replacement = " " + def.body;
										code.replace(stmtTerminator, 1, replacement);

										classBodyEnd += (int)replacement.size() - 1;

										int synthLineStart = splice_detail::CountLines(code, stmtTerminator);
										int synthLineEnd = splice_detail::CountLines(code, stmtTerminator + (int)replacement.size());
										SourceMapping sm;
										sm.synthSection = sections[secIdx].name;
										sm.synthLineStart = synthLineStart;
										sm.synthLineEnd = synthLineEnd;
										sm.origSection = def.sourceName;
										sm.origLine = def.sourceLine;
										sourceMap_.push_back(sm);

										splicedSignatures.insert(sigKey);
										declFound = true;
										break;
									}

									scanPos2 = stmtEnd;
								}

								if( declFound ) break;
							}
						}
					}
				}

				searchPos += slen;
			}

			if( declFound ) break;
		}

		if( !classFound )
		{
			string msg = "No class '" + def.className + "' found for out-of-class definition";
			engine->WriteMessage(def.sourceName.c_str(), def.sourceLine, 0, asMSGTYPE_ERROR, msg.c_str());
			errors++;
		}
		else if( !declFound )
		{
			string methodName;
			vector<string> dt = splice_detail::Tokenize(engine, def.declTokens);
			if( dt.size() >= 2 )
				methodName = dt[1];
			else
				methodName = def.declTokens;

			string msg = "No matching declaration for '" + def.className + "::" + methodName + "'";
			engine->WriteMessage(def.sourceName.c_str(), def.sourceLine, 0, asMSGTYPE_ERROR, msg.c_str());
			errors++;
		}
	}

	return errors;
}

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_SPLICE_PASS_HPP
