#include "test_support.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace std;
using OcrFixture = test_support::OcrTest;

namespace {

std::wstring GetGeneratedOcrBmpPath() {
    return test_support::GetTempBmpPath(L"op_test_ocr_console_like.bmp");
}

void ExpectHelloWorldDigits(const std::wstring &text) {
    const std::wstring normalized_letters = test_support::NormalizeOcrLetters(text);
    const std::wstring digits = test_support::ExtractDigits(text);
    EXPECT_LE(test_support::EditDistance(normalized_letters, L"helloworld"), 3u) << "normalized letters differ too much";
    EXPECT_NE(digits.find(L"12345"), wstring::npos) << "missing digit anchor 12345";
}

} // namespace

TEST_F(OcrFixture, Ocr) {
    wstring text;
    op.Ocr(0, 0, 100, 100, L"ffffff-000000", 0.8, text);
    wcout << L"Ocr(0,0,100,100): " << (text.empty() ? L"(empty)" : text) << endl;
}

TEST_F(OcrFixture, OcrEx) {
    wstring text;
    op.OcrEx(0, 0, 100, 100, L"ffffff-000000", 0.8, text);
    wcout << L"OcrEx(0,0,100,100): " << (text.empty() ? L"(empty)" : text.substr(0, 50)) << endl;
}

TEST_F(OcrFixture, OcrAuto) {
    wstring text;
    op.OcrAuto(0, 0, 100, 100, 0.8, text);
    wcout << L"OcrAuto(0,0,100,100): " << (text.empty() ? L"(empty)" : text) << endl;
}

TEST_F(OcrFixture, OcrFromFile) {
    const std::wstring path = GetGeneratedOcrBmpPath();
    ASSERT_TRUE(test_support::CreateConsoleLikeBmp(path, L"Hello World! 12345"));

    wstring text;
    op.OcrFromFile(path.c_str(), L"ffffff-000000", 0.8, text);
    wcout << L"OcrFromFile(" << path << L"): " << (text.empty() ? L"(empty)" : text) << endl;
    ExpectHelloWorldDigits(text);
}

TEST_F(OcrFixture, OcrFromFileWithColorFilter) {
    const std::wstring path = GetGeneratedOcrBmpPath();
    ASSERT_TRUE(test_support::CreateConsoleLikeBmp(path, L"Hello World! 12345"));

    std::wstring text;
    op.OcrFromFile(path.c_str(), L"ffffff-000000", 0.8, text);
    wcout << L"OcrFromFile(color filtered, " << path << L"): " << (text.empty() ? L"(empty)" : text) << endl;
    ExpectHelloWorldDigits(text);
}

TEST_F(OcrFixture, OcrAutoFromFile) {
    const std::wstring path = GetGeneratedOcrBmpPath();
    ASSERT_TRUE(test_support::CreateConsoleLikeBmp(path, L"Hello World! 12345"));

    wstring text;
    op.OcrAutoFromFile(path.c_str(), 0.8, text);
    wcout << L"OcrAutoFromFile(" << path << L"): " << (text.empty() ? L"(empty)" : text) << endl;
    ExpectHelloWorldDigits(text);
}


TEST_F(OcrFixture, FindStr) {
    long find_x = 0;
    long find_y = 0;
    op.FindStr(0, 0, 200, 200, L"test", L"ffffff-000000", 0.8, &find_x, &find_y, &ret);
    cout << "FindStr(test): ret=" << ret << " at (" << find_x << "," << find_y << ")" << endl;
}

TEST_F(OcrFixture, FindStrEx) {
    wstring find_results;
    op.FindStrEx(0, 0, 200, 200, L"test|demo", L"ffffff-000000", 0.8, find_results);
    wcout << L"FindStrEx(test|demo): " << (find_results.empty() ? L"(empty)" : find_results.substr(0, 30))
          << L"..." << endl;
}

TEST_F(OcrFixture, SetOcrEngine) {
    const std::wstring endpoint = test_support::GetConfiguredOcrEndpoint();
    const long engine_ret = op.SetOcrEngine(endpoint.c_str(), L"", L"--timeout=5000");
    cout << "SetOcrEngine: " << engine_ret << endl;
    EXPECT_EQ(engine_ret, 1);
}

TEST_F(OcrFixture, SetOcrEngineBaseUrl) {
    const long engine_ret = op.SetOcrEngine(L"http://127.0.0.1:8080", L"", L"");
    cout << "SetOcrEngine(base url): " << engine_ret << endl;
    EXPECT_EQ(engine_ret, 1);
}

TEST_F(OcrFixture, SetOcrEngineInvalidUrl) {
    const long engine_ret = op.SetOcrEngine(L"http://", L"", L"--timeout=5000");
    cout << "SetOcrEngine(invalid): " << engine_ret << endl;
    EXPECT_EQ(engine_ret, 0);
}

TEST_F(OcrFixture, SetOcrEngineViaDllNameArg) {
    const std::wstring endpoint = test_support::GetConfiguredOcrEndpoint();
    const long engine_ret = op.SetOcrEngine(L"", endpoint.c_str(), L"--timeout=2000");
    cout << "SetOcrEngine(via dll_name): " << engine_ret << endl;
    EXPECT_EQ(engine_ret, 1);
}

TEST_F(OcrFixture, SetOcrEngineTesseractAlias) {
    const long engine_ret = op.SetOcrEngine(L"tesseract", L"", L"--timeout=2000");
    cout << "SetOcrEngine(tesseract): " << engine_ret << endl;
    EXPECT_EQ(engine_ret, 1);
}

TEST_F(OcrFixture, SetOcrEnginePaddleAlias) {
    const long engine_ret = op.SetOcrEngine(L"paddle", L"", L"--timeout=2000");
    cout << "SetOcrEngine(paddle): " << engine_ret << endl;
    EXPECT_EQ(engine_ret, 1);
}

TEST_F(OcrFixture, OcrAutoFromGeneratedConsoleLikeBmpContainsExpectedText) {
    const std::wstring path = GetGeneratedOcrBmpPath();
    ASSERT_TRUE(test_support::CreateConsoleLikeBmp(path, L"Hello World! 12345"));

    std::wstring text;
    op.OcrAutoFromFile(path.c_str(), 0.8, text);
    wcout << L"Generated OCR result: " << (text.empty() ? L"(empty)" : text) << endl;

    ASSERT_FALSE(text.empty()) << "Expected OCR text from generated BMP";
    ExpectHelloWorldDigits(text);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}
