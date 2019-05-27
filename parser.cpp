#include "parser.h"

namespace fidl {

#define CASE_TOKEN(K) \
    Token::KindAndSubkind(K, Token::Subkind::kNone).combined()

#define CASE_IDENTIFIER(K) \
    Token::KindAndSubkind(Token::Kind::kIdentifier, K).combined()

namespace {
enum {
    More,
    Done,
};
} // namespace

Parser::Parser(Lexer* lexer, ErrorReporter* error_reporter)
    : lexer_(lexer), error_reporter_(error_reporter) {
    last_token_ = Lex();
}

decltype(nullptr) Parser::Fail() {
    return Fail("found unexpected token");
}

decltype(nullptr) Parser::Fail(StringView message) {
    if (Ok()) {
        error_reporter_->ReportError(last_token_, std::move(message));
    }
    return nullptr;
}

std::unique_ptr<raw::Identifier> Parser::ParseIdentifier(bool is_discarded) {
    ASTScope scope(this, is_discarded);
    ConsumeToken(OfKind(Token::Kind::kIdentifier));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Identifier>(scope.GetSourceElement());
}

std::unique_ptr<raw::CompoundIdentifier> Parser::ParseCompoundIdentifier() {
    ASTScope scope(this);
    std::vector<std::unique_ptr<raw::Identifier>> components;

    components.emplace_back(ParseIdentifier());
    if (!Ok())
        return Fail();

    auto parse_component = [&components, this]() {
        switch (Peek().combined()) {
        default:
            return Done;
        case CASE_TOKEN(Token::Kind::kDot):
            ConsumeToken(OfKind(Token::Kind::kDot));
            if (Ok()) {
                components.emplace_back(ParseIdentifier());
            }
            return More;
        }
    };

    while (parse_component() == More) {
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::CompoundIdentifier>(scope.GetSourceElement(), std::move(components));
}

std::unique_ptr<raw::StringLiteral> Parser::ParseStringLiteral() {
    ASTScope scope(this);
    ConsumeToken(OfKind(Token::Kind::kStringLiteral));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::StringLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::NumericLiteral> Parser::ParseNumericLiteral() {
    ASTScope scope(this);
    ConsumeToken(OfKind(Token::Kind::kNumericLiteral));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::NumericLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::TrueLiteral> Parser::ParseTrueLiteral() {
    ASTScope scope(this);
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kTrue));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::TrueLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::FalseLiteral> Parser::ParseFalseLiteral() {
    ASTScope scope(this);
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kFalse));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::FalseLiteral>(scope.GetSourceElement());
}

std::unique_ptr<raw::Literal> Parser::ParseLiteral() {
    switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kStringLiteral):
        return ParseStringLiteral();

    case CASE_TOKEN(Token::Kind::kNumericLiteral):
        return ParseNumericLiteral();

    case CASE_IDENTIFIER(Token::Subkind::kTrue):
        return ParseTrueLiteral();

    case CASE_IDENTIFIER(Token::Subkind::kFalse):
        return ParseFalseLiteral();

    default:
        return Fail();
    }
}

std::unique_ptr<raw::Constant> Parser::ParseConstant() {
    switch (Peek().combined()) {
    case CASE_TOKEN(Token::Kind::kIdentifier): {
        auto identifier = ParseCompoundIdentifier();
        if (!Ok())
            return Fail();
        return std::make_unique<raw::IdentifierConstant>(std::move(identifier));
    }
    case CASE_IDENTIFIER(Token::Subkind::kTrue):
    case CASE_IDENTIFIER(Token::Subkind::kFalse):
    case CASE_TOKEN(Token::Kind::kStringLiteral):
    case CASE_TOKEN(Token::Kind::kNumericLiteral): {
        auto literal = ParseLiteral();
        if (!Ok())
            return Fail();
        return std::make_unique<raw::LiteralConstant>(std::move(literal));
    }
    default:
        return Fail();
    }
}

std::unique_ptr<raw::Using> Parser::ParseUsing() {
    ASTScope scope(this);
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUsing));
    if (!Ok())
        return Fail();
    auto using_path = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Identifier> maybe_alias;
    std::unique_ptr<raw::TypeConstructor> type_ctor;

    if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kAs))) {
        if (!Ok())
            return Fail();
        maybe_alias = ParseIdentifier();
        if (!Ok())
            return Fail();
    } else if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
        if (!Ok() || using_path->components.size() != 1u)
            return Fail();
        type_ctor = ParseTypeConstructor();
        if (!Ok())
            return Fail();
        return std::make_unique<raw::UsingAlias>(
            scope.GetSourceElement(), std::move(using_path->components[0]), std::move(type_ctor));
    }

    return std::make_unique<raw::UsingLibrary>(
        scope.GetSourceElement(), std::move(using_path), std::move(maybe_alias));
}

