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
    virtual ~SourceFile();

    StringView filename() const { return filename_; }
    StringView data() const { return data_; }

private:
    std::string filename_;
    std::string data_;
    std::vector<StringView> lines_;
};

} // namespace fidl

#endif // SOURCE_FILE_H_
