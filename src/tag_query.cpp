#include "../include/tag_query.hpp"
#include "../include/tag.hpp"
#include "../include/text.hpp"

TagLeaf::TagLeaf(const std::string &name) : foldedName(fold_case(name)) {}

// A file matches a tag or any tag nested under it: {Music} finds Music/Album/Disc_1 too.
bool TagLeaf::Evaluate(const std::vector<tag*> &fileTags, const std::string &) const
{
    for(tag* tg : fileTags)
    {
        if(tag_under(tg->get_folded_id(), this->foldedName))
        {
            return true;
        }
    }
    return false;
}

bool NotNode::Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath) const
{
    return !this->child->Evaluate(fileTags, searchPath);
}

bool AndNode::Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath) const
{
    return (this->left->Evaluate(fileTags, searchPath) && this->right->Evaluate(fileTags, searchPath));
}

bool OrNode::Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath) const
{
    return (this->left->Evaluate(fileTags, searchPath) || this->right->Evaluate(fileTags, searchPath));    
}


ExtLeaf::ExtLeaf(const std::string &extension) : ext(fold_case(extension)) {}

bool ExtLeaf::Evaluate(const std::vector<tag*> &, const std::string &searchPath) const
{
    size_t dot = searchPath.rfind('.');
    size_t slash = searchPath.find_last_of("/\\");
    if(dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return false;
    return fold_case(searchPath.substr(dot + 1)) == ext;
}

bool InboxLeaf::Evaluate(const std::vector<tag*> &fileTags, const std::string &) const
{
    for(tag* tg : fileTags)
    {
        if(tag_under(tg->get_folded_id(), "maison") || tag_under(tg->get_folded_id(), "ref"))
            return false;
    }
    return true;
}

// '_' and blanks read as one space, so "blood luster" finds blood_luster.
static std::string flat_blanks(const std::string &text)
{
    std::string out;
    for(char c : text)
    {
        if(c == '_' || c == '\t')
            c = ' ';
        if(c != ' ' || (!out.empty() && out.back() != ' '))
            out += c;
    }
    return out;
}

PhraseLeaf::PhraseLeaf(const std::string &text) : phrase(flat_blanks(fold_search(text)))
{
    while(!phrase.empty() && phrase.back() == ' ')
        phrase.pop_back();
}

bool PhraseLeaf::Evaluate(const std::vector<tag*> &fileTags, const std::string &searchPath) const
{
    if(flat_blanks(searchPath).find(phrase) != std::string::npos)
        return true;
    for(tag* tg : fileTags)
    {
        if(flat_blanks(tg->get_search_name()).find(phrase) != std::string::npos)
            return true;
    }
    return false;
}

Token WordToToken(const std::string word)
{
    std::string key = fold_case(word);
    if(key == "inbox:")
        return Token(QueryTokenType::Inbox);
    if(key.compare(0, 4, "ext:") == 0)
    {
        size_t start = 4;
        while(start < key.size() && key[start] == '.')
            start++;
        if(start < key.size())
            return Token(QueryTokenType::Ext, key.substr(start));
        return Token(QueryTokenType::Bad); // ext: with nothing after it
    }
    if(word == "AND")
        return Token(QueryTokenType::And);
    if(word == "OR")
        return Token(QueryTokenType::Or);
    if(word == "NOT")
        return Token(QueryTokenType::Not);
    return Token(QueryTokenType::Tag, word);
}

std::vector<Token> Tokenize(const std::string &input)
{
    std::vector<Token> out;
    std::string word;
    for(size_t i=0; i<input.size(); i++)
    {
        char c = input[i];

        if(c == ' ')
        {
            if(word.size() > 0)
            {
                out.push_back(WordToToken(word));
                word = "";
            }
            continue;
        }
        else if(c == '(')
        {
            if(word.size() > 0)
            {
                out.push_back(WordToToken(word));
                word = "";
            }
            out.push_back(Token(QueryTokenType::LParen));
        }
        else if(c == ')')
        {
            if(word.size() > 0)
            {
                out.push_back(WordToToken(word));
                word = "";
            }
            out.push_back(Token(QueryTokenType::RParen));    
        }    
        else if(c == '|')
        {
            if(word.size() > 0)
            {
                out.push_back(WordToToken(word));
                word = "";
            }
            out.push_back(Token(QueryTokenType::Or));
        }
        else if(c == '"')
        {
            if(word.size() > 0)
            {
                out.push_back(WordToToken(word));
                word = "";
            }
            size_t close = input.find('"', i + 1);
            if(close == std::string::npos || close == i + 1 || fold_search(input.substr(i + 1, close - i - 1)).find_first_not_of(" _") == std::string::npos)
            {
                out.push_back(Token(QueryTokenType::Bad)); // unterminated or empty
                break;
            }
            out.push_back(Token(QueryTokenType::Phrase, input.substr(i + 1, close - i - 1)));
            i = close;
        }
        else if(c == '-' && word.empty())
        {
            // Only a leading '-' negates: inside a word it is part of the tag name ("sci-fi").
            out.push_back(Token(QueryTokenType::Minus));
        }
        else
        {
            // word++
            word += c;
        }


        
    }
    if(word.size() > 0)
    {
        out.push_back(WordToToken(word));
        word = "";
    }
    return out;
}

class Parser
{
    const std::vector<Token> &tokens;
    size_t pos = 0;

    public:

    const Token& Peek() const
    {
        return tokens[pos];
    }
    bool AtEnd() const
    {
        return (tokens.size() <= pos);
    }
    Token Advance()
    {
        Token curr = Peek();
        pos++;
        return curr;
    }
    bool Check(QueryTokenType t) const
    {
        return Peek().type == t;
    }
    bool CanStartFactor() const
    {
        Token curr = Peek();
        return (
            curr.type == QueryTokenType::LParen ||
            curr.type == QueryTokenType::Minus ||
            curr.type == QueryTokenType::Not ||
            curr.type == QueryTokenType::Tag ||
            curr.type == QueryTokenType::Ext ||
            curr.type == QueryTokenType::Inbox ||
            curr.type == QueryTokenType::Phrase
        );
    }

    explicit Parser(const std::vector<Token> &t) : tokens(t) {}

    std::unique_ptr<QueryNode> ParseExpression()
    {
        std::unique_ptr<QueryNode> left = ParseTerm();
        if (!left) return nullptr;
        while (!AtEnd() && Check(QueryTokenType::Or))
        {
            Advance();
            std::unique_ptr<QueryNode> right = ParseTerm();
            if (!right) return nullptr;

            left = std::make_unique<OrNode>(std::move(left), std::move(right));
        }
        return left;
    }
    std::unique_ptr<QueryNode> ParseTerm()
    {
        std::unique_ptr<QueryNode> left = ParseFactor();
        if(!left) return nullptr;
        while(!AtEnd() && (Check(QueryTokenType::And) || CanStartFactor()))
        {
            if(Check(QueryTokenType::And))
            {
                Advance();
            }
            std::unique_ptr<QueryNode> right = ParseFactor();
            if(!right) return nullptr;
            left = std::make_unique<AndNode>(std::move(left), std::move(right));
        }
        return left;
    }
    std::unique_ptr<QueryNode> ParseFactor()
    {
        if(!AtEnd() && (Check(QueryTokenType::Minus) || Check(QueryTokenType::Not)))
        {
            Advance();
            std::unique_ptr<QueryNode> child = ParseFactor();
            if(!child) return nullptr;
            return std::make_unique<NotNode>(std::move(child));
        }
        return ParseAtom();
    }
    std::unique_ptr<QueryNode> ParseAtom()
    {
        
        if (AtEnd())
        return nullptr;
        
        if(Check(QueryTokenType::Tag))
        {
            std::string val = Advance().value;
            return std::make_unique<TagLeaf>(val);
        }
        
        if(Check(QueryTokenType::Ext))
            return std::make_unique<ExtLeaf>(Advance().value);
        if(Check(QueryTokenType::Inbox))
        {
            Advance();
            return std::make_unique<InboxLeaf>();
        }
        if(Check(QueryTokenType::Phrase))
            return std::make_unique<PhraseLeaf>(Advance().value);

        if(Check(QueryTokenType::LParen))
        {
            Advance();
            std::unique_ptr<QueryNode> inner = ParseExpression();
            if (!inner || AtEnd() || !Check(QueryTokenType::RParen))
                return nullptr;
            Advance();
            return inner;
        }
        return nullptr;
    

    }

};

std::unique_ptr<QueryNode> ParseQuery(const std::string &exprText)
{
    std::vector<Token> tokens = Tokenize(exprText);
    Parser parser(tokens);
    auto result = parser.ParseExpression();

    if (!result || !parser.AtEnd())   // leftover token = error
        return nullptr;

    return result;
}