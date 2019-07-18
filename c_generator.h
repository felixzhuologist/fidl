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

    struct Member {
        flat::Type::Kind kind;
        flat::Decl::Kind decl_kind;
        std::string type;
        std::string name;

        // Name of the element type for sequential collections.
        // For (multidimensional-) arrays, it names the inner-most type.
        // For FIDL vector<T>, it names T.
        std::string element_type;
        std::vector<uint32_t> array_counts;
        types::Nullability nullability;
        // Bound on the element count for string and vector collection types.
        // When there is no limit, its value is UINT32_MAX.
        // Method parameters are pre-validated against this bound at the beginning of a FIDL call.
        uint32_t max_num_elements;
    };

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

    struct NamedStruct {
        std::string c_name;
        std::string coded_name;
        const flat::Struct& struct_info;
    };

    struct NamedTable {
        std::string c_name;
        std::string coded_name;
        const flat::Table& table_info;
    };

    enum class StructKind {
        kMessage,
        kNonmessage,
    };

    void GeneratePrologues();
    void GenerateEpilogues();

    void GenerateIntegerDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    void GenerateIntegerTypedef(types::PrimitiveSubtype subtype, StringView name);
    void GeneratePrimitiveDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    void GenerateStringDefine(StringView name, StringView value);
    void GenerateStructTypedef(StringView name);

    void GenerateStructDeclaration(StringView name, const std::vector<Member>& members, StructKind kind);

    std::map<const flat::Decl*, NamedBits>
    NameBits(const std::vector<std::unique_ptr<flat::Bits>>& bits_infos);
    std::map<const flat::Decl*, NamedConst>
    NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos);
    std::map<const flat::Decl*, NamedEnum>
    NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos);
    std::map<const flat::Decl*, NamedStruct>
    NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos);
    std::map<const flat::Decl*, NamedTable>
    NameTables(const std::vector<std::unique_ptr<flat::Table>>& table_infos);

    void ProduceBitsForwardDeclaration(const NamedBits& named_bits);
    void ProduceConstForwardDeclaration(const NamedConst& named_const);
    void ProduceEnumForwardDeclaration(const NamedEnum& named_enum);
    void ProduceStructForwardDeclaration(const NamedStruct& named_struct);
    void ProduceTableForwardDeclaration(const NamedTable& named_table);

    void ProduceConstDeclaration(const NamedConst& named_const);
    void ProduceStructDeclaration(const NamedStruct& named_struct);

    const flat::Library* library_;
    std::ostringstream file_;
};

} // namespace fidl

#endif