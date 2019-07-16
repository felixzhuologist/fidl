cc_binary(
    name = "main",
    srcs = ["main.cpp"],
    deps = [
        ":lexer",
        ":parser",
        ":json_generator",
        ":c_generator",
        ":names"
    ]
)

cc_test(
    name = "unit",
    srcs = ["unit_tests.cpp"],
    deps = [
        ":lexer",
        ":parser",
        "@gtest//:gtest",
        "@gtest//:gtest_main",
    ]
)

cc_library(
    name = "json_generator",
    srcs = ["json_generator.cpp"],
    hdrs = ["json_generator.h", "string_view.h"],
    deps = [":flat_ast"]
)

cc_library(
    name = "c_generator",
    srcs = ["c_generator.cpp"],
    hdrs = ["c_generator.h", "string_view.h"],
    deps = [":flat_ast"]
)

cc_library(
    name = "flat_ast",
    srcs = ["flat_ast.cpp"],
    hdrs = ["flat_ast.h", "typeshape.h", "utils.h"],
    deps = [":virtual_source_file", ":raw_ast", ":attributes", ":error_reporter", ":names"],
)

cc_library(
    name = "parser",
    srcs = ["parser.cpp"],
    hdrs = ["parser.h"],
    deps = [":lexer", ":raw_ast", ":attributes"],
)

cc_library(
    name = "lexer",
    srcs = ["lexer.cpp"],
    hdrs = ["lexer.h", "token.h", "token_definitions.inc"],
    deps = [":source_location", ":error_reporter"],
)

cc_library(
    name = "attributes",
    srcs = ["attributes.cpp"],
    hdrs = ["attributes.h", "error_reporter.h"],
    deps = [":raw_ast"]
)

cc_library(
    name = "raw_ast",
    srcs = ["raw_ast.cpp"],
    hdrs = ["raw_ast.h", "token.h", "token_definitions.inc", "tree_visitor.h", "types.h"],
    deps = [":source_location"],
)

cc_library(
    name = "error_reporter",
    srcs = ["error_reporter.cpp"],
    hdrs = ["error_reporter.h", "token.h", "token_definitions.inc", "string_view.h"],
    deps = [":source_location"],
)

cc_library(
    name = "virtual_source_file",
    srcs = ["virtual_source_file.cpp"],
    hdrs = ["string_view.h", "virtual_source_file.h"],
    deps = [":source_location"]
)

cc_library(
    name = "source_location",
    srcs = ["source_location.cpp"],
    hdrs = ["string_view.h", "source_location.h"],
    deps = [":source_manager"],
)

cc_library(
    name = "source_manager",
    srcs = ["source_manager.cpp"],
    hdrs = ["string_view.h", "source_manager.h"],
    deps = [":source_file"]
)

cc_library(
    name = "source_file",
    srcs = ["source_file.cpp"],
    hdrs = ["string_view.h", "source_file.h"],
)

cc_library(
    name = "names",
    srcs = ["names.cpp"],
    hdrs = [
        "names.h",
        "raw_ast.h",
        "flat_ast.h",
        "string_view.h",
        "types.h",
        "error_reporter.h",
        "virtual_source_file.h",
        "source_location.h",
        "source_manager.h",
        "source_file.h",
        "token.h",
        "token_definitions.inc",
        "typeshape.h"
    ]
)
