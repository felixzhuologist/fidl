#include "source_manager.h"

#include <sys/stat.h>
#include <utility>

namespace fidl {

bool SourceManager::CreateSource(StringView filename) {
    struct stat s;
    if (stat(filename.data(), &s) != 0)
        return false;

    if ((s.st_mode & S_IFREG) != S_IFREG)
        return false;

    FILE* file = fopen(filename.data(), "rb");
    if (!file)
        return false;

    std::string data;
    fseek(file, 0, SEEK_END);
    auto filesize = ftell(file);
    data.resize(filesize);
    rewind(file);
    fread(&data[0], 1, filesize, file);
    fclose(file);

    AddSourceFile(std::make_unique<SourceFile>(filename, std::move(data)));
    return true;
}

void SourceManager::AddSourceFile(std::unique_ptr<SourceFile> file) {
    sources_.push_back(std::move(file));
}

} // namespace fidl
