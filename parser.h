#ifndef PARSER_H_
#define PARSER_H_

#include "error_reporter.h"
#include "lexer.h"
#include "raw_ast.h"

namespace fidl {

class Parser {
public:
    Parser(Lexer* lexer, ErrorReporter* error_reporter);

    std::unique_ptr<raw::File> Parse() { return ParseFile(); }

    bool Ok() const { return error_reporter_->errors().size() == 0; }

private:
    Token Lex() { return lexer_->LexNoComments(); }

    Token::KindAndSubkind Peek() { return last_token_.kind_and_subkind(); }

    class ASTScope {
    public:
        explicit ASTScope(Parser* parser) : parser_(parser) {
            suppress_ = parser_->suppress_gap_checks_;
            parser_->suppress_gap_checks_ = false;
            parser_->active_ast_scopes_.push_back(raw::SourceElement(Token(), Token()));
        }

        ASTScope(Parser* parser, bool suppress)
            : parser_(parser) {
            parser_->active_ast_scopes_.push_back(raw::SourceElement(Token(), Token()));
            suppress_ = parser_->suppress_gap_checks_;
            parser_->suppress_gap_checks_ = suppress;
        }

        raw::SourceElement GetSourceElement() {
            parser_->active_ast_scopes_.back().end_ = parser_->previous_token_;
            if (!parser_->suppress_gap_checks_) {
                parser_->last_was_gap_start_ = true;
            }
            return raw::SourceElement(parser_->active_ast_scopes_.back());
        }

        ~ASTScope() {
            parser_->suppress_gap_checks_ = suppress_;
            parser_->active_ast_scopes_.pop_back();
        }

        ASTScope(const ASTScope&) = delete;
        ASTScope& operator=(const ASTScope&) = delete;

    private:
        Parser* parser_;
        bool suppress_;
    };

    void UpdateMarks(Token& token) {
        if (active_ast_scopes_.size() == 0) {
            Fail("Internal compiler error: unbalanced parse tree");
        }

        if (!suppress_gap_checks_) {
            // the previous_token_ condition seems to be redundant: it is only
            // true for the very first token (since Lex() will never return a kNotAToken)
            // but last_was_gap_start_ can't be true in this case

            // the first condition is true if previous_token_ is the start of this scope
            // and. we are therefore setting gap_start_ to be the gap between the
            // first and second tokens of this scope
            if (last_was_gap_start_ && previous_token_.kind() != Token::Kind::kNotAToken) {
                gap_start_ = token.previous_end();
                last_was_gap_start_ = false;
            }

            // mark that this token is the first of the topmost scope
            if (active_ast_scopes_.back().start_.kind() == Token::kNotAToken) {
                last_was_gap_start_ = true;
            }
        }

        if (gap_start_.valid()) {
            token.set_previous_end(gap_start_);
        }

        for (auto& scope : active_ast_scopes_) {
            if (scope.start_.kind() == Token::Kind::kNotAToken) {
                scope.start_ = token;
            }
        }

        previous_token_ = token;
    }

    template<class Predicate>
    Token ConsumeToken(Predicate p) {
        std::unique_ptr<std::string> failure_message = p(Peek());
        if (failure_message) {
            Fail(*failure_message);
        }
        UpdateMarks(last_token_);
        auto token = last_token_;
        last_token_ = Lex();
        return token;
    }

    template<class Predicate>
    bool MaybeConsumeToken(Predicate p) {
        std::unique_ptr<std::string> failure_message = p(Peek());
        if (failure_message) {
            return false;
        }
        UpdateMarks(last_token_);
        last_token_ = Lex();
        return true;
    }

    static auto OfKind(Token::Kind expected_kind) {
        return [expected_kind](Token::KindAndSubkind actual) -> std::unique_ptr<std::string> {
            if (actual.kind() != expected_kind) {
                auto message = std::make_unique<std::string>("unexpected token ");
                message->append(Token::Name(actual));
                message->append(", was expecting ");
                message->append(Token::Name(Token::KindAndSubkind(expected_kind, Token::Subkind::kNone)));
                return message;
            }
            return nullptr;
        };
    }

    static auto IdentifierOfSubkind(Token::Subkind expected_subkind) {
        return [expected_subkind](Token::KindAndSubkind actual) -> std::unique_ptr<std::string> {
            auto expected = Token::KindAndSubkind(Token::Kind::kIdentifier, expected_subkind);
            if (actual.combined() != expected.combined()) {
                auto message = std::make_unique<std::string>("unexpected identifier ");
                message->append(Token::Name(actual));
                message->append(", was expecting ");
                message->append(Token::Name(Token::KindAndSubkind(Token::Kind::kIdentifier, Token::Subkind::kNone)));
                return message;
            }
            return nullptr;
        };
    }

    decltype(nullptr) Fail();
    decltype(nullptr) Fail(StringView message);

    std::unique_ptr<raw::Identifier> ParseIdentifier(bool is_discarded = false);
    std::unique_ptr<raw::CompoundIdentifier> ParseCompoundIdentifier();

    std::unique_ptr<raw::StringLiteral> ParseStringLiteral();
    std::unique_ptr<raw::NumericLiteral> ParseNumericLiteral();
    std::unique_ptr<raw::TrueLiteral> ParseTrueLiteral();
    std::unique_ptr<raw::FalseLiteral> ParseFalseLiteral();
    std::unique_ptr<raw::Literal> ParseLiteral();

    std::unique_ptr<raw::Constant> ParseConstant();

    std::unique_ptr<raw::Using> ParseUsing();
    std::unique_ptr<raw::UsingAlias> ParseUsingAlias(ASTScope&);

    std::unique_ptr<raw::TypeConstructor> ParseTypeConstructor();

    std::unique_ptr<raw::ConstDeclaration> ParseConstDeclaration(ASTScope&);

    std::unique_ptr<raw::StructMember> ParseStructMember();
    std::unique_ptr<raw::StructDeclaration> ParseStructDeclaration(ASTScope&);

    std::unique_ptr<raw::File> ParseFile();

    Lexer* lexer_;
    ErrorReporter* error_reporter_;

    std::vector<raw::SourceElement> active_ast_scopes_;
    SourceLocation gap_start_;
    bool last_was_gap_start_ = false;
    bool suppress_gap_checks_ = false;
    Token previous_token_;
    Token last_token_;
};

} // namespace fidl

#endif // PARSER_H_