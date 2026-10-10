#include <gtest/gtest.h>
#include <angelscript.h>
#include "as_parser.h"
#include "as_scriptcode.h"
#include "as_scriptengine.h"
#include "as_compiler.h"
#include <cstring>

namespace
{
	struct Parsed
	{
		asCScriptEngine*         engine{};
		asCScriptCode            code;
		asCArray<sParseMessage>  messages;
		asCParser*               parser{};
		int                      r{-1};

		explicit Parsed(char const* text)
		{
			engine = static_cast<asCScriptEngine*>(asCreateScriptEngine());
			code.SetCode("condition", text, std::strlen(text), true);
			parser = new asCParser(engine, &messages);
			r = parser->ParseAssignmentExpression(&code);
		}
		~Parsed()
		{
			delete parser;
			engine->ShutDownAndRelease();
		}
		asCScriptNode* root() { return parser->GetScriptNode(); }
	};

	int ChildCount(asCScriptNode* n)
	{
		int c = 0;
		for(asCScriptNode* k = n->firstChild; k; k = k->next) { ++c; }
		return c;
	}
}

TEST(AngelScriptParseCondition, TernaryAtTopParsesAsScriptDoes)
{
	Parsed p("a ? b : c ? d : e");
	ASSERT_EQ(p.r, 0);
	ASSERT_EQ(p.root()->nodeType, snAssignment);
	asCScriptNode* cond = p.root()->firstChild;
	ASSERT_EQ(cond->nodeType, snCondition);
	EXPECT_EQ(ChildCount(cond), 3);                    // EXPR, ASSIGN, ASSIGN
	EXPECT_EQ(cond->lastChild->nodeType, snAssignment);  // the nested ternary is an arm
}

TEST(AngelScriptParseCondition, TernaryInsideAParenthesisedOperand)
{
	Parsed p("x < (c ? 1 : 2)");
	EXPECT_EQ(p.r, 0);
	EXPECT_TRUE(p.messages.GetLength() == 0);
}

TEST(AngelScriptParseCondition, AssignOperatorParsesForTheWalkerToRefuse)
{
	Parsed p("x = 5");
	ASSERT_EQ(p.r, 0);
	EXPECT_EQ(ChildCount(p.root()), 3);   // CONDITION ASSIGNOP ASSIGN
}

TEST(AngelScriptParseCondition, LessThanAtTheEndReadsNothingPastTheCode)
{
	// After a dot, the template-list lookahead decides call or property: `a.hp < limit` reaches
	// the end of the code and checked the byte there for '>'. Sharing a buffer whose next byte IS
	// '>' made that over-read read `hp<limit>` as a template call (an ASan build reports the read
	// whatever the byte).
	static char const buffer[] = "a.hp < limit>";
	asCScriptEngine* engine = static_cast<asCScriptEngine*>(asCreateScriptEngine());
	{
		asCScriptCode           code;
		asCArray<sParseMessage> messages;
		code.SetCode("condition", buffer, std::strlen(buffer) - 1, false);
		asCParser parser(engine, &messages);
		EXPECT_EQ(parser.ParseAssignmentExpression(&code), 0);
		EXPECT_EQ(messages.GetLength(), 0u);
	}
	engine->ShutDownAndRelease();
}

TEST(AngelScriptParseCondition, TrailingTokensAreAnError)
{
	Parsed p("a b");
	EXPECT_EQ(p.r, -1);
	ASSERT_GT(p.messages.GetLength(), 0u);
	EXPECT_EQ(p.messages[0].row, 1);
	EXPECT_EQ(p.messages[0].col, 3);
}

TEST(AngelScriptParseCondition, TypesAreNotChecked)
{
	Parsed p("Undeclared::Thing == other.member() && sees_plant()");
	EXPECT_EQ(p.r, 0);
}

TEST(AngelScriptParseCondition, SyntaxErrorReachesTheSinkNotABuilder)
{
	Parsed p("hp <");
	EXPECT_EQ(p.r, -1);
	EXPECT_GT(p.messages.GetLength(), 0u);
}

TEST(AngelScriptParseCondition, PrecedenceIsStatic)
{
	// The node destructor is protected, so nodes come from the engine's pool as the parser's own do.
	asCScriptEngine* engine = static_cast<asCScriptEngine*>(asCreateScriptEngine());
	asCScriptNode* star = new(engine->memoryMgr.AllocScriptNode()) asCScriptNode(snExprOperator);
	star->tokenType = ttStar;
	asCScriptNode* andNode = new(engine->memoryMgr.AllocScriptNode()) asCScriptNode(snExprOperator);
	andNode->tokenType = ttAnd;
	EXPECT_EQ(asCCompiler::GetPrecedence(star), -1);
	EXPECT_EQ(asCCompiler::GetPrecedence(andNode), -9);
	star->Destroy(engine);
	andNode->Destroy(engine);
	engine->ShutDownAndRelease();
}
