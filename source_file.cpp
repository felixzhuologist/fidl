#include "source_file.h"

namespace fidl {

SourceFile::SourceFile(std::string filename, std::string data)
    : filename_(std::move(filename)), data_(std::move(data)) {
        size_t size = 0u;
        auto start_of_line = data_.cbegin();

        for (auto it = data_.cbegin(); it < data_.cend(); ++it) {
            ++size;
            if (*it == '\n' || *it == '\0') {
                auto& position = *start_of_line;
                lines_.push_back(StringView(&position, size));

                size = 0u;
                start_of_line = it + 1;
            }
        }
    }

} // namespace fidl
