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
        /// `searchPath` is the file's path folded by fold_search (file::get_search_path); only ext:, inbox: and phrases read it.
        virtual bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const = 0;
};

class TagLeaf : public QueryNode 
{ 
    std::string foldedName; // folded once here, not for every file
    
    public:
    
    explicit TagLeaf(const std::string &name);
    bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const override;
};

class NotNode : public QueryNode
{
    std::unique_ptr<QueryNode> child;

    public:

    explicit NotNode(std::unique_ptr<QueryNode> child) : child(std::move(child)) {}
    bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const override;
};

class AndNode : public QueryNode
{
    std::unique_ptr<QueryNode> left, right;

    public:

    explicit AndNode(std::unique_ptr<QueryNode> left, std::unique_ptr<QueryNode> right)
        : left(std::move(left)), right(std::move(right)) {}
    bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const override;
};
class OrNode : public QueryNode
{
    std::unique_ptr<QueryNode> left, right;

    public:

    explicit OrNode(std::unique_ptr<QueryNode> left, std::unique_ptr<QueryNode> right)
        : left(std::move(left)), right(std::move(right)) {}
    bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const override;
};

/// ext:psd, true for files with that extension (case ignored).
class ExtLeaf : public QueryNode
{
    std::string ext; // folded, no dot

    public:

    explicit ExtLeaf(const std::string &extension);
    bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const override;
};

/// inbox:, true for files with no maison/* or ref/* tag.
class InboxLeaf : public QueryNode
{
    public:

    bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const override;
};

/// "blood luster": the phrase in the path or in a tag name, ignoring case and accents, '_' reading as a space.
class PhraseLeaf : public QueryNode
{
    std::string phrase;

    public:

    explicit PhraseLeaf(const std::string &text);
    bool Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath = "") const override;
};

enum class QueryTokenType
{
    LParen,
    RParen,
    And,
    Or,
    Not,
    Minus,
    Tag,
    Ext,        // value: the extension
    Inbox,
    Phrase,     // value: the text between the quotes
    Bad         // an unterminated quote: nothing parses it
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
    /// The files in `files` found for a word only through their text, not their name or tags (see
    /// library::search with in_content), so the window can say why they matched. Empty when content wasn't searched.
    std::vector<file*> content_hits;
    SearchError error = SearchError::None;

    bool valid() const { return error == SearchError::None; }
};

#endif