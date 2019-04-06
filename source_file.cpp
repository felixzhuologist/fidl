#include <assert.h>

#include <algorithm>
#include <functional>

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

StringView SourceFile::LineContaining(StringView view, Position* position_out) const {
    auto ptr_less_equal = std::less_equal<const char*>();

    // check that the view (the ptr) is between the start and end of the file
    assert(ptr_less_equal(data().data(), view.data()) && "The view is not part of this SourceFile");
    assert(ptr_less_equal(view.data() + view.size(), data().data() + data().size()) &&
           "The view is not part of this SourceFile");

    // given that the view is in the file, the line containing view is the first
    // line <= the view, searching from the back of the file
    auto is_in_line = [&ptr_less_equal](const StringView& left, const StringView& right) {
        return ptr_less_equal(right.data(), left.data());
    };
    auto line = std::upper_bound(lines_.crbegin(), lines_.crend(), view, is_in_line);
    assert(line != lines_.crend());

    if (position_out != nullptr) {
        // calculate from the end to get 1 indexed
        int line_number = static_cast<int>(lines_.crend() - line);
        int column_number = static_cast<int>(view.data() - line->data());
        *position_out = {line_number, column_number};
    }
    return *line;
}

} // namespace fidl
