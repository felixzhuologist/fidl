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

std::string NameLibrary(const std::vector<StringView>& library_name) {
    return StringJoin(library_name, ".");
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
        return true;
    }

private:
    Library* library_;
    TypeDecl* type_decl_;
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

bool Library::Fail(StringView message) {
    error_reporter_->ReportError(message);
    return false;
}

bool Library::Fail(const SourceLocation& location, StringView message) {
    error_reporter_->ReportError(location, message);
    return false;
}

bool Library::CompileCompoundIdentifier(const raw::CompoundIdentifier* compound_identifier,
                                        SourceLocation location, Name* name_out) {
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

    auto filename = location.source_file().filename();
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

bool Library::ConsumeConstant(std::unique_ptr<raw::Constant> raw_constant, SourceLocation location,
                              std::unique_ptr<Constant>* out_constant) {
    switch (raw_constant->kind) {
    case raw::Constant::Kind::kIdentifier: {
        auto identifier = static_cast<raw::IdentifierConstant*>(raw_constant.get());
        Name name;
        if (!CompileCompoundIdentifier(identifier->identifier.get(), location, &name))
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
                                     SourceLocation location,
                                     std::unique_ptr<TypeConstructor>* out_type_ctor) {
    Name name;
    if (!CompileCompoundIdentifier(raw_type_ctor->identifier.get(), location, &name))
        return false;

    std::unique_ptr<TypeConstructor> maybe_arg_type_ctor;
    if (raw_type_ctor->maybe_arg_type_ctor != nullptr) {
        if (!ConsumeTypeConstructor(std::move(raw_type_ctor->maybe_arg_type_ctor), location, &maybe_arg_type_ctor))
            return false;
    }

    std::unique_ptr<Constant> maybe_size;
    if (raw_type_ctor->maybe_size != nullptr) {
        if (!ConsumeConstant(std::move(raw_type_ctor->maybe_size), location, &maybe_size))
            return false;
    }

    *out_type_ctor = std::make_unique<TypeConstructor>(
        std::move(name),
        std::move(maybe_arg_type_ctor),
        std::move(maybe_size),
        raw_type_ctor->nullability);
    return true;
}

bool Library::ConsumeConstDeclaration(std::unique_ptr<raw::ConstDeclaration> const_declaration) {
    auto location = const_declaration->identifier->location();
    auto name = Name(this, location);

    std::unique_ptr<TypeConstructor> type_ctor;
    if (!ConsumeTypeConstructor(std::move(const_declaration->type_ctor), location, &type_ctor))
        return false;

    std::unique_ptr<Constant> constant;
    if (!ConsumeConstant(std::move(const_declaration->constant), location, &constant))
        return false;

    const_declarations_.push_back(std::make_unique<Const>(std::move(name), std::move(type_ctor), std::move(constant)));

    auto decl = const_declarations_.back().get();
    RegisterConst(decl);
    return RegisterDecl(decl);
} 

bool Library::ConsumeFile(std::unique_ptr<raw::File> file) {
    // validate the library name of this file
    std::vector<StringView> new_name;
    for (const auto& part : file->library_name->components) {
        new_name.push_back(part->location().data());
    }
    // when would this every not be true? it doesn't look like we initialize library
    // name before calling ConsumeFile, and we only call ConsumeFile once.
    if (!library_name_.empty()) {
        if (new_name != library_name_) {
            return Fail(file->library_name->components[0]->location(),
                        "Tow files in the library disagree about the library name");
        }
    } else {
        library_name_ = new_name;
    }

    auto const_declaration_list = std::move(file->const_declaration_list);
    for (auto& const_declaration : const_declaration_list) {
        if (!ConsumeConstDeclaration(std::move(const_declaration))) {
            return false;
        }
    }

    return true;
}

} // namespace flat
} // namespace fidl