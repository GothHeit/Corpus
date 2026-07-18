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
    std::string tagName;
    
    public:
    
    explicit TagLeaf(const std::string &name) : tagName(name) {}
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

enum class TokenType
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
    TokenType type;
    std::string value; // tag val

    explicit Token(TokenType type) : type(type) {}
    explicit Token(TokenType type, std::string value) : type(type), value{value} {}
};


struct SearchResult
{
    std::vector<file*> files;
    bool valid; // false = bloco mal formado
};

#endif