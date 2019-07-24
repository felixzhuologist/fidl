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
    std::ostringstream ProduceClient();

    enum class Transport {
        Channel,
        SocketControl,
    };

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

    struct NamedMessage {
        std::string c_name;
        std::string coded_name;
        const std::vector<flat::Struct::Member>& parameters;
        const TypeShape& typeshape;
    };
    
    struct NamedMethod {
        uint32_t ordinal;
        std::string ordinal_name;
        uint32_t generated_ordinal;
        std::string generated_ordinal_name;
        std::string identifier;
        std::string c_name;
        std::unique_ptr<NamedMessage> request;
        std::unique_ptr<NamedMessage> response;
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

    struct NamedInterface {
        std::string c_name;
        std::string discoverable_name;
        Transport transport;
        std::vector<NamedMethod> methods;
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

    struct NamedUnion {
        std::string name;
        const flat::Union& union_info;
    };

    struct NamedXUnion {
        std::string name;
        const flat::XUnion& xunion_info;
    };

    enum class StructKind {
        kMessage,
        kNonmessage,
    };

    uint32_t GetMaxHandlesFor(Transport transport, const TypeShape& typeshape);

    void GeneratePrologues();
    void GenerateEpilogues();

    void GenerateIntegerDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    void GenerateIntegerTypedef(types::PrimitiveSubtype subtype, StringView name);
    void GeneratePrimitiveDefine(StringView name, types::PrimitiveSubtype subtype, StringView value);
    void GenerateStringDefine(StringView name, StringView value);
    void GenerateStructTypedef(StringView name);

    void GenerateStructDeclaration(StringView name, const std::vector<Member>& members, StructKind kind);
    void GenerateTaggedUnionDeclaration(StringView name, const std::vector<Member>& members);
    void GenerateTaggedXUnionDeclaration(StringView name, const std::vector<Member>& members);

    std::map<const flat::Decl*, NamedBits>
    NameBits(const std::vector<std::unique_ptr<flat::Bits>>& bits_infos);
    std::map<const flat::Decl*, NamedConst>
    NameConsts(const std::vector<std::unique_ptr<flat::Const>>& const_infos);
    std::map<const flat::Decl*, NamedEnum>
    NameEnums(const std::vector<std::unique_ptr<flat::Enum>>& enum_infos);
    std::map<const flat::Decl*, NamedInterface>
    NameInterfaces(const std::vector<std::unique_ptr<flat::Interface>>& interface_infos);
    std::map<const flat::Decl*, NamedStruct>
    NameStructs(const std::vector<std::unique_ptr<flat::Struct>>& struct_infos);
    std::map<const flat::Decl*, NamedTable>
    NameTables(const std::vector<std::unique_ptr<flat::Table>>& table_infos);
    std::map<const flat::Decl*, NamedUnion>
    NameUnions(const std::vector<std::unique_ptr<flat::Union>>& union_infos);
    std::map<const flat::Decl*, NamedXUnion>
    NameXUnions(const std::vector<std::unique_ptr<flat::XUnion>>& xunion_infos);

    void ProduceBitsForwardDeclaration(const NamedBits& named_bits);
    void ProduceConstForwardDeclaration(const NamedConst& named_const);
    void ProduceEnumForwardDeclaration(const NamedEnum& named_enum);
    void ProduceInterfaceForwardDeclaration(const NamedInterface& named_interface);
    void ProduceStructForwardDeclaration(const NamedStruct& named_struct);
    void ProduceTableForwardDeclaration(const NamedTable& named_table);
    void ProduceUnionForwardDeclaration(const NamedUnion& named_union);
    void ProduceXUnionForwardDeclaration(const NamedXUnion& named_xunion);

    void ProduceInterfaceExternDeclaration(const NamedInterface& named_interface);

    void ProduceConstDeclaration(const NamedConst& named_const);
    void ProduceMessageDeclaration(const NamedMessage& named_message);
    void ProduceInterfaceDeclaration(const NamedInterface& named_interface);
    void ProduceStructDeclaration(const NamedStruct& named_struct);
    void ProduceUnionDeclaration(const NamedUnion& named_union);
    void ProduceXUnionDeclaration(const NamedXUnion& named_xunion);

    void ProduceInterfaceClientDeclaration(const NamedInterface& named_interface);
    void ProduceInterfaceClientImplementation(const NamedInterface& named_interface);

    void ProduceInterfaceServerDeclaration(const NamedInterface& named_interface);

    const flat::Library* library_;
    std::ostringstream file_;
};

} // namespace fidl

#endif