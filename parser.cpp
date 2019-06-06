#include <errno.h>

#include "attributes.h"
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

std::unique_ptr<raw::Ordinal> Parser::ParseOrdinal() {
    ASTScope scope(this);

    ConsumeToken(OfKind(Token::Kind::kNumericLiteral));
    if (!Ok())
        return Fail();
    auto data = scope.GetSourceElement().location().data();
    std::string string_data(data.data(), data.data() + data.size());
    errno = 0;
    unsigned long long value = strtoull(string_data.data(), nullptr, 0);
    assert(errno == 0 && "Unparsable number should not be lexed.");
    if (value > std::numeric_limits<uint32_t>::max())
        return Fail("Ordinal out-of-bound");
    uint32_t ordinal = static_cast<uint32_t>(value);
    if (ordinal == 0u)
        return Fail("Fidl ordinals cannot be 0");

    ConsumeToken(OfKind(Token::Kind::kColon));
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Ordinal>(scope.GetSourceElement(), ordinal);
}

std::unique_ptr<raw::Attribute> Parser::ParseAttribute() {
    ASTScope scope(this);
    auto name = ParseIdentifier();
    if (!Ok())
        return Fail();
    std::unique_ptr<raw::StringLiteral> value;
    if (MaybeConsumeToken(OfKind(Token::Kind::kEqual))) {
        value = ParseStringLiteral();
        if (!Ok())
            return Fail();
    }

    std::string str_name("");
    std::string str_value("");
    if (name)
        str_name = std::string(name->location().data().data(), name->location().data().size());
    if (value) {
        auto data = value->location().data();
        // wouldn't this always be true if ParseStringLiteral succeeded?
        if (data.size() >= 2 && data[0] == '"' && data[data.size() - 1] == '"') {
            str_value = std::string(data.data() + 1, data.size() - 2);
        }
    }
    return std::make_unique<raw::Attribute>(scope.GetSourceElement(), str_name, str_value);
}

std::unique_ptr<raw::AttributeList>
Parser::ParseAttributeList(std::unique_ptr<raw::Attribute>&& doc_comment, ASTScope& scope) {
    AttributesBuilder attributes_builder(error_reporter_);
    if (doc_comment) {
        if (!attributes_builder.Insert(std::move(doc_comment)))
            return Fail();
    }
    ConsumeToken(OfKind(Token::Kind::kLeftSquare));
    if (!Ok())
        return Fail();
    for (;;) {
        auto attribute = ParseAttribute();
        if (!Ok())
            return Fail();
        if (!attributes_builder.Insert(std::move(attribute)))
            return Fail();
        if (!MaybeConsumeToken(OfKind(Token::Kind::kComma)))
            break;
    }
    ConsumeToken(OfKind(Token::Kind::kRightSquare));
    if (!Ok())
        return Fail();
    auto attribute_list = std::make_unique<raw::AttributeList>(scope.GetSourceElement(), attributes_builder.Done());
    return attribute_list;
}

std::unique_ptr<raw::Attribute> Parser::ParseDocComment() {
    ASTScope scope(this);
    std::string str_value("");

    Token doc_line;
    while (Peek().kind() == Token::Kind::kDocComment) {
        doc_line = ConsumeToken(OfKind(Token::Kind::kDocComment));
        // exclude the "///" part of the comment (each Token corresponds to
        // an entire line)
        auto data = doc_line.location().data();
        str_value += std::string(data.data() + 3, data.size() - 2);
        // why assert instead of just returning Fail() like everywhere else?
        assert(Ok());
    }
    return std::make_unique<raw::Attribute>(scope.GetSourceElement(), "Doc", str_value);
}

