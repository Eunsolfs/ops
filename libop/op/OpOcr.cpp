#include "OpContext.h"
#include "OpCaptureHelpers.h"
#include "OpResult.h"

#include "ocr/OcrService.h"
#include "base/Utils.h"

#include <libop.h>

#include <string>
#include <vector>

long op::Op::SetOcrEngine(const wchar_t *path_of_engine, const wchar_t *dll_name, const wchar_t *argv) {
    string argvs = argv ? _ws2string(argv) : "";
    vector<string> vstr;
    split(argvs, vstr, " ");
    const std::wstring engine = path_of_engine ? path_of_engine : L"";
    const std::wstring dll = dll_name ? dll_name : L"";
    return op::ocr::HttpOcrService::getInstance()->init(engine, dll, vstr) == 0 ? 1 : 0;
}

// 识别屏幕范围(x1,y1,x2,y2)内符合color_format的字符串,并且相似度为sim,sim取值范围(0.1-1.0),
void op::Op::Ocr(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, std::wstring &retstr) {
    wstring str;
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.OCR(color, sim, str);
    });
    retstr = str;
}
// 回识别到的字符串，以及每个字符的坐标.
void op::Op::OcrEx(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, std::wstring &retstr) {
    wstring str;
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.OcrEx(color, sim, str);
    });
    retstr = str;
}
// 在屏幕范围(x1,y1,x2,y2)内,查找string(可以是任意个字符串的组合),并返回符合color_format的坐标位置
void op::Op::FindStr(long x1, long y1, long x2, long y2, const wchar_t *strs, const wchar_t *color, double sim,
                    long *retx, long *rety, long *ret) {
    long x = -1;
    long y = -1;
    internal::set_result(retx, x);
    internal::set_result(rety, y);
    internal::set_result(ret, 0L);
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        internal::set_result(ret, m_context->image_proc.FindStr(strs, color, sim, x, y));
        internal::set_result(retx, x);
        internal::set_result(rety, y);
    });
}
// 返回符合color_format的所有坐标位置
void op::Op::FindStrEx(long x1, long y1, long x2, long y2, const wchar_t *strs, const wchar_t *color, double sim,
                      std::wstring &retstr) {
    std::wstring str;
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2,
                                   [&]() { m_context->image_proc.FindStrEx(strs, color, sim, str); });
    retstr = str;
}

void op::Op::OcrAuto(long x1, long y1, long x2, long y2, double sim, std::wstring &retstr) {
    wstring str;
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2, [&]() {
        m_context->image_proc.OcrAuto(sim, str);
    });
    retstr = str;
}

// 从文件中识别图片
void op::Op::OcrFromFile(const wchar_t *file_name, const wchar_t *color_format, double sim, std::wstring &retstr) {
    wstring str;
    m_context->image_proc.OcrFromFile(file_name, color_format, sim, str);
    retstr = str;
}
// 从文件中识别图片,无需指定颜色
void op::Op::OcrAutoFromFile(const wchar_t *file_name, double sim, std::wstring &retstr) {
    wstring str;
    m_context->image_proc.OcrAutoFromFile(file_name, sim, str);
    retstr = str;
}

void op::Op::FindLine(long x1, long y1, long x2, long y2, const wchar_t *color, double sim, wstring &retstr) {
    retstr.clear();
    internal::with_captured_region(m_context.get(), x1, y1, x2, y2,
                                   [&]() { m_context->image_proc.FindLine(color, sim, retstr); });
}
