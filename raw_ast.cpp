#include "raw_ast.h"
#include "tree_visitor.h"

namespace fidl {
namespace raw {

SourceElementMark::SourceElementMark(TreeVisitor* tv, const SourceElement& element)
    : tv_(tv), element_(element) {
    tv_->OnSourceElementStart(element_);
}

SourceElementMark::~SourceElementMark() {
    tv_->OnSourceElementEnd(element_);
}

void Identifier::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void CompoundIdentifier::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    for (auto& i : components) {
        visitor->OnIdentifier(i);
    }
}

void StringLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void NumericLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void TrueLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void FalseLiteral::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void IdentifierConstant::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(identifier);
}

void LiteralConstant::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnLiteral(literal);
}

void Attribute::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
}

void AttributeList::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    for (auto& i : attributes) {
        visitor->OnAttribute(i);
    }
}

void TypeConstructor::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(identifier);
    if (maybe_arg_type_ctor != nullptr)
        visitor->OnTypeConstructor(maybe_arg_type_ctor);
    if (maybe_size != nullptr)
        visitor->OnConstant(maybe_size);
    visitor->OnNullability(nullability);
}

void UsingLibrary::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(using_path);
    if (maybe_alias != nullptr)
        visitor->OnIdentifier(maybe_alias);
}

void UsingAlias::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnIdentifier(alias);
    visitor->OnTypeConstructor(type_ctor);
}

void ConstDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
    visitor->OnConstant(constant);
}

void BitsMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    visitor->OnConstant(value);
}

void BitsDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    if (maybe_type_ctor != nullptr) {
        visitor->OnTypeConstructor(maybe_type_ctor);
    }
    for (auto member = members.begin(); member != members.end(); ++member) {
        visitor->OnBitsMember(*member);
    }
}

void EnumMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    visitor->OnConstant(value);
}

void EnumDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    if (maybe_type_ctor != nullptr) {
        visitor->OnTypeConstructor(maybe_type_ctor);
    }
    for (auto& member : members) {
        visitor->OnEnumMember(member);
    }
}

void Parameter::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
}

void ParameterList::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    for (auto const& parameter : parameter_list) {
        visitor->OnParameter(parameter);
    }
}

void InterfaceMethod::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    if (ordinal != nullptr) {
        visitor->OnOrdinal(*ordinal);
    }
    if (maybe_request != nullptr) {
        visitor->OnParameterList(maybe_request);
    }
    if (maybe_response != nullptr) {
        visitor->OnParameterList(maybe_response);
    }
    if (maybe_error_ctor != nullptr) {
        visitor->OnTypeConstructor(maybe_error_ctor);
    }
}

void ComposeProtocol::Accept(TreeVisitor *visitor) const {
    SourceElementMark sem(visitor, *this);
    visitor->OnCompoundIdentifier(protocol_name);
}

void InterfaceDeclaration::Accept(TreeVisitor *visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto const& superinterface : superinterfaces) {
        visitor->OnComposeProtocol(superinterface);
    }
    for (auto const& method : methods) {
        visitor->OnInterfaceMethod(method);
    }
}

void StructMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
    if (maybe_default_value != nullptr) {
        visitor->OnConstant(maybe_default_value);
    }
}

void StructDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto member = members.begin(); member != members.end(); ++member) {
        visitor->OnStructMember(*member);
    }
}

void TableMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (maybe_used != nullptr) {
        if (maybe_used->attributes != nullptr) {
            visitor->OnAttributeList(maybe_used->attributes);
        }
    }
    visitor->OnOrdinal(*ordinal);
    if (maybe_used != nullptr) {
        visitor->OnTypeConstructor(maybe_used->type_ctor);
        visitor->OnIdentifier(maybe_used->identifier);
        if (maybe_used->maybe_default_value) {
            visitor->OnConstant(maybe_used->maybe_default_value);
        }
    }
}

void TableDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor->OnTableMember(*member);
    }
}

void UnionMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
}

void UnionDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto member = members.begin();
         member != members.end();
         ++member) {
        visitor->OnUnionMember(*member);
    }
}

void XUnionMember::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }

    visitor->OnTypeConstructor(type_ctor);
    visitor->OnIdentifier(identifier);
}

void XUnionDeclaration::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }
    visitor->OnIdentifier(identifier);
    for (auto& member : members) {
        visitor->OnXUnionMember(member);
    }
}

void File::Accept(TreeVisitor* visitor) const {
    SourceElementMark sem(visitor, *this);
    if (attributes != nullptr) {
        visitor->OnAttributeList(attributes);
    }

    visitor->OnCompoundIdentifier(library_name);
    for (auto& i : using_list) {
        visitor->OnUsing(i);
    }

    for (auto& i : const_declaration_list) {
        visitor->OnConstDeclaration(i);
    }
    for (auto& i : bits_declaration_list) {
        visitor->OnBitsDeclaration(i);
    }
    for (auto& i : enum_declaration_list) {
        visitor->OnEnumDeclaration(i);
    }
    for (auto& i : interface_declaration_list) {
        visitor->OnInterfaceDeclaration(i);
    }
    for (auto& i : struct_declaration_list) {
        visitor->OnStructDeclaration(i);
    }
    for (auto& i : table_declaration_list) {
        visitor->OnTableDeclaration(i);
    }
    for (auto& i : union_declaration_list) {
        visitor->OnUnionDeclaration(i);
    }
    for (auto& i : xunion_declaration_list) {
        visitor->OnXUnionDeclaration(i);
    }
}

} // namespace raw
} // namespace fidl
