#include <fstream>
#include <iostream>
#include <map>
#include <vector>

#include "flat_ast.h"
#include "lexer.h"
#include "names.h"
#include "parser.h"
#include "source_manager.h"
#include "c_generator.h"
#include "json_generator.h"

namespace {

void Usage() {
    std::cout
        << "usage: fidlc [--c-header HEADER_PATH]\n"
           "             [--json JSON_PATH]\n"
           "             [--name LIBRARY_NAME]\n"
           "             [--werror]\n"
           "             [--files [FIDL_FILE...]...]\n"
           "             [--help]\n"
           "\n"
           " * `--c-header HEADER_PATH`. If present, this flag instructs `fidlc` to output\n"
           "   a C header at the given path.\n"
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

enum class Behavior {
    kCHeader,
    kCClient,
    kJSON,
};

[[noreturn]] void FailWithUsage(const char* message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    Usage();
    exit(1);
}

[[noreturn]] void Fail(const char* message, ...) {
    va_list args;
    va_start(args, message);
    vfprintf(stderr, message, args);
    va_end(args);
    exit(1);
}

std::fstream Open(std::string filename, std::ios::openmode mode) {
    // TODO: create parent dirs if they don't exist
    std::fstream stream;
    stream.open(filename, mode);
    if (!stream.is_open()) {
        Fail("Could not open file: %s\n", filename.data());
    }
    return stream;
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

bool Parse(const fidl::SourceFile& source_file,
           fidl::ErrorReporter* error_reporter,
           fidl::flat::Library* library) {
  fidl::Lexer lexer(source_file, error_reporter);
  fidl::Parser parser(&lexer, error_reporter);
  auto ast = parser.Parse();
  if (!parser.Ok()) {
    return false;
  }
  if (!library->ConsumeFile(std::move(ast))) {
    return false;
  }
  return true;
}

void Write(std::ostringstream output, std::fstream file) {
  file << output.str();
  file.flush();
}

} // namespace

int compile(fidl::ErrorReporter* error_reporter,
            fidl::flat::Typespace* typespace,
            std::string library_name,
            std::map<Behavior, std::fstream> outputs,
            std::vector<fidl::SourceManager> source_managers) {
  fidl::flat::Libraries all_libraries;
  fidl::flat::Library* final_library = nullptr;
  for (const auto& source_manager : source_managers) {
    if (source_manager.sources().empty()) {
      continue;
    }

    auto library = std::make_unique<fidl::flat::Library>(&all_libraries, error_reporter, typespace);
    for (const auto& source_file : source_manager.sources()) {
      if (!Parse(*source_file, error_reporter, library.get())) {
        return 1;
      }
    }

    if (!library->Compile()) {
      return 1;
    }

    final_library = library.get();
    if (!all_libraries.Insert(std::move(library))) {
        const auto& name = library->name();
        Fail("Mulitple libraries with the same name: '%s'\n",
             NameLibrary(name).data());
    }
  }

  if (final_library == nullptr) {
    Fail("No library was produced.\n");
  }

  // Verify that the produced library's name matches the expected name.
  std::string final_name = NameLibrary(final_library->name());
  if (!library_name.empty() && final_name != library_name) {
      Fail("Generated library '%s' did not match --name argument: %s\n",
           final_name.data(), library_name.data());
  }

  for (auto& output : outputs) {
    auto& behavior = output.first;
    auto& output_file = output.second;

    switch (behavior) {
    case Behavior::kCHeader: {
        fidl::CGenerator generator(final_library);
        Write(generator.ProduceHeader(), std::move(output_file));
        break;
    }
    case Behavior::kCClient: {
        fidl::CGenerator generator(final_library);
        Write(generator.ProduceClient(), std::move(output_file));
        break;
    }
    case Behavior::kJSON: {
      fidl::JSONGenerator generator(final_library);
      Write(generator.Produce(), std::move(output_file));
      break;
    }
    }
  }

  return 0;
}

int main(int argc, char* argv[]) {
    auto argv_args = std::make_unique<ArgvArguments>(argc, argv);

    // parse the program name
    argv_args->Claim();

    if (!argv_args->Remaining()) {
        Usage();
        exit(0);
    }

    std::string library_name;
    bool warnings_as_errors = false;
    std::map<Behavior, std::fstream> outputs;
    while (argv_args->Remaining()) {
        std::string flag = argv_args->Claim();
        if (flag == "--help") {
            Usage();
            exit(0);
        } else if (flag == "--werror") {
            warnings_as_errors = true;
        } else if (flag == "--c-header") {
            outputs.emplace(Behavior::kCHeader, Open(argv_args->Claim(), std::ios::out));
        } else if (flag == "--c-client") {
            outputs.emplace(Behavior::kCClient, Open(argv_args->Claim(), std::ios::out));
        }else if (flag == "--json") {
            outputs.emplace(Behavior::kJSON, Open(argv_args->Claim(), std::ios::out));
        } else if (flag == "--name") {
            library_name = argv_args->Claim();
        } else if (flag == "--files") {
            break;
        } else {
            FailWithUsage("Unknown argument: %s\n", flag.data());
        }
    }

    std::vector<fidl::SourceManager> source_managers;
    source_managers.push_back(fidl::SourceManager());
    while (argv_args->Remaining()) {
      std::string arg = argv_args->Claim();
      if (arg == "--files") {
        source_managers.emplace_back();
      } else {
        if (!source_managers.back().CreateSource(arg.data())) {
          Fail("Couldn't read in source data from %s\n", arg.data());
        }
      }
    }

    fidl::ErrorReporter error_reporter(warnings_as_errors);
    auto typespace = fidl::flat::Typespace::RootTypes(&error_reporter);
    auto status = compile(&error_reporter,
                          &typespace,
                          library_name,
                          std::move(outputs),
                          std::move(source_managers));
    error_reporter.PrintReports();
    return status;
}

