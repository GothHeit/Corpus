#include "../include/tag_query.hpp"
#include "../include/tag.hpp"
#include <cctype>

static std::string ToLower(std::string s)
{
    for (char &c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool TagLeaf::Evaluate(const std::vector<tag*> &fileTags) const
{
    for(tag* tg : fileTags)
    {
        if(ToLower(tg->get_id()) == ToLower(this->tagName))
        {
            return true;
        }
    }
    return false;
}

bool NotNode::Evaluate(const std::vector<tag*> &fileTags) const
{
    return !this->child->Evaluate(fileTags);
}

bool AndNode::Evaluate(const std::vector<tag*> &fileTags) const
{
    return (this->left->Evaluate(fileTags) && this->right->Evaluate(fileTags));
}

bool OrNode::Evaluate(const std::vector<tag*> &fileTags) const
{
    return (this->left->Evaluate(fileTags) || this->right->Evaluate(fileTags));    
}


Token WordToToken(const std::string word)
{
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
        else if(c == '-')
        {
            if(word.size() > 0)
            {
                out.push_back(WordToToken(word));
                word = "";
            }
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
            curr.type == QueryTokenType::Tag
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