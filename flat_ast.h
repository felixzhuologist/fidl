#ifndef FLAT_AST_H_
#define FLAT_AST_H_

#include <assert.h>
#include <stdint.h>

#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include "error_reporter.h"
#include "raw_ast.h"
#include "typeshape.h"

namespace fidl {
namespace flat {

template <typename T>
struct PtrCompare {
    bool operator()(const T* left, const T* right) const { return *left < *right; }
};


class Typespace;
struct Decl;
class Library;

std::string LibraryName(const Library* library, StringView separator);

struct Name {
    Name() {}

    Name(const Library* library, const SourceLocation name)
        : library_(library),
          name_from_source_(std::make_unique<SourceLocation>(name)) {}

    Name(const Library* library, const std::string& name)
        : library_(library),
          anonymous_name_(std::make_unique<std::string>(name)) {}

    Name(Name&&) = default;
    Name& operator=(Name&&) = default;

    bool is_anonymous() const { return name_from_source_ == nullptr; }
    const Library* library() const { return library_; }
    const SourceLocation& source_location() const {
        return *name_from_source_.get();
    }
    const StringView name_part() const {
        if (is_anonymous())
            return *anonymous_name_.get();
        return name_from_source_->data();
    }

    bool operator==(const Name& other) const {
        // can't use the library name yet, not necesserily compiled!
        auto library_ptr = reinterpret_cast<uintptr_t>(library_);
        auto other_library_ptr = reinterpret_cast<uintptr_t>(other.library_);
        if (library_ptr != other_library_ptr)
            return false;
        return name_part() == other.name_part();
    }
    bool operator!=(const Name& other) const { return !operator==(other); }

    bool operator<(const Name& other) const {
        // can't use the library name yet, not necesserily compiled!
        auto library_ptr = reinterpret_cast<uintptr_t>(library_);
        auto other_library_ptr = reinterpret_cast<uintptr_t>(other.library_);
        if (library_ptr != other_library_ptr)
            return library_ptr < other_library_ptr;
        return name_part() < other.name_part();
    }

private:
    const Library* library_ = nullptr;
    std::unique_ptr<SourceLocation> name_from_source_;
    std::unique_ptr<std::string> anonymous_name_;
};

struct ConstantValue {
    virtual ~ConstantValue() {}

    enum class Kind {
        kInt8,
        kInt16,
        kInt32,
        kInt64,
        kUint8,
        kUint16,
        kUint32,
        kUint64,
        kFloat32,
        kFloat64,
        kBool,
        kString,
    };

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const = 0;

    const Kind kind;

protected:
    explicit ConstantValue(Kind kind)
        : kind(kind) {}
};

struct BoolConstantValue : ConstantValue {
    BoolConstantValue(bool value)
        : ConstantValue(ConstantValue::Kind::kBool), value(value) {}

    operator bool() const { return value; }

    friend bool operator==(const BoolConstantValue& l, const BoolConstantValue& r) {
        return l.value == r.value;
    }

    friend bool operator!=(const BoolConstantValue& l, const BoolConstantValue& r) {
        return l.value != r.value;
    }

    friend std::ostream& operator<<(std::ostream& os, const BoolConstantValue& v) {
        os << v.value;
        return os;
    }

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
        assert(out_value != nullptr);
        switch (kind) {
        case Kind::kBool:
            *out_value = std::make_unique<BoolConstantValue>(value);
            return true;
        default:
            return false;
        }
    }

    bool value;
};

struct StringConstantValue : ConstantValue {
    explicit StringConstantValue(StringView value)
        : ConstantValue(ConstantValue::Kind::kString), value(value) {}

    friend std::ostream& operator<<(std::ostream& os, const StringConstantValue& v) {
        os << v.value.data();
        return os;
    }

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
        assert(out_value != nullptr);
        switch (kind) {
        case Kind::kString:
            *out_value = std::make_unique<StringConstantValue>(StringView(value));
            return true;
        default:
            return false;
        }
    }

    StringView value;
};


template <typename ValueType>
struct NumericConstantValue : ConstantValue {
    static_assert(std::is_arithmetic<ValueType>::value && !std::is_same<ValueType, bool>::value,
                  "NumericConstantValue can only be used with a numeric ValueType!");

    NumericConstantValue(ValueType value)
        : ConstantValue(GetKind()), value(value) {}

    operator ValueType() const { return value; }

