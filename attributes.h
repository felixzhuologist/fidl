#ifndef ATTRIBUTES_H_
#define ATTRIBUTES_H_

#include <set>
#include <vector>

#include "error_reporter.h"
#include "raw_ast.h"

namespace fidl {

class AttributesBuilder {
public:
    AttributesBuilder(ErrorReporter* error_reporter)
        : error_reporter_(error_reporter) {}

    AttributesBuilder(ErrorReporter* error_reporter, std::vector<std::unique_ptr<raw::Attribute>> attributes)
        : error_reporter_(error_reporter), attributes_(std::move(attributes)) {
        for (auto& attribute : attributes_) {
            names_.emplace(attribute->name);
        }
    }

    bool Insert(std::unique_ptr<raw::Attribute> attribute);
    std::vector<std::unique_ptr<raw::Attribute>> Done();

private:
    ErrorReporter* error_reporter_;
    std::vector<std::unique_ptr<raw::Attribute>> attributes_;
    std::set<std::string> names_;
};

} // namespace fidl

#endif // ATTRIBUTES_H_