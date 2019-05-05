#include "json_generator.h"

namespace fidl {

namespace {

constexpr const char* kIndent = "  ";

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

} // namespace

void JSONGenerator::GenerateEOF() {
    EmitNewLine(&json_file_);
}

// template <typename Type>
// void JSONGenerator::GenerateObjectMember(StringView key, const Type& value, Position position) {
//     GenerateObjectPunctuation(position);
//     EmitObjectKey(&json_file_, indent_level_, key);
//     Generate(value);
// }

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
    //     GenerateObjectMember("version", StringView("0.0.1"), Position::kFirst);
    //     GenerateObjectMember("name", LibraryName(library_, "."));
    });
    GenerateEOF();
    return std::move(json_file_);
} 

} // namespace fidl
