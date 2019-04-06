#ifndef SOURCE_LOCATION_H_
#define SOURCE_LOCATION_H_

#include "source_manager.h"
#include "string_view.h"

namespace fidl {

class SourceLocation {
public: 
    SourceLocation(StringView data, const SourceFile& source_file)
        : data_(data), source_file_(&source_file) {}

    SourceLocation() : data_(StringView()), source_file_(nullptr) {}

    bool valid() const { return source_file_ != nullptr; }

    const StringView& data() const { return data_; }
    const SourceFile& source_file() const { return *source_file_; }

    // Return entire line from file containing this sourcelocation, and write
    // out its position to position_out
    StringView SourceLine(SourceFile::Position* position_out) const;

    // Return string displaying this sourcelocation's position as "[filename]:[line]:[col]""
    std::string position() const;

private:
    StringView data_;
    const SourceFile* source_file_;
};

} // namespace fidl

#endif // SOURCE_LOCATION_H_
