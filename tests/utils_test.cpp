#include "test_support.h"

#include "../libop/base/ThreadPool.h"
#include "../libop/base/Utils.h"
#include "../libop/image/ImageSearchAlgorithms.h"
#include "../include/op_c_api.h"
#include "op_i.h"

#include <algorithm>
#include <iostream>
#include <limits>
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

TEST(UtilsTest, DpiAwarenessQueryMatchesOsState) {
    // 用 C API 创建 Op，确保 C API 自己的 DPI 配置被锁定；OpGet 返回实际 OS 状态。
    op_handle handle = OpCreate();
    ASSERT_NE(handle, nullptr);
    EXPECT_EQ(OpSetProcessDpiAware(1), 0);
    EXPECT_EQ(op::Op::IsProcessDpiAware(), OpGetProcessDpiAware() != 0);
    OpDestroy(handle);
}

TEST(UtilsTest, IntegralImageHandlesBeyondInt32) {
    // 4096x2160 全 255：总和 2,256,076,800 > INT_MAX，旧 int32 积分图会回绕。
    op::ImageBin bin;
    bin.create(4096, 2160);
    std::fill(bin.pixels.begin(), bin.pixels.end(), static_cast<unsigned char>(255));

    op::image::ImageSearchAlgorithms alg;
    alg.record_sum(bin);
    const long long expected = 4096LL * 2160 * 255;
    EXPECT_GT(expected, static_cast<long long>(std::numeric_limits<int>::max()));
    EXPECT_EQ(alg.region_sum(0, 0, 4096, 2160), expected);
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
TEST(UtilsTest, ComIidIsCurrentAndUnknownIidRejected) {
    // 旧接口已删除；这里验证当前 IID 可创建，任意未知 IID 干净失败。
    const GUID unknown_iid = {0xdeadbeef, 0x1234, 0x5678, {0x90, 0xab, 0xcd, 0xef, 0x01, 0x23, 0x45, 0x67}};
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
    EXPECT_TRUE(SUCCEEDED(factory->CreateInstance(nullptr, IID_IOpAutomation2,
                                                  reinterpret_cast<LPVOID *>(&current))));
    IUnknown *unknown = nullptr;
    EXPECT_EQ(factory->CreateInstance(nullptr, unknown_iid, reinterpret_cast<LPVOID *>(&unknown)), E_NOINTERFACE);
    EXPECT_EQ(unknown, nullptr);
    if (current)
        current->Release();
    factory->Release();
    ::FreeLibrary(module);
}

TEST(UtilsTest, ComRegistrationEntryPoints) {
    // 注册/卸载入口存在，且类工厂可创建对象；真正的 regsvr32 需要写 HKCR/HKLM，
    // 属于环境相关操作，不在此测试里执行（避免污染测试机注册表）。
    HMODULE module = ::LoadLibraryW(L"op_x64.dll");
    ASSERT_NE(module, nullptr);
    EXPECT_NE(::GetProcAddress(module, "DllRegisterServer"), nullptr);
    EXPECT_NE(::GetProcAddress(module, "DllUnregisterServer"), nullptr);
    EXPECT_NE(::GetProcAddress(module, "DllGetClassObject"), nullptr);
    EXPECT_NE(::GetProcAddress(module, "DllCanUnloadNow"), nullptr);
    ::FreeLibrary(module);
}
#endif

TEST(UtilsTest, RectDivideBlock) {
    op::rect_t rc(0, 0, 100, 100);
    vector<op::rect_t> blocks;
    rc.divideBlock(2, false, blocks);
    EXPECT_EQ(blocks.size(), 2u);
}
