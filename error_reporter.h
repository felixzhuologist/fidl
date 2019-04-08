#ifndef ERROR_REPORTER_H_
#define ERROR_REPORTER_H_

#include <string>
#include <vector>

#include "source_location.h"
#include "string_view.h"
#include "token.h"

namespace fidl {

class ErrorReporter {
public:
    ErrorReporter(bool warnings_as_errors = false)
        : warnings_as_errors_(warnings_as_errors) {}

    void ReportError(const SourceLocation& location, StringView message);
    void ReportError(const Token& token, StringView message);
    const std::vector<std::string>& errors() const { return errors_; }
    const std::vector<std::string>& warnings() const { return warnings_; }

private:
    bool warnings_as_errors_;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
};

} // namespace fidl

#endif // ERROR_REPORTER_H_
