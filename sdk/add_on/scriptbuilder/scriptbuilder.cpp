#include "scriptbuilder.h"

#include <string.h>  // _stricmp
#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#endif
#if defined(_MSC_VER) && !defined(_WIN32_WCE) && !defined(__S3E__)
#include <direct.h>
#endif
#ifdef _WIN32_WCE
#include <windows.h>
#endif
#if defined(__S3E__) || defined(__APPLE__) || defined(__GNUC__)
#include <unistd.h>
#endif

#include "builderinternals/types.hpp"
#include "builderinternals/script_text.hpp"
#include "builderinternals/scope_tracker.hpp"
#include "builderinternals/conditional_pass.hpp"
#include "builderinternals/directive_pass.hpp"
#include "builderinternals/decorator_pass.hpp"
#include "builderinternals/metadata_pass.hpp"
#include "builderinternals/splice_pass.hpp"

// Internal AngelScript headers for debug info remapping
#include "as_scriptengine.h"
#include "as_scriptfunction.h"
#include "as_module.h"

#include <map>
#include <set>
#include <assert.h>

using namespace std;

BEGIN_AS_NAMESPACE

// ---------------------------------------------------------------------------
// File-static helpers
// ---------------------------------------------------------------------------

static string GetCurrentDir()
{
	char buffer[1024];
#if defined(_MSC_VER) || defined(_WIN32)
	#ifdef _WIN32_WCE
	static TCHAR apppath[MAX_PATH] = TEXT("");
	if (!apppath[0])
	{
		GetModuleFileName(NULL, apppath, MAX_PATH);

		int appLen = _tcslen(apppath);

		while (appLen > 1)
		{
			if (apppath[appLen-1] == TEXT('\\'))
				break;
			appLen--;
		}

		apppath[appLen] = TEXT('\0');
	}
		#ifdef _UNICODE
	wcstombs(buffer, apppath, min(1024, wcslen(apppath)*sizeof(wchar_t)));
		#else
	memcpy(buffer, apppath, min(1024, strlen(apppath)));
		#endif

	return buffer;
	#elif defined(__S3E__)
	return getcwd(buffer, (int)1024);
	#elif _XBOX_VER >= 200
	return "game:/";
	#elif defined(_M_ARM)
	return "";
	#else
	return _getcwd(buffer, (int)1024);
	#endif
#elif defined(__APPLE__) || defined(__linux__)
	return getcwd(buffer, 1024);
#else
	return "";
#endif
}

static string GetAbsolutePath(const string &file)
{
	string str = file;

	if( !((str.length() > 0 && (str[0] == '/' || str[0] == '\\')) ||
		  str.find(":") != string::npos) )
	{
		str = GetCurrentDir() + "/" + str;
	}

	string::size_type pos = 0;
	while( (pos = str.find("\\", pos)) != string::npos )
		str[pos] = '/';

	pos = 0;
	while( (pos = str.find("/./", pos)) != string::npos )
		str.erase(pos+1, 2);

	pos = 0;
	while( (pos = str.find("/../")) != string::npos )
	{
		string::size_type pos2 = str.rfind("/", pos-1);
		if( pos2 != string::npos )
			str.erase(pos2, pos+3-pos2);
		else
			break;
	}

	return str;
}

// Helper: check if a type ID refers to a funcdef, and if so collect it
static void CollectFuncdefDep(asIScriptEngine *eng, int typeId,
	set<string> &funcdefs)
{
	typeId &= asTYPEID_MASK_OBJECT | asTYPEID_MASK_SEQNBR;
	asITypeInfo *type = eng->GetTypeInfoById(typeId);
	if( !type ) return;
	if( !(type->GetFlags() & asOBJ_FUNCDEF) ) return;

	asIScriptFunction *sig = type->GetFuncdefSignature();
	if( !sig ) return;

	string decl = "funcdef ";
	decl += sig->GetDeclaration(false, false, true);
	decl += ";\n";
	funcdefs.insert(decl);
}

