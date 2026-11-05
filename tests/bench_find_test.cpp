// 合成图基准：不依赖仓外 assets，用于给 REVIEW_OPTIMIZATION 的 O5/O20/O21 提供数据。
// 默认跳过；设置 OP_RUN_BENCH=1 后运行：
//   $env:OP_RUN_BENCH=1; .\bin\x64\op_test.exe --gtest_filter=BenchTest.*
// 输出 CSV：BENCH,<name>,<w>x<h>,<tpl>,<iters>,mean_ms,p50_ms,p95_ms

#include "test_support.h"

#include "../libop/image/Image.h"
#include "../libop/image/ImageSearchAlgorithms.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
#include <cwchar>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace {

bool bench_enabled() {
    const char *flag = std::getenv("OP_RUN_BENCH");
    return flag && std::string(flag) == "1";
}

double now_ms() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double, std::milli>(clock::now().time_since_epoch()).count();
}

struct Stats {
    double mean = 0;
    double p50 = 0;
    double p95 = 0;
};

Stats summarize(std::vector<double> samples) {
    Stats s;
    if (samples.empty())
        return s;
    double sum = 0;
    for (double v : samples)
        sum += v;
    s.mean = sum / static_cast<double>(samples.size());
    std::sort(samples.begin(), samples.end());
    s.p50 = samples[samples.size() / 2];
    s.p95 = samples[(samples.size() * 95) / 100];
    return s;
}

op::Image make_source(int width, int height) {
    op::Image image;
    image.create(width, height);
    // 确定性伪随机 + 渐变：避免纯色导致整屏同色，找图会退化成“处处命中”。
    uint32_t xorshift = 0x12345678u;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            xorshift ^= xorshift << 13;
            xorshift ^= xorshift >> 17;
            xorshift ^= xorshift << 5;
            const unsigned char b = static_cast<unsigned char>((xorshift & 0xff));
            const unsigned char g = static_cast<unsigned char>(((xorshift >> 8) & 0xff));
            const unsigned char r = static_cast<unsigned char>(((xorshift >> 16) & 0xff));
            image.at<uint32_t>(y, x) = (0xffu << 24) | (static_cast<uint32_t>(r) << 16) |
                                       (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        }
    }
    return image;
}

void paste_patch(op::Image &source, const op::Image &patch, int left, int top) {
    for (int y = 0; y < patch.height; ++y) {
        for (int x = 0; x < patch.width; ++x) {
            source.at<uint32_t>(top + y, left + x) = patch.at<uint32_t>(y, x);
        }
    }
}

op::Image crop_patch(const op::Image &source, int width, int height, int left, int top) {
    op::Image patch;
    patch.create(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            patch.at<uint32_t>(y, x) = source.at<uint32_t>(top + y, left + x);
        }
    }
    return patch;
}

void report(const char *name, int width, int height, int tpl, int iters, const Stats &s) {
    std::cout << "BENCH," << name << "," << width << "x" << height << "," << tpl << "," << iters << ","
              << s.mean << "," << s.p50 << "," << s.p95 << std::endl;
}

// 用一张合成图 + 唯一模板，分别测：
//  - cold：每次重新生成灰度/积分图（模拟每帧新图）
//  - warm：同一帧复用缓存（frameId 不变）
void bench_find_pic(int width, int height, int tpl_size, int cold_iters, int warm_iters) {
    op::Image source = make_source(width, height);
    const int left = (width - tpl_size) / 2;
    const int top = (height - tpl_size) / 2;
    op::Image patch = crop_patch(source, tpl_size, tpl_size, left, top);
    paste_patch(source, patch, left, top);

    op::image::ImageSearchAlgorithms alg;
    alg._src = source;
    alg.set_offset(0, 0);

    op::image::PicMatchTemplate prepared;
    op::image::build_pic_match_template(patch, prepared);
    std::vector<op::image::PicMatchTemplate *> views{&prepared};
    const op::color_t dfcolor{};

    std::vector<double> cold;
    cold.reserve(static_cast<size_t>(cold_iters));
    for (int i = 0; i < cold_iters; ++i) {
        alg.invalidate_frame_cache();
        alg.set_frame_id(static_cast<unsigned int>(i + 1));
        long x = -1, y = -1;
        const double start = now_ms();
        const long ret = alg.FindPic(views, dfcolor, 0.9, 0, x, y);
        cold.push_back(now_ms() - start);
        ASSERT_GE(ret, 0) << "synthetic patch should match";
        ASSERT_EQ(x, left);
        ASSERT_EQ(y, top);
    }
    report("findpic_cold", width, height, tpl_size, cold_iters, summarize(cold));

    alg.invalidate_frame_cache();
    alg.set_frame_id(0xFFFFFFFFu);
    long warm_x = -1, warm_y = -1;
    ASSERT_EQ(alg.FindPic(views, dfcolor, 0.9, 0, warm_x, warm_y), 0); // 预热

    std::vector<double> warm;
    warm.reserve(static_cast<size_t>(warm_iters));
    for (int i = 0; i < warm_iters; ++i) {
        long x = -1, y = -1;
        const double start = now_ms();
        const long ret = alg.FindPic(views, dfcolor, 0.9, 0, x, y);
        warm.push_back(now_ms() - start);
        ASSERT_GE(ret, 0);
        ASSERT_EQ(x, left);
        ASSERT_EQ(y, top);
    }
    report("findpic_warm", width, height, tpl_size, warm_iters, summarize(warm));
}

