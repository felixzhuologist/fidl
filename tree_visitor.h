#include "raw_ast.h"

#ifndef TREE_VISITOR_H_
#define TREE_VISITOR_H_

namespace fidl {
namespace raw {

class TreeVisitor {
public:
    virtual void OnSourceElementStart(const SourceElement& element) {}
    virtual void OnSourceElementEnd(const SourceElement& element) {}

    virtual void OnIdentifier(std::unique_ptr<Identifier> const& element) {
        element->Accept(this);
    }
    virtual void OnCompoundIdentifier(std::unique_ptr<CompoundIdentifier> const& element) {
        element->Accept(this);
    }

    virtual void OnLiteral(std::unique_ptr<fidl::raw::Literal> const& element) {
        fidl::raw::Literal::Kind kind = element->kind;
        switch (kind) {
        case Literal::Kind::kString: {
            StringLiteral* literal = static_cast<StringLiteral*>(element.get());
            OnStringLiteral(*literal);
        }
        case Literal::Kind::kNumeric: {
            NumericLiteral* literal = static_cast<NumericLiteral*>(element.get());
            OnNumericLiteral(*literal);
            break;
        }
        case Literal::Kind::kTrue: {
            TrueLiteral* literal = static_cast<TrueLiteral*>(element.get());
            OnTrueLiteral(*literal);
            break;
        }
        case Literal::Kind::kFalse: {
            FalseLiteral* literal = static_cast<FalseLiteral*>(element.get());
            OnFalseLiteral(*literal);
            break;
        }
        default:
            // Die!
            break;
        }
    }

    virtual void OnStringLiteral(StringLiteral& element) {
        element.Accept(this);
    }

    virtual void OnNumericLiteral(NumericLiteral& element) {
        element.Accept(this);
    }

    virtual void OnTrueLiteral(TrueLiteral& element) {
        element.Accept(this);
    }

    virtual void OnFalseLiteral(FalseLiteral& element) {
        element.Accept(this);
    }

    virtual void OnConstant(std::unique_ptr<Constant> const& element) {
        Constant::Kind kind = element->kind;
        std::unique_ptr<Constant>& unconst_element = const_cast<std::unique_ptr<Constant>&>(element);
        switch(kind) {
        case Constant::Kind::kIdentifier: {
            IdentifierConstant* ptr = static_cast<IdentifierConstant*>(unconst_element.get());
            std::unique_ptr<IdentifierConstant> uptr(ptr);
            OnIdentifierConstant(uptr);
            uptr.release();
            break;
        }
        case Constant::Kind::kLiteral: {
            LiteralConstant* ptr = static_cast<LiteralConstant*>(unconst_element.get());
            std::unique_ptr<LiteralConstant> uptr(ptr);
            OnLiteralConstant(uptr);
            uptr.release();
            break;
        }
        }
    }

    virtual void OnIdentifierConstant(std::unique_ptr<IdentifierConstant> const& element) {
        element->Accept(this);
    }

    virtual void OnLiteralConstant(std::unique_ptr<LiteralConstant> const& element) {
        element->Accept(this);
    }

    virtual void OnAttribute(std::unique_ptr<Attribute> const& element) {
        element->Accept(this);
    }

    virtual void OnAttributeList(std::unique_ptr<AttributeList> const& element) {
        element->Accept(this);
    }

    virtual void OnTypeConstructor(std::unique_ptr<TypeConstructor> const& element) {
        element->Accept(this);
    }

    virtual void OnUsing(std::unique_ptr<Using> const& element) {
        Using::Kind kind = element->kind;
        switch(kind) {
        case Using::Kind::kLibrary: {
            UsingLibrary* ptr = static_cast<UsingLibrary*>(element.get());
            OnUsingLibrary(*ptr);
            break;
        }
        case Using::Kind::kAlias: {
            UsingAlias* ptr = static_cast<UsingAlias*>(element.get());
            OnUsingAlias(*ptr);
            break;
        }
        }
    }
    virtual void OnUsingLibrary(UsingLibrary& element) {
        element.Accept(this);
    }

    virtual void OnUsingAlias(UsingAlias& element) {
        element.Accept(this);
    }

    virtual void OnConstDeclaration(std::unique_ptr<ConstDeclaration> const& element) {
        element->Accept(this);
    }

    virtual void OnBitsMember(std::unique_ptr<BitsMember> const& element) {
        element->Accept(this);
    }
    
    virtual void OnBitsDeclaration(std::unique_ptr<BitsDeclaration> const& element) {
        element->Accept(this);
    }

    virtual void OnEnumMember(std::unique_ptr<EnumMember> const& element) {
        element->Accept(this);
    }

    virtual void OnEnumDeclaration(std::unique_ptr<EnumDeclaration> const& element) {
        element->Accept(this);
    }

    virtual void OnStructMember(std::unique_ptr<StructMember> const& element) {
        element->Accept(this);
    }

    virtual void OnStructDeclaration(std::unique_ptr<StructDeclaration> const& element) {
        element->Accept(this);
    }

    virtual void OnUnionMember(std::unique_ptr<UnionMember> const& element) {
        element->Accept(this);
    }

    virtual void OnUnionDeclaration(std::unique_ptr<UnionDeclaration> const& element) {
        element->Accept(this);
    }

    virtual void OnXUnionMember(std::unique_ptr<XUnionMember> const& element) {
        element->Accept(this);
    }

    virtual void OnXUnionDeclaration(std::unique_ptr<XUnionDeclaration> const& element) {
        element->Accept(this);
    }

    virtual void OnFile(std::unique_ptr<File> const& element) {
        element->Accept(this);
    }

    virtual void OnNullability(types::Nullability nullability) {}
};

} // namespace raw
} // namespace fidl

#endif // TREE_VISITOR_H_