    friend bool operator==(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value == r.value;
    }

    friend bool operator<(const NumericConstantValue<ValueType>& l,
                          const NumericConstantValue<ValueType>& r) {
        return l.value < r.value;
    }

    friend bool operator>(const NumericConstantValue<ValueType>& l,
                          const NumericConstantValue<ValueType>& r) {
        return l.value > r.value;
    }

    friend bool operator!=(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value != r.value;
    }

    friend bool operator<=(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value <= r.value;
    }

    friend bool operator>=(const NumericConstantValue<ValueType>& l,
                           const NumericConstantValue<ValueType>& r) {
        return l.value >= r.value;
    }

    friend std::ostream& operator<<(std::ostream& os, const NumericConstantValue<ValueType>& v) {
        if constexpr (GetKind() == Kind::kInt8)
            os << static_cast<int>(v.value);
        else if constexpr (GetKind() == Kind::kUint8)
            os << static_cast<unsigned>(v.value);
        else
            os << v.value;
        return os;
    }

    virtual bool Convert(Kind kind, std::unique_ptr<ConstantValue>* out_value) const override {
        assert(out_value != nullptr);
        switch (kind) {
        case Kind::kInt8: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int8_t>::lowest() ||
                value > std::numeric_limits<int8_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int8_t>>(
                static_cast<int8_t>(value));
            return true;
        }
        case Kind::kInt16: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int16_t>::lowest() ||
                value > std::numeric_limits<int16_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int16_t>>(
                static_cast<int16_t>(value));
            return true;
        }
        case Kind::kInt32: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int32_t>::lowest() ||
                value > std::numeric_limits<int32_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int32_t>>(
                static_cast<int32_t>(value));
            return true;
        }
        case Kind::kInt64: {
            if (std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<int64_t>::lowest() ||
                value > std::numeric_limits<int64_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<int64_t>>(
                static_cast<int64_t>(value));
            return true;
        }
        case Kind::kUint8: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint8_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint8_t>>(
                static_cast<uint8_t>(value));
            return true;
        }
        case Kind::kUint16: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint16_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint16_t>>(
                static_cast<uint16_t>(value));
            return true;
        }
        case Kind::kUint32: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint32_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint32_t>>(
                static_cast<uint32_t>(value));
            return true;
        }
        case Kind::kUint64: {
            if (std::is_floating_point<ValueType>::value ||
                value < 0 || value > std::numeric_limits<uint64_t>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<uint64_t>>(
                static_cast<uint64_t>(value));
            return true;
        }
        case Kind::kFloat32: {
            if (!std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<float>::lowest() ||
                value > std::numeric_limits<float>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<float>>(static_cast<float>(value));
            return true;
        }
        case Kind::kFloat64: {
            if (!std::is_floating_point<ValueType>::value ||
                value < std::numeric_limits<double>::lowest() ||
                value > std::numeric_limits<double>::max()) {
                return false;
            }
            *out_value = std::make_unique<NumericConstantValue<double>>(static_cast<double>(value));
            return true;
        }
        case Kind::kString:
        case Kind::kBool:
            return false;
        }
    }

    static NumericConstantValue<ValueType> Min() {
        return NumericConstantValue<ValueType>(std::numeric_limits<ValueType>::lowest());
    }

    static NumericConstantValue<ValueType> Max() {
        return NumericConstantValue<ValueType>(std::numeric_limits<ValueType>::max());
    }

    ValueType value;

private:
    constexpr static Kind GetKind() {
        if constexpr (std::is_same_v<ValueType, uint64_t>)
            return Kind::kUint64;
        if constexpr (std::is_same_v<ValueType, int64_t>)
            return Kind::kInt64;
        if constexpr (std::is_same_v<ValueType, uint32_t>)
            return Kind::kUint32;
        if constexpr (std::is_same_v<ValueType, int32_t>)
            return Kind::kInt32;
        if constexpr (std::is_same_v<ValueType, uint16_t>)
            return Kind::kUint16;
        if constexpr (std::is_same_v<ValueType, int16_t>)
            return Kind::kInt16;
        if constexpr (std::is_same_v<ValueType, uint8_t>)
            return Kind::kUint8;
        if constexpr (std::is_same_v<ValueType, int8_t>)
            return Kind::kInt8;
        if constexpr (std::is_same_v<ValueType, double>)
            return Kind::kFloat64;
        if constexpr (std::is_same_v<ValueType, float>)
            return Kind::kFloat32;
    }
};

