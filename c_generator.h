#ifndef C_GENERATOR_H_
#define C_GENERATOR_H_

#include <sstream>
#include <string>
#include <vector>

#include "flat_ast.h"
#include "string_view.h"

namespace fidl {

class CGenerator {
public:
    explicit CGenerator(const flat::Library* library)
        : library_(library) {}

    ~CGenerator() = default;

    std::ostringstream ProduceHeader();

private:
    struct NamedBits {
        std::string name;
        const flat::Bits& bits_info;
    };

    struct NamedConst {
        std::string name;
        const flat::Const& const_info;
    };

    struct NamedEnum {
        std::string name;
        const flat::Enum& enum_info;
    };

    void GeneratePrologues();
    void GenerateEpilogues();

    void GenerateIntegerDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    void GenerateIntegerTypedef(types::PrimitiveSubtype subtype, StringView name);
    void GeneratePrimitiveDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    void GenerateStringDefine(StringView name, StringView value);

    std::map<const flat::Decl*, NamedBits>
    NameBits(const std::vector<std::unique_ptr<flat::Bits>>& bits_infos);
    std::map<const flat::Decl*, NamedConst>
    NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos);
    std::map<const flat::Decl*, NamedEnum>
    NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos);

    void ProduceBitsForwardDeclaration(const NamedBits& named_bits);
    void ProduceConstForwardDeclaration(const NamedConst& named_const);
    void ProduceEnumForwardDeclaration(const NamedEnum& named_enum);

    void ProduceConstDeclaration(const NamedConst& named_const);

    const flat::Library* library_;
    std::ostringstream file_;
};

} // namespace fidl

#endif