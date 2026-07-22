#ifndef SCRIPTBUILDER_H
#define SCRIPTBUILDER_H

#ifndef ANGELSCRIPT_H
#include <angelscript.h>
#endif

#include <string>
#include <vector>

BEGIN_AS_NAMESPACE

class CScriptBuilder;

typedef int (*INCLUDECALLBACK_t)(const char *include, const char *from, CScriptBuilder *builder, void *userParam);
typedef int (*PRAGMACALLBACK_t)(const std::string &pragmaText, CScriptBuilder &builder, void *userParam);
typedef void (*MSGCALLBACK_t)(const asSMessageInfo *msg, void *param);

// Section flags -- combinable with bitwise OR
enum SectionFlags {
	kAllowShared              = 1 << 0,
	kAllowExternal            = 1 << 1,
	kAllowImport              = 1 << 2,
	kInterpretAccessModifiers = 1 << 3,
	kDefaultFlags             = kAllowShared|kAllowExternal|kAllowImport
};

enum Visibility {
	visUndefined = 0,
	visPublic,
	visPrivate,
	visProtected
};

enum ProcessingFlags {
	kProcessConditionals = 1 << 0,
	kProcessIncludes     = 1 << 1,
	kProcessPragmas      = 1 << 2,
	kProcessMetadata     = 1 << 3,
	kProcessSplice       = 1 << 4,
	kProcessOriginal     = kProcessConditionals|kProcessIncludes|kProcessPragmas|kProcessMetadata,
	kProcessAll          = ~0
};

class CScriptBuilder
{
public:
	CScriptBuilder(int processingFlags = kProcessOriginal);
	~CScriptBuilder();

	int StartNewModule(asIScriptEngine *engine, const char *moduleName);

	int AddSectionFromFile(const char *filename, int flags = kDefaultFlags);

	int AddSectionFromMemory(const char *sectionName,
	                         const char *scriptCode,
	                         unsigned int scriptLength = 0,
	                         int lineOffset = 0,
	                         int flags = kDefaultFlags);

	// thread safe
	int Preprocess();
	// mutates engine, calls preprocess if it wasn't called by caller 
	int BuildModule();

	void SetMessageCallback(MSGCALLBACK_t callback, void *param);
	void ClearMessageCallback();

	Visibility GetVisibility(asITypeInfo *type) const;
	Visibility GetVisibility(asIScriptFunction *func) const;

	asIScriptEngine *GetEngine();
	asIScriptModule *GetModule();

	void SetIncludeCallback(INCLUDECALLBACK_t callback, void *userParam);
	void SetPragmaCallback(PRAGMACALLBACK_t callback, void *userParam);

	void DefineWord(const char *word);

	unsigned int GetSectionCount() const;
	std::string  GetSectionName(unsigned int idx) const;

	// Returns the source text of line `row` (1-based, as reported by compiler
	// messages — the section's lineOffset is accounted for) of the named
	// section, with any trailing CR stripped. Looks up the *preprocessed*
	// sections (the exact text fed to the compiler), so it matches reported
	// rows/sections exactly. Empty string if the section or line is not found.
	// Valid after Preprocess()/BuildModule().
	std::string GetSectionSourceLine(const char *sectionName, int row) const;

	std::vector<std::string> GetMetadataForType(int typeId);
	std::vector<std::string> GetMetadataForFunc(asIScriptFunction *func);
	std::vector<std::string> GetMetadataForVar(int varIdx);
	std::vector<std::string> GetMetadataForTypeProperty(int typeId, int varIdx);
	std::vector<std::string> GetMetadataForTypeMethod(int typeId, asIScriptFunction *method);

private:
	CScriptBuilder(const CScriptBuilder &);
	CScriptBuilder &operator=(const CScriptBuilder &);

	struct Impl;
	Impl *impl_;
};


// Generate external shared stub declarations for a compiled library module
std::string BuildLibrarySurface(asIScriptModule *lib,
	bool (*isPublicType)(asITypeInfo*),
	bool (*isPublicFunc)(asIScriptFunction*));

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_H
