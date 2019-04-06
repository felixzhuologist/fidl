#include "gtest/gtest.h"
#include "source_file.h"

TEST(SourceFileTest, ReadsLines) {
    auto src = fidl::SourceFile("myfile.txt", "line1\nline2\nlonger line3");
    ASSERT_EQ(src.filename(), fidl::StringView("myfile.txt"));
}

TEST(SourceFileTest, LineContaining) {
    std::string data = "line1\nbla line2\nlonger line3";
    auto view = fidl::StringView(data.data() + 11, 5);
    auto src = fidl::SourceFile("myfile.txt", std::move(data));
    fidl::SourceFile::Position position_out;
    src.LineContaining(view, &position_out);
    ASSERT_EQ(position_out.line, 2);
    ASSERT_EQ(position_out.column, 5);
}