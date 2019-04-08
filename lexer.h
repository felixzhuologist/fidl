#ifndef LEXER_H_
#define LEXER_H_

#include <map>
#include <stdint.h>

#include "error_reporter.h"
#include "source_location.h"
#include "string_view.h"
#include "token.h"

namespace fidl {

// call .Lex() to get a single Token out of the backing StringView
class Lexer {
public:
    Lexer(const SourceFile& source_file, ErrorReporter* error_reporter)
        : source_file_(source_file), error_reporter_(error_reporter) {
        keyword_table_ = {
#define KEYWORD(Name, Spelling) {Spelling, Token::Subkind::k##Name},
#include "token_definitions.inc"
#undef KEYWORD
        };
        current_ = data().data();
        end_of_file_ = current_ + data().size();
        previous_end_ = token_start_ = current_;
    }

    Token Lex();
    // same as lex but ignore any comments
    Token LexNoComments();

private:
    StringView data() { return source_file_.data(); }

    // return the next character
    constexpr char Peek() const;
    // skip the next character
    void Skip();
    // consume the next character and update the current token info
    char Consume();
    // reset current token info, and return the previous token
    StringView Reset(Token::Kind kind);
    // emits a Token and then resets the current token info. identifiers should
    // use lex identifier instead
    Token Finish(Token::Kind kind);

    // keep skipping while we see whitespace
    void SkipWhitespace();

    Token LexEndOfStream();
    Token LexNumericLiteral();
    Token LexIdentifier();
    Token LexEscapedIdentifier();
    Token LexStringLiteral();
    Token LexCommentOrDocComment();

    const SourceFile& source_file_;
    // e.g. "array" -> Token::Subkind::kArray
    std::map<StringView, Token::Subkind> keyword_table_;
    ErrorReporter* error_reporter_;

    const char* current_ = nullptr;
    const char* end_of_file_ = nullptr;
    // start of the current token that we are lexing
    const char* token_start_ = nullptr;
    // end of the previous token that we lexed
    const char* previous_end_ = nullptr;
    size_t token_size_ = 0u;
};

} // namespace fidl

#endif // LEXER_H_
