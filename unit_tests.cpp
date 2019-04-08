#include "gtest/gtest.h"
#include "lexer.h"
#include "parser.h"
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

TEST(LexerTest, Const) {
    std::string data = "const int8 offset = -33;";
    fidl::SourceFile src("myfile.txt", std::move(data));
    fidl::ErrorReporter error_reporter(false);
    fidl::Lexer lexer(src, &error_reporter);

    ASSERT_EQ(lexer.Lex().subkind(), fidl::Token::Subkind::kConst);
    ASSERT_EQ(lexer.Lex().kind(), fidl::Token::Kind::kIdentifier);
    ASSERT_EQ(lexer.Lex().kind(), fidl::Token::Kind::kIdentifier);
    ASSERT_EQ(lexer.Lex().kind(), fidl::Token::Kind::kEqual);
    ASSERT_EQ(lexer.Lex().kind(), fidl::Token::Kind::kNumericLiteral);
    ASSERT_EQ(lexer.Lex().kind(), fidl::Token::Kind::kSemicolon);
}

TEST(ParserTest, Const) {
    std::string data = "library textures;\nconst int8 offset = -33;";
    fidl::SourceFile src("myfile.txt", std::move(data));
    fidl::ErrorReporter error_reporter(false);
    fidl::Lexer lexer(src, &error_reporter);
    fidl::Parser parser(&lexer, &error_reporter);

    auto ast = parser.Parse();
    ASSERT_TRUE(parser.Ok());
}
