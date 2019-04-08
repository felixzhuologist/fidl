cc_binary(
    name = "main",
    srcs = ["main.cpp"],
    deps = [":lexer", ":flat_ast"]
)

cc_library(
    name = "lexer",
    srcs = ["lexer.cpp"],
    hdrs = ["string_view.h", "lexer.h", "token.h", "token_definitions.inc"],
    deps = [":source_location"],
)

cc_library(
    name = "raw_ast",
    srcs = ["raw_ast.cpp"],
    hdrs = ["raw_ast.h", "token.h", "token_definitions.inc", "tree_visitor.h", "types.h"],
    deps = [":source_location"],
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

cc_test(
    name = "source_file_test",
    srcs = ["source_file_test.cpp"],
    deps = [
        ":source_file",
        "@gtest//:gtest",
        "@gtest//:gtest_main",
    ]
)
