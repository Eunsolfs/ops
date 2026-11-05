#include "OpContext.h"
#include "OpCaptureHelpers.h"
#include "OpResult.h"

#include "capture/FrameInfo.h"
#include "base/Utils.h"

#include <libop.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>
#include <vector>

namespace {

using op::capture::FrameInfo;

constexpr int small_block_size = 10;
constexpr int SC_DATA_TOP = 0;
constexpr int SC_DATA_BOTTOM = 1;

} // namespace

void op::Op::EnablePicCache(long enable, long *ret) {
    m_context->image_proc._enable_cache = enable;
    internal::set_result(ret, 1L);
}

void op::Op::CapturePre(const wchar_t *file, LONG *ret) {
    internal::set_result(ret, m_context->image_proc.Capture(file));
}

void op::Op::SetScreenDataMode(long mode, long *ret) {
    m_context->screen_data_mode = mode;
    internal::set_result(ret, 1L);
}
void op::Op::Capture(long x1, long y1, long x2, long y2, const wchar_t *file_name, long *ret) {

    internal::set_result(ret, 0L);

    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        const long capture_ret = m_context->image_proc.Capture(file_name);
        if (capture_ret != 1)
            internal::set_last_error(m_context.get(), internal::OpErrorCode::capture);
        internal::set_result(ret, capture_ret);
    });
}
// 比较指定坐标点(x,y)的颜色
void op::Op::CmpColor(long x, long y, const wchar_t *color, double sim, long *ret) {
    // LONG rx = -1, ry = -1;
    long tx = x + small_block_size, ty = y + small_block_size;
    internal::set_result(ret, 0L);
    internal::with_captured_region(m_context.get(), x, y, tx, ty, small_block_size, small_block_size, [&]() {
        internal::set_result(ret, m_context->image_proc.CmpColor(x, y, color, sim));
    });
}
// 查找指定区域内的颜色
void op::Op::FindColor(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, long dir, long *x, long *y,
                           long *ret) {

    long found_x = -1;
    long found_y = -1;
    internal::set_result(ret, 0L);
    internal::set_result(x, found_x);
    internal::set_result(y, found_y);

    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        internal::set_result(ret, m_context->image_proc.FindColor(color, sim, dir, found_x, found_y));
        internal::set_result(x, found_x);
        internal::set_result(y, found_y);
    });
}
// 查找指定区域内的所有颜色
void op::Op::FindColorEx(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, long dir,
                        std::wstring &retstr) {
    // wstring str;
    retstr.clear();
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.FindColorEx(color, sim, dir, retstr);
    });
}
// 根据指定的多点查找颜色坐标
void op::Op::FindMultiColor(long x1, long y1, long x2, long y2, const wchar_t *first_color, const wchar_t *offset_color,
                           double sim, long dir, long *x, long *y, long *ret) {

    long found_x = -1;
    long found_y = -1;
    internal::set_result(ret, 0L);
    internal::set_result(x, found_x);
    internal::set_result(y, found_y);

    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        internal::set_result(ret,
                             m_context->image_proc.FindMultiColor(first_color, offset_color, sim, dir, found_x,
                                                                  found_y));
        internal::set_result(x, found_x);
        internal::set_result(y, found_y);
    });

    /*if (*ret) {
            rx += x1; ry += y1;
            rx -= m_context->bkproc._capture->get_client_x();
            ry -= m_context->bkproc._capture->get_client_y();
        }*/
}
// 根据指定的多点查找所有颜色坐标
void op::Op::FindMultiColorEx(long x1, long y1, long x2, long y2, const wchar_t *first_color,
                             const wchar_t *offset_color, double sim, long dir, std::wstring &retstr) {
    retstr.clear();
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.FindMultiColorEx(first_color, offset_color, sim, dir, retstr);
    });
    // retstr = str;
}
// 查找指定区域内的图片
void op::Op::FindPic(long x1, long y1, long x2, long y2, const wchar_t *files, const wchar_t *delta_color, double sim,
                    long dir, long *x, long *y, long *ret) {

    long found_x = -1;
    long found_y = -1;
    internal::set_result(ret, -1L);
    internal::set_result(x, found_x);
    internal::set_result(y, found_y);

    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        internal::set_result(ret, m_context->image_proc.FindPic(files, delta_color, sim, dir, found_x, found_y));
        internal::set_result(x, found_x);
        internal::set_result(y, found_y);
    });

    /*if (*ret) {
            rx += x1; ry += y1;
            rx -= m_context->bkproc._capture->get_client_x();
            ry -= m_context->bkproc._capture->get_client_y();
        }*/
}
// 查找多个图片
void op::Op::FindPicEx(long x1, long y1, long x2, long y2, const wchar_t *files, const wchar_t *delta_color, double sim,
                      long dir, std::wstring &retstr) {

    retstr.clear();
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.FindPicEx(files, delta_color, sim, dir, retstr);
    });
}

