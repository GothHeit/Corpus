#ifndef TAG_QUERY_HPP
#define TAG_QUERY_HPP

#include <string>
#include <vector>
#include <memory>

class tag;
class file;

class QueryNode {
    public:
        virtual ~QueryNode() = default;
        virtual bool Evaluate(const std::vector<tag*> &fileTags) const = 0;
};

class TagLeaf : public QueryNode 
{ 
    std::string foldedName; // folded once here, not for every file
    
    public:
    
    explicit TagLeaf(const std::string &name);
    bool Evaluate(const std::vector<tag*> &fileTags) const override;
};

class NotNode : public QueryNode
{
    std::unique_ptr<QueryNode> child;

    public:

    explicit NotNode(std::unique_ptr<QueryNode> child) : child(std::move(child)) {}
    bool Evaluate(const std::vector<tag*> &fileTags) const override;
};

class AndNode : public QueryNode
{
    std::unique_ptr<QueryNode> left, right;

    public:

    explicit AndNode(std::unique_ptr<QueryNode> left, std::unique_ptr<QueryNode> right)
        : left(std::move(left)), right(std::move(right)) {}
    bool Evaluate(const std::vector<tag*> &fileTags) const override;
};
class OrNode : public QueryNode
{
    std::unique_ptr<QueryNode> left, right;

    public:

    explicit OrNode(std::unique_ptr<QueryNode> left, std::unique_ptr<QueryNode> right)
        : left(std::move(left)), right(std::move(right)) {}
    bool Evaluate(const std::vector<tag*> &fileTags) const override;
};

enum class QueryTokenType
{
    LParen,
    RParen,
    And,
    Or,
    Not,
    Minus,
    Tag
};

struct Token
{
    QueryTokenType type;
    std::string value; // tag val

    explicit Token(QueryTokenType type) : type(type) {}
    explicit Token(QueryTokenType type, std::string value) : type(type), value{value} {}
};


std::unique_ptr<QueryNode> ParseQuery(const std::string &exprText);

/// Why a search was rejected. The search stops at the first problem it finds, reading left to right.
enum class SearchError
{
    None,
    UnclosedBrace,      // a '{' with no '}' after it, or a '{' opened inside a group
    BadExpression       // a group that is empty or not a complete tag expression, like {} or {a AND}
};

struct SearchResult
{
    std::vector<file*> files;
    bool valid;
    SearchError error = SearchError::None;
};

#endif