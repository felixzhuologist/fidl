#include <iostream>
#include <regex>
#include <sstream>

#include "attributes.h"
#include "names.h"
#include "flat_ast.h"
#include "utils.h"

namespace fidl {
namespace flat {

std::string LibraryName(const Library* library, StringView separator) {
    if (library != nullptr) {
        return StringJoin(library->name(), separator);
    }
    return std::string();
}

uint32_t AlignTo(uint64_t size, uint64_t alignment) {
    return static_cast<uint32_t>(
        std::min((size + alignment - 1) & -alignment,
                 static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t ClampedMultiply(uint32_t a, uint32_t b) {
    return static_cast<uint32_t>(
        std::min(static_cast<uint64_t>(a) * static_cast<uint64_t>(b),
                 static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t ClampedAdd(uint32_t a, uint32_t b) {
    return static_cast<uint32_t>(
        std::min(static_cast<uint64_t>(a) + static_cast<uint64_t>(b),
                 static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

TypeShape Struct::Shape(std::vector<FieldShape*>* fields, uint32_t extra_handles) {
    uint32_t size = 0u;
    uint32_t alignment = 0u;
    uint32_t depth = 0u;
    uint32_t max_handles = 0u;
    uint32_t max_out_of_line = 0u;
    bool has_padding = false;

    for (FieldShape* field : *fields) {
        TypeShape typeshape = field->Typeshape();
        alignment = std::max(alignment, typeshape.Alignment());
        size = AlignTo(size, typeshape.Alignment());
        field->SetOffset(size);
        size += typeshape.Size();
        depth = std::max(depth, field->Depth());
        max_handles = ClampedAdd(max_handles, typeshape.MaxHandles());
        max_out_of_line = ClampedAdd(max_out_of_line, typeshape.MaxOutOfLine());
        has_padding |= typeshape.HasPadding();
    }

    max_handles = ClampedAdd(max_handles, extra_handles);

    size = AlignTo(size, alignment);

    if (fields->empty()) {
        assert(size == 0);
        assert(alignment == 1);

        // empty structs are defined to have a size of 1 byte
        size = 1;
    }

    for (size_t i = 0; i + 1 < fields->size(); ++i) {
        auto& current = fields->at(i);
        auto& next = fields->at(i + 1);
        current->SetPadding(next->Offset() - current->Offset() - current->Size());
        has_padding |= current->Padding() > 0;
    }
    if (!fields->empty()) {
        auto& last = fields->back();
        last->SetPadding(size - last->Offset() - last->Size());
        has_padding |= last->Padding() > 0;
    }

    return TypeShape(size, alignment, depth, max_handles, max_out_of_line, has_padding);
}

TypeShape Union::Shape(std::vector<FieldShape*>* fields) {
    uint32_t size = 0u;
    uint32_t alignment = 0u;
    uint32_t depth = 0u;
    uint32_t max_handles = 0u;
    uint32_t max_out_of_line = 0u;
    bool has_padding = false;

    for (const auto& field : *fields) {
        auto& fieldshape = *field;
        size = std::max(size, fieldshape.Size());
        alignment = std::max(alignment, fieldshape.Alignment());
        depth = std::max(depth, fieldshape.Depth());
        max_handles = std::max(max_handles, fieldshape.Typeshape().MaxHandles());
        max_out_of_line = std::max(max_out_of_line, fieldshape.Typeshape().MaxOutOfLine());
        has_padding |= fieldshape.Typeshape().HasPadding();
    }

    size = AlignTo(size, alignment);

    // calculate offset of the union tag
    auto member_typeshape = TypeShape(size, alignment, depth, max_handles, max_out_of_line);
    auto member_fieldshape = FieldShape(member_typeshape);
    auto tag = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint32));
    std::vector<FieldShape*> fidl_union = {&tag, &member_fieldshape};
    // update offset in membershape
    auto typeshape = Struct::Shape(&fidl_union, 0);

    auto offset = member_fieldshape.Offset();
    assert(offset == 4 || offset == 8);
    for (auto& field : *fields) {
        field->SetOffset(offset);
    }

    // the tag is 4 bytes, so padding is required between the tag and the first
    // union member if the union member has an alignment greater than 4 (ie 8)
    if (offset == 8) {
        has_padding = true;
    }

    for (auto& field : *fields) {
        // padding is from end of member to end of the entire union
        field->SetPadding(typeshape.Size() - offset - field->Size());
        has_padding |= field->Padding() > 0;
    }

    return TypeShape(size, alignment, depth, max_handles, max_out_of_line, has_padding);
}

TypeShape PointerTypeShape(const TypeShape& element, uint32_t max_element_count = 1u) {
    // Because FIDL supports recursive data structures, we might not have
    // computed the TypeShape for the element we're pointing to. In that case,
    // the size will be zero and we'll use |numeric_limits<uint32_t>::max()| as
    // the depth. We'll never see a zero size for a real TypeShape because empty
    // structs are banned.
    //
    // We're careful to check for saturation before incrementing the depth
    // because recursive data structures have a depth pegged at the numeric
    // limit.
    uint32_t depth = std::numeric_limits<uint32_t>::max();
    if (element.Size() > 0 && element.Depth() < std::numeric_limits<uint32_t>::max())
        depth = ClampedAdd(element.Depth(), 1);

    // The element(s) will be stored out-of-line.
    uint32_t elements_size = ClampedMultiply(element.Size(), max_element_count);
    // Out-of-line data is aligned to 8 bytes.
    elements_size = AlignTo(elements_size, 8);
    // The elements may each carry their own out-of-line data.
    uint32_t elements_out_of_line = ClampedMultiply(element.MaxOutOfLine(), max_element_count);

    uint32_t max_handles = ClampedMultiply(element.MaxHandles(), max_element_count);
    uint32_t max_out_of_line = ClampedAdd(elements_size, elements_out_of_line);

    return TypeShape(8u, 8u, depth, max_handles, max_out_of_line, element.HasPadding());
}

TypeShape CEnvelopeTypeShape(const TypeShape& contained_type) {
    auto packed_sizes_field = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint64));
    auto pointer_type = FieldShape(PointerTypeShape(contained_type));
    std::vector<FieldShape*> header{&packed_sizes_field, &pointer_type};
    return Struct::Shape(&header);
}

TypeShape XUnion::Shape(std::vector<FieldShape*>* fields, uint32_t extra_handles) {
    uint32_t depth = 0u;
    uint32_t max_handles = 0u;
    uint32_t max_out_of_line = 0u;
    bool has_padding = false;

    for (auto& field : *fields) {
        const auto& envelope = CEnvelopeTypeShape(field->Typeshape());

        depth = ClampedAdd(depth, envelope.Depth());
        max_handles = ClampedAdd(max_handles, envelope.MaxHandles());
        max_out_of_line = std::max(max_out_of_line, envelope.MaxOutOfLine());
        has_padding |= field->Typeshape().HasPadding();
    }

    // XUnion payload is aligned to 8 bytes.
    for (auto& field : *fields) {
        field->SetPadding(AlignTo(field->Size(), 8) - field->Size());
        has_padding |= field->Padding() > 0;
    }

    return TypeShape(24u, 8u, depth, max_handles, max_out_of_line, has_padding);
}

TypeShape ArrayType::Shape(TypeShape element, uint32_t count) {
    return TypeShape(ClampedMultiply(element.Size(), count),
                     element.Alignment(),
                     element.Depth(),
                     ClampedMultiply(element.MaxHandles(), count),
                     ClampedMultiply(element.MaxOutOfLine(), count));
}

TypeShape VectorType::Shape(TypeShape element, uint32_t max_element_count) {
    auto size = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint64));
    auto data = FieldShape(PointerTypeShape(element, max_element_count));
    std::vector<FieldShape*> header{&size, &data};
    return Struct::Shape(&header);
}

TypeShape StringType::Shape(uint32_t max_length) {
    auto size = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint64));
    auto data = FieldShape(PointerTypeShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint8), max_length));
    std::vector<FieldShape*> header{&size, &data};
    return Struct::Shape(&header);
}

uint32_t PrimitiveType::SubtypeSize(types::PrimitiveSubtype subtype) {
    switch (subtype) {
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kUint8:
        return 1u;

    case types::PrimitiveSubtype::kInt16:
    case types::PrimitiveSubtype::kUint16:
        return 2u;

    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kInt32:
    case types::PrimitiveSubtype::kUint32:
        return 4u;

    case types::PrimitiveSubtype::kFloat64:
    case types::PrimitiveSubtype::kInt64:
    case types::PrimitiveSubtype::kUint64:
        return 8u;
    }
}

TypeShape PrimitiveType::Shape(types::PrimitiveSubtype subtype) {
    return TypeShape(SubtypeSize(subtype), SubtypeSize(subtype));
}

std::string Decl::GetName() const {
    return name.name_part();
}

bool Typespace::Create(const flat::Name& name,
                       const Type* arg_type,
                       const Size* size,
                       types::Nullability nullability,
                       const Type** out_type) {
    std::unique_ptr<Type> type;
    if (!CreateNotOwned(name, arg_type, size, nullability, &type))
        return false;
    types_.push_back(std::move(type));
    *out_type = types_.back().get();
    return true;
}

bool Typespace::CreateNotOwned(const flat::Name& name,
                       const Type* arg_type,
                       const Size* size,
                       types::Nullability nullability,
                       std::unique_ptr<Type>* out_type) {
    auto const& location = name.source_location();
    auto type_template = LookupTemplate(name);
    if (type_template == nullptr) {
        std::string message("unknown type ");
        message.append(name.name_part());
        error_reporter_->ReportError(location, message);
        return false;
    }
    return type_template->Create(location, arg_type, size, nullability, out_type);
}

void Typespace::AddTemplate(std::unique_ptr<TypeTemplate> type_template) {
    templates_.emplace(type_template->name(), std::move(type_template));
}

const TypeTemplate* Typespace::LookupTemplate(const flat::Name& name) const {
    Name global_name(nullptr, name.name_part());
    auto iter1 = templates_.find(&global_name);
    if (iter1 != templates_.end())
        return iter1->second.get();

    auto iter2 = templates_.find(&name);
    if (iter2 != templates_.end())
        return iter2->second.get();

    return nullptr;
}

bool TypeTemplate::Fail(const SourceLocation& location, const std::string& content) const {
    std::string message(NameName(name_, ".", "/"));
    message.append(" ");
    message.append(content);
    error_reporter_->ReportError(location, message);
    return false;
}

class PrimitiveTypeTemplate : public TypeTemplate {
public:
    PrimitiveTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter,
                          const std::string& name, types::PrimitiveSubtype subtype)
        : TypeTemplate(Name(nullptr, name), typespace, error_reporter),
          subtype_(subtype) {}