// Helper: scan a function's parameter and return types for funcdef dependencies
static void CollectFuncdefDeps(asIScriptEngine *eng, asIScriptFunction *func,
	set<string> &funcdefs)
{
	CollectFuncdefDep(eng, func->GetReturnTypeId(), funcdefs);

	for( asUINT p = 0; p < func->GetParamCount(); p++ )
	{
		int paramTypeId = 0;
		func->GetParam(p, &paramTypeId);
		CollectFuncdefDep(eng, paramTypeId, funcdefs);
	}
}

// ---------------------------------------------------------------------------
// CScriptBuilder::Impl
// ---------------------------------------------------------------------------

struct CScriptBuilder::Impl
{
	int processingFlags;
	CScriptBuilder *owner_;

	asIScriptEngine *engine;
	asIScriptModule *module;

	ConditionalPass conditional;
	DirectivePass   directive;
	DecoratorPass   decorator;
	MetadataPass    metadata;
	SplicePass      splice;

	vector<SectionEntry> pendingSections;
	vector<SectionEntry> processedSections;
	bool preprocessed;
	string modifiedScript;

	MSGCALLBACK_t msgCallback;
	void         *msgParam;

#ifdef _WIN32
	struct ci_less
	{
		bool operator()(const string &a, const string &b) const
		{
			return _stricmp(a.c_str(), b.c_str()) < 0;
		}
	};
	set<string, ci_less> includedScripts;
#else
	set<string>          includedScripts;
#endif

	Impl(int flags)
		: processingFlags(flags)
		, owner_(0)
		, engine(0)
		, module(0)
		, conditional(flags)
		, directive(flags)
		, decorator(flags)
		, metadata(flags)
		, splice(flags)
		, preprocessed(false)
		, msgCallback(0)
		, msgParam(0)
	{
	}

	void ClearAll();
	int  ProcessScriptSection(const char *script, unsigned int length,
	                          const char *sectionname, int lineOffset,
	                          int flags);
	int  LoadScriptSection(const char *filename, int flags);
	bool IncludeIfNotAlreadyIncluded(const char *filename);
	static void InternalMessageCallback(const asSMessageInfo *msg, void *param);
};

// ---------------------------------------------------------------------------
// CScriptBuilder delegation methods
// ---------------------------------------------------------------------------

CScriptBuilder::CScriptBuilder(int processingFlags)
	: impl_(new Impl(processingFlags))
{
	impl_->owner_ = this;
}

CScriptBuilder::~CScriptBuilder()
{
	delete impl_;
}


int CScriptBuilder::StartNewModule(asIScriptEngine *inEngine, const char *moduleName)
{
	if( inEngine == 0 ) return -1;

	impl_->engine = inEngine;
	impl_->module = inEngine->GetModule(moduleName, asGM_ALWAYS_CREATE);
	if( impl_->module == 0 )
		return -1;

	impl_->ClearAll();

	return 0;
}

asIScriptEngine *CScriptBuilder::GetEngine()
{
	return impl_->engine;
}

asIScriptModule *CScriptBuilder::GetModule()
{
	return impl_->module;
}

void CScriptBuilder::SetIncludeCallback(INCLUDECALLBACK_t cb, void *p)
{
	impl_->directive.includeCb_ = cb;
	impl_->directive.includeParam_ = p;
}

void CScriptBuilder::SetPragmaCallback(PRAGMACALLBACK_t cb, void *p)
{
	impl_->directive.pragmaCb_ = cb;
	impl_->directive.pragmaParam_ = p;
}

void CScriptBuilder::DefineWord(const char *word)
{
	impl_->conditional.definedWords.insert(word);
}

void CScriptBuilder::SetMessageCallback(MSGCALLBACK_t callback, void *param)
{
	impl_->msgCallback = callback;
	impl_->msgParam = param;
}

void CScriptBuilder::ClearMessageCallback()
{
	impl_->msgCallback = 0;
	impl_->msgParam = 0;
}

