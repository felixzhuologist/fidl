#ifndef SOURCE_MANAGER_H_
#define SOURCE_MANAGER_H_

#include <memory>
#include <vector>

#include "source_file.h"
#include "string_view.h"

namespace fidl {

class SourceManager {
public:
    bool CreateSource(StringView filename);
    void AddSourceFile(std::unique_ptr<SourceFile> file);

    const std::vector<std::unique_ptr<SourceFile>>& sources() const { return sources_; }

private:
    std::vector<std::unique_ptr<SourceFile>> sources_;
};

} // namespace fidl

#endif // SOURCE_MANAGER_H
