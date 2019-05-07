#include "json_generator.h"
#include "names.h"

namespace fidl {

namespace {

constexpr const char* kIndent = "  ";

void EmitBoolean(std::ostream* file, bool value) {
    if (value)
        *file << "true";
    else
        *file << "false";
}

void EmitString(std::ostream* file, StringView value) {
    *file << "\"";

    for (size_t i = 0; i < value.size(); i++) {
        const char c = value[i];
        switch (c) {
            case '"':
                *file << "\\\"";
                break;
            case '\\':
                *file << "\\\\";
                break;
            case '\n':
                *file << "\\n";
                break;
            default:
                *file << c;
                break;
        }
    }
    *file << "\"";
}

void EmitLiteral(std::ostream* file, StringView value) {
    file->rdbuf()->sputn(value.data(), value.size());
}

void EmitUint32(std::ostream* file, uint32_t value) {
    *file << value;
}

void EmitNewLine(std::ostream* file) {
    *file << "\n";
}

void EmitNewlineAndIdent(std::ostream* file, int indent_level) {
    *file << "\n";
    while (indent_level--)
        *file << kIndent;
}

void EmitObjectBegin(std::ostream* file) {
    *file << "{";
}

void EmitObjectEnd(std::ostream* file) {
    *file << "}";
}

void EmitObjectSeparator(std::ostream* file, int indent_level) {
    *file << ",";
    EmitNewlineAndIdent(file, indent_level);
}

void EmitObjectKey(std::ostream* file, int indent_level, StringView key) {
    EmitString(file, key);
    *file << ": ";
}

void EmitArrayBegin(std::ostream* file) {
    *file << "[";
}

void EmitArraySeparator(std::ostream* file, int indent_level) {
    *file << ",";
    EmitNewlineAndIdent(file, indent_level);
}

void EmitArrayEnd(std::ostream* file) {
    *file << "]";
}

} // namespace

void JSONGenerator::GenerateEOF() {
    EmitNewLine(&json_file_);
}

template <typename Iterator>
void JSONGenerator::GenerateArray(Iterator begin, Iterator end) {
    EmitArrayBegin(&json_file_);

    if (begin != end)
        EmitNewlineAndIdent(&json_file_, ++indent_level_);

    for (Iterator it = begin; it != end; ++it) {
        if (it != begin)
            EmitArraySeparator(&json_file_, indent_level_);
        Generate(*it);
    }

    // when would this ever be true?
    if (begin != end)
        EmitNewlineAndIdent(&json_file_, --indent_level_);

    EmitArrayEnd(&json_file_);
}

template <typename Collection>
void JSONGenerator::GenerateArray(const Collection& collection) {
    GenerateArray(collection.begin(), collection.end());
}

void JSONGenerator::GenerateObjectPunctuation(Position position) {
    switch (position) {
    case Position::kFirst:
        EmitNewlineAndIdent(&json_file_, ++indent_level_);
        break;
    case Position::kSubsequent:
        EmitObjectSeparator(&json_file_, indent_level_);
        break;
    }
}

template <typename T>
void JSONGenerator::Generate(const std::unique_ptr<T>& value) {
    Generate(*value);
}

template <typename T>
void JSONGenerator::Generate(const std::vector<T>& value) {
    GenerateArray(value);
}

void JSONGenerator::Generate(bool value) {
    EmitBoolean(&json_file_, value);
}

void JSONGenerator::Generate(StringView value) {
    EmitString(&json_file_, value);
}

void JSONGenerator::Generate(SourceLocation value) {
    EmitString(&json_file_, value.data());
}

void JSONGenerator::Generate(NameLocation value) {
    GenerateObject([&]() {
        GenerateObjectMember("filename", value.filename, Position::kFirst);
        GenerateObjectMember("line", (uint32_t)value.position.line);
        GenerateObjectMember("column", (uint32_t)value.position.column);
    });
}

void JSONGenerator::Generate(uint32_t value) {
    EmitUint32(&json_file_, value);
}

void JSONGenerator::Generate(types::Nullability value) {
    switch(value) {
    case types::Nullability::kNullable:
        EmitBoolean(&json_file_, true);
        break;
    case types::Nullability::kNonnullable:
        EmitBoolean(&json_file_, false);
        break;
    }
}

void JSONGenerator::Generate(types::PrimitiveSubtype value) {
    EmitString(&json_file_, NamePrimitiveSubtype(value));
}

void JSONGenerator::Generate(const raw::Identifier& value) {
    EmitString(&json_file_, value.location().data());
}

void JSONGenerator::Generate(const raw::Literal& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameRawLiteralKind(value.kind), Position::kFirst);

