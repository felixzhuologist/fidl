#ifndef JSON_GENERATOR_H_
#define JSON_GENERATOR_H_

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "flat_ast.h"
#include "string_view.h"

namespace fidl {

struct NameLocation {
    explicit NameLocation(const SourceLocation& source_location)
        : filename(source_location.source_file().filename()) {
        source_location.SourceLine(&position);
    }
    explicit NameLocation(const flat::Name& name) : NameLocation(name.source_location()) {}
    const std::string filename;
    SourceFile::Position position;
};

class JSONGenerator {
public:
    explicit JSONGenerator(const flat::Library* library)
        : library_(library) {}

    ~JSONGenerator() = default;

    std::ostringstream Produce();

private:
    enum class Position {
        kFirst,
        kSubsequent,
    };

    void GenerateEOF();

    template <typename Iterator>
    void GenerateArray(Iterator begin, Iterator end);

    template <typename Collection>
    void GenerateArray(const Collection& collection);

    void GenerateObjectPunctuation(Position position);

    template <typename Type>
    void GenerateObjectMember(StringView key, const Type& value, Position position = Position::kSubsequent);

    template <typename T>
    void Generate(const std::unique_ptr<T>& value);

    template <typename T>
    void Generate(const std::vector<T>& value);

    void Generate(bool value);
    void Generate(StringView value);
    void Generate(SourceLocation value);
    void Generate(NameLocation value);
    void Generate(uint32_t value);

    void Generate(types::Nullability value);
    void Generate(types::PrimitiveSubtype value);

    void Generate(const raw::Identifier& value);
    void Generate(const raw::Literal& value);
    void Generate(const raw::Attribute& value);
    void Generate(const raw::AttributeList& value);
    void Generate(const raw::Ordinal& value);

    void Generate(const flat::Name& value);
    void Generate(const flat::Type* value);
    void Generate(const flat::Constant& value);
    void Generate(const flat::Const& value);
    void Generate(const flat::Bits& value);
    void Generate(const flat::Bits::Member& value);
    void Generate(const flat::Enum& value);
    void Generate(const flat::Enum::Member& value);
    void Generate(const flat::Struct& value);
    void Generate(const flat::Struct::Member& value);
    void Generate(const flat::Table& value);
    void Generate(const flat::Table::Member& value);
    void Generate(const flat::Union& value);
    void Generate(const flat::Union::Member& value);
    void Generate(const flat::XUnion& value);
    void Generate(const flat::XUnion::Member& value);
    void Generate(const flat::Library* library);

    void GenerateDeclarationsEntry(int count, const flat::Name& name, StringView decl);
    void GenerateDeclarationsMember(const flat::Library* library,
                               Position position = Position::kSubsequent);

    template <typename Callback>
    void GenerateObject(Callback callback);

    const flat::Library* library_;
    int indent_level_;
    std::ostringstream json_file_;
};

} // namespace fidl

#endif // JSON_GENERATOR_H_
