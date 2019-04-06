cc_binary(
    name = "main",
    srcs = ["main.cpp"],
    deps = [":source_manager", ":source_file", ":flat_ast"]
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

cc_library(
    name = "flat_ast",
    srcs = ["flat_ast.cpp"],
    hdrs = ["string_view.h", "flat_ast.h"]
)
