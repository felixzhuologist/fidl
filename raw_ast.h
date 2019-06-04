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
    SourceElementMark(TreeVisitor* tv, const SourceElement& element);

    ~SourceElementMark();

private:
    TreeVisitor* tv_;
    const SourceElement& element_;
};

class Identifier final : public SourceElement {
public:
    explicit Identifier(SourceElement const& element)
        : SourceElement(element) {}

    virtual ~Identifier() {}

    void Accept(TreeVisitor* visitor) const;
};

class CompoundIdentifier final : public SourceElement {
public:
    CompoundIdentifier(SourceElement const& element, std::vector<std::unique_ptr<Identifier>> components)
        : SourceElement(element), components(std::move(components)) {}

    virtual ~CompoundIdentifier() {}

    std::vector<std::unique_ptr<Identifier>> components;

    void Accept(TreeVisitor* visitor) const;
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

    void Accept(TreeVisitor* visitor) const;
};

class NumericLiteral : public Literal {
public:
    NumericLiteral(SourceElement const& element)
        : Literal(element, Kind::kNumeric) {}

    void Accept(TreeVisitor* visitor) const;
};

class TrueLiteral : public Literal {
public:
    TrueLiteral(SourceElement const& element)
        : Literal(element, Kind::kTrue) {}

    void Accept(TreeVisitor* visitor) const;
};

class FalseLiteral : public Literal {
public:
    FalseLiteral(SourceElement const& element)
        : Literal(element, Kind::kFalse) {}

    void Accept(TreeVisitor* visitor) const;
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

    void Accept(TreeVisitor* visitor) const;
};


class LiteralConstant : public Constant {
public:
    explicit LiteralConstant(std::unique_ptr<Literal> literal)
        : Constant(literal->start_, Kind::kLiteral), literal(std::move(literal)) {}

    std::unique_ptr<Literal> literal;

    void Accept(TreeVisitor* visitor) const;
};

class Attribute final : public SourceElement {
public:
    Attribute(SourceElement const& element, std::string name, std::string value)
        : SourceElement(element), name(std::move(name)), value(std::move(value)) {}

    void Accept(TreeVisitor* visitor) const;

    const std::string name;
    const std::string value;
};

class AttributeList final : public SourceElement {
public:
    AttributeList(SourceElement const& element, std::vector<std::unique_ptr<Attribute>> attributes)
        : SourceElement(element), attributes(std::move(attributes)) {}

    bool HasAttribute(std::string name) const {
        for (const auto& attribute : attributes) {
            if (attribute->name == name)
                return true;
        }
        return false;
    }

    void Accept(TreeVisitor* visitor) const;

    std::vector<std::unique_ptr<Attribute>> attributes;
};

class TypeConstructor final : public SourceElement {
public:
    TypeConstructor(SourceElement const& element,
        std::unique_ptr<CompoundIdentifier> identifier,
        std::unique_ptr<TypeConstructor> maybe_arg_type_ctor,
        std::unique_ptr<Constant> maybe_size,
        types::Nullability nullability)
        : SourceElement(element.start_, element.end_),
          identifier(std::move(identifier)),
          maybe_arg_type_ctor(std::move(maybe_arg_type_ctor)),
          maybe_size(std::move(maybe_size)),
          nullability(nullability) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<CompoundIdentifier> identifier;
    std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
    std::unique_ptr<Constant> maybe_size;
    types::Nullability nullability;
};

class Using : public SourceElement {
public:
    enum class Kind {
      kLibrary,
      kAlias,
    };

    explicit Using(SourceElement const& element, Kind kind)
        : SourceElement(element), kind(kind) {}

    virtual ~Using() {}

    const Kind kind;
};

class UsingLibrary : public Using {
public:
  UsingLibrary(SourceElement const& element, std::unique_ptr<CompoundIdentifier> using_path,
        std::unique_ptr<Identifier> maybe_alias)
      : Using(element, Kind::kLibrary), using_path(std::move(using_path)),
        maybe_alias(std::move(maybe_alias)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<CompoundIdentifier> using_path;
  std::unique_ptr<Identifier> maybe_alias;
};

class UsingAlias : public Using {
public:
  UsingAlias(SourceElement const& element,
        std::unique_ptr<Identifier> alias,
        std::unique_ptr<TypeConstructor> type_ctor)
      : Using(element, Kind::kAlias), alias(std::move(alias)), type_ctor(std::move(type_ctor)) {}