std::unique_ptr<raw::AttributeList> Parser::MaybeParseAttributeList() {
    ASTScope scope(this);
    std::unique_ptr<raw::Attribute> doc_comment;
    if (Peek().kind() == Token::Kind::kDocComment) {
        doc_comment = ParseDocComment();
    }
    if (Peek().kind() == Token::Kind::kLeftSquare) {
        return ParseAttributeList(std::move(doc_comment), scope);
    }

    if (doc_comment) {
        AttributesBuilder attributes_builder(error_reporter_);
        if (!attributes_builder.Insert(std::move(doc_comment)))
            return Fail();
        return std::make_unique<raw::AttributeList>(scope.GetSourceElement(), attributes_builder.Done());
    }
    return nullptr;
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

std::unique_ptr<raw::ConstDeclaration>
Parser::ParseConstDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
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
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(type_ctor),
        std::move(identifier),
        std::move(constant));
}

std::unique_ptr<raw::BitsMember> Parser::ParseBitsMember() {
    ASTScope scope(this);
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kEqual));
    if (!Ok())
        return Fail();

    auto member_value = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::BitsMember>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(member_value));
}

std::unique_ptr<raw::BitsDeclaration>
Parser::ParseBitsDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
    std::vector<std::unique_ptr<raw::BitsMember>> members;
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kBits));
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::TypeConstructor> maybe_type_ctor;
    if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
        if (!Ok())
            return Fail();
        maybe_type_ctor = ParseTypeConstructor();
        if (!Ok())
            return Fail();
    }

    ConsumeToken(OfKind(Token::Kind::kLeftCurly));
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        if (Peek().kind() == Token::Kind::kRightCurly) {
          ConsumeToken(OfKind(Token::Kind::kRightCurly));
          return Done;
        } else {
          members.emplace_back(ParseBitsMember());
          return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail("must have at least one bits member");

    return std::make_unique<raw::BitsDeclaration>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(maybe_type_ctor),
        std::move(members));
}


std::unique_ptr<raw::EnumMember> Parser::ParseEnumMember() {
    ASTScope scope(this);
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kEqual));
    if (!Ok())
        return Fail();

    auto member_value = ParseConstant();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::EnumMember>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(member_value));
}

std::unique_ptr<raw::EnumDeclaration>
Parser::ParseEnumDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
    std::vector<std::unique_ptr<raw::EnumMember>> members;
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kEnum));
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    std::unique_ptr<raw::TypeConstructor> maybe_type_ctor;
    if (MaybeConsumeToken(OfKind(Token::Kind::kColon))) {
        if (!Ok())
            return Fail();
        maybe_type_ctor = ParseTypeConstructor();
        if (!Ok())
            return Fail();
    }

    ConsumeToken(OfKind(Token::Kind::kLeftCurly));
    if (!Ok())
        return Fail();

    auto parse_member = [&members, this]() {
        if (Peek().kind() == Token::Kind::kRightCurly) {
          ConsumeToken(OfKind(Token::Kind::kRightCurly));
          return Done;
        } else {
          members.emplace_back(ParseEnumMember());
          return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        return Fail();

    return std::make_unique<raw::EnumDeclaration>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(maybe_type_ctor),
        std::move(members));
}

std::unique_ptr<raw::Parameter> Parser::ParseParameter() {
    ASTScope scope(this);
    auto type_ctor = ParseTypeConstructor();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::Parameter>(scope.GetSourceElement(), std::move(type_ctor),
                                            std::move(identifier));
}

std::unique_ptr<raw::ParameterList> Parser::ParseParameterList() {
    ASTScope scope(this);
    std::vector<std::unique_ptr<raw::Parameter>> parameter_list;

    if (Peek().kind() != Token::Kind::kRightParen) {
        auto parameter = ParseParameter();
        parameter_list.emplace_back(std::move(parameter));
        if (!Ok())
            return Fail();
        while (Peek().kind() == Token::Kind::kComma) {
            ConsumeToken(OfKind(Token::Kind::kComma));
            if (!Ok())
                return Fail();
            parameter_list.emplace_back(ParseParameter());
            if (!Ok())
                return Fail();
        }
    }

    return std::make_unique<raw::ParameterList>(scope.GetSourceElement(), std::move(parameter_list));
}

