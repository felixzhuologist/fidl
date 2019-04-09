#include <iostream>

#include "flat_ast.h"

namespace fidl {
namespace flat {

std::string StringJoin(const std::vector<StringView>& strings, StringView separator) {
    std::string result;
    bool first = true;
    for (const auto& part : strings) {
        if (!first) {
            result += separator;
        }
        first = false;
        result += part;
    }
    return result;
}

std::string LibraryName(const Library* library, StringView separator) {
    if (library != nullptr) {
        return StringJoin(library->name(), separator);
    }
    return std::string();
}

std::string NameIdentifier(SourceLocation name) {
    // TODO(TO-704) C name escaping and ergonomics.
    return name.data();
}

std::string NameName(const flat::Name& name, StringView library_separator, StringView name_separator) {
    std::string compiled_name("");
    if (name.library() != nullptr) {
        compiled_name += LibraryName(name.library(), library_separator);
        compiled_name += name_separator;
    }
    compiled_name += name.name_part();
    return compiled_name;
}

uint32_t AlignTo(uint64_t size, uint64_t alignment) {
    return static_cast<uint32_t>(
        std::min((size + alignment - 1) & -alignment,
                 static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())));
}

uint32_t ClampedMultipy(uint32_t a, uint32_t b) {
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

    for (FieldShape* field : *fields) {
        TypeShape typeshape = field->Typeshape();
        alignment = std::max(alignment, typeshape.Alignment());
        size = AlignTo(size, typeshape.Alignment());
        field->SetOffset(size);
        size += typeshape.Size();
        depth = std::max(depth, field->Depth());
        max_handles = ClampedAdd(max_handles, typeshape.MaxHandles());
        max_out_of_line = ClampedAdd(max_out_of_line, typeshape.MaxOutOfLine());
    }

    max_handles = ClampedAdd(max_handles, extra_handles);

    size = AlignTo(size, alignment);

    if (fields->empty()) {
        assert(size == 0);
        assert(alignment == 1);

        // empty structs are defined to have a size of 1 byte
        size = 1;
    }

    return TypeShape(size, alignment, depth, max_handles, max_out_of_line);
}

// why is clamped add/multiply OK for calculating sizes? it avoids overflow
// but then the calculated is size (the element sizes don't get clamped magically...)
TypeShape PointerTypeShape(const TypeShape& element, uint32_t max_element_count = 1u) {
    uint32_t depth = std::numeric_limits<uint32_t>::max();
    // we may not have computed the typeshape for element if this is a recursive data structure
    if (element.Size() > 0 && element.Depth() < std::numeric_limits<uint32_t>::max())
        depth = ClampedAdd(element.Depth(), 1);

    uint32_t elements_size = ClampedMultipy(element.Size(), max_element_count);
    // out of line data is aligned to 8 bytes
    elements_size = AlignTo(elements_size, 8);
    // elements may carry their own out of line dat
    uint32_t elements_out_of_line = ClampedMultipy(element.MaxOutOfLine(), max_element_count);
    uint32_t max_out_of_line = ClampedAdd(elements_size, elements_out_of_line);

    uint32_t max_handles = ClampedAdd(element.MaxHandles(), max_element_count);

    return TypeShape(8u, 8u, depth, max_handles, max_out_of_line);
}

TypeShape ArrayType::Shape(TypeShape element, uint32_t count) {
    return TypeShape(ClampedMultipy(element.Size(), count),
                     element.Alignment(),
                     element.Depth(),
                     ClampedMultipy(element.MaxHandles(), count),
                     ClampedMultipy(element.MaxOutOfLine(), count));
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

Libraries::Libraries() {

}

} // namespace flat
} // namespace fidl