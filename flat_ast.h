#ifndef FLAT_AST_H_
#define FLAT_AST_H_

#include <map>
#include <vector>

#include "string_view.h"

namespace fidl {
namespace flat {

class Library;

class Libraries {
public:
    Libraries();

    bool Insert(std::unique_ptr<Library> library);

private:
    std::map<std::vector<StringView>, std::unique_ptr<Library>> all_libraries_;
};

class Library {
public:
    Library(const Libraries* all_libraries) : all_libraries_(all_libraries) {}

private:
    const Libraries* all_libraries_;
};

} // namespace flat
} // namespace fidl

#endif // FLAT_AST_H_
