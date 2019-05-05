#ifndef JSON_GENERATOR_H_
#define JSON_GENERATOR_H_

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "flat_ast.h"
#include "string_view.h"

namespace fidl {

class JSONGenerator {
public:
    explicit JSONGenerator(const flat::Library* library)
        : library_(library) {}

    ~JSONGenerator() = default;

    std::ostringstream Produce();

private:
    void GenerateEOF();
    
    template <typename Callback>
    void GenerateObject(Callback callback);

    const flat::Library* library_;
    int indent_level_;
    std::ostringstream json_file_;
};

} // namespace fidl

#endif // JSON_GENERATOR_H_