using Size = NumericConstantValue<uint32_t>;

struct Constant {
    virtual ~Constant() {}

    enum struct Kind {
        kIdentifier,
        kLiteral,
        kSynthesized,
    };

    explicit Constant(Kind kind)
        : kind(kind), value_(nullptr) {}


    bool IsResolved() const { return value_ != nullptr; }

    void ResolveTo(std::unique_ptr<ConstantValue> value) {
        assert(value != nullptr);
        assert(!IsResolved() && "Constants should only be resolved once!");
        value_ = std::move(value);
    }

    const ConstantValue& Value() const {
        assert(IsResolved() && "Accessing the value of an unresolved Constant!");
        return *value_;
    }

    const Kind kind;

protected:
    std::unique_ptr<ConstantValue> value_;
};

struct IdentifierConstant : Constant {
    explicit IdentifierConstant(Name name)
        : Constant(Kind::kIdentifier), name(std::move(name)) {}

    Name name;
};

struct LiteralConstant : Constant {
    explicit LiteralConstant(std::unique_ptr<raw::Literal> literal)
        : Constant(Kind::kLiteral), literal(std::move(literal)) {}

    std::unique_ptr<raw::Literal> literal;
};

struct SynthesizedConstant : Constant {
    explicit SynthesizedConstant(std::unique_ptr<ConstantValue> value)
        : Constant(Kind::kSynthesized) {
        ResolveTo(std::move(value));
    }
};

struct Decl {
    virtual ~Decl() {}

    enum class Kind {
        kConst,
        kStruct,
    };

    Decl(Kind kind, Name name)
        : kind(kind), name(std::move(name)) {}

    const Kind kind;

    const Name name;

    std::string GetName() const;

    bool compiling = false;
    bool compiled = false;
};

struct TypeDecl : public Decl {
    TypeDecl(Kind kind, Name name)
        : Decl(kind, std::move(name)) {}
    TypeShape typeshape;
    bool recursive = false;
};

struct Type {
    virtual ~Type() {}

    enum class Kind {
        kArray,
        kVector,
        kString,
        kPrimitive,
        kIdentifier,
    };

    explicit Type(Kind kind, types::Nullability nullability, TypeShape shape)
        : kind(kind), nullability(nullability), shape(shape) {}

    const Kind kind;
    const types::Nullability nullability;
    TypeShape shape;

    // Comparison helper object.
    class Comparison {
    public:
        Comparison() = default;
        template <class T>
        Comparison Compare(const T& a, const T& b) const {
            if (result_ != 0)
                return Comparison(result_);
            if (a < b)
                return Comparison(-1);
            if (b < a)
                return Comparison(1);
            return Comparison(0);
        }

        bool IsLessThan() const {
            return result_ < 0;
        }

    private:
        Comparison(int result)
            : result_(result) {}
        const int result_ = 0;
    };

    bool operator<(const Type& other) const {
        if (kind != other.kind)
            return kind < other.kind;
        return Compare(other).IsLessThan();
    }

    // Compare this object against 'other'.
    // It's guaranteed that this->kind == other.kind.
    // Return <0 if *this < other, ==0 if *this == other, and >0 if *this > other.
    // Derived types should override this, but also call this implementation.
    virtual Comparison Compare(const Type& other) const {
        assert(kind == other.kind);
        return Comparison()
            .Compare(nullability, other.nullability);
    }
};

struct ArrayType : public Type {
    ArrayType(const Type* element_type, const Size* element_count)
        : Type(
              Kind::kArray,
              types::Nullability::kNonnullable,
              Shape(element_type->shape, element_count->value)),
          element_type(element_type), element_count(element_count) {}

    const Type* element_type;
    const Size* element_count;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const ArrayType&>(other);
        return Type::Compare(o)
            .Compare(element_count->value, o.element_count->value)
            .Compare(*element_type, *o.element_type);
    }

    static TypeShape Shape(TypeShape element, uint32_t count);
};


struct VectorType : public Type {
    VectorType(const Type* element_type, const Size* element_count, types::Nullability nullability)
        : Type(Kind::kVector, nullability, Shape(element_type->shape, element_count->value)),
          element_type(element_type), element_count(element_count) {}