Visibility CScriptBuilder::GetVisibility(asITypeInfo *type) const
{
	return impl_->decorator.GetVisibility(type);
}

Visibility CScriptBuilder::GetVisibility(asIScriptFunction *func) const
{
	return impl_->decorator.GetVisibility(func);
}

vector<string> CScriptBuilder::GetMetadataForType(int typeId)
{
	return impl_->metadata.GetMetadataForType(typeId);
}

vector<string> CScriptBuilder::GetMetadataForFunc(asIScriptFunction *func)
{
	return impl_->metadata.GetMetadataForFunc(func);
}

vector<string> CScriptBuilder::GetMetadataForVar(int varIdx)
{
	return impl_->metadata.GetMetadataForVar(varIdx);
}

vector<string> CScriptBuilder::GetMetadataForTypeProperty(int typeId, int varIdx)
{
	return impl_->metadata.GetMetadataForTypeProperty(typeId, varIdx);
}

vector<string> CScriptBuilder::GetMetadataForTypeMethod(int typeId, asIScriptFunction *method)
{
	return impl_->metadata.GetMetadataForTypeMethod(typeId, method);
}

unsigned int CScriptBuilder::GetSectionCount() const
{
	return (unsigned int)(impl_->includedScripts.size());
}

string CScriptBuilder::GetSectionName(unsigned int idx) const
{
	if( idx >= impl_->includedScripts.size() ) return "";

#ifdef _WIN32
	set<string, Impl::ci_less>::const_iterator it = impl_->includedScripts.begin();
#else
	set<string>::const_iterator it = impl_->includedScripts.begin();
#endif
	while( idx-- > 0 ) it++;
	return *it;
}

string CScriptBuilder::GetSectionSourceLine(const char *sectionName, int row) const
{
	if( sectionName == 0 || row <= 0 )
		return string();

	// Prefer the preprocessed sections — that's the exact text the compiler
	// saw, so its rows index into it. Fall back to pending if Preprocess()
	// hasn't run yet.
	const SectionEntry *sec = 0;
	for( size_t i = 0; i < impl_->processedSections.size(); i++ )
		if( impl_->processedSections[i].name == sectionName )
		{ sec = &impl_->processedSections[i]; break; }
	if( sec == 0 )
		for( size_t i = 0; i < impl_->pendingSections.size(); i++ )
			if( impl_->pendingSections[i].name == sectionName )
			{ sec = &impl_->pendingSections[i]; break; }
	if( sec == 0 )
		return string();

	// Compiler rows are 1-based and include the section's lineOffset.
	int line = row - sec->lineOffset;
	if( line <= 0 )
		return string();

	const string &code = sec->code;
	size_t start = 0;
	for( int n = 1; n < line; n++ )
	{
		size_t nl = code.find('\n', start);
		if( nl == string::npos )
			return string();
		start = nl + 1;
	}

	size_t end = code.find('\n', start);
	string out = code.substr(start, end == string::npos ? string::npos : end - start);
	if( !out.empty() && out.back() == '\r' )
		out.pop_back();
	return out;
}

int CScriptBuilder::AddSectionFromFile(const char *filename, int flags)
{
	string fullpath = GetAbsolutePath(filename);

	if( impl_->IncludeIfNotAlreadyIncluded(fullpath.c_str()) )
	{
		int r = impl_->LoadScriptSection(fullpath.c_str(), flags);
		if( r < 0 )
			return r;
		else
			return 1;
	}

	return 0;
}

int CScriptBuilder::AddSectionFromMemory(const char *sectionName, const char *scriptCode, unsigned int scriptLength, int lineOffset, int flags)
{
	if( impl_->IncludeIfNotAlreadyIncluded(sectionName) )
	{
		SectionEntry entry;
		entry.name = sectionName;
		if( scriptLength )
			entry.code.assign(scriptCode, scriptLength);
		else
			entry.code = scriptCode;
		entry.lineOffset = lineOffset;
		entry.flags = flags;
		impl_->pendingSections.push_back(entry);
		impl_->preprocessed = false;
		return 1;
	}

	return 0;
}

