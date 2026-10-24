#include "test_support.h"

#include "../libop/base/ThreadPool.h"
#include "../libop/base/Utils.h"

#include <map>
#include <vector>

using namespace std;

TEST(UtilsTest, ThreadPool) {
    ThreadPool pool(4);
    auto fut = pool.enqueue([] { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

TEST(UtilsTest, Utf8ToWstringKeepsNonAnsi) {
    // 中文 + emoji：旧的 utf8_to_ansi(CP936) + _s2wstring(CP_ACP) 链会丢字符/乱码。
    const std::string utf8 = "\xE4\xB8\xAD\xE6\x96\x87\xF0\x9F\x98\x80";
    EXPECT_EQ(utf8_to_wstring(utf8), std::wstring(L"\u4E2D\u6587\U0001F600"));
}

TEST(UtilsTest, PointStrictWeakOrder) {
    // 旧比较器：a(0,8) < b(10,0)、b < c(0,16)，但 a 与 c 等价，不是严格弱序。
    const op::point_t a(0, 8);
    const op::point_t b(10, 0);
    const op::point_t c(0, 16);
    EXPECT_FALSE((a < b) && (b < c) && (c < a));
    EXPECT_FALSE((a < b) && (b < a));
    std::map<op::point_t, int> points;
    points[a] = 1;
    points[b] = 2;
    points[c] = 3;
    EXPECT_EQ(points.size(), 3u);
}

TEST(UtilsTest, RectDivideBlock) {
    op::rect_t rc(0, 0, 100, 100);
    vector<op::rect_t> blocks;
    rc.divideBlock(2, false, blocks);
    EXPECT_EQ(blocks.size(), 2u);
}