    const Type* element_type;
    const Size* element_count;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const VectorType&>(other);
        return Type::Compare(o)
            .Compare(element_count->value, o.element_count->value)
            .Compare(*element_type, *o.element_type);
    }

    static TypeShape Shape(TypeShape element, uint32_t max_element_count);
};

struct StringType : public Type {
    StringType(const Size* max_size, types::Nullability nullability)
        : Type(Kind::kString, nullability, Shape(max_size->value)), max_size(max_size) {}

    const Size* max_size;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const StringType&>(other);
        return Type::Compare(o)
            .Compare(max_size->value, o.max_size->value);
    }

    static TypeShape Shape(uint32_t max_length);
};

struct PrimitiveType : public Type {

    explicit PrimitiveType(types::PrimitiveSubtype subtype)
        : Type(
            Kind::kPrimitive,
            types::Nullability::kNonnullable,
            Shape(subtype)),
        subtype(subtype) {}

    types::PrimitiveSubtype subtype;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const PrimitiveType&>(other);
        return Type::Compare(o)
            .Compare(subtype, o.subtype);
    }

    static TypeShape Shape(types::PrimitiveSubtype subtype);
    static uint32_t SubtypeSize(types::PrimitiveSubtype subtype);
};

struct IdentifierType : public Type {
    IdentifierType(Name name, types::Nullability nullability, const TypeDecl* type_decl, TypeShape shape)
        : Type(Kind::kIdentifier, nullability, shape),
          name(std::move(name)), type_decl(type_decl) {}

    Name name;
    const TypeDecl* type_decl;

    Comparison Compare(const Type& other) const override {
        const auto& o = static_cast<const IdentifierType&>(other);
        return Type::Compare(o)
            .Compare(name, o.name);
    }
};

struct TypeConstructor {
    TypeConstructor(Name name, std::unique_ptr<TypeConstructor> maybe_arg_type_ctor,
               std::unique_ptr<Constant> maybe_size, types::Nullability nullability)
        : name(std::move(name)), maybe_arg_type_ctor(std::move(maybe_arg_type_ctor)),
          maybe_size(std::move(maybe_size)), nullability(nullability) {}

    // Set during construction.
    const Name name;
    const std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
    const std::unique_ptr<Constant> maybe_size;
    const types::Nullability nullability;

    // Set during compilation.
    bool compiling = false;
    bool compiled = false;
    const Type* type = nullptr;
};

struct Const : public Decl {
    Const(Name name,
          std::unique_ptr<TypeConstructor> type_ctor,
          std::unique_ptr<Constant> value)
        : Decl(Kind::kConst, std::move(name)), type_ctor(std::move(type_ctor)),
          value(std::move(value)) {}
    std::unique_ptr<TypeConstructor> type_ctor;
    std::unique_ptr<Constant> value;
};

struct Struct : public TypeDecl {
    struct Member {
        Member(std::unique_ptr<TypeConstructor> type_ctor, SourceLocation name,
               std::unique_ptr<Constant> maybe_default_value)
            : type_ctor(std::move(type_ctor)), name(std::move(name)),
              maybe_default_value(std::move(maybe_default_value)) {}
        std::unique_ptr<TypeConstructor> type_ctor;
        SourceLocation name;
        std::unique_ptr<Constant> maybe_default_value;
        FieldShape fieldshape;
    };

    Struct(Name name,
           std::vector<Member> members, bool anonymous = false)
        : TypeDecl(Kind::kStruct, std::move(name)),
          members(std::move(members)), anonymous(anonymous) {
    }

    std::vector<Member> members;
    const bool anonymous;

    static TypeShape Shape(std::vector<FieldShape*>* fields, uint32_t extra_handles = 0u);
};


class TypeTemplate {
public:
    TypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter)
        : typespace_(typespace), name_(std::move(name)), error_reporter_(error_reporter) {}

    TypeTemplate(TypeTemplate&& type_template) = default;

    virtual ~TypeTemplate() = default;

    const Name* name() const { return &name_; }

    virtual bool Create(const SourceLocation& location,
                        const Type* arg_type,
                        const Size* size,
                        types::Nullability nullability,
                        std::unique_ptr<Type>* out_type) const = 0;