int CScriptBuilder::Preprocess()
{
	if( impl_->preprocessed )
		return 0;

	int totalErrors = 0;

	// Process each pending section
	// Note: ProcessScriptSection may add new entries via #include,
	// which can cause vector reallocation. Copy fields to locals before the call.
	for( size_t i = 0; i < impl_->pendingSections.size(); i++ )
	{
		string secCode = impl_->pendingSections[i].code;
		string secName = impl_->pendingSections[i].name;
		int secLineOffset   = impl_->pendingSections[i].lineOffset;
		int secFlags        = impl_->pendingSections[i].flags;

		int r = impl_->ProcessScriptSection(
			secCode.c_str(),
			(unsigned int)secCode.size(),
			secName.c_str(),
			secLineOffset,
			secFlags);
		if( r < 0 )
		{
			totalErrors++;
			continue;
		}

		SectionEntry processed;
		processed.name = secName;
		processed.code = impl_->modifiedScript;
		processed.lineOffset = secLineOffset;
		processed.flags = secFlags;
		impl_->processedSections.push_back(processed);
	}

	impl_->pendingSections.clear();

	if( totalErrors > 0 )
		return -1;

	// Out-of-class method splicing
	{
		int spliceErrors = impl_->splice.Extract(impl_->processedSections, impl_->engine);
		spliceErrors += impl_->splice.Splice(impl_->processedSections, impl_->engine);
		if( spliceErrors > 0 )
			return -1;
	}

	impl_->preprocessed = true;
	return 0;
}

int CScriptBuilder::BuildModule()
{
	int r = 0;

	// Preprocess if not already done
	if( !impl_->preprocessed )
	{
		r = Preprocess();
		if( r < 0 )
			return r;
	}

	// Feed all processed sections to the engine
	for( size_t i = 0; i < impl_->processedSections.size(); i++ )
	{
		impl_->module->AddScriptSection(
			impl_->processedSections[i].name.c_str(),
			impl_->processedSections[i].code.c_str(),
			impl_->processedSections[i].code.size(),
			impl_->processedSections[i].lineOffset);
	}

	// Save the engine's current message callback — it may have been set by
	// the host, and Preprocess callbacks (include/pragma) or other code on the
	// call stack may have cleared or replaced it since we last checked.
	// We restore whatever was here after Build so we don't leak our internal
	// callback or clobber the host's.
	asSFuncPtr prevCbFunc;
	void      *prevCbObj  = 0;
	asDWORD    prevCbConv = 0;
	bool hadPrevCb = (impl_->engine->GetMessageCallback(&prevCbFunc, &prevCbObj, &prevCbConv) >= 0);

	if( impl_->msgCallback )
		impl_->engine->SetMessageCallback(asFUNCTION(Impl::InternalMessageCallback), impl_, asCALL_CDECL);

	r = impl_->module->Build();

	if( impl_->msgCallback )
	{
		if( hadPrevCb )
			impl_->engine->SetMessageCallback(prevCbFunc, prevCbObj, prevCbConv);
		else
			impl_->engine->ClearMessageCallback();
	}

	if( r >= 0 )
	{
		impl_->decorator.Resolve(impl_->module);
		impl_->metadata.Resolve(impl_->module, impl_->engine);
		impl_->splice.RemapDebugInfo(impl_->module);
	}

	return r;
}

// ---------------------------------------------------------------------------
// Impl method bodies
// ---------------------------------------------------------------------------

void CScriptBuilder::Impl::ClearAll()
{
	includedScripts.clear();
	pendingSections.clear();
	processedSections.clear();
	preprocessed = false;
	conditional.definedWords.clear();
	directive.pendingIncludes_.clear();
	decorator.ClearAll();
	metadata.ClearAll();
	splice.ClearAll();
}