    bool Create(const SourceLocation& location,
                const Type* maybe_arg_type,
                const Size* maybe_size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        if (maybe_arg_type != nullptr)
            return CannotBeParameterized(location);
        if (maybe_size != nullptr)
            return CannotHaveSize(location);
        if (nullability == types::Nullability::kNullable)
            return CannotBeNullable(location);

        *out_type = std::make_unique<PrimitiveType>(subtype_);
        return true;
    }

private:
    const types::PrimitiveSubtype subtype_;
};

class ArrayTypeTemplate : public TypeTemplate {
public:
    ArrayTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "array"), typespace, error_reporter) {}

    bool Create(const SourceLocation& location,
                const Type* arg_type,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        if (arg_type == nullptr)
            return MustBeParameterized(location);
        if (size == nullptr)
            return MustHaveSize(location);
        if (nullability == types::Nullability::kNullable)
            return CannotBeNullable(location);

        *out_type = std::make_unique<ArrayType>(arg_type, size);
        return true;
    }
};

class VectorTypeTemplate : public TypeTemplate {
public:
    VectorTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "vector"), typespace, error_reporter) {}

    bool Create(const SourceLocation& location,
                const Type* arg_type,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        if (arg_type == nullptr)
            return MustBeParameterized(location);
        if (size == nullptr)
            size = &max_size;

        *out_type = std::make_unique<VectorType>(arg_type, size, nullability);
        return true;
    }

private:
    Size max_size = Size::Max();
};

class StringTypeTemplate : public TypeTemplate {
public:
    StringTypeTemplate(Typespace* typespace, ErrorReporter* error_reporter)
        : TypeTemplate(Name(nullptr, "string"), typespace, error_reporter) {}

    bool Create(const SourceLocation& location,
                const Type* arg_type,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        if (arg_type != nullptr)
            return CannotBeParameterized(location);
        if (size == nullptr)
            size = &max_size;

        *out_type = std::make_unique<StringType>(size, nullability);
        return true;
    }

private:
    Size max_size = Size::Max();
};

class TypeDeclTypeTemplate : public TypeTemplate {
public:
    TypeDeclTypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter,
                         Library* library, TypeDecl* type_decl)
        : TypeTemplate(std::move(name), typespace, error_reporter),
          library_(library), type_decl_(type_decl) {}

    bool Create(const SourceLocation& location,
                const Type* arg_type,
                const Size* size,
                types::Nullability nullability,
                std::unique_ptr<Type>* out_type) const {
        if (!type_decl_->compiled) {
            if (type_decl_->compiling) {
                type_decl_->recursive = true;
            } else {
                if (!library_->CompileDecl(type_decl_)) {
                    return false;
                }
            }
        }

        auto typeshape = type_decl_->typeshape;
        switch (type_decl_->kind) {
        default:
            if (nullability == types::Nullability::kNullable)
                typeshape = PointerTypeShape(typeshape);
            break;
        }

        *out_type = std::make_unique<IdentifierType>(
            Name(name()->library(), std::string(name()->name_part())),
            nullability, type_decl_, typeshape);
        return true;
    }

private:
    Library* library_;
    TypeDecl* type_decl_;
};

class TypeAliasTypeTemplate : public TypeTemplate {
public:
    TypeAliasTypeTemplate(Name name, Typespace* typespace, ErrorReporter* error_reporter,
                          Library* library, std::unique_ptr<TypeConstructor> partial_type_ctor)
        : TypeTemplate(std::move(name), typespace, error_reporter),
          library_(library), partial_type_ctor_(std::move(partial_type_ctor)) {}

    bool Create(const SourceLocation& location,
                const Type* maybe_arg_type,
                const Size* maybe_size,
                types::Nullability maybe_nullability,
                std::unique_ptr<Type>* out_type) const {
        const Type* arg_type = nullptr;
        if (partial_type_ctor_->maybe_arg_type_ctor) {
            if (maybe_arg_type) {
                return Fail(location, "cannot parametrize twice");
            }
            if (!partial_type_ctor_->maybe_arg_type_ctor->type) {
                if (!library_->CompileTypeConstructor(
                        partial_type_ctor_->maybe_arg_type_ctor.get(),
                        nullptr /* out_typeshape */))
                    return false;
            }
            arg_type = partial_type_ctor_->maybe_arg_type_ctor->type;
        } else {
            arg_type = maybe_arg_type;
        }

        const Size* size = nullptr;
        if (partial_type_ctor_->maybe_size) {
            if (maybe_size)
                return Fail(location, "cannot bind twice");
            if (!library_->ResolveConstant(partial_type_ctor_->maybe_size.get(), &library_->kSizeType))
                return Fail(location, "unable to parse size bound");
            size = static_cast<const Size*>(&partial_type_ctor_->maybe_size->Value());
        } else {
            size = maybe_size;
        }

        types::Nullability nullability;
        if (partial_type_ctor_->nullability == types::Nullability::kNullable) {
            if (maybe_nullability == types::Nullability::kNullable) {
                return Fail(location, "cannot indicate nullability twice");
            }
            nullability = types::Nullability::kNullable;
        } else {
            nullability = maybe_nullability;
        }

        return typespace_->CreateNotOwned(
            partial_type_ctor_->name,
            arg_type,
            size,
            nullability,
            out_type);
    }

private:
    Library* library_;
    std::unique_ptr<TypeConstructor> partial_type_ctor_;
};

Typespace Typespace::RootTypes(ErrorReporter* error_reporter) {
    Typespace root_typespace(error_reporter);

    auto add_template = [&](std::unique_ptr<TypeTemplate> type_template) {
        auto name = type_template->name();
        root_typespace.templates_.emplace(name, std::move(type_template));
    };

    auto add_primitive = [&](const std::string& name, types::PrimitiveSubtype subtype) {
        add_template(std::make_unique<PrimitiveTypeTemplate>(
            &root_typespace, error_reporter, name, subtype));
    };

    add_primitive("bool", types::PrimitiveSubtype::kBool);

    add_primitive("int8", types::PrimitiveSubtype::kInt8);
    add_primitive("int16", types::PrimitiveSubtype::kInt16);
    add_primitive("int32", types::PrimitiveSubtype::kInt32);
    add_primitive("int64", types::PrimitiveSubtype::kInt64);
    add_primitive("uint8", types::PrimitiveSubtype::kUint8);
    add_primitive("uint16", types::PrimitiveSubtype::kUint16);
    add_primitive("uint32", types::PrimitiveSubtype::kUint32);
    add_primitive("uint64", types::PrimitiveSubtype::kUint64);

    add_primitive("float32", types::PrimitiveSubtype::kFloat32);
    add_primitive("float64", types::PrimitiveSubtype::kFloat64);

    add_primitive("byte", types::PrimitiveSubtype::kUint8);

    add_template(std::make_unique<ArrayTypeTemplate>(
        &root_typespace, error_reporter));
    add_template(std::make_unique<VectorTypeTemplate>(
        &root_typespace, error_reporter));
    add_template(std::make_unique<StringTypeTemplate>(
        &root_typespace, error_reporter));

    return root_typespace;
}