protected:
    bool MustBeParameterized(const SourceLocation& location) const { return Fail(location, "must be parametrized"); }
    bool MustHaveSize(const SourceLocation& location) const { return Fail(location, "must have size"); }
    bool CannotBeParameterized(const SourceLocation& location) const { return Fail(location, "cannot be parametrized"); }
    bool CannotHaveSize(const SourceLocation& location) const { return Fail(location, "cannot have size"); }
    bool CannotBeNullable(const SourceLocation& location) const { return Fail(location, "cannot be nullable"); }
    bool Fail(const SourceLocation& location, const std::string& content) const;

    Typespace* typespace_;

private:
    Name name_;
    ErrorReporter* error_reporter_;
};

// Typespace provides builders for all types (e.g. array, vector, string), and
// ensures canonicalization, i.e. the same type is represented by one object,
// shared amongst all uses of said type. For instance, while the text
// `vector<uint8>:7` may appear multiple times in source, these all indicate
// the same type.
class Typespace {
public:
    explicit Typespace(ErrorReporter* error_reporter)
        : error_reporter_(error_reporter) {}

    bool Create(const flat::Name& name,
                const Type* arg_type,
                const Size* size,
                types::Nullability nullability,
                const Type** out_type);

    void AddTemplate(std::unique_ptr<TypeTemplate> type_template);

    // BoostrapRootTypes creates a instance with all primitive types. It is
    // meant to be used as the top-level types lookup mechanism, providing
    // definitional meaning to names such as `int64`, or `bool`.
    static Typespace RootTypes(ErrorReporter* error_reporter);

private:
    friend class TypeAliasTypeTemplate;

    bool CreateNotOwned(const flat::Name& name,
                        const Type* arg_type,
                        const Size* size,
                        types::Nullability nullability,
                        std::unique_ptr<Type>* out_type);
    const TypeTemplate* LookupTemplate(const flat::Name& name) const;

    struct cmpName {
        bool operator()(const flat::Name* a, const flat::Name* b) const {
            return *a < *b;
        }
    };

    std::map<const flat::Name*, std::unique_ptr<TypeTemplate>, cmpName> templates_;
    std::vector<std::unique_ptr<Type>> types_;

    ErrorReporter* error_reporter_;
};

class Libraries {
public:
    Libraries();

    bool Insert(std::unique_ptr<Library> library);

private:
    std::map<std::vector<StringView>, std::unique_ptr<Library>> all_libraries_;
};

class Dependencies {
public:
    bool Register(StringView filename, Library* dep_library,
                  const std::unique_ptr<raw::Identifier>& maybe_alias);

    bool Lookup(StringView filename, const std::vector<StringView>& name,
                Library** out_library);

    const std::set<Library*>& dependencies() const { return dependencies_aggregate_; }

private:
    bool InsertByName(StringView filename, const std::vector<StringView>& name,
                      Library* library);

    using ByName = std::map<std::vector<StringView>, Library*>;
    using ByFilename = std::map<std::string, std::unique_ptr<ByName>>;

    ByFilename dependencies_;
    std::set<Library*> dependencies_aggregate_;
};

class Library {
public:
    Library(const Libraries* all_libraries, ErrorReporter* error_reporter, Typespace* typespace)
        : all_libraries_(all_libraries), error_reporter_(error_reporter), typespace_(typespace) {}

    bool ConsumeFile(std::unique_ptr<raw::File> File);

    const std::vector<StringView>& name() const { return library_name_; }

    std::vector<StringView> library_name_;

    std::vector<std::unique_ptr<Const>> const_declarations_;

private:
    bool Fail(StringView message);
    bool Fail(const SourceLocation& location, StringView message);
    bool Fail(const Name& name, StringView message) {
        if (name.is_anonymous()) {
            return Fail(message);
        }
        return Fail(name.source_location(), message);
    }
    bool Fail(const Decl& decl, StringView message) { return Fail(decl.name, message); }

    bool CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier,
                                   SourceLocation location, Name* out_name);
    void RegisterConst(Const* decl);
    bool RegisterDecl(Decl* decl);

    bool ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant, SourceLocation location,
                         std::unique_ptr<Constant>* out_constant);
    bool ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                SourceLocation location,
                                std::unique_ptr<TypeConstructor>* out_type);

    bool ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration);

    const Libraries* all_libraries_;

    Dependencies dependencies_;

    std::map<const Name*, Decl*, PtrCompare<Name>> declarations_;
    std::map<const Name*, Const*, PtrCompare<Name>> constants_;

    ErrorReporter* error_reporter_;
    Typespace* typespace_;
};

} // namespace flat
} // namespace fidl

#endif // FLAT_AST_H_