bool CScriptBuilder::Impl::IncludeIfNotAlreadyIncluded(const char *filename)
{
	string scriptFile = filename;
	if( includedScripts.find(scriptFile) != includedScripts.end() )
		return false;

	includedScripts.insert(scriptFile);
	return true;
}

int CScriptBuilder::Impl::LoadScriptSection(const char* filename, int flags)
{
	string scriptFile = filename;
#if _MSC_VER >= 1500 && !defined(__S3E__)
  #ifdef _WIN32
	wchar_t bufUTF16_name[10000] = {0};
	wchar_t bufUTF16_mode[10] = {0};
	MultiByteToWideChar(CP_UTF8, 0, filename, -1, bufUTF16_name, 10000);
	MultiByteToWideChar(CP_UTF8, 0, "rb", -1, bufUTF16_mode, 10);

	FILE *f = 0;
	_wfopen_s(&f, bufUTF16_name, bufUTF16_mode);
  #else
	FILE* f = 0;
	fopen_s(&f, scriptFile.c_str(), "rb");
  #endif
#else
	FILE *f = fopen(scriptFile.c_str(), "rb");
#endif
	if( f == 0 )
	{
		string msg = "Failed to open script file '" + GetAbsolutePath(scriptFile) + "'";
		engine->WriteMessage(filename, 0, 0, asMSGTYPE_ERROR, msg.c_str());
		return -1;
	}

	fseek(f, 0, SEEK_END);
	int len = ftell(f);
	fseek(f, 0, SEEK_SET);

	string code;
	size_t c = 0;
	if( len > 0 )
	{
		code.resize(len);
		c = fread(&code[0], len, 1, f);
	}

	fclose(f);

	if( c == 0 && len > 0 )
	{
		string msg = "Failed to load script file '" + GetAbsolutePath(scriptFile) + "'";
		engine->WriteMessage(filename, 0, 0, asMSGTYPE_ERROR, msg.c_str());
		return -1;
	}

	SectionEntry entry;
	entry.name = filename;
	entry.code = code;
	entry.lineOffset = 0;
	entry.flags = flags;
	pendingSections.push_back(entry);
	preprocessed = false;
	return 0;
}

int CScriptBuilder::Impl::ProcessScriptSection(const char *script, unsigned int length,
                                                const char *sectionname, int lineOffset,
                                                int flags)
{
	int errors = 0;

	if( length )
		modifiedScript.assign(script, length);
	else
		modifiedScript = script;

	ScriptText text(modifiedScript, engine);

	// Conditional exclusion (#if/#else/#endif pre-pass)
	errors += conditional.Exclude(text);

	// Then check for meta data and pre-processor directives
	directive.pendingIncludes_.clear();
	decorator.hadDecorator_ = false;
	decorator.pendingVis_ = 0;
	decorator.errors_ = 0;
	text.pos = 0;
	ScopeTracker scope;
	while( text.pos < text.code.size() )
	{
		Token tok = text.Next();

		if( tok.type == asTC_UNKNOWN && tok.span.len == 0 )
			break;

		// Bare ';' (e.g. after '};' terminating a class) — skip without
		// invoking SkipStatement, which would consume the following statement.
		if( text.Equals(tok, ";") )
			continue;

		// Decorators: shared, abstract, mixin, external, public, private, protected
		if( decorator.HandleDecorator(text, tok, flags, sectionname, scope.currentClass.len != 0) )
			continue;

		// Class/interface: shared insert + scope tracking + annotation
		if( (text.Equals(tok, "class") || text.Equals(tok, "interface")) && scope.currentClass.len == 0 )
		{
			if( decorator.HandleClassDecl(text, tok, scope, flags) )
				continue;
		}

		// Let ScopeTracker handle '}' (class exit, namespace exit) and 'namespace'
		if( scope.Update(text, tok) )
			continue;

		// Metadata: [...]
		if( text.Equals(tok, "[") )
		{
			int metaPos = metadata.HandleMetadata(text, tok.span.pos, scope);
			if( metaPos >= 0 )
			{
				text.pos = metaPos;
				continue;
			}
		}

		// Is this a preprocessor directive?
		if( text.Equals(tok, "#") && (tok.span.pos + 1 < modifiedScript.size()) )
		{
			int r = directive.HandleDirective(text, tok.span.pos, sectionname, flags, owner_);
			if( r < 0 )
				return r;
			errors += r;
			continue;
		}

		// Import restriction
		if( text.Equals(tok, "import") && !(flags & kAllowImport) )
		{
			engine->WriteMessage(sectionname, text.line, 0,
				asMSGTYPE_ERROR, "Keyword 'import' is not allowed in this section");
			errors++;
		}

		// Other declarations: shared insert + visibility annotation
		decorator.HandleOtherDecl(text, tok, scope, flags);

		text.SkipStatement();
	}

	errors += decorator.errors_;

	{
		int r = directive.ResolveIncludes(sectionname, flags, owner_);
		if( r < 0 )
			return r;
	}

	return errors > 0 ? -1 : 0;
}