void AttributeSchema::ValidatePlacement(ErrorReporter* error_reporter,
                                   const raw::Attribute* attribute,
                                   Placement placement) const {
    if (allowed_placements_.size() == 0)
        return;
    auto iter = allowed_placements_.find(placement);
    if (iter != allowed_placements_.end())
        return;
    std::string message("placement of attribute '");
    message.append(attribute->name);
    message.append("' disallowed here");
    error_reporter->ReportError(attribute->location(), message);
}

void AttributeSchema::ValidateValue(ErrorReporter* error_reporter,
                                    const raw::Attribute* attribute) const {
    if (allowed_values_.size() == 0)
        return;
    auto iter = allowed_values_.find(attribute->value);
    if (iter != allowed_values_.end())
        return;
    std::string message("attribute '");
    message.append(attribute->name);
    message.append("' has invalid value '");
    message.append(attribute->value);
    message.append("', should be one of '");
    bool first = true;
    for (const auto& hint : allowed_values_) {
        if (!first)
            message.append(", ");
        message.append(hint);
        message.append("'");
        first = false;
    }
    error_reporter->ReportError(attribute->location(), message);
}

void AttributeSchema::ValidateConstraint(ErrorReporter* error_reporter,
                                         const raw::Attribute* attribute,
                                         const Decl* decl) const {
    auto check = error_reporter->Checkpoint();
    auto passed = constraint_(error_reporter, attribute, decl);
    if (passed) {
        assert(check.NoNewErrors() && "cannot add errors and pass");
    } else if (check.NoNewErrors()) {
        std::string message("declaration did not satisfy constraint of attribute '");
        message.append(attribute->name);
        message.append("' with value '");
        message.append(attribute->value);
        message.append("'");
        error_reporter->ReportError(attribute->location(), message);
    }
}

bool ParseBound(ErrorReporter* error_reporter, const SourceLocation& location,
                const std::string& input, uint32_t* out_value) {
    auto result = utils::ParseNumeric(input, out_value, 10);
    switch (result) {
    case utils::ParseNumericResult::kOutOfBounds:
        error_reporter->ReportError(location, "bound is too big");
        return false;
    case utils::ParseNumericResult::kMalformed: {
        std::string message("unable to parse bound '");
        message.append(input);
        message.append("'");
        error_reporter->ReportError(location, message);
        return false;
    }
    case utils::ParseNumericResult::kSuccess:
        return true;
    }
}

bool MaxBytesConstraint(ErrorReporter* error_reporter,
                        const raw::Attribute* attribute,
                        const Decl* decl) {
    uint32_t bound;
    if (!ParseBound(error_reporter, attribute->location(), attribute->value, &bound))
        return false;

    uint32_t max_bytes = std::numeric_limits<uint32_t>::max();
    switch (decl->kind) {
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        max_bytes = struct_decl->typeshape.Size() + struct_decl->typeshape.MaxOutOfLine();
        break;
    }
    default:
        assert(false && "unexpected kind");
        return false;
    }
    if (max_bytes > bound) {
        std::ostringstream message;
        message << "too large: only ";
        message << bound;
        message << " bytes allowed, but ";
        message << max_bytes;
        message << " bytes found";
        error_reporter->ReportError(attribute->location(), message.str());
        return false;
    }
    return true;
}

Libraries::Libraries() {
    AddAttributeSchema("Doc", AttributeSchema({
        /* any placement */
    }, {
        /* any value */
    }));
    AddAttributeSchema("MaxBytes", AttributeSchema({
        AttributeSchema::Placement::kStructDecl,
    }, {
        /* any value */
    },
    MaxBytesConstraint));
}

bool Libraries::Insert(std::unique_ptr<Library> library) {
    std::vector<fidl::StringView> library_name = library->name();
    auto iter = all_libraries_.emplace(library_name, std::move(library));
    return iter.second;
}

bool Libraries::Lookup(const std::vector<StringView>& library_name,
                       Library** out_library) const {
    auto iter = all_libraries_.find(library_name);
    if (iter == all_libraries_.end())
        return false;

    *out_library = iter->second.get();
    return true;
}

const AttributeSchema* Libraries::RetrieveAttributeSchema(
    ErrorReporter* error_reporter, const raw::Attribute* attribute) const {
    const auto& name = attribute->name;
    auto iter = attribute_schemas_.find(name);
    if (iter != attribute_schemas_.end()) {
        const auto& schema = iter->second;
        return &schema;
    }

    // TODO: typo check
    if (error_reporter) {
        std::string message("unknown attribute: '");
        message.append(name);
        message.append("'");
        error_reporter->ReportWarning(attribute->location(), message);
    }
    return nullptr;
}

bool Dependencies::Register(StringView filename, Library* dep_library,
                            const std::unique_ptr<raw::Identifier>& maybe_alias) {
    auto library_name = dep_library->name();
    if (!InsertByName(filename, library_name, dep_library))
        return false;

    if (maybe_alias) {
        std::vector<StringView> alias_name = {maybe_alias->location().data()};
        if (!InsertByName(filename, alias_name, dep_library))
            return false;
    }

    dependencies_aggregate_.insert(dep_library);

    return true;
}

bool Dependencies::InsertByName(StringView filename, const std::vector<StringView>& name,
                                Library* library) {
    auto iter = dependencies_.find(filename);
    if (iter == dependencies_.end()) {
        dependencies_.emplace(filename, std::make_unique<ByName>());
    }

    iter = dependencies_.find(filename);
    assert(iter != dependencies_.end());

    auto insert = iter->second->emplace(name, library);
    return insert.second;
}

bool Dependencies::Lookup(StringView filename, const std::vector<StringView>& name,
                          Library** out_library) {
    auto iter1 = dependencies_.find(filename);
    if (iter1 == dependencies_.end())
        return false;

    auto iter2 = iter1->second->find(name);
    if (iter2 == iter1->second->end())
        return false;

    *out_library = iter2->second;
    return true;
}

bool Library::Fail(StringView message) {
    error_reporter_->ReportError(message);
    return false;
}

bool Library::Fail(const SourceLocation& location, StringView message) {
    error_reporter_->ReportError(location, message);
    return false;
}

void Library::ValidateAttributesPlacement(AttributeSchema::Placement placement,
                                          const raw::AttributeList* attributes) {
    if (attributes == nullptr)
        return;
    for (const auto& attribute : attributes->attributes) {
        auto schema = all_libraries_->RetrieveAttributeSchema(error_reporter_, attribute.get());
        if (schema != nullptr) {
            schema->ValidatePlacement(error_reporter_, attribute.get(), placement);
            schema->ValidateValue(error_reporter_, attribute.get());
        }
    }
}

void Library::ValidateAttributesConstraints(const Decl* decl,
                                            const raw::AttributeList* attributes) {
    if (attributes == nullptr)
        return;
    for (const auto& attribute : attributes->attributes) {
        auto schema = all_libraries_->RetrieveAttributeSchema(nullptr, attribute.get());
        if (schema != nullptr)
            schema->ValidateConstraint(error_reporter_, attribute.get(), decl);
    }
}

bool Library::CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier,
                                        Name* name_out) {
    const auto& components = compound_identifier->components;
    assert(components.size() >= 1);

    SourceLocation decl_name = components.back()->location();

    if (components.size() == 1) {
        *name_out = Name(this, decl_name);
        return true;
    }

    std::vector<StringView> library_name;
    for (auto iter = components.begin(); iter != components.end() - 1; ++iter) {
        library_name.push_back((*iter)->location().data());
    }

    auto filename = decl_name.source_file().filename();
    Library* dep_library = nullptr;
    if (!dependencies_.Lookup(filename, library_name, &dep_library)) {
        std::string message("Unknown dependent library ");
        message += NameLibrary(library_name);
        message += ". Did you require it with `using`?";
        const auto& location = components[0]->location();
        return Fail(location, message);
    }

    *name_out = Name(dep_library, decl_name);
    return true;
}

void Library::RegisterConst(Const* decl) {
    const Name* name = &decl->name;
    constants_.emplace(name, decl);
}

bool Library::RegisterDecl(Decl* decl) {
    const Name* name = &decl->name;
    auto iter = declarations_.emplace(name, decl);
    if (!iter.second) {
        std::string message = "Name collision: ";
        message.append(name->name_part());
        return Fail(*name, message);
    }

    switch (decl->kind) {
    case Decl::Kind::kBits:
    case Decl::Kind::kEnum:
    case Decl::Kind::kStruct: {
        auto type_decl = static_cast<TypeDecl*>(decl);
        auto type_template = std::make_unique<TypeDeclTypeTemplate>(
            Name(name->library(), std::string(name->name_part())),
            typespace_, error_reporter_, this, type_decl);
        typespace_->AddTemplate(std::move(type_template));
        break;
    }
    default:
        assert(decl->kind == Decl::Kind::kConst);
    }
    return true;
}