std::unique_ptr<raw::TypeConstructor> Parser::ParseTypeConstructor() {
    ASTScope scope(this);
    auto identifier = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::TypeConstructor> maybe_arg_type_ctor;
    if (MaybeConsumeToken(OfKind(Token::Kind::kLeftAngle))) {
        if (!Ok())
            return Fail();
        maybe_arg_type_ctor = ParseTypeConstructor();
        if (!Ok())
            return Fail();
        ConsumeToken(OfKind(Token::Kind::kRightAngle));
        if (!Ok())
            return Fail();
    }

    std::unique_ptr<raw::Constant> maybe_size;
    if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
        if (!Ok())
            return Fail();
        maybe_size = ParseConstant();
        if (!Ok())
            return Fail();
    }

    auto nullability = types::Nullability::kNonnullable;
    if (MaybeConsumeToken(OfKind(Token::Kind::kQuestion))) {
        if (!Ok())
            return Fail();
        nullability = types::Nullability::kNullable;
    }

    return std::make_unique<raw::TypeConstructor>(
        scope.GetSourceElement(),
        std::move(identifier),
        std::move(maybe_arg_type_ctor),
        std::move(maybe_size),
        nullability);
}

std::unique_ptr<raw::UsingAlias> Parser::ParseUsingAlias(ASTScope& scope) {
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUsing));
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kEqual));
    if (!Ok())
        return Fail();

    auto type_ctor = ParseTypeConstructor();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::UsingAlias>(
        scope.GetSourceElement(), std::move(identifier), std::move(type_ctor));
}

std::unique_ptr<raw::ConstDeclaration> Parser::ParseConstDeclaration(ASTScope& scope) {
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kConst));
    if (!Ok())
        return Fail();

    auto type_ctor = ParseTypeConstructor();
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kEqual));
    if (!Ok())
        return Fail();

    auto constant = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::ConstDeclaration>(
        scope.GetSourceElement(), std::move(type_ctor), std::move(identifier), std::move(constant));
}

std::unique_ptr<raw::StructMember> Parser::ParseStructMember() {
    ASTScope scope(this);
    auto type_ctor = ParseTypeConstructor();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::Constant> maybe_default_value;
    if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
        if (!Ok())
            return Fail();
        maybe_default_value = ParseConstant();
        if (!Ok())
            return Fail();
    }

    return std::make_unique<raw::StructMember>(
        scope.GetSourceElement(), std::move(type_ctor), std::move(identifier), std::move(maybe_default_value));
}

std::unique_ptr<raw::StructDeclaration> Parser::ParseStructDeclaration(ASTScope& scope) {
    std::vector<std::unique_ptr<raw::StructMember>> members;

    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kStruct));
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kLeftCurly));
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        if (Peek().kind() == Token::Kind::kRightCurly) {
            ConsumeToken(OfKind(Token::Kind::kRightCurly));
            return Done;
        } else {
            members.emplace_back(ParseStructMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<raw::StructDeclaration>(
        scope.GetSourceElement(), std::move(identifier), std::move(members));
}

std::unique_ptr<raw::File> Parser::ParseFile() {
    ASTScope scope(this);
    std::vector<std::unique_ptr<raw::Using>> using_list;
    std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;

    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kLibrary));
    if (!Ok())
        return Fail();

    auto library_name = ParseCompoundIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kSemicolon));
    if (!Ok())
        return Fail();

    auto parse_using = [&using_list, this]() {
        ASTScope scope(this);

        switch (Peek().combined()) {
        default:
            return Done;

        case CASE_IDENTIFIER(Token::Subkind::kUsing):
            using_list.emplace_back(ParseUsing());
            return More;
        }
    };

    while (parse_using() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }

    auto parse_decl = [&using_list, &const_declaration_list, &struct_declaration_list, this]() {
        ASTScope scope(this);

        switch (Peek().combined()) {
        default:
            return Done;

        case CASE_IDENTIFIER(Token::Subkind::kConst):
            const_declaration_list.emplace_back(ParseConstDeclaration(scope));
            return More;
        case CASE_IDENTIFIER(Token::Subkind::kStruct):
            struct_declaration_list.emplace_back(ParseStructDeclaration(scope));
            return More;
        case CASE_IDENTIFIER(Token::Subkind::kUsing):
            using_list.emplace_back(ParseUsingAlias(scope));
            return More;
        }
    };

    while (parse_decl() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }

    Token end = ConsumeToken(OfKind(Token::Kind::kEndOfFile));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::File>(
        scope.GetSourceElement(),
        end,
        std::move(library_name),
        std::move(using_list),
        std::move(const_declaration_list),
        std::move(struct_declaration_list));
}

} // namespace fidl
