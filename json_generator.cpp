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

} // namespace

void JSONGenerator::GenerateEOF() {
    EmitNewLine(&json_file_);
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
    });
    GenerateEOF();
    return std::move(json_file_);
} 

} // namespace fidl
