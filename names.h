#ifndef NAMES_H_
#define NAMES_H_

#include <sstream>
#include <string>

#include "flat_ast.h"
#include "raw_ast.h"
#include "string_view.h"
#include "types.h"

namespace fidl {

std::string StringJoin(const std::vector<StringView>& strings, StringView separator);

std::string NameIdentifier(SourceLocation name);
std::string NameName(const flat::Name& name, StringView library_separator, StringView separator);

std::string NameLibrary(const std::vector<StringView>& library_name);
std::string NameLibraryCHeader(const std::vector<StringView>& library_name);

std::string NamePrimitiveCType(types::PrimitiveSubtype subtype);
std::string NamePrimitiveSubtype(types::PrimitiveSubtype subtype);
std::string NamePrimitiveIntegerCConstantMacro(types::PrimitiveSubtype type);

std::string NameRawLiteralKind(raw::Literal::Kind kind);

std::string NameFlatConstantKind(flat::Constant::Kind kind);
std::string NameFlatTypeKind(flat::Type::Kind kind);
std::string NameUnionTag(std::string_view union_name, const flat::Union::Member& member);
std::string NameXUnionTag(std::string_view xunion_name, const flat::XUnion::Member& member);
std::string NameFlatConstant(const flat::Constant* constant);
std::string NameFlatTypeConstructor(const flat::TypeConstructor* type_ctor);
std::string NameFlatType(const flat::Type* type);
std::string NameFlatCType(const flat::Type* type, flat::Decl::Kind decl_kind);
std::string NameInterface(const flat::Interface& interface);
std::string NameDiscoverable(const flat::Interface& interface);
std::string NameMethod(StringView interface_name, const flat::Interface::Method& method);
std::string NameOrdinal(StringView method_name);
std::string NameGenOrdinal(StringView method_name);
std::string NameMessage(StringView method_name, types::MessageKind kind);

std::string NameTable(StringView type_name);
std::string NamePointer(StringView name);
std::string NameMembers(StringView name);
std::string NameFields(StringView name);

std::string NameCodedStruct(const flat::Struct* struct_decl);
std::string NameCodedArray(StringView element_name, uint64_t size);
std::string NameCodedVector(StringView element_name, uint64_t max_size,
                            types::Nullability nullability);
std::string NameCodedString(uint64_t max_size, types::Nullability nullability);

} // namespace fidl

#endif // NAMES_H_
