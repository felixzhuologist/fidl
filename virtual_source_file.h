#ifndef VIRTUAL_SOURCE_FILE_H_
#define VIRTUAL_SOURCE_FILE_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source_file.h"
#include "source_location.h"
#include "string_view.h"

namespace fidl {

class VirtualSourceFile : public SourceFile {
public:
    VirtualSourceFile(std::string filename) : SourceFile(filename, "") {}
    virtual ~VirtualSourceFile() = default;

    StringView LineContaining(StringView view, Position* position_out) const override;

    SourceLocation AddLine(const std::string& line);

private:
    std::vector<std::unique_ptr<std::string>> virtual_lines_;
};

} // namespace fidl

#endif // VIRTUAL_SOURCE_FILE_H_
