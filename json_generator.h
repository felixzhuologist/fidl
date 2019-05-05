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

    void GenerateObjectPunctuation(Position position);

    template <typename Type>
    void GenerateObjectMember(StringView key, const Type& value, Position position = Position::kSubsequent);

    template <typename T>
    void Generate(const std::unique_ptr<T>& value);

    void Generate(bool value);
    void Generate(StringView value);
    void Generate(SourceLocation value);
    void Generate(NameLocation value);
    void Generate(uint32_t value);

    template <typename Callback>
    void GenerateObject(Callback callback);

    const flat::Library* library_;
    int indent_level_;
    std::ostringstream json_file_;
};

} // namespace fidl

#endif // JSON_GENERATOR_H_
