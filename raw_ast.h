#ifndef RAW_AST_H_
#define RAW_AST_H_

#include <memory>
#include <utility>
#include <vector>

#include "source_location.h"
#include "token.h"
#include "types.h"

namespace fidl {
namespace raw {

class TreeVisitor;

class SourceElement {
public:
    explicit SourceElement(SourceElement const& element)
        : start_(element.start_), end_(element.end_) {}

    explicit SourceElement(Token start, Token end)
        : start_(start), end_(end) {}

    SourceLocation location() const { return start_.location(); }

    virtual ~SourceElement() {}

    Token start_;
    Token end_;
};

class SourceElementMark {
public:
    SourceElementMark(TreeVisitor& tv, const SourceElement& element);

    ~SourceElementMark();

private:
    TreeVisitor& tv_;
    const SourceElement& element_;
};

class Identifier : public SourceElement {
public:
    explicit Identifier(SourceElement const& element)
        : SourceElement(element) {}

    virtual ~Identifier() {}

    void Accept(TreeVisitor& visitor) {
        SourceElementMark sem(visitor, *this);
    }
};

class CompoundIdentifier : public SourceElement {
public:
    CompoundIdentifier(SourceElement const& element, std::vector<std::unique_ptr<Identifier>> components)
        : SourceElement(element), components(std::move(components)) {}

    virtual ~CompoundIdentifier() {}

    std::vector<std::unique_ptr<Identifier>> components;

    void Accept(TreeVisitor& visitor);
};

class Literal : public SourceElement {
public:
    enum class Kind {
        kString,
        kNumeric,
        kTrue,
        kFalse,
    };

    explicit Literal(SourceElement const& element, Kind kind)
        : SourceElement(element), kind(kind) {}

    virtual ~Literal() {}

    const Kind kind;
};

class StringLiteral : public Literal {
public:
    explicit StringLiteral(SourceElement const& element)
        : Literal(element, Kind::kString) {}

    void Accept(TreeVisitor& visitor);
};

class NumericLiteral : public Literal {
public:
    NumericLiteral(SourceElement const& element)
        : Literal(element, Kind::kNumeric) {}

    void Accept(TreeVisitor& visitor);
};

class TrueLiteral : public Literal {
public:
    TrueLiteral(SourceElement const& element)
        : Literal(element, Kind::kTrue) {}

    void Accept(TreeVisitor& visitor);
};

class FalseLiteral : public Literal {
public:
    FalseLiteral(SourceElement const& element)
        : Literal(element, Kind::kFalse) {}

    void Accept(TreeVisitor& visitor);
};

class Constant : public SourceElement {
public:
    enum class Kind {
        kIdentifier,
        kLiteral,
    };

    explicit Constant(Token token, Kind kind)
        : SourceElement(token, token), kind(kind) {}

    virtual ~Constant() {}

    const Kind kind;
};

class IdentifierConstant : public Constant {
public:
    explicit IdentifierConstant(std::unique_ptr<CompoundIdentifier> identifier)
        : Constant(identifier->start_, Kind::kIdentifier), identifier(std::move(identifier)) {}

    std::unique_ptr<CompoundIdentifier> identifier;

    void Accept(TreeVisitor& visitor);
};


class LiteralConstant : public Constant {
public:
    explicit LiteralConstant(std::unique_ptr<Literal> literal)
        : Constant(literal->start_, Kind::kLiteral), literal(std::move(literal)) {}

    std::unique_ptr<Literal> literal;

    void Accept(TreeVisitor& visitor);
};

class TypeConstructor final : public SourceElement {
public:
    TypeConstructor(SourceElement const& element,
        std::unique_ptr<CompoundIdentifier> identifier,
        std::unique_ptr<TypeConstructor> maybe_arg_type_ctor,
        std::unique_ptr<types::HandleSubtype> maybe_handle_subtype,
        std::unique_ptr<Constant> maybe_size,
        types::Nullability nullability)
        : SourceElement(element.start_, element.end_),
          identifier(std::move(identifier)),
          maybe_arg_type_ctor(std::move(maybe_arg_type_ctor)),
          maybe_handle_subtype(std::move(maybe_handle_subtype)),
          maybe_size(std::move(maybe_size)),
          nullability(nullability) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<CompoundIdentifier> identifier;
    std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
    std::unique_ptr<types::HandleSubtype> maybe_handle_subtype;
    std::unique_ptr<Constant> maybe_size;
    types::Nullability nullability;
};

class ConstDeclaration : public SourceElement {
public:
    ConstDeclaration(SourceElement const& element,
                     std::unique_ptr<TypeConstructor> type_ctor,
                     std::unique_ptr<Identifier> identifier,
                     std::unique_ptr<Constant> constant)
        : SourceElement(element),
          type_ctor(std::move(type_ctor)),
          identifier(std::move(identifier)),
          constant(std::move(constant)) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<TypeConstructor> type_ctor;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> constant;
};

class File : public SourceElement {
public:
    File(SourceElement const& element,
         Token end,
         std::unique_ptr<CompoundIdentifier> library_name,
         std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list)
        : SourceElement(element),
          library_name(std::move(library_name)),
          const_declaration_list(std::move(const_declaration_list)),
          end_(end) {}

    void Accept(TreeVisitor& visitor);

    std::unique_ptr<CompoundIdentifier> library_name;
    std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
    Token end_;
};

} // namespace raw
} // namespace fidl

#endif // RAW_AST_H_