std::unique_ptr<raw::InterfaceMethod> Parser::ParseProtocolEvent(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope,
    std::unique_ptr<raw::Ordinal> ordinal) {

    ConsumeToken(OfKind(Token::Kind::kArrow));
    if (!Ok())
        return Fail();

    auto method_name = ParseIdentifier();
    if (!Ok())
        return Fail();

    auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
        ConsumeToken(OfKind(Token::Kind::kLeftParen));
        if (!Ok())
            return false;
        *params_out = ParseParameterList();
        if (!Ok())
            return false;
        ConsumeToken(OfKind(Token::Kind::kRightParen));
        if (!Ok())
            return false;
        return true;
    };

    std::unique_ptr<raw::ParameterList> response;
    if (!parse_params(&response))
        return Fail();

    std::unique_ptr<raw::TypeConstructor> maybe_error;
    if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kError))) {
        maybe_error = ParseTypeConstructor();
        if (!Ok())
            return Fail();
    }

    assert(method_name);
    assert(response);

    return std::make_unique<raw::InterfaceMethod>(scope.GetSourceElement(),
                                                  std::move(attributes),
                                                  std::move(ordinal),
                                                  std::move(method_name),
                                                  nullptr /* maybe_request */,
                                                  std::move(response),
                                                  std::move(maybe_error));
}

std::unique_ptr<raw::InterfaceMethod> Parser::ParseProtocolMethod(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope,
    std::unique_ptr<raw::Ordinal> ordinal,
    std::unique_ptr<raw::Identifier> method_name) {

    auto parse_params = [this](std::unique_ptr<raw::ParameterList>* params_out) {
        ConsumeToken(OfKind(Token::Kind::kLeftParen));
        if (!Ok())
            return false;
        *params_out = ParseParameterList();
        if (!Ok())
            return false;
        ConsumeToken(OfKind(Token::Kind::kRightParen));
        if (!Ok())
            return false;
        return true;
    };

    std::unique_ptr<raw::ParameterList> request;
    if (!parse_params(&request))
        return Fail();

    std::unique_ptr<raw::ParameterList> maybe_response;
    std::unique_ptr<raw::TypeConstructor> maybe_error;
    if (MaybeConsumeToken(OfKind(Token::Kind::kArrow))) {
        if (!Ok())
            return Fail();
        if (!parse_params(&maybe_response))
            return Fail();
        if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kError))) {
            maybe_error = ParseTypeConstructor();
            if (!Ok())
                return Fail();
        }
    }

    assert(method_name);
    assert(request);

    return std::make_unique<raw::InterfaceMethod>(scope.GetSourceElement(),
                                                  std::move(attributes),
                                                  std::move(ordinal),
                                                  std::move(method_name),
                                                  std::move(request),
                                                  std::move(maybe_response),
                                                  std::move(maybe_error));
}

void Parser::ParseProtocolMember(
    std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope,
    std::vector<std::unique_ptr<raw::ComposeProtocol>>* composed_protocols,
    std::vector<std::unique_ptr<raw::InterfaceMethod>>* methods) {

    switch (Peek().kind()) {
    case Token::Kind::kArrow: {
        auto event = ParseProtocolEvent(std::move(attributes), scope, nullptr /* ordinal */);
        methods->push_back(std::move(event));
        break;
    }
    case Token::Kind::kIdentifier: {
        // we need to "look ahead" to see whether this is a method or a compose
        auto identifier = ParseIdentifier();
        if (!Ok())
            break;

        // allow "compose" as a method name (we assume it is if we see a left paren)
        if (Peek().kind() == Token::Kind::kLeftParen) {
            auto method = ParseProtocolMethod(
                std::move(attributes),
                scope,
                nullptr /* ordinal */,
                std::move(identifier));
            methods->push_back(std::move(method));
            break;
        } else if (identifier->location().data() == "compose") {
            if (attributes) {
                Fail("Cannot attach attributes to compose stanza");
                break;
            }
            auto protocol_name = ParseCompoundIdentifier();
            if (!Ok())
                break;
            composed_protocols->push_back(std::make_unique<raw::ComposeProtocol>(
                raw::SourceElement(identifier->start_, protocol_name->end_),
                std::move(protocol_name)));
            break;
        } else {
            Fail("unrecognized protocol member");
            break;
        }
    }
    default:
        break;
    }
}