  void Accept(TreeVisitor* visitor) const;

  std::unique_ptr<Identifier> alias;
  std::unique_ptr<TypeConstructor> type_ctor;
};

class ConstDeclaration final : public SourceElement {
public:
    ConstDeclaration(SourceElement const& element,
                     std::unique_ptr<AttributeList> attributes,
                     std::unique_ptr<TypeConstructor> type_ctor,
                     std::unique_ptr<Identifier> identifier,
                     std::unique_ptr<Constant> constant)
        : SourceElement(element),
          attributes(std::move(attributes)),
          type_ctor(std::move(type_ctor)),
          identifier(std::move(identifier)),
          constant(std::move(constant)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<TypeConstructor> type_ctor;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> constant;
};

class BitsMember final : public SourceElement {
public:
    BitsMember(SourceElement const& element,
               std::unique_ptr<AttributeList> attributes,
               std::unique_ptr<Identifier> identifier,
               std::unique_ptr<Constant> value)
        : SourceElement(element),
          attributes(std::move(attributes)),
          identifier(std::move(identifier)),
          value(std::move(value)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> value;
};

class BitsDeclaration final : public SourceElement {
public:
    BitsDeclaration(SourceElement const& element,
                    std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<TypeConstructor> maybe_type_ctor,
                    std::vector<std::unique_ptr<BitsMember>> members)
        : SourceElement(element),
          attributes(std::move(attributes)),
          identifier(std::move(identifier)),
          maybe_type_ctor(std::move(maybe_type_ctor)),
          members(std::move(members)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<TypeConstructor> maybe_type_ctor;
    std::vector<std::unique_ptr<BitsMember>> members;
};

class EnumMember final : public SourceElement {
public:
    EnumMember(SourceElement const& element,
               std::unique_ptr<AttributeList> attributes,
               std::unique_ptr<Identifier> identifier,
               std::unique_ptr<Constant> value)
        : SourceElement(element),
          attributes(std::move(attributes)),
          identifier(std::move(identifier)),
          value(std::move(value)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<Constant> value;
};

class EnumDeclaration final : public SourceElement {
public:
    EnumDeclaration(SourceElement const& element,
                    std::unique_ptr<AttributeList> attributes,
                    std::unique_ptr<Identifier> identifier,
                    std::unique_ptr<TypeConstructor> maybe_type_ctor,
                    std::vector<std::unique_ptr<EnumMember>> members)
        : SourceElement(element),
          attributes(std::move(attributes)),
          identifier(std::move(identifier)),
          maybe_type_ctor(std::move(maybe_type_ctor)),
          members(std::move(members)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::unique_ptr<TypeConstructor> maybe_type_ctor;
    std::vector<std::unique_ptr<EnumMember>> members;
};

class StructMember final : public SourceElement {
public:
    StructMember(SourceElement const& element,
                 std::unique_ptr<AttributeList> attributes,
                 std::unique_ptr<TypeConstructor> type_ctor,
                 std::unique_ptr<Identifier> identifier,
                 std::unique_ptr<Constant> maybe_default_value)
    : SourceElement(element),
      attributes(std::move(attributes)),
      type_ctor(std::move(type_ctor)),
      identifier(std::move(identifier)),
      maybe_default_value(std::move(maybe_default_value)) {}

      void Accept(TreeVisitor* visitor) const;

