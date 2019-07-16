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

private:
    struct NamedConst {
        std::string name;
        const flat::Const& const_info;
    };
    
    std::ostringstream ProduceHeader();

    void GeneratePrologues();
    void GenerateEpilogues();

    const flat::Library* library_;
    std::ostringstream file_;
};

} // namespace fidl

#endif