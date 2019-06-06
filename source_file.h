#ifndef SOURCE_FILE_H_
#define SOURCE_FILE_H_

#include <string>
#include <utility>
#include <vector>

#include "string_view.h"

namespace fidl {

class SourceFile {
public:
    SourceFile(std::string filename, std::string data);
    virtual ~SourceFile() = default;

    StringView filename() const { return filename_; }
    StringView data() const { return data_; }

    struct Position {
        int line;
        int column;
    };

    // return the entire line containing the given stringview, and write out
    // its position into position_out. fails if view is not a part of this source file
    virtual StringView LineContaining(StringView view, Position* position_out) const;

private:
    std::string filename_;
    std::string data_;
    std::vector<StringView> lines_;
};

} // namespace fidl

#endif // SOURCE_FILE_H_