void bench_find_color(int width, int height, size_t color_count, int iters) {
    op::Image source = make_source(width, height);
    op::image::ImageSearchAlgorithms alg;
    alg._src = source;
    alg.set_offset(0, 0);

    // 使用图中不存在的颜色，强制整图扫描（最坏情况）。随机图里可能恰好出现某颜色，
    // 这里先查一遍，避免基准把“命中”当成失败。
    auto color_present = [&source](unsigned char b, unsigned char g, unsigned char r) {
        const uint32_t packed = (0xffu << 24) | (static_cast<uint32_t>(r) << 16) |
                                (static_cast<uint32_t>(g) << 8) | static_cast<uint32_t>(b);
        for (int y = 0; y < source.height; ++y) {
            for (int x = 0; x < source.width; ++x) {
                if (source.at<uint32_t>(y, x) == packed)
                    return true;
            }
        }
        return false;
    };

    std::vector<op::color_df_t> colors(color_count);
    unsigned char seed = 1;
    for (size_t i = 0; i < color_count; ++i) {
        unsigned char b = 0, g = 0, r = 0;
        do {
            b = static_cast<unsigned char>(0x10 + ((seed * 37) & 0x7f));
            g = static_cast<unsigned char>(0x20 + ((seed * 53) & 0x5f));
            r = static_cast<unsigned char>(0x30 + ((seed * 71) & 0x4f));
            ++seed;
        } while (color_present(b, g, r));

        wchar_t text[16] = {0};
        swprintf_s(text, L"%02X%02X%02X", static_cast<unsigned>(r), static_cast<unsigned>(g),
                   static_cast<unsigned>(b));
        colors[i].color.str2color(text);
        colors[i].df = op::color_t{};
    }

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iters));
    for (int i = 0; i < iters; ++i) {
        long x = -1, y = -1;
        const double start = now_ms();
        const long ret = alg.FindColor(colors, 1.0, 0, x, y);
        samples.push_back(now_ms() - start);
        ASSERT_EQ(ret, 0);
    }
    report("findcolor", width, height, static_cast<int>(color_count), iters, summarize(samples));
}

void bench_opencv_match(int width, int height, int tpl_size, int cold_iters, int warm_iters) {
    op::Image source = make_source(width, height);
    const int left = (width - tpl_size) / 2;
    const int top = (height - tpl_size) / 2;
    op::Image patch = crop_patch(source, tpl_size, tpl_size, left, top);
    paste_patch(source, patch, left, top);

    cv::Mat bgra(height, width, CV_8UC4, source.pdata);
    cv::Mat gray;
    cv::Mat patch_bgra(tpl_size, tpl_size, CV_8UC4, patch.pdata);
    cv::Mat patch_gray;

    std::vector<double> cold;
    cold.reserve(static_cast<size_t>(cold_iters));
    for (int i = 0; i < cold_iters; ++i) {
        const double start = now_ms();
        cv::cvtColor(bgra, gray, cv::COLOR_BGRA2GRAY);
        cv::cvtColor(patch_bgra, patch_gray, cv::COLOR_BGRA2GRAY);
        cv::Mat result;
        cv::matchTemplate(gray, patch_gray, result, cv::TM_CCOEFF_NORMED);
        double min_val = 0, max_val = 0;
        cv::Point min_loc, max_loc;
        cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);
        cold.push_back(now_ms() - start);
        ASSERT_GE(max_val, 0.9);
    }
    report("opencv_cold", width, height, tpl_size, cold_iters, summarize(cold));

    cv::cvtColor(bgra, gray, cv::COLOR_BGRA2GRAY);
    cv::cvtColor(patch_bgra, patch_gray, cv::COLOR_BGRA2GRAY);
    std::vector<double> warm;
    warm.reserve(static_cast<size_t>(warm_iters));
    for (int i = 0; i < warm_iters; ++i) {
        const double start = now_ms();
        cv::Mat result;
        cv::matchTemplate(gray, patch_gray, result, cv::TM_CCOEFF_NORMED);
        double min_val = 0, max_val = 0;
        cv::Point min_loc, max_loc;
        cv::minMaxLoc(result, &min_val, &max_val, &min_loc, &max_loc);
        warm.push_back(now_ms() - start);
        ASSERT_GE(max_val, 0.9);
    }
    report("opencv_warm", width, height, tpl_size, warm_iters, summarize(warm));
}

} // namespace

TEST(BenchTest, FindPicSynthetic) {
    if (!bench_enabled())
        GTEST_SKIP() << "set OP_RUN_BENCH=1 to run synthetic benchmarks";

    for (const auto &size : {std::pair<int, int>{1280, 720}, {1920, 1080}, {2560, 1440}}) {
        for (int tpl : {32, 64}) {
            bench_find_pic(size.first, size.second, tpl, 5, 15);
        }
    }
}

TEST(BenchTest, FindColorSynthetic) {
    if (!bench_enabled())
        GTEST_SKIP() << "set OP_RUN_BENCH=1 to run synthetic benchmarks";

    for (const auto &size : {std::pair<int, int>{1280, 720}, {1920, 1080}, {2560, 1440}}) {
        bench_find_color(size.first, size.second, 1, 10);
        bench_find_color(size.first, size.second, 4, 10);
    }
}

TEST(BenchTest, OpenCvMatchBaseline) {
    if (!bench_enabled())
        GTEST_SKIP() << "set OP_RUN_BENCH=1 to run synthetic benchmarks";

    for (const auto &size : {std::pair<int, int>{1280, 720}, {1920, 1080}, {2560, 1440}}) {
        for (int tpl : {32, 64}) {
            bench_opencv_match(size.first, size.second, tpl, 5, 15);
        }
    }
}