// ---------------------------------------------------------------------------
// RemapDebugInfo — fix bytecode debug info to point to original source
// locations instead of the synthesized splice locations.
// ---------------------------------------------------------------------------

void SplicePass::RemapDebugInfo(asIScriptModule *module)
{
	if( sourceMap_.empty() ) return;

	asCModule *mod = static_cast<asCModule*>(module);
	asCScriptEngine *eng = mod->m_engine;

	for( asUINT fi = 0; fi < mod->m_scriptFunctions.GetLength(); fi++ )
	{
		asCScriptFunction *func = mod->m_scriptFunctions[fi];
		if( !func || !func->scriptData ) continue;

		asCScriptFunction::ScriptFunctionData *sd = func->scriptData;

		// Get the function's default section name
		string funcSection;
		if( sd->scriptSectionIdx >= 0 )
			funcSection = eng->scriptSectionNames[sd->scriptSectionIdx]->AddressOf();

		// Build new sectionIdxs entries — collect them separately so we
		// don't mutate sectionIdxs while iterating lineNumbers
		struct SectionEntry { int pos; int idx; };
		vector<SectionEntry> newEntries;

		// Walk lineNumbers (paired: [bytecodePos, encodedLine, ...])
		for( asUINT n = 0; n + 1 < sd->lineNumbers.GetLength(); n += 2 )
		{
			int encoded = sd->lineNumbers[n + 1];
			int line = encoded & 0xFFFFF;
			int col  = encoded >> 20;

			// Check if this line falls in a spliced range
			// Use the function's default section for the lookup (all bytecode
			// in a spliced method originally belongs to the synth section)
			for( size_t mi = 0; mi < sourceMap_.size(); mi++ )
			{
				const SourceMapping &mapping = sourceMap_[mi];
				if( mapping.synthSection == funcSection &&
				    line >= mapping.synthLineStart &&
				    line <= mapping.synthLineEnd )
				{
					int offset = line - mapping.synthLineStart;
					int newLine = mapping.origLine + offset;
					sd->lineNumbers[n + 1] = (newLine & 0xFFFFF) | (col << 20);

					int origSecIdx = eng->GetScriptSectionNameIndex(mapping.origSection.c_str());
					SectionEntry se;
					se.pos = sd->lineNumbers[n];
					se.idx = origSecIdx;
					newEntries.push_back(se);
					break;
				}
			}
		}

		// Apply collected section index entries
		for( size_t i = 0; i < newEntries.size(); i++ )
		{
			sd->sectionIdxs.PushLast(newEntries[i].pos);
			sd->sectionIdxs.PushLast(newEntries[i].idx);
		}

		// Sort sectionIdxs by bytecode position so GetLineNumber's
		// linear scan works correctly
		for( asUINT a = 0; a + 3 < sd->sectionIdxs.GetLength(); a += 2 )
		{
			for( asUINT b = a + 2; b + 1 < sd->sectionIdxs.GetLength(); b += 2 )
			{
				if( sd->sectionIdxs[b] < sd->sectionIdxs[a] )
				{
					int tmpPos = sd->sectionIdxs[a];
					int tmpIdx = sd->sectionIdxs[a + 1];
					sd->sectionIdxs[a]     = sd->sectionIdxs[b];
					sd->sectionIdxs[a + 1] = sd->sectionIdxs[b + 1];
					sd->sectionIdxs[b]     = tmpPos;
					sd->sectionIdxs[b + 1] = tmpIdx;
				}
			}
		}
	}
}