std::unique_ptr<raw::InterfaceDeclaration>
Parser::ParseProtocolDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kProtocol));
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kLeftCurly));
    if (!Ok())
        return Fail();

    std::vector<std::unique_ptr<raw::ComposeProtocol>> composed_protocols;
    std::vector<std::unique_ptr<raw::InterfaceMethod>> methods;
    auto parse_member = [&composed_protocols, &methods, this]() {
        ASTScope scope(this);
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek().kind()) {
        default:
            ConsumeToken(OfKind(Token::Kind::kRightCurly));
            return Done;

        case Token::Kind::kArrow:
        case Token::Kind::kIdentifier:
            ParseProtocolMember(
                std::move(attributes), scope, &composed_protocols, &methods);
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    return std::make_unique<raw::InterfaceDeclaration>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(composed_protocols),
        std::move(methods)); 
}

std::unique_ptr<raw::StructMember> Parser::ParseStructMember() {
    ASTScope scope(this);
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
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
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(type_ctor),
        std::move(identifier),
        std::move(maybe_default_value));
}

std::unique_ptr<raw::StructDeclaration>
Parser::ParseStructDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
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
        scope.GetSourceElement(), std::move(attributes), std::move(identifier), std::move(members));
}

std::unique_ptr<raw::TableMember>
Parser::ParseTableMember() {
    ASTScope scope(this);
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();

    auto ordinal = ParseOrdinal();
    if (!Ok())
        return Fail();

    if (MaybeConsumeToken(IdentifierOfSubkind(Token::Subkind::kReserved))) {
        if (!Ok())
            return Fail();
        if (attributes != nullptr)
            return Fail("Cannot attach attributes to reserved ordinals");
        return std::make_unique<raw::TableMember>(scope.GetSourceElement(), std::move(ordinal));     
    }

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

    return std::make_unique<raw::TableMember>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(ordinal),
        std::move(type_ctor),
        std::move(identifier),
        std::move(maybe_default_value));
}

std::unique_ptr<raw::TableDeclaration>
Parser::ParseTableDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
    std::vector<std::unique_ptr<raw::TableMember>> members;

    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kTable));
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
            members.emplace_back(ParseTableMember());
            return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            return Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        return Fail();

    return std::make_unique<raw::TableDeclaration>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(members));
}

std::unique_ptr<raw::UnionMember> Parser::ParseUnionMember() {
    ASTScope scope(this);
    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();
    auto type_ctor = ParseTypeConstructor();
    if (!Ok())
        return Fail();
    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::UnionMember>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(type_ctor),
        std::move(identifier));
}

std::unique_ptr<raw::UnionDeclaration>
Parser::ParseUnionDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
    std::vector<std::unique_ptr<raw::UnionMember>> members;

    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kUnion));
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
          members.emplace_back(ParseUnionMember());
          return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();
        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }
    if (!Ok())
        Fail();

    if (members.empty())
        Fail();

    return std::make_unique<raw::UnionDeclaration>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(members));
}

std::unique_ptr<raw::XUnionMember> Parser::ParseXUnionMember() {
    ASTScope scope(this);

    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();

    auto type_ctor = ParseTypeConstructor();
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    return std::make_unique<raw::XUnionMember>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(type_ctor),
        std::move(identifier));
}

