#ifndef SCRIPTBUILDER_TYPES_HPP
#define SCRIPTBUILDER_TYPES_HPP

#ifndef ANGELSCRIPT_H
#include <angelscript.h>
#endif
#include <string>
#include <vector>

BEGIN_AS_NAMESPACE

struct StringSpan
{
	unsigned int pos;
	unsigned int len;
};

struct Token
{
	asETokenClass type;
	StringSpan    span;
};

struct SectionEntry
{
	std::string name;
	std::string code;
	int lineOffset;
	int flags;
};

struct SourceMapping
{
	std::string synthSection;
	int synthLineStart;
	int synthLineEnd;
	std::string origSection;
	int origLine;
};

enum AnnotationTarget
{
	AT_TYPE        = 1,
	AT_FUNC        = 2,
	AT_VAR         = 3,
	AT_VIRTPROP    = 4,
	AT_FUNC_OR_VAR = 5
};

struct PendingAnnotation
{
	AnnotationTarget         target;
	std::string              name;
	std::string              declaration;
	std::string              parentClass;
	std::string              nameSpace;
	int                      visibility;  // 0 = undefined
	std::vector<std::string> metadata;
};

END_AS_NAMESPACE

#endif // SCRIPTBUILDER_TYPES_HPP
