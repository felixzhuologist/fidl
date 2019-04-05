#include <iostream>

void Usage() {
    std::cout
        << "usage: fidlc [--c-header HEADER_PATH]\n"
           "             [--c-client CLIENT_PATH]\n"
           "             [--c-server SERVER_PATH]\n"
           "             [--tables TABLES_PATH]\n"
           "             [--json JSON_PATH]\n"
           "             [--name LIBRARY_NAME]\n"
           "             [--werror]\n"
           "             [--files [FIDL_FILE...]...]\n"
           "             [--help]\n"
           "\n"
           " * `--c-header HEADER_PATH`. If present, this flag instructs `fidlc` to output\n"
           "   a C header at the given path.\n"
           "\n"
           " * `--c-client CLIENT_PATH`. If present, this flag instructs `fidlc` to output\n"
           "   the simple C client implementation at the given path.\n"
           "\n"
           " * `--c-server SERVER_PATH`. If present, this flag instructs `fidlc` to output\n"
           "   the simple C server implementation at the given path.\n"
           "\n"
           " * `--tables TABLES_PATH`. If present, this flag instructs `fidlc` to output\n"
           "   coding tables at the given path. The coding tables are required to encode and\n"
           "   decode messages from the C and C++ bindings.\n"
           "\n"
           " * `--json JSON_PATH`. If present, this flag instructs `fidlc` to output the\n"
           "   library's intermediate representation at the given path. The intermediate\n"
           "   representation is JSON that conforms to the schema available via --json-schema.\n"
           "   The intermediate representation is used as input to the various backends.\n"
           "\n"
           " * `--name LIBRARY_NAME`. If present, this flag instructs `fidlc` to validate\n"
           "   that the library being compiled has the given name. This flag is useful to\n"
           "   cross-check between the library's declaration in a build system and the\n"
           "   actual contents of the library.\n"
           "\n"
           " * `--files [FIDL_FILE...]...`. Each `--file [FIDL_FILE...]` chunk of arguments\n"
           "   describes a library, all of which must share the same top-level library name\n"
           "   declaration. Libraries must be presented in dependency order, with later\n"
           "   libraries able to use declarations from preceding libraries but not vice versa.\n"
           "   Output is only generated for the final library, not for each of its dependencies.\n"
           "\n"
           " * `--json-schema`. If present, this flag instructs `fidlc` to output the\n"
           "   JSON schema of the intermediate representation.\n"
           "\n"
           " * `--werror`. Treats warnings as errors.\n"
           "\n"
           " * `--help`. Prints this help, and exit immediately.\n"
           "\n"
           "All of the arguments can also be provided via a response file, denoted as\n"
           "`@responsefile`. The contents of the file at `responsefile` will be interpreted\n"
           "as a whitespace-delimited list of arguments. Response files cannot be nested,\n"
           "and must be the only argument.\n"
           "\n"
           "See <https://fuchsia.googlesource.com/fuchsia/+/master/zircon/docs/fidl/compiler.md>\n"
           "for more information.\n";
    std::cout.flush();
}

[[noreturn]] void FailWithUsage(const char* message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    Usage();
    exit(1);
}

class Arguments {
public:
    virtual ~Arguments() {}
    virtual std::string Claim() = 0;
    virtual bool Remaining() const = 0;
};


class ArgvArguments : public Arguments {
public:
    ArgvArguments(int count, char** arguments)
        : count_(count), arguments_(const_cast<const char**>(arguments)) {}

    std::string Claim() override {
        if (count_ < 1) {
            FailWithUsage("Missing part of an argument\n");
        }

        std::string argument = arguments_[0];
        --count_;
        ++arguments_;
        return argument;
    }

    bool Remaining() const override { return count_ > 0; }

private:
    int count_;
    const char** arguments_;
};


int main(int argc, char* argv[]) {
    auto argv_args = std::make_unique<ArgvArguments>(argc, argv);

    // parse the program name
    argv_args->Claim();

    if (!argv_args->Remaining()) {
        Usage();
        exit(0);
    }

    return 0;
}