void CScriptBuilder::Impl::InternalMessageCallback(const asSMessageInfo *msg, void *param)
{
	Impl *self = static_cast<Impl*>(param);
	if( !self->msgCallback ) return;

	asSMessageInfo translated;
	translated.section = msg->section;
	translated.row     = msg->row;
	translated.col     = msg->col;
	translated.type    = msg->type;
	translated.message = msg->message;

	string translatedSection;

	const vector<SourceMapping> &sourceMap = self->splice.GetSourceMap();
	for( size_t i = 0; i < sourceMap.size(); i++ )
	{
		const SourceMapping &mapping = sourceMap[i];
		if( mapping.synthSection == msg->section &&
		    msg->row >= mapping.synthLineStart &&
		    msg->row <= mapping.synthLineEnd )
		{
			int offset = msg->row - mapping.synthLineStart;
			translatedSection = mapping.origSection;
			translated.section = translatedSection.c_str();
			translated.row = mapping.origLine + offset;
			break;
		}
	}

	self->msgCallback(&translated, self->msgParam);
}

// ---------------------------------------------------------------------------
// BuildLibrarySurface
// ---------------------------------------------------------------------------

string BuildLibrarySurface(asIScriptModule *lib,
	bool (*isPublicType)(asITypeInfo*),
	bool (*isPublicFunc)(asIScriptFunction*))
{
	asIScriptEngine *eng = lib->GetEngine();
	set<string> funcdefDecls;
	string out;

	// Shared classes and interfaces
	for( asUINT i = 0; i < lib->GetObjectTypeCount(); i++ )
	{
		asITypeInfo *type = lib->GetObjectTypeByIndex(i);
		if( !(type->GetFlags() & asOBJ_SHARED) ) continue;
		if( !(type->GetFlags() & asOBJ_SCRIPT_OBJECT) ) continue;
		if( isPublicType && !isPublicType(type) ) continue;

		for( asUINT m = 0; m < type->GetMethodCount(); m++ )
			CollectFuncdefDeps(eng, type->GetMethodByIndex(m), funcdefDecls);

		bool isInterface = (type->GetSize() == 0);
		if( isInterface )
			out += "external shared interface ";
		else
			out += "external shared class ";
		out += type->GetName();
		out += ";\n";
	}

	// Shared enums
	for( asUINT i = 0; i < lib->GetEnumCount(); i++ )
	{
		asITypeInfo *type = lib->GetEnumByIndex(i);
		if( !(type->GetFlags() & asOBJ_SHARED) ) continue;
		if( isPublicType && !isPublicType(type) ) continue;

		out += "external shared enum ";
		out += type->GetName();
		out += ";\n";
	}

	// Shared global functions
	for( asUINT i = 0; i < lib->GetFunctionCount(); i++ )
	{
		asIScriptFunction *func = lib->GetFunctionByIndex(i);
		if( !func->IsShared() ) continue;
		if( func->GetObjectType() ) continue;
		if( isPublicFunc && !isPublicFunc(func) ) continue;

		CollectFuncdefDeps(eng, func, funcdefDecls);

		out += "external shared ";
		out += func->GetDeclaration(false, false, true);
		out += ";\n";
	}

	// Prepend funcdef declarations
	string result;
	for( set<string>::iterator it = funcdefDecls.begin();
	     it != funcdefDecls.end(); ++it )
	{
		result += *it;
	}
	result += out;

	return result;
}

END_AS_NAMESPACE
