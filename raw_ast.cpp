#include "raw_ast.h"
#include "tree_visitor.h"

namespace fidl {
namespace raw {

SourceElementMark::SourceElementMark(TreeVisitor& tv, const SourceElement& element)
    : tv_(tv), element_(element) {
    tv_.OnSourceElementStart(element_);
}

SourceElementMark::~SourceElementMark() {
    tv_.OnSourceElementEnd(element_);
}

void CompoundIdentifier::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    for (auto& i : components) {
        visitor.OnIdentifier(i);
    }
}

void StringLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void NumericLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void TrueLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void FalseLiteral::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
}

void IdentifierConstant::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnCompoundIdentifier(identifier);
}

void LiteralConstant::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnLiteral(literal);
}

void TypeConstructor::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnCompoundIdentifier(identifier);
    if (maybe_arg_type_ctor != nullptr)
        visitor.OnTypeConstructor(maybe_arg_type_ctor);
    if (maybe_handle_subtype != nullptr)
        visitor.OnHandleSubtype(*maybe_handle_subtype);
    if (maybe_size != nullptr)
        visitor.OnConstant(maybe_size);
    visitor.OnNullability(nullability);
}

void ConstDeclaration::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);
    visitor.OnTypeConstructor(type_ctor);
    visitor.OnIdentifier(identifier);
    visitor.OnConstant(constant);
}

void File::Accept(TreeVisitor& visitor) {
    SourceElementMark sem(visitor, *this);

    visitor.OnCompoundIdentifier(library_name);
    for (auto& i : const_declaration_list) {
        visitor.OnConstDeclaration(i);
    }
}

} // namespace raw
} // namespace fidl
