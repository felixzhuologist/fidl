#include "attributes.h"

namespace fidl {

bool AttributesBuilder::Insert(std::unique_ptr<raw::Attribute> attribute) {
    if (!names_.emplace(attribute->name).second) {
        std::string message("duplicate attribute with name '");
        message.append(attribute->name);
        message.append("'");
        error_reporter_->ReportError(attribute->location(), message);
        return false;
    }
    attributes_.push_back(std::move(attribute));
    return true;
}

std::vector<std::unique_ptr<raw::Attribute>> AttributesBuilder::Done() {
    return std::move(attributes_);
}

} // namespace fidl
