#include "test_support.h"

#include "../libop/base/ThreadPool.h"
#include "../libop/base/Utils.h"
#include "op_i.h"

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

#ifdef _M_IX86
TEST(UtilsTest, X86CapiUndecoratedAliases) {
    // x86 __stdcall 导出名是 _OpCreate@0；.def 仍保留它，并加 OpCreate 别名，
    // 让 GetProcAddress 的两种写法都能工作。
    HMODULE module = ::LoadLibraryW(L"op_c_api_x86.dll");
    ASSERT_NE(module, nullptr);
    EXPECT_NE(::GetProcAddress(module, "OpCreate"), nullptr);
    EXPECT_NE(::GetProcAddress(module, "OpOcr"), nullptr);
    EXPECT_NE(::GetProcAddress(module, "_OpCreate@0"), nullptr);
    ::FreeLibrary(module);
}
#else
TEST(UtilsTest, ComIidBumpedForEarlyBinding) {
    // 早期绑定客户端必须因旧 IID 得到 E_NOINTERFACE，而不是静默跳错 vtable。
    const GUID old_iid = {0x85f31925, 0xb7ce, 0x4e26, {0x91, 0x8e, 0x08, 0x28, 0x93, 0xbd, 0xe6, 0xff}};
    HMODULE module = ::LoadLibraryW(L"op_x64.dll");
    ASSERT_NE(module, nullptr);
    using GetClassObjectFn = HRESULT(STDAPICALLTYPE *)(REFCLSID, REFIID, LPVOID *);
    auto get_class_object = reinterpret_cast<GetClassObjectFn>(::GetProcAddress(module, "DllGetClassObject"));
    ASSERT_NE(get_class_object, nullptr);

    IClassFactory *factory = nullptr;
    ASSERT_TRUE(SUCCEEDED(get_class_object(CLSID_OpAutomation, IID_IClassFactory,
                                           reinterpret_cast<LPVOID *>(&factory))));
    ASSERT_NE(factory, nullptr);

    IUnknown *current = nullptr;
    EXPECT_TRUE(SUCCEEDED(factory->CreateInstance(nullptr, IID_IOpAutomation,
                                                  reinterpret_cast<LPVOID *>(&current))));
    IUnknown *legacy = nullptr;
    EXPECT_EQ(factory->CreateInstance(nullptr, old_iid, reinterpret_cast<LPVOID *>(&legacy)), E_NOINTERFACE);
    EXPECT_EQ(legacy, nullptr);
    if (current)
        current->Release();
    factory->Release();
    ::FreeLibrary(module);
}
#endif

TEST(UtilsTest, RectDivideBlock) {
    op::rect_t rc(0, 0, 100, 100);
    vector<op::rect_t> blocks;
    rc.divideBlock(2, false, blocks);
    EXPECT_EQ(blocks.size(), 2u);
}