bool Library::ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant,
                              std::unique_ptr<Constant>* out_constant) {
    switch (raw_constant->kind) {
    case raw::Constant::Kind::kIdentifier: {
        auto identifier = static_cast<raw::IdentifierConstant*>(raw_constant.get());
        Name name;
        if (!CompileCompoundIdentifier(identifier->identifier.get(), &name))
            return false;
        *out_constant = std::make_unique<IdentifierConstant>(std::move(name));
        break;
    }
    case raw::Constant::Kind::kLiteral: {
        auto literal = static_cast<raw::LiteralConstant*>(raw_constant.get());
        *out_constant = std::make_unique<LiteralConstant>(std::move(literal->literal));
        break;
    }
    }
    return true;
}

bool Library::ConsumeTypeConstructor(std::unique_ptr<raw::TypeConstructor> raw_type_ctor,
                                     std::unique_ptr<TypeConstructor>* out_type_ctor) {
    Name name;
    if (!CompileCompoundIdentifier(raw_type_ctor->identifier.get(), &name))
        return false;

    std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
    if (raw_type_ctor->maybe_arg_type_ctor != nullptr) {
        if (!ConsumeTypeConstructor(std::move(raw_type_ctor->maybe_arg_type_ctor), &maybe_arg_type_ctor))
            return false;
    }

    std::unique_ptr<Constant> maybe_size;
    if (raw_type_ctor->maybe_size != nullptr) {
        if (!ConsumeConstant(std::move(raw_type_ctor->maybe_size), &maybe_size))
            return false;
    }

    *out_type_ctor = std::make_unique<TypeConstructor>(
        std::move(name),
        std::move(maybe_arg_type_ctor),
        std::move(maybe_size),
        raw_type_ctor->nullability);
    return true;
}

bool Library::ConsumeUsing(std::unique_ptr<raw::Using> using_directive) {
    switch (using_directive->kind) {
    case raw::Using::Kind::kLibrary: {
        auto using_library = static_cast<raw::UsingLibrary*>(using_directive.get());
        return ConsumeUsingLibrary(using_library);
    }
    case raw::Using::Kind::kAlias: {
        auto using_alias = static_cast<raw::UsingAlias*>(using_directive.get());
        return ConsumeTypeAlias(using_alias);
    }
    }
}

bool Library::ConsumeUsingLibrary(raw::UsingLibrary* using_library) {
    std::vector<StringView> library_name;
    for (const auto& component : using_library->using_path->components) {
        library_name.push_back(component->location().data());
    }

    Library* dep_library = nullptr;
    if (!all_libraries_->Lookup(library_name, &dep_library)) {
        std::string message("Could not find library named ");
        message += NameLibrary(library_name);
        message += ". Did you include its sources with --files?";
        const auto& location = using_library->using_path->components[0]->location();
        return Fail(location, message);
    }

    auto filename = using_library->location().source_file().filename();
    if (!dependencies_.Register(filename, dep_library, using_library->maybe_alias)) {
        std::string message("Library ");
        message += NameLibrary(library_name);
        message += " already imported. Did you required it twice?";
        return Fail(message);
    }

    const auto& declarations = dep_library->declarations_;
    declarations_.insert(declarations.begin(), declarations.end());
    return true;
}

bool Library::ConsumeTypeAlias(raw::UsingAlias* using_alias) {
    auto location = using_alias->alias->location();
    auto alias_name = Name(this, location);
    std::unique_ptr<TypeConstructor> partial_type_ctor_;
    if (!ConsumeTypeConstructor(std::move(using_alias->type_ctor), &partial_type_ctor_))
        return false;
    typespace_->AddTemplate(std::make_unique<TypeAliasTypeTemplate>(
        std::move(alias_name), typespace_, error_reporter_, this, std::move(partial_type_ctor_)));
    return true;
}

bool Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
    auto location = const_declaration->identifier->location();
    auto name = Name(this, location);

    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(const_declaration->type_ctor), &type_ctor))
        return false;

    std::unique_ptr<Constant> constant;
    if (!ConsumeConstant(std::move(const_declaration->constant), &constant))
        return false;

    const_declarations_.push_back(std::make_unique<Const>(
        std::move(name), std::move(const_declaration->attributes), std::move(type_ctor), std::move(constant)));

    auto decl = const_declarations_.back().get();
    RegisterConst(decl);
    return RegisterDecl(decl);
} 

bool Library::ConsumeBitsDeclaration(std::unique_ptr<raw::BitsDeclaration> bits_declaration) {
    std::vector<Bits::Member> members;
    for (auto& member : bits_declaration->members) {
        std::unique_ptr<Constant> value;
        if (!ConsumeConstant(std::move(member->value), &value))
            return false;
        auto location = member->identifier->location();
        members.emplace_back(std::move(member->attributes), location, std::move(value));
    }

    std::unique_ptr<TypeConstructor> type_ctor;
    if (bits_declaration->maybe_type_ctor) {
        if (!ConsumeTypeConstructor(std::move(bits_declaration->maybe_type_ctor), &type_ctor))
            return false;
    } else {
        type_ctor = std::make_unique<TypeConstructor>(
            Name(nullptr, "uint32"),
            nullptr /* maybe_arg_type */,
            nullptr /* maybe_size */,
            types::Nullability::kNonnullable);
    }

    bits_declarations_.push_back(
        std::make_unique<Bits>(
            std::move(bits_declaration->attributes),
            Name(this, bits_declaration->identifier->location()),
            std::move(type_ctor),
            std::move(members)));
    return RegisterDecl(bits_declarations_.back().get());
}

bool Library::ConsumeEnumDeclaration(std::unique_ptr<raw::EnumDeclaration> enum_declaration) {
    std::vector<Enum::Member> members;
    for (auto& member : enum_declaration->members) {
        std::unique_ptr<Constant> value;
        if (!ConsumeConstant(std::move(member->value), &value))
            return false;
        auto location = member->identifier->location();
        members.emplace_back(std::move(member->attributes), location, std::move(value));
    }

    std::unique_ptr<TypeConstructor> type_ctor;
    if (enum_declaration->maybe_type_ctor) {
        if (!ConsumeTypeConstructor(std::move(enum_declaration->maybe_type_ctor), &type_ctor))
            return false;
    } else {
        type_ctor = std::make_unique<TypeConstructor>(
            Name(nullptr, "uint32"),
            nullptr /* maybe_arg_type */,
            nullptr /* maybe_size */,
            types::Nullability::kNonnullable);
    }

    enum_declarations_.push_back(
        std::make_unique<Enum>(
            std::move(enum_declaration->attributes),
            Name(this, enum_declaration->identifier->location()),
            std::move(type_ctor),
            std::move(members)));
    return RegisterDecl(enum_declarations_.back().get());
}

bool Library::ConsumeStructDeclaration(std::unique_ptr<raw::StructDeclaration> struct_declaration) {
    std::vector<Struct::Member> members;
    for (auto& member : struct_declaration->members) {
        std::unique_ptr<TypeConstructor> type_ctor;
        auto location = member->identifier->location();
        if (!ConsumeTypeConstructor(std::move(member->type_ctor), &type_ctor))
            return false;
        std::unique_ptr<Constant> maybe_default_value;
        if (member->maybe_default_value != nullptr) {
            if (!ConsumeConstant(std::move(member->maybe_default_value), &maybe_default_value))
                return false;
        }
        members.emplace_back(
            std::move(member->attributes),
            std::move(type_ctor),
            location,
            std::move(maybe_default_value));
    }

    auto name = Name(this, struct_declaration->identifier->location());
    struct_declarations_.push_back(
        std::make_unique<Struct>(
            std::move(name),
            std::move(struct_declaration->attributes),
            std::move(members)));
    return RegisterDecl(struct_declarations_.back().get());
}

bool Library::ConsumeUnionDeclaration(std::unique_ptr<raw::UnionDeclaration> union_declaration) {
    std::vector<Union::Member> members;
    for (auto& member : union_declaration->members) {
        std::unique_ptr<TypeConstructor> type_ctor;
        if (!ConsumeTypeConstructor(std::move(member->type_ctor), &type_ctor))
            return false;

        auto attributes = std::move(member->attributes);
        auto location = member->identifier->location();
        members.emplace_back(std::move(attributes), std::move(type_ctor), location);
    }

    auto attributes = std::move(union_declaration->attributes);
    auto name = Name(this, union_declaration->identifier->location());
    union_declarations_.push_back(
        std::make_unique<Union>(
            std::move(attributes),
            std::move(name),
            std::move(members)));
    return RegisterDecl(union_declarations_.back().get());
}