void op::Op::FindPicExS(long x1, long y1, long x2, long y2, const wchar_t *files, const wchar_t *delta_color, double sim,
                       long dir, std::wstring &retstr) {
    retstr.clear();
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.FindPicEx(files, delta_color, sim, dir, retstr, false);
    });
}

void op::Op::FindColorBlock(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, long count,
                           long height, long width, long *x, long *y, long *ret) {
    long found_x = -1;
    long found_y = -1;
    internal::set_result(ret, 0L);
    internal::set_result(x, found_x);
    internal::set_result(y, found_y);
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        internal::set_result(ret,
                             m_context->image_proc.FindColorBlock(color, sim, count, height, width, found_x, found_y));
        internal::set_result(x, found_x);
        internal::set_result(y, found_y);
    });
}

void op::Op::FindColorBlockEx(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, long count,
                             long height, long width, std::wstring &retstr) {

    retstr.clear();
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.FindColorBlockEx(color, sim, count, height, width, retstr);
    });
}

// 获取(x,y)的颜色
void op::Op::GetColor(long x, long y, std::wstring &ret) {
    color_t cr = {};
    auto tx = x + small_block_size, ty = y + small_block_size;
    internal::with_captured_region(m_context.get(), x, y, tx, ty, small_block_size, small_block_size, [&]() {
        cr = m_context->image_proc._src.at<color_t>(0, 0);
    });

    // 采集失败时不要回退成零初始化的 "000000"：那与真实黑色无法区分，
    // 上层（如 Twinkle 用 color.empty() 判失败）会误判为成功。
    if (m_context->last_error_code != 0) {
        ret.clear();
        return;
    }
    ret = cr.towstr();
}

void op::Op::GetColorNum(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, long *ret) {
    internal::set_result(ret, 0L);
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        internal::set_result(ret, m_context->image_proc.GetColorNum(color, sim));
    });
}

void op::Op::SetDisplayInput(const wchar_t *mode, long *ret) {
    internal::set_result(ret, m_context->bkproc.set_display_method(mode));
}

void op::Op::LoadPic(const wchar_t *file_name, long *ret) {
    const long load_ret = m_context->image_proc.LoadPic(file_name);
    if (load_ret <= 0)
        internal::set_last_error(m_context.get(), internal::OpErrorCode::io);
    internal::set_result(ret, load_ret);
}

void op::Op::FreePic(const wchar_t *file_name, long *ret) {
    internal::set_result(ret, m_context->image_proc.FreePic(file_name));
}

void op::Op::LoadMemPic(const wchar_t *file_name, void *data, long size, long *ret) {
    const long load_ret = m_context->image_proc.LoadMemPic(file_name, data, size);
    if (load_ret != 1)
        internal::set_last_error(m_context.get(), internal::OpErrorCode::io);
    internal::set_result(ret, load_ret);
}

void op::Op::GetPicSize(const wchar_t *pic_name, long *width, long *height, long *ret) {
    internal::set_result(width, 0L);
    internal::set_result(height, 0L);
    const long size_ret = m_context->image_proc.GetPicSize(pic_name, width, height);
    if (size_ret != 1)
        internal::set_last_error(m_context.get(), internal::OpErrorCode::io);
    internal::set_result(ret, size_ret);
}

void op::Op::GetScreenData(long x1, long y1, long x2, long y2, size_t *data, long *ret) {
    internal::set_result(data, 0);
    internal::set_result(ret, 0L);
    auto &img = m_context->image_proc._src;
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->screenData.resize(img.size() * 4);

        if (m_context->screen_data_mode == SC_DATA_BOTTOM) {
            for (int i = 0; i < img.height; i++) {
                memcpy(m_context->screenData.data() + i * img.width * 4, img.ptr<char>(img.height - 1 - i),
                       img.width * 4);
            }
        } else {
            memcpy(m_context->screenData.data(), img.pdata, img.size() * 4);
        }
        internal::set_result(data, reinterpret_cast<size_t>(m_context->screenData.data()));
        internal::set_result(ret, 1L);
    });
}

void op::Op::CopyScreenData(long x1, long y1, long x2, long y2, long mode, void *dst, long dst_size, long *width,
                            long *height, long *stride, long *needed, long *ret) {
    internal::set_result(width, 0L);
    internal::set_result(height, 0L);
    internal::set_result(stride, 0L);
    internal::set_result(needed, 0L);
    internal::set_result(ret, 0L);

    if (!dst) {
        setlog(L"CopyScreenData: dst is null");
        internal::set_last_error(m_context.get(), internal::OpErrorCode::invalid_arg);
        return;
    }

    auto &img = m_context->image_proc._src;
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        const long row_bytes = static_cast<long>(img.width) * 4;
        const long total = row_bytes * static_cast<long>(img.height);
        internal::set_result(width, static_cast<long>(img.width));
        internal::set_result(height, static_cast<long>(img.height));
        internal::set_result(stride, row_bytes);
        internal::set_result(needed, total);

        if (dst_size < total) {
            internal::set_result(ret, -2L);
            return;
        }

        auto *out = static_cast<unsigned char *>(dst);
        if (mode == SC_DATA_BOTTOM) {
            for (int i = 0; i < img.height; ++i) {
                memcpy(out + static_cast<size_t>(i) * row_bytes, img.ptr<char>(img.height - 1 - i),
                       static_cast<size_t>(row_bytes));
            }
        } else {
            memcpy(out, img.pdata, static_cast<size_t>(total));
        }
        internal::set_result(ret, 1L);
    });
}