        switch (value.kind) {
        case raw::Literal::Kind::kString: {
            auto type = static_cast<const raw::StringLiteral*>(&value);
            EmitObjectSeparator(&json_file_, indent_level_);
            EmitObjectKey(&json_file_, indent_level_, "value");
            EmitLiteral(&json_file_, type->location().data());
            break;
        }
        case raw::Literal::Kind::kNumeric: {
            auto type = static_cast<const raw::NumericLiteral*>(&value);
            GenerateObjectMember("value", type->location().data());
            break;
        }
        // why not generate anything for bools?
        case raw::Literal::Kind::kTrue: {
            break;
        }
        case raw::Literal::Kind::kFalse: {
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Constant& value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameFlatConstantKind(value.kind), Position::kFirst);
        switch (value.kind) {
        case flat::Constant::Kind::kIdentifier: {
            auto type = static_cast<const flat::IdentifierConstant*>(&value);
            GenerateObjectMember("identifier", type->name);
            break;
        }
        case flat::Constant::Kind::kLiteral: {
            auto type = static_cast<const flat::LiteralConstant*>(&value);
            GenerateObjectMember("literal", type->literal);
            break;
        }
        case flat::Constant::Kind::kSynthesized: {
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Type* value) {
    GenerateObject([&]() {
        GenerateObjectMember("kind", NameFlatTypeKind(value->kind), Position::kFirst);

        switch (value->kind) {
        case flat::Type::Kind::kArray: {
            auto type = static_cast<const flat::ArrayType*>(value);
            GenerateObjectMember("element_type", type->element_type);
            GenerateObjectMember("element_count", type->element_count->value);
            break;
        }
        case flat::Type::Kind::kVector: {
            auto type = static_cast<const flat::VectorType*>(value);
            GenerateObjectMember("element_type", type->element_type);
            if (*type->element_count < flat::Size::Max())
                GenerateObjectMember("maybe_element_count", type->element_count->value);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        case flat::Type::Kind::kString: {
            auto type = static_cast<const flat::StringType*>(value);
            if (*type->max_size < flat::Size::Max())
                GenerateObjectMember("maybe_element_count", type->max_size->value);
            GenerateObjectMember("nullable", type->nullability);
            break;   
        }
        case flat::Type::Kind::kPrimitive: {
            auto type = static_cast<const flat::PrimitiveType*>(value);
            GenerateObjectMember("subtype", type->subtype);
            break;
        }
        case flat::Type::Kind::kIdentifier: {
            auto type = static_cast<const flat::IdentifierType*>(value);
            GenerateObjectMember("identifier", type->name);
            GenerateObjectMember("nullable", type->nullability);
            break;
        }
        }
    });
}

void JSONGenerator::Generate(const flat::Name& value) {
    Generate(NameName(value, ".", "/"));
}

void JSONGenerator::Generate(const flat::Const& value) {
    GenerateObject([&]() {
        GenerateObjectMember("name", value.name, Position::kFirst);
        GenerateObjectMember("location", NameLocation(value.name));
        GenerateObjectMember("type", value.type_ctor->type);
        GenerateObjectMember("value", value.value);
    });
}

void JSONGenerator::Generate(const flat::Library* library) {
    GenerateObject([&]() {
        auto library_name = flat::LibraryName(library, ".");
        GenerateObjectMember("name", library_name, Position::kFirst);
        GenerateDeclarationsMember(library);
    });
}

void JSONGenerator::GenerateDeclarationsEntry(int count, const flat::Name& name, StringView decl) {
    if (count == 0)
        EmitNewlineAndIdent(&json_file_, ++indent_level_);
    else
        EmitObjectSeparator(&json_file_, indent_level_);
    EmitObjectKey(&json_file_, indent_level_, NameName(name, ".", "/"));
    EmitString(&json_file_, decl);
}

void JSONGenerator::GenerateDeclarationsMember(const flat::Library* library, Position position) {
    GenerateObjectPunctuation(position);
    EmitObjectKey(&json_file_, indent_level_, "declarations");
    GenerateObject([&]() {
        int count = 0;
        for (const auto& decl : library->const_declarations_)
            GenerateDeclarationsEntry(count++, decl->name, "const");
    });
}

template <typename Type>
void JSONGenerator::GenerateObjectMember(StringView key, const Type& value, Position position) {
    GenerateObjectPunctuation(position);
    EmitObjectKey(&json_file_, indent_level_, key);
    Generate(value);
}

template <typename Callback>
void JSONGenerator::GenerateObject(Callback callback) {
    int original_indent_level = indent_level_;

    EmitObjectBegin(&json_file_);

    callback();

    // shouldn't this be an error?
    if (indent_level_ > original_indent_level)
        EmitNewlineAndIdent(&json_file_, --indent_level_);

    EmitObjectEnd(&json_file_);
}

std::ostringstream JSONGenerator::Produce() {
    indent_level_ = 0;
    GenerateObject([&]() {
        GenerateObjectMember("version", StringView("0.0.1"), Position::kFirst);
        GenerateObjectMember("name", LibraryName(library_, "."));

        std::vector<flat::Library*> dependencies;
        for (const auto& dep_library : library_->dependencies()) {
            dependencies.push_back(dep_library);
        }
        GenerateObjectMember("library_dependencies", dependencies);

        GenerateObjectMember("const_declarations", library_->const_declarations_);

        std::vector<std::string> declaration_order;
        for (flat::Decl* decl : library_->declaration_order_) {
            if (decl->kind == flat::Decl::Kind::kStruct) {
                auto struct_decl = static_cast<flat::Struct*>(decl);
                if (struct_decl->anonymous)
                    continue;
            }
            if (decl->name.library() == library_)
                declaration_order.push_back(NameName(decl->name, ".", "/"));
        }
        GenerateObjectMember("declaration_order", declaration_order);

        GenerateDeclarationsMember(library_);
    });
    GenerateEOF();
    return std::move(json_file_);
} 

} // namespace fidl