bool Library::ConsumeXUnionDeclaration(std::unique_ptr<raw::XUnionDeclaration> xunion_declaration) {
    std::vector<XUnion::Member> members;
    int ordinal_val = 0;
    for (auto& member : xunion_declaration->members) {
        // TODO: generate ordinal the correct way
        auto ordinal = std::make_unique<raw::Ordinal>(*member, ordinal_val++);

        std::unique_ptr<TypeConstructor> type_ctor;
        if (!ConsumeTypeConstructor(std::move(member->type_ctor), &type_ctor))
            return false;

        if (type_ctor->nullability != types::Nullability::kNonnullable) {
            return Fail(member->location(), "Extensible union members cannot be nullable");
        }

        auto location = member->identifier->location();
        members.emplace_back(
            std::move(ordinal),
            std::move(member->attributes),
            std::move(type_ctor),
            location);
    }

    auto name = Name(this, xunion_declaration->identifier->location());
    xunion_declarations_.push_back(
        std::make_unique<XUnion>(
            std::move(xunion_declaration->attributes),
            std::move(name),
            std::move(members)));
    return RegisterDecl(xunion_declarations_.back().get());
}

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
    if (file->attributes) {
        ValidateAttributesPlacement(AttributeSchema::Placement::kLibrary, file->attributes.get());
        if (!attributes_) {
            attributes_ = std::move(file->attributes);
        } else {
            AttributesBuilder attributes_builder(error_reporter_, std::move(attributes_->attributes));
            for (auto& attribute : file->attributes->attributes) {
                if (!attributes_builder.Insert(std::move(attribute)))
                    return false;
            }
            attributes_ = std::make_unique<raw::AttributeList>(
                // looks like there's no real "correct" way to do this, as we
                // are combining parser nodes from multiple files into one
                raw::SourceElement(file->attributes->start_, file->attributes->end_),
                attributes_builder.Done());
        }
    }

    // validate the library name of this file
    std::vector<StringView> new_name;
    for (const auto& part : file->library_name->components) {
        new_name.push_back(part->location().data());
    }

    if (!library_name_.empty()) {
        if (new_name != library_name_) {
            return Fail(file->library_name->components[0]->location(),
                        "Two files in the library disagree about the library name");
        }
    } else {
        library_name_ = new_name;
    }

    auto using_list = std::move(file->using_list);
    for (auto& using_directive : using_list) {
        if (!ConsumeUsing(std::move(using_directive)))
            return false;
    }

    auto const_declaration_list = std::move(file->const_declaration_list);
    for (auto& const_declaration : const_declaration_list) {
        if (!ConsumeConstDeclaration(std::move(const_declaration))) {
            return false;
        }
    }

    auto bits_declaration_list = std::move(file->bits_declaration_list);
    for (auto& bits_declaration : bits_declaration_list) {
        if (!ConsumeBitsDeclaration(std::move(bits_declaration))) {
            return false;
        }
    }

    auto enum_declaration_list = std::move(file->enum_declaration_list);
    for (auto& enum_declaration : enum_declaration_list) {
        if (!ConsumeEnumDeclaration(std::move(enum_declaration))) {
            return false;
        }
    }

    auto struct_declaration_list = std::move(file->struct_declaration_list);
    for (auto& struct_declaration : struct_declaration_list) {
        if (!ConsumeStructDeclaration(std::move(struct_declaration))) {
            return false;
        }
    }

    auto union_declaration_list = std::move(file->union_declaration_list);
    for (auto& union_declaration : union_declaration_list) {
        if (!ConsumeUnionDeclaration(std::move(union_declaration))) {
            return false;
        }
    }

    auto xunion_declaration_list = std::move(file->xunion_declaration_list);
    for (auto& xunion_declaration : xunion_declaration_list) {
        if (!ConsumeXUnionDeclaration(std::move(xunion_declaration))) {
            return false;
        }
    }

    return true;
}

bool Library::ResolveConstant(Constant* constant, const Type* type) {
    assert(constant != nullptr);

    if (constant->IsResolved())
        return true;

    switch (constant->kind) {
    case Constant::Kind::kIdentifier: {
        auto identifier_constant = static_cast<IdentifierConstant*>(constant);
        return ResolveIdentifierConstant(identifier_constant, type);
    }
    case Constant::Kind::kLiteral: {
        auto literal_constant = static_cast<LiteralConstant*>(constant);
        return ResolveLiteralConstant(literal_constant, type);
    }
    case Constant::Kind::kSynthesized: {
        assert(false && "Compiler bug: synthesized constant does not have a resolved value!");
    }
    }
}