std::unique_ptr<raw::XUnionDeclaration>
Parser::ParseXUnionDeclaration(std::unique_ptr<raw::AttributeList> attributes, ASTScope& scope) {
    std::vector<std::unique_ptr<raw::XUnionMember>> members;

    ConsumeToken(IdentifierOfSubkind(Token::Subkind::kXUnion));
    if (!Ok())
        return Fail();

    auto identifier = ParseIdentifier();
    if (!Ok())
        return Fail();

    ConsumeToken(OfKind(Token::Kind::kLeftCurly));
    if (!Ok())
        return Fail();

    auto parse_member = [&]() {
        if (Peek().kind() == Token::Kind::kRightCurly) {
          ConsumeToken(OfKind(Token::Kind::kRightCurly));
          return Done;
        } else {
          members.emplace_back(ParseXUnionMember());
          return More;
        }
    };

    while (parse_member() == More) {
        if (!Ok())
            Fail();

        ConsumeToken(OfKind(Token::Kind::kSemicolon));
        if (!Ok())
            return Fail();
    }

    if (!Ok())
        Fail();

    return std::make_unique<raw::XUnionDeclaration>(
        scope.GetSourceElement(),
        std::move(attributes),
        std::move(identifier),
        std::move(members));
}

std::unique_ptr<raw::File> Parser::ParseFile() {
    ASTScope scope(this);
    std::vector<std::unique_ptr<raw::Using>> using_list;
    std::vector<std::unique_ptr<raw::ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<raw::BitsDeclaration>> bits_declaration_list;
    std::vector<std::unique_ptr<raw::EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<raw::InterfaceDeclaration>> interface_declaration_list;
    std::vector<std::unique_ptr<raw::StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<raw::TableDeclaration>> table_declaration_list;
    std::vector<std::unique_ptr<raw::UnionDeclaration>> union_declaration_list;
    std::vector<std::unique_ptr<raw::XUnionDeclaration>> xunion_declaration_list;

    auto attributes = MaybeParseAttributeList();
    if (!Ok())
        return Fail();

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

    auto parse_decl = [&using_list,
                       &const_declaration_list,
                       &bits_declaration_list,
                       &enum_declaration_list,
                       &interface_declaration_list,
                       &struct_declaration_list,
                       &table_declaration_list,
                       &union_declaration_list,
                       &xunion_declaration_list,
                       this]() {
        ASTScope scope(this);
        std::unique_ptr<raw::AttributeList> attributes = MaybeParseAttributeList();
        if (!Ok())
            return More;

        switch (Peek().combined()) {
        default:
            return Done;

        case CASE_IDENTIFIER(Token::Subkind::kUsing):
            if (attributes != nullptr) {
                Fail("Type alias cannot have attributes");
                return More;
            }
            using_list.emplace_back(ParseUsingAlias(scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kConst):
            const_declaration_list.emplace_back(ParseConstDeclaration(std::move(attributes), scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kBits):
            bits_declaration_list.emplace_back(ParseBitsDeclaration(std::move(attributes), scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kEnum):
            enum_declaration_list.emplace_back(ParseEnumDeclaration(std::move(attributes), scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kProtocol):
            interface_declaration_list.emplace_back(
                ParseProtocolDeclaration(std::move(attributes), scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kStruct):
            struct_declaration_list.emplace_back(ParseStructDeclaration(std::move(attributes), scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kTable):
            table_declaration_list.emplace_back(ParseTableDeclaration(std::move(attributes), scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kUnion):
            union_declaration_list.emplace_back(ParseUnionDeclaration(std::move(attributes), scope));
            return More;

        case CASE_IDENTIFIER(Token::Subkind::kXUnion):
            xunion_declaration_list.emplace_back(ParseXUnionDeclaration(std::move(attributes), scope));
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
        std::move(attributes),
        std::move(library_name),
        std::move(using_list),
        std::move(const_declaration_list),
        std::move(bits_declaration_list),
        std::move(enum_declaration_list),
        std::move(interface_declaration_list),
        std::move(struct_declaration_list),
        std::move(table_declaration_list),
        std::move(union_declaration_list),
        std::move(xunion_declaration_list));
}

} // namespace fidl
