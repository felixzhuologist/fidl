#ifndef TYPES_H_
#define TYPES_H_

namespace fidl {
namespace types {

typedef uint32_t zx_obj_type_t;

#define ZX_OBJ_TYPE_NONE            ((zx_obj_type_t)0u)
#define ZX_OBJ_TYPE_PROCESS         ((zx_obj_type_t)1u)
#define ZX_OBJ_TYPE_THREAD          ((zx_obj_type_t)2u)
#define ZX_OBJ_TYPE_VMO             ((zx_obj_type_t)3u)
#define ZX_OBJ_TYPE_CHANNEL         ((zx_obj_type_t)4u)
#define ZX_OBJ_TYPE_EVENT           ((zx_obj_type_t)5u)
#define ZX_OBJ_TYPE_PORT            ((zx_obj_type_t)6u)
#define ZX_OBJ_TYPE_INTERRUPT       ((zx_obj_type_t)9u)
#define ZX_OBJ_TYPE_PCI_DEVICE      ((zx_obj_type_t)11u)
#define ZX_OBJ_TYPE_LOG             ((zx_obj_type_t)12u)
#define ZX_OBJ_TYPE_SOCKET          ((zx_obj_type_t)14u)
#define ZX_OBJ_TYPE_RESOURCE        ((zx_obj_type_t)15u)
#define ZX_OBJ_TYPE_EVENTPAIR       ((zx_obj_type_t)16u)
#define ZX_OBJ_TYPE_JOB             ((zx_obj_type_t)17u)
#define ZX_OBJ_TYPE_VMAR            ((zx_obj_type_t)18u)
#define ZX_OBJ_TYPE_FIFO            ((zx_obj_type_t)19u)
#define ZX_OBJ_TYPE_GUEST           ((zx_obj_type_t)20u)
#define ZX_OBJ_TYPE_VCPU            ((zx_obj_type_t)21u)
#define ZX_OBJ_TYPE_TIMER           ((zx_obj_type_t)22u)
#define ZX_OBJ_TYPE_IOMMU           ((zx_obj_type_t)23u)
#define ZX_OBJ_TYPE_BTI             ((zx_obj_type_t)24u)
#define ZX_OBJ_TYPE_PROFILE         ((zx_obj_type_t)25u)
#define ZX_OBJ_TYPE_PMT             ((zx_obj_type_t)26u)
#define ZX_OBJ_TYPE_SUSPEND_TOKEN   ((zx_obj_type_t)27u)
#define ZX_OBJ_TYPE_PAGER           ((zx_obj_type_t)28u)
#define ZX_OBJ_TYPE_EXCEPTION       ((zx_obj_type_t)29u)

enum struct Nullability {
    kNullable,
    kNonnullable,
};

// Note: must keep in sync with userspace lib internal.h FidlHandleSubtype.
enum struct HandleSubtype : zx_obj_type_t {
    // special case to indicate subtype is not specified.
    kHandle = ZX_OBJ_TYPE_NONE,

    kProcess = ZX_OBJ_TYPE_PROCESS,
    kThread = ZX_OBJ_TYPE_THREAD,
    kVmo = ZX_OBJ_TYPE_VMO,
    kChannel = ZX_OBJ_TYPE_CHANNEL,
    kEvent = ZX_OBJ_TYPE_EVENT,
    kPort = ZX_OBJ_TYPE_PORT,
    kInterrupt = ZX_OBJ_TYPE_INTERRUPT,
    kLog = ZX_OBJ_TYPE_LOG,
    kSocket = ZX_OBJ_TYPE_SOCKET,
    kResource = ZX_OBJ_TYPE_RESOURCE,
    kEventpair = ZX_OBJ_TYPE_EVENTPAIR,
    kJob = ZX_OBJ_TYPE_JOB,
    kVmar = ZX_OBJ_TYPE_VMAR,
    kFifo = ZX_OBJ_TYPE_FIFO,
    kGuest = ZX_OBJ_TYPE_GUEST,
    kTimer = ZX_OBJ_TYPE_TIMER,
    kBti = ZX_OBJ_TYPE_BTI,
    kProfile = ZX_OBJ_TYPE_PROFILE,
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