bool Library::ResolveIdentifierConstant(IdentifierConstant* identifier_constant, const Type* type) {
    assert(TypeCanBeConst(type) && "Compiler bug: resolving identifier constant to non const-able type!");

    auto decl = LookupDeclByName(identifier_constant->name);
    if (!decl || decl->kind != Decl::Kind::kConst)
        return false;

    auto const_decl = static_cast<Const*>(decl);
    if (!CompileConst(const_decl))
        return false;
    assert(const_decl->value->IsResolved());

    const ConstantValue& const_val = const_decl->value->Value();
    std::unique_ptr<ConstantValue> resolved_val;
    switch (type->kind) {
    case Type::Kind::kString: {
        if (!TypeIsConvertibleTo(const_decl->type_ctor->type, type))
            goto fail_cannot_convert;

        if (!const_val.Convert(ConstantValue::Kind::kString, &resolved_val))
            goto fail_cannot_convert;
        break;
    }

    case Type::Kind::kPrimitive: {
        auto primitive_type = static_cast<const PrimitiveType*>(type);
        switch (primitive_type->subtype) {
        case types::PrimitiveSubtype::kBool:
            if (!const_val.Convert(ConstantValue::Kind::kBool, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt8:
            if (!const_val.Convert(ConstantValue::Kind::kInt8, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt16:
            if (!const_val.Convert(ConstantValue::Kind::kInt16, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt32:
            if (!const_val.Convert(ConstantValue::Kind::kInt32, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kInt64:
            if (!const_val.Convert(ConstantValue::Kind::kInt64, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint8:
            if (!const_val.Convert(ConstantValue::Kind::kUint8, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint16:
            if (!const_val.Convert(ConstantValue::Kind::kUint16, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint32:
            if (!const_val.Convert(ConstantValue::Kind::kUint32, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kUint64:
            if (!const_val.Convert(ConstantValue::Kind::kUint64, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kFloat32:
            if (!const_val.Convert(ConstantValue::Kind::kFloat32, &resolved_val))
                goto fail_cannot_convert;
            break;
        case types::PrimitiveSubtype::kFloat64:
            if (!const_val.Convert(ConstantValue::Kind::kFloat64, &resolved_val))
                goto fail_cannot_convert;
            break;
        }
        break;
    }
    default: {
        assert(false && "Compiler bug: const-able type not handled during identifer constant resolution!");
    }
    }

    identifier_constant->ResolveTo(std::move(resolved_val));
    return true;

fail_cannot_convert:
    std::ostringstream msg_stream;
    msg_stream << NameFlatConstant(identifier_constant) << ", of type ";
    msg_stream << NameFlatTypeConstructor(const_decl->type_ctor.get());
    msg_stream << ", cannot be converted to type " << NameFlatType(type);
    return Fail(msg_stream.str());
}

bool Library::ResolveLiteralConstant(LiteralConstant* literal_constant, const Type* type) {
    switch (literal_constant->literal->kind) {
    case raw::Literal::Kind::kString: {
        if (type->kind != Type::Kind::kString)
            goto return_fail;
        auto string_type = static_cast<const StringType*>(type);
        auto string_literal = static_cast<raw::StringLiteral*>(literal_constant->literal.get());
        auto string_data = string_literal->location().data();

        // TODO(pascallouis): because data() contains the raw content,
        // with the two " to identify strings, we need to take this
        // into account. We should expose the actual size of string
        // literals properly, and take into account escaping.
        uint64_t string_size = string_data.size() - 2;
        if (string_type->max_size->value < string_size) {
            std::ostringstream msg_stream;
            msg_stream << NameFlatConstant(literal_constant) << " (string:" << string_size;
            msg_stream << ") exceeds the size bound of type " << NameFlatType(type);
            return Fail(literal_constant->literal->location(), msg_stream.str());
        }

        literal_constant->ResolveTo(
            std::make_unique<StringConstantValue>(string_literal->location().data()));
        return true;
    }
    case raw::Literal::Kind::kTrue: {
        if (type->kind != Type::Kind::kPrimitive)
            goto return_fail;
        if (static_cast<const PrimitiveType*>(type)->subtype != types::PrimitiveSubtype::kBool)
            goto return_fail;
        literal_constant->ResolveTo(std::make_unique<BoolConstantValue>(true));
        return true;
    }
    case raw::Literal::Kind::kFalse: {
        if (type->kind != Type::Kind::kPrimitive)
            goto return_fail;
        if (static_cast<const PrimitiveType*>(type)->subtype != types::PrimitiveSubtype::kBool)
            goto return_fail;
        literal_constant->ResolveTo(std::make_unique<BoolConstantValue>(false));
        return true;
    }
    case raw::Literal::Kind::kNumeric: {
        if (type->kind != Type::Kind::kPrimitive)
            goto return_fail;

        // These must be initialized out of line to allow for goto statement
        const raw::NumericLiteral* numeric_literal;
        const PrimitiveType* primitive_type;
        numeric_literal =
            static_cast<const raw::NumericLiteral*>(literal_constant->literal.get());
        primitive_type = static_cast<const PrimitiveType*>(type);
        switch (primitive_type->subtype) {
        case types::PrimitiveSubtype::kInt8: {
            int8_t value;
            if (!ParseNumericLiteral<int8_t>(numeric_literal, &value))
                goto return_fail;
            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int8_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kInt16: {
            int16_t value;
            if (!ParseNumericLiteral<int16_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int16_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kInt32: {
            int32_t value;
            if (!ParseNumericLiteral<int32_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int32_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kInt64: {
            int64_t value;
            if (!ParseNumericLiteral<int64_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<int64_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint8: {
            uint8_t value;
            if (!ParseNumericLiteral<uint8_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint8_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint16: {
            uint16_t value;
            if (!ParseNumericLiteral<uint16_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint16_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint32: {
            uint32_t value;
            if (!ParseNumericLiteral<uint32_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint32_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kUint64: {
            uint64_t value;
            if (!ParseNumericLiteral<uint64_t>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<uint64_t>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kFloat32: {
            float value;
            if (!ParseNumericLiteral<float>(numeric_literal, &value))
                goto return_fail;
            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<float>>(value));
            return true;
        }
        case types::PrimitiveSubtype::kFloat64: {
            double value;
            if (!ParseNumericLiteral<double>(numeric_literal, &value))
                goto return_fail;

            literal_constant->ResolveTo(std::make_unique<NumericConstantValue<double>>(value));
            return true;
        }
        default:
            goto return_fail;
        }

    return_fail:
        std::ostringstream msg_stream;
        msg_stream << NameFlatConstant(literal_constant) << " cannot be interpreted as type ";
        msg_stream << NameFlatType(type);
        return Fail(literal_constant->literal->location(), msg_stream.str());
    }
    }
}

bool Library::TypeCanBeConst(const Type* type) {
    switch (type->kind) {
    case flat::Type::Kind::kString:
        return type->nullability != types::Nullability::kNullable;
    case flat::Type::Kind::kPrimitive:
        return true;
    default:
        return false;
    } // switch
}

bool Library::TypeIsConvertibleTo(const Type* from_type, const Type* to_type) {
    switch (to_type->kind) {
    case flat::Type::Kind::kString: {
        if (from_type->kind != flat::Type::Kind::kString)
            return false;

        auto from_string_type = static_cast<const flat::StringType*>(from_type);
        auto to_string_type = static_cast<const flat::StringType*>(to_type);

        if (to_string_type->nullability == types::Nullability::kNonnullable &&
            from_string_type->nullability != types::Nullability::kNonnullable)
            return false;

        if (to_string_type->max_size->value < from_string_type->max_size->value)
            return false;

        return true;
    }
    case flat::Type::Kind::kPrimitive: {
        if (from_type->kind != flat::Type::Kind::kPrimitive) {
            return false;
        }

        auto from_primitive_type = static_cast<const flat::PrimitiveType*>(from_type);
        auto to_primitive_type = static_cast<const flat::PrimitiveType*>(to_type);

        switch (to_primitive_type->subtype) {
        case types::PrimitiveSubtype::kBool:
            return from_primitive_type->subtype == types::PrimitiveSubtype::kBool;
        default:
            // TODO(pascallouis): be more precise about convertibility, e.g. it
            // should not be allowed to convert a float to an int.
            return from_primitive_type->subtype != types::PrimitiveSubtype::kBool;
        }
    }
    default:
        return false;
    } // switch
}

Decl* Library::LookupConstant(const TypeConstructor* type_ctor, const Name& name) {
    auto decl = LookupDeclByName(type_ctor->name);
    if (decl == nullptr) {
        // the constant is not of a user defined type, which means that it must
        // be declared as a top level const since those can only  have a type
        // of string or primitive type
        auto iter = constants_.find(&name);
        if (iter == constants_.end()) {
            return nullptr;
        }
        return iter->second;
    }

    // otherwise, the only user defined type that constants can be is of an enum
    if (decl->kind != Decl::Kind::kEnum)
        return nullptr;

    auto enum_decl = static_cast<Enum*>(decl);
    for (auto& member : enum_decl->members) {
        if (member.name.data() == name.name_part()) {
            return enum_decl;
        }
    }

    return nullptr;
}

Decl* Library::LookupDeclByName(const Name& name) const {
    auto iter = declarations_.find(&name);
    if (iter == declarations_.end()) {
        return nullptr;
    }
    return iter->second;
}

template <typename NumericType>
bool Library::ParseNumericLiteral(const raw::NumericLiteral* literal,
                                  NumericType* out_value) const {
    assert(literal != nullptr);
    assert(out_value != nullptr);

    auto data = literal->location().data();
    std::string string_data(data.data(), data.data() + data.size());
    auto result = utils::ParseNumeric(string_data, out_value);
    return result == utils::ParseNumericResult::kSuccess;
}

bool Library::DeclDependencies(Decl* decl, std::set<Decl*>* out_edges) {
    std::set<Decl*> edges;

    auto maybe_add_decl = [this, &edges](const TypeConstructor* type_ctor) {
        for (;;) {
            const auto& name = type_ctor->name;
            if (name.name_part() == "request") {
                return;
            } else if (type_ctor->maybe_arg_type_ctor) {
                // I think this is assuming that no user defined types have arg types?
                type_ctor = type_ctor->maybe_arg_type_ctor.get();
            } else if (type_ctor->nullability == types::Nullability::kNullable) {
                return;
            } else {
                if (auto decl = LookupDeclByName(name); decl) {
                    edges.insert(decl);
                }
                return;
            }
        }
    };

    auto maybe_add_constant = [this, &edges](const TypeConstructor* type_ctor, const Constant* constant) -> bool {
        switch (constant->kind) {
        case Constant::Kind::kIdentifier: {
            auto identifier = static_cast<const flat::IdentifierConstant*>(constant);
            auto decl = LookupConstant(type_ctor, identifier->name);
            if (decl == nullptr) {
                std::string message("Unable to find the constant named: ");
                message += identifier->name.name_part();
                return Fail(identifier->name, message.data());
            }
            edges.insert(decl);
            break;
        }
        case Constant::Kind::kLiteral:
        case Constant::Kind::kSynthesized: {
            // literals and synthesized constants have no deps on other decls
            break;
        }
        }
        return true;
    };

    switch (decl->kind) {
    case Decl::Kind::kConst: {
        auto const_decl = static_cast<const Const*>(decl);
        if (!maybe_add_constant(const_decl->type_ctor.get(), const_decl->value.get()))
            return false;
        break;
    }
    case Decl::Kind::kBits: {
        auto bits_decl = static_cast<const Bits*>(decl);
        for (const auto& member : bits_decl->members) {
            // why don't we check for failure here?
            maybe_add_constant(bits_decl->subtype_ctor.get(), member.value.get());
        }
        break;
    }
    case Decl::Kind::kEnum: {
        auto enum_decl = static_cast<const Enum*>(decl);
        for (const auto& member : enum_decl->members) {
            maybe_add_constant(enum_decl->subtype_ctor.get(), member.value.get());
        }
        break;
    }
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<const Struct*>(decl);
        for (const auto& member : struct_decl->members) {
            maybe_add_decl(member.type_ctor.get());
            if (member.maybe_default_value) {
                if (!maybe_add_constant(member.type_ctor.get(), member.maybe_default_value.get()))
                    return false;
            }
        }
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<const Union*>(decl);
        for (const auto& member : union_decl->members) {
            maybe_add_decl(member.type_ctor.get());
        }
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_decl = static_cast<const XUnion*>(decl);
        for (const auto& member : xunion_decl->members) {
            maybe_add_decl(member.type_ctor.get());
        }
        break;
    }
    }
    *out_edges = std::move(edges);
    return true;
}

namespace {
// To compare two Decl's in the same library, it suffices to compare the unqualified names of the Decl's.
struct CmpDeclInLibrary {
    bool operator()(const Decl* a, const Decl* b) const {
        assert(a->name != b->name || a == b);
        return a->name < b->name;
    }
};
} // namespace

bool Library::SortDeclarations() {
    std::map<Decl*, uint32_t, CmpDeclInLibrary> degrees;
    for (auto& name_and_decl : declarations_) {
        Decl* decl = name_and_decl.second;
        degrees[decl] = 0u;
    }

    std::map<Decl*, std::vector<Decl*>, CmpDeclInLibrary> inverse_dependencies;
    for (auto& name_and_decl : declarations_) {
        Decl* decl = name_and_decl.second;
        std::set<Decl*> deps;
        if (!DeclDependencies(decl, &deps))
            return false;
        degrees[decl] += deps.size();
        for (Decl* dep : deps) {
            inverse_dependencies[dep].push_back(decl);
        }
    }

    std::vector<Decl*> decls_without_deps;
    for (const auto& decl_and_degree : degrees) {
        if (decl_and_degree.second == 0u) {
            decls_without_deps.push_back(decl_and_degree.first);
        }
    }

    while (!decls_without_deps.empty()) {
        auto decl = decls_without_deps.back();
        decls_without_deps.pop_back();
        assert(degrees[decl] == 0u);
        declaration_order_.push_back(decl);

        auto& inverse_deps = inverse_dependencies[decl];
        for (Decl* inverse_dep : inverse_deps) {
            uint32_t& degree = degrees[inverse_dep];
            assert(degree != 0u);
            degree -= 1;
            if (degree == 0u)
                decls_without_deps.push_back(inverse_dep);
        }
    }

    if (declaration_order_.size() != degrees.size()) {
        return Fail("There is an includes-cycle in declarations");
    }

    return true;
}

namespace {

class ScopeInsertResult {
public:
    explicit ScopeInsertResult(std::unique_ptr<SourceLocation> previous_occurrence)
        : previous_occurrence_(std::move(previous_occurrence)) {}

    static ScopeInsertResult Ok() { return ScopeInsertResult(nullptr); }
    static ScopeInsertResult FailureAt(SourceLocation previous) {
        return ScopeInsertResult(std::make_unique<SourceLocation>(previous));
    }

    bool ok() const {
        return previous_occurrence_ == nullptr;
    }

    const SourceLocation& previous_occurrence() const {
        assert(!ok());
        return *previous_occurrence_;
    }

private:
    std::unique_ptr<SourceLocation> previous_occurrence_;
};

template <typename T>
class Scope {
public:
    ScopeInsertResult Insert(const T& t, SourceLocation location) {
        auto iter = scope_.find(t);
        if (iter != scope_.end()) {
            return ScopeInsertResult::FailureAt(iter->second);
        } else {
            scope_.emplace(t, location);
            return ScopeInsertResult::Ok();
        }
    }

    typename std::map<T, SourceLocation>::const_iterator begin() const {
        return scope_.begin();
    }

    typename std::map<T, SourceLocation>::const_iterator end() const {
        return scope_.end();
    }

private:
    std::map<T, SourceLocation> scope_;
};

struct MethodScope {
    Scope<uint32_t> ordinals;
    Scope<StringView> names;
};

// A helper class to track when a Decl is compiling and compiled.
class Compiling {
public:
    explicit Compiling(Decl* decl)
        : decl_(decl) {
        decl_->compiling = true;
    }
    ~Compiling() {
        decl_->compiling = false;
        decl_->compiled = true;
    }

private:
    Decl* decl_;
};

} // namespace

bool Library::CompileDecl(Decl* decl) {
    Compiling guard(decl);
    switch (decl->kind) {
    case Decl::Kind::kConst: {
        auto const_decl = static_cast<Const*>(decl);
        if (!CompileConst(const_decl))
            return false;
        break;
    }
    case Decl::Kind::kBits: {
        auto bits_decl = static_cast<Bits*>(decl);
        if (!CompileBits(bits_decl))
            return false;
        break;
    }
    case Decl::Kind::kEnum: {
        auto enum_decl = static_cast<Enum*>(decl);
        if (!CompileEnum(enum_decl))
            return false;
        break;
    }
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<Struct*>(decl);
        if (!CompileStruct(struct_decl))
            return false;
        break;
    }
    case Decl::Kind::kUnion: {
        auto union_decl = static_cast<Union*>(decl);
        if (!CompileUnion(union_decl))
            return false;
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_decl = static_cast<XUnion*>(decl);
        if (!CompileXUnion(xunion_decl))
            return false;
        break;
    }
    } // switch
    return true;
}

bool Library::VerifyDeclAttributes(Decl* decl) {
    assert(decl->compiled && "verification must happen after compilation of decls");
    auto placement_ok = error_reporter_->Checkpoint();
    switch(decl->kind) {
    case Decl::Kind::kConst: {
        auto const_decl = static_cast<Const*>(decl);
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kConstDecl, const_decl->attributes.get());
        break;
    }
    case Decl::Kind::kBits: {
        auto bits_declaration = static_cast<Bits*>(decl);
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kBitsDecl,
            bits_declaration->attributes.get());
        for (const auto& member : bits_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kBitsMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            ValidateAttributesConstraints(
                bits_declaration, bits_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kEnum: {
        auto enum_declaration = static_cast<Enum*>(decl);
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kEnumDecl,
            enum_declaration->attributes.get());
        for (const auto& member : enum_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kEnumMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            ValidateAttributesConstraints(
                enum_declaration, enum_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kStruct: {
        auto struct_decl = static_cast<Struct*>(decl);
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kStructDecl, struct_decl->attributes.get());
        for (const auto& member : struct_decl->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kStructMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            ValidateAttributesConstraints(
                struct_decl, struct_decl->attributes.get());
        }
    }
    case Decl::Kind::kUnion: {
        auto union_declaration = static_cast<Union*>(decl);
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kUnionDecl,
            union_declaration->attributes.get());
        for (const auto& member : union_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kUnionMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            ValidateAttributesConstraints(
                union_declaration,
                union_declaration->attributes.get());
        }
        break;
    }
    case Decl::Kind::kXUnion: {
        auto xunion_declaration = static_cast<XUnion*>(decl);
        ValidateAttributesPlacement(
            AttributeSchema::Placement::kXUnionDecl,
            xunion_declaration->attributes.get());
        for (const auto& member : xunion_declaration->members) {
            ValidateAttributesPlacement(
                AttributeSchema::Placement::kXUnionMember,
                member.attributes.get());
        }
        if (placement_ok.NoNewErrors()) {
            ValidateAttributesConstraints(
                xunion_declaration,
                xunion_declaration->attributes.get());
        }
    }
    } // switch
    return true;
}

bool Library::CompileConst(Const* const_declaration) {
    TypeShape typeshape;
    if (!CompileTypeConstructor(const_declaration->type_ctor.get(), &typeshape))
        return false;

    const auto* const_type = const_declaration->type_ctor.get()->type;
    if (!TypeCanBeConst(const_type)) {
        std::ostringstream msg_stream;
        msg_stream << "invalid constant type " << NameFlatType(const_type);
        return Fail(*const_declaration, msg_stream.str());
    }
    if (!ResolveConstant(const_declaration->value.get(), const_type))
        return Fail(*const_declaration, "unable to resolve constant value");

    return true;
}

bool Library::CompileBits(Bits* bits_declaration) {
    if (!CompileTypeConstructor(bits_declaration->subtype_ctor.get(), &bits_declaration->typeshape))
        return false;

    if (bits_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
        std::string message("bits may only be of unsigned integral primitive type, found ");
        message.append(NameFlatType(bits_declaration->subtype_ctor->type));
        return Fail(*bits_declaration, message);
    }

    auto primitive_type = static_cast<const PrimitiveType*>(bits_declaration->subtype_ctor->type);
    switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kUint8: {
        uint8_t mask;
        if (!ValidateBitsMembersAndCalcMask<uint8_t>(bits_declaration, &mask))
            return false;
        bits_declaration->mask = mask;
        break;
    }
    case types::PrimitiveSubtype::kUint16: {
        uint16_t mask;
        if (!ValidateBitsMembersAndCalcMask<uint16_t>(bits_declaration, &mask))
            return false;
        bits_declaration->mask = mask;
        break;
    }
    case types::PrimitiveSubtype::kUint32: {
        uint32_t mask;
        if (!ValidateBitsMembersAndCalcMask<uint32_t>(bits_declaration, &mask))
            return false;
        bits_declaration->mask = mask;
        break;
    }
    case types::PrimitiveSubtype::kUint64: {
        uint64_t mask;
        if (!ValidateBitsMembersAndCalcMask<uint64_t>(bits_declaration, &mask))
            return false;
        bits_declaration->mask = mask;
        break;
    }
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kInt8:
    case types::PrimitiveSubtype::kInt16:
    case types::PrimitiveSubtype::kInt32:
    case types::PrimitiveSubtype::kInt64:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
        std::string message("bits may only be of unsigned integral primitive type, found ");
        message.append(NameFlatType(bits_declaration->subtype_ctor->type));
        return Fail(*bits_declaration, message);
    }

    return true;
}

bool Library::CompileEnum(Enum* enum_declaration) {
    if (!CompileTypeConstructor(enum_declaration->subtype_ctor.get(), &enum_declaration->typeshape))
        return false;

    if (enum_declaration->subtype_ctor->type->kind != Type::Kind::kPrimitive) {
        std::string message("enums may only be of integral primitive type, found ");
        message.append(NameFlatType(enum_declaration->subtype_ctor->type));
        return Fail(*enum_declaration, message);
    }

    // Validate constants.
    auto primitive_type = static_cast<const PrimitiveType*>(enum_declaration->subtype_ctor->type);
    enum_declaration->type = primitive_type;
    switch (primitive_type->subtype) {
    case types::PrimitiveSubtype::kInt8:
        if (!ValidateBitsOrEnumMembers<Enum, int8_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kInt16:
        if (!ValidateBitsOrEnumMembers<Enum, int16_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kInt32:
        if (!ValidateBitsOrEnumMembers<Enum, int32_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kInt64:
        if (!ValidateBitsOrEnumMembers<Enum, int64_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint8:
        if (!ValidateBitsOrEnumMembers<Enum, uint8_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint16:
        if (!ValidateBitsOrEnumMembers<Enum, uint16_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint32:
        if (!ValidateBitsOrEnumMembers<Enum, uint32_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kUint64:
        if (!ValidateBitsOrEnumMembers<Enum, uint64_t>(enum_declaration))
            return false;
        break;
    case types::PrimitiveSubtype::kBool:
    case types::PrimitiveSubtype::kFloat32:
    case types::PrimitiveSubtype::kFloat64:
        std::string message("enums may only be of integral primitive type, found ");
        message.append(NameFlatType(enum_declaration->subtype_ctor->type));
        return Fail(*enum_declaration, message);
    }

    return true;
}

bool Library::CompileStruct(Struct* struct_declaration) {
    Scope<StringView> scope;
    std::vector<FieldShape*> fidl_struct;

    for (auto& member : struct_declaration->members) {
        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple struct fields with the same name; previous was at " +
                            name_result.previous_occurrence().position());
        if (!CompileTypeConstructor(member.type_ctor.get(), &member.fieldshape.Typeshape()))
            return false;
        fidl_struct.push_back(&member.fieldshape);
    }

    uint32_t max_member_handles = 0;
    if (struct_declaration->recursive) {
        max_member_handles = std::numeric_limits<uint32_t>::max();
    } else {
        max_member_handles = 0;
    }

    struct_declaration->typeshape = Struct::Shape(&fidl_struct, max_member_handles);

    return true;
}

bool Library::CompileUnion(Union* union_decl) {
    Scope<StringView> scope;
    std::vector<FieldShape*> fields;
    for (auto& member : union_decl->members) {
        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple union members with the same name; previous was at " +
                            name_result.previous_occurrence().position());
        if (!CompileTypeConstructor(member.type_ctor.get(), &member.fieldshape.Typeshape()))
            return false;

        fields.push_back(&member.fieldshape);
    }

    auto tag = FieldShape(PrimitiveType::Shape(types::PrimitiveSubtype::kUint32));
    union_decl->membershape = FieldShape(Union::Shape(&fields));
    uint32_t extra_handles = 0;
    if (union_decl->recursive && union_decl->membershape.MaxHandles()) {
        extra_handles = std::numeric_limits<uint32_t>::max();
    }
    std::vector<FieldShape*> fidl_union = {&tag, &union_decl->membershape};
    union_decl->typeshape = Struct::Shape(&fidl_union, extra_handles);

    return true;
}

bool Library::CompileXUnion(XUnion* xunion_declaration) {
    Scope<StringView> scope;
    Scope<uint32_t> ordinal_scope;

    for (auto& member : xunion_declaration->members) {
        auto ordinal_result = ordinal_scope.Insert(member.ordinal->value, member.ordinal->location());
        if (!ordinal_result.ok())
            return Fail(member.ordinal->location(),
                        "Multiple xunion fields with the same ordinal; previous was at " +
                            ordinal_result.previous_occurrence().position());

        auto name_result = scope.Insert(member.name.data(), member.name);
        if (!name_result.ok())
            return Fail(member.name,
                        "Multiple xunion members with the same name; previous was at " +
                            name_result.previous_occurrence().position());

        if (!CompileTypeConstructor(member.type_ctor.get(), &member.fieldshape.Typeshape()))
            return false;
    }

    uint32_t max_member_handles;
    if (xunion_declaration->recursive) {
        max_member_handles = std::numeric_limits<uint32_t>::max();
    } else {
        // Member handles will be counted by CXUnionTypeShape.
        max_member_handles = 0u;
    }

    std::vector<FieldShape*> fields;
    for (auto& member : xunion_declaration->members) {
        fields.push_back(&member.fieldshape);
    }
    xunion_declaration->typeshape = XUnion::Shape(&fields, max_member_handles);

    return true;
}

bool Library::CompileLibraryName() {
    const std::regex pattern("^[a-z][a-z0-9]*$");
    for (const auto& part_view : library_name_) {
        std::string part = part_view;
        if (!std::regex_match(part, pattern)) {
            return Fail("Invalid library name part " + part);
        }
    }
    return true;
}

bool Library::CompileTypeConstructor(TypeConstructor* type_ctor, TypeShape* out_typeshape) {
    auto const& location = type_ctor->name.source_location();
    const Type* maybe_arg_type = nullptr;
    if (type_ctor->maybe_arg_type_ctor != nullptr) {
        if (!CompileTypeConstructor(type_ctor->maybe_arg_type_ctor.get(), nullptr))
            return false;
        maybe_arg_type = type_ctor->maybe_arg_type_ctor->type;
    }

    const Size* size = nullptr;
    if (type_ctor->maybe_size != nullptr) {
        if (!ResolveConstant(type_ctor->maybe_size.get(), &kSizeType))
            return Fail(location, "unable to parse size bound");
        size = static_cast<const Size*>(&type_ctor->maybe_size->Value());
    }

    if (!typespace_->Create(type_ctor->name,
                            maybe_arg_type,
                            size,
                            type_ctor->nullability,
                            &type_ctor->type))
        return false;

    if (out_typeshape)
        *out_typeshape = type_ctor->type->shape;
    return true;
}

template <typename DeclType, typename MemberType>
bool Library::ValidateBitsOrEnumMembers(DeclType* decl) {
    assert(decl != nullptr);

    constexpr const char* decl_type = std::is_same_v<DeclType, Enum> ? "enum" : "bits";

    Scope<std::string> name_scope;
    Scope<MemberType> value_scope;
    bool success = true;
    for (auto& member : decl->members) {
        assert(member.value != nullptr && "Compiler bug: member value is null!");

        if (!ResolveConstant(member.value.get(), decl->subtype_ctor->type)) {
            std::string failure_message = "unable to resolve ";
            failure_message += decl_type;
            failure_message += " member";
            return Fail(member.name, failure_message);
        }

        std::string name = NameIdentifier(member.name);
        auto name_result = name_scope.Insert(name, member.name);
        if (!name_result.ok()) {
            std::ostringstream msg_stream;
            msg_stream << "name of member " << name;
            msg_stream << " conflicts with previously declared member in the ";
            msg_stream << decl_type << " " << decl->GetName();

            // We can log the error and then continue validating for other issues in the decl
            success = Fail(member.name, msg_stream.str());
        }

        MemberType value = static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
        auto value_result = value_scope.Insert(value, member.name);
        if (!value_result.ok()) {
            std::ostringstream msg_stream;
            msg_stream << "value of member " << name;
            msg_stream << " conflicts with previously declared member ";
            msg_stream << NameIdentifier(value_result.previous_occurrence()) << " in the ";
            msg_stream << decl_type << " " << decl->GetName();

            // We can log the error and then continue validating other members for other bugs
            success = Fail(member.name, msg_stream.str());
        }
    }

    return success;
}

template <typename T>
static bool IsPowerOfTwo(T t) {
    if (t == 0) {
        return false;
    }
    if ((t & (t - 1)) != 0) {
        return false;
    }
    return true;
}

template <typename MemberType>
bool Library::ValidateBitsMembersAndCalcMask(Bits* bits_decl, MemberType* out_mask) {
    static_assert(std::is_unsigned<MemberType>::value && !std::is_same<MemberType, bool>::value,
                  "Bits members must be an unsigned integral type!");

    if (!ValidateBitsOrEnumMembers<Bits, MemberType>(bits_decl))
        return false;

    MemberType mask = 0u;
    bool success = true;
    for (auto& member : bits_decl->members) {
        MemberType value = static_cast<const NumericConstantValue<MemberType>&>(member.value->Value()).value;
        if (!IsPowerOfTwo(value)) {
            Fail(member.name, "bits members must be powers of two");
            success = false;
        }
        mask |= value;
    }
    *out_mask = mask;
    return success;
}

bool Library::Compile() {
    for (const auto& dep_library : dependencies_.dependencies()) {
        constants_.insert(dep_library->constants_.begin(), dep_library->constants_.end());
    }

    if (!CompileLibraryName())
        return false;

    if (!SortDeclarations())
        return false;

    for (Decl* decl : declaration_order_) {
        if (!CompileDecl(decl))
            return false;
    }

    for (Decl* decl : declaration_order_) {
        if (!VerifyDeclAttributes(decl))
            return false;
    }

    return error_reporter_->errors().size() == 0;
}

const std::set<Library*>& Library::dependencies() const {
    return dependencies_.dependencies();
}

} // namespace flat
} // namespace fidl