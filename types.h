#ifndef TYPES_H_
#define TYPES_H_

namespace fidl {
namespace types {

enum struct Nullability {
    kNullable,
    kNonnullable,
};

enum struct PrimitiveSubtype {
    kBool,
    kInt8,
    kInt16,
    kInt32,
    kInt64,
    kUint8,
    kUint16,
    kUint32,
    kUint64,
    kFloat32,
    kFloat64,
};

enum struct MessageKind {
    kRequest,
    kResponse,
    kEvent,
};

} // namespace types
} // namespace fidl

#endif // TYPES_H_