void op::Op::GetScreenDataBmp(long x1, long y1, long x2, long y2, size_t *data, long *size, long *ret) {
    internal::set_result(data, 0);
    internal::set_result(size, 0L);
    internal::set_result(ret, 0L);
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        auto &img = m_context->image_proc._src;

        BITMAPFILEHEADER bfh = {0}; // bmp file header
        BITMAPINFOHEADER bih = {0}; // bmp info header
        const int szBfh = sizeof(BITMAPFILEHEADER);
        const int szBih = sizeof(BITMAPINFOHEADER);
        bfh.bfOffBits = szBfh + szBih;
        bfh.bfSize = bfh.bfOffBits + img.width * img.height * 4;
        bfh.bfType = static_cast<WORD>(0x4d42);

        bih.biBitCount = 32; // 每个像素字节大小
        bih.biCompression = BI_RGB;
        // bih.biHeight = -img.height;//高度 反
        bih.biHeight = m_context->screen_data_mode == SC_DATA_BOTTOM ? img.height : -img.height; // 高度
        bih.biPlanes = 1;
        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biSizeImage = img.width * 4 * img.height; // 图像数据大小
        bih.biWidth = img.width;                      // 宽度

        m_context->screenDataBmp.resize(bfh.bfSize);
        /*	std::ofstream f;
        f.open("xx.bmp",std::ios::binary);
        if (f) {
            f.write((char*)&bfh, sizeof(bfh));
            f.write((char*)&bih, sizeof(bih));
            f.write((char*)img.pdata, img.size() * 4);
        }

        f.close();*/
        auto dst = m_context->screenDataBmp.data();

        memcpy(dst, &bfh, sizeof(bfh));
        memcpy(dst + sizeof(bfh), &bih, sizeof(bih));
        dst += sizeof(bfh) + sizeof(bih);
        if (m_context->screen_data_mode == SC_DATA_BOTTOM) {
            for (int i = 0; i < img.height; i++) {
                memcpy(dst + i * img.width * 4, img.ptr<char>(img.height - 1 - i), img.width * 4);
            }
        } else {
            memcpy(dst, img.pdata, img.size() * 4);
        }

        // memcpy(dst + sizeof(bfh)+sizeof(bih), img.pdata, img.size()*4);
        internal::set_result(data, reinterpret_cast<size_t>(m_context->screenDataBmp.data()));
        internal::set_result(size, bfh.bfSize);
        internal::set_result(ret, 1L);
    });
}

void op::Op::GetScreenFrameInfo(long *frame_id, long *time) {
    FrameInfo info = {};
    if (m_context->bkproc.IsBind()) {
        m_context->bkproc._capture->getFrameInfo(info);
    }
    internal::set_result(frame_id, info.frameId);
    internal::set_result(time, info.time);
}

void op::Op::MatchPicName(const wchar_t *pic_name, std::wstring &retstr) {
    retstr.clear();
    std::wstring s(pic_name);
    if (s.find(L'/') != s.npos || s.find(L'\\') != s.npos) {
        setlog("invalid pic_name");
    }

    s = std::regex_replace(s, std::wregex(L"(\\.|\\(|\\)|\\[|\\]|\\{|\\})"), L"\\$1");
    /*s = std::regex_replace(s, std::wregex(L"\\("), L"\\(");
    s = std::regex_replace(s, std::wregex(L"\\)"), L"\\)");
    s = std::regex_replace(s, std::wregex(L"\\["), L"\\[");
    s = std::regex_replace(s, std::wregex(L"\\]"), L"\\]");*/
    s = std::regex_replace(s, std::wregex(L"\\*"), L".*?");
    s = std::regex_replace(s, std::wregex(L"\\?"), L".?");

    // setlog(s.data());
    namespace fs = std::filesystem;
    fs::path path(m_context->curr_path);
    if (fs::exists(path)) {
        fs::directory_iterator iter(path);
        std::wstring tmp;
        std::wregex e(s);
        for (auto &it : iter) {
            if (it.status().type() == fs::file_type::regular) {
                tmp = it.path().filename();
                try {
                    if (std::regex_match(tmp, e)) {
                        retstr += tmp;
                        retstr += L"|";
                    }
                } catch (...) {
                    setlog("exception!");
                }
            }
        }
        if (!retstr.empty() && retstr.back() == L'|')
            retstr.pop_back();
    }
}