      std::unique_ptr<AttributeList> attributes;
      std::unique_ptr<TypeConstructor> type_ctor;
      std::unique_ptr<Identifier> identifier;
      std::unique_ptr<Constant> maybe_default_value;
};

class StructDeclaration final : public SourceElement {
public:
    StructDeclaration(SourceElement const& element,
                      std::unique_ptr<AttributeList> attributes,
                      std::unique_ptr<Identifier> identifier,
                      std::vector<std::unique_ptr<StructMember>> members)
    : SourceElement(element),
      attributes(std::move(attributes)),
      identifier(std::move(identifier)),
      members(std::move(members)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<StructMember>> members;
};

class UnionMember final : public SourceElement {
public:
    UnionMember(SourceElement const& element,
                std::unique_ptr<AttributeList> attributes,
                std::unique_ptr<TypeConstructor> type_ctor,
                std::unique_ptr<Identifier> identifier)
        : SourceElement(element),
          attributes(std::move(attributes)),
          type_ctor(std::move(type_ctor)),
          identifier(std::move(identifier)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<TypeConstructor> type_ctor;
    std::unique_ptr<Identifier> identifier;
};

class UnionDeclaration final : public SourceElement {
public:
    UnionDeclaration(SourceElement const& element,
                     std::unique_ptr<AttributeList> attributes,
                     std::unique_ptr<Identifier> identifier,
                     std::vector<std::unique_ptr<UnionMember>> members)
        : SourceElement(element),
          attributes(std::move(attributes)),
          identifier(std::move(identifier)),
          members(std::move(members)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<UnionMember>> members;
};

class XUnionMember final : public SourceElement {
public:
    XUnionMember(SourceElement const& element,
                 std::unique_ptr<AttributeList> attributes,
                 std::unique_ptr<TypeConstructor> type_ctor,
                 std::unique_ptr<Identifier> identifier)
        : SourceElement(element),
          attributes(std::move(attributes)),
          type_ctor(std::move(type_ctor)),
          identifier(std::move(identifier)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<TypeConstructor> type_ctor;
    std::unique_ptr<Identifier> identifier;
};

class XUnionDeclaration final : public SourceElement {
public:
    XUnionDeclaration(SourceElement const& element,
                      std::unique_ptr<AttributeList> attributes,
                      std::unique_ptr<Identifier> identifier,
                      std::vector<std::unique_ptr<XUnionMember>> members)
        : SourceElement(element),
          attributes(std::move(attributes)),
          identifier(std::move(identifier)),
          members(std::move(members)) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<Identifier> identifier;
    std::vector<std::unique_ptr<XUnionMember>> members;
};

class File final : public SourceElement {
public:
    File(SourceElement const& element,
         Token end,
         std::unique_ptr<AttributeList> attributes,
         std::unique_ptr<CompoundIdentifier> library_name,
         std::vector<std::unique_ptr<Using>> using_list,
         std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list,
         std::vector<std::unique_ptr<BitsDeclaration>> bits_declaration_list,
         std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list,
         std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list,
         std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list,
         std::vector<std::unique_ptr<XUnionDeclaration>> xunion_declaration_list)
        : SourceElement(element),
          attributes(std::move(attributes)),
          library_name(std::move(library_name)),
          using_list(std::move(using_list)),
          const_declaration_list(std::move(const_declaration_list)),
          bits_declaration_list(std::move(bits_declaration_list)),
          enum_declaration_list(std::move(enum_declaration_list)),
          struct_declaration_list(std::move(struct_declaration_list)),
          union_declaration_list(std::move(union_declaration_list)),
          xunion_declaration_list(std::move(xunion_declaration_list)),
          end_(end) {}

    void Accept(TreeVisitor* visitor) const;

    std::unique_ptr<AttributeList> attributes;
    std::unique_ptr<CompoundIdentifier> library_name;
    std::vector<std::unique_ptr<Using>> using_list;
    std::vector<std::unique_ptr<ConstDeclaration>> const_declaration_list;
    std::vector<std::unique_ptr<BitsDeclaration>> bits_declaration_list;
    std::vector<std::unique_ptr<EnumDeclaration>> enum_declaration_list;
    std::vector<std::unique_ptr<StructDeclaration>> struct_declaration_list;
    std::vector<std::unique_ptr<UnionDeclaration>> union_declaration_list;
    std::vector<std::unique_ptr<XUnionDeclaration>> xunion_declaration_list;
    Token end_;
};

} // namespace raw
} // namespace fidl

#endif // RAW_AST_H_