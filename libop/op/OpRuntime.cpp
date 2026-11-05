#include "OpContext.h"
#include "OpResult.h"

#include "base/Environment.h"
#include "base/Utils.h"

#include <libop.h>
#include <Shlwapi.h>
#include <tchar.h>

std::wstring op::Op::Ver() {
    return _T(OP_VERSION);
}

bool op::Op::SetProcessDpiAware(bool enable) {
    return internal::set_process_dpi_aware(enable);
}

bool op::Op::IsProcessDpiAware() {
    return internal::process_dpi_aware_actual();
}

namespace {
// Windows 分隔符；用常量避免转义歧义。
const wchar_t kBackslash = static_cast<wchar_t>(0x5C);
// 统一转成绝对路径，避免相对路径在换工作目录或多次 SetPath 后叠加。
std::wstring absolute_path(const std::wstring &path) {
    const DWORD needed = ::GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
        return path;

    std::wstring out(needed, L'\0');
    const DWORD len = ::GetFullPathNameW(path.c_str(), needed, out.data(), nullptr);
    if (len == 0 || len >= needed)
        return path;

    out.resize(len);
    // 去掉尾部分隔符（保留 "C:\" 这种根路径）。
    while (out.size() > 3 && (out.back() == kBackslash || out.back() == L'/'))
        out.pop_back();
    return out;
}
} // namespace

void op::Op::SetPath(const wchar_t *path, long *ret) {
    if (!path) {
        setlog(L"SetPath: path is null");
        internal::set_result(ret, 0L);
        return;
    }

    std::wstring fpath = path;
    replacew(fpath, L"/", L"\\");

    std::wstring resolved;
    if (::PathFileExistsW(fpath.data())) {
        resolved = fpath;
    } else {
        // 相对路径按当前资源目录解析，保持旧行为。
        if (!fpath.empty() && fpath[0] != kBackslash)
            fpath = m_context->curr_path + L"\\" + fpath;
        else
            fpath = m_context->curr_path + fpath;

        if (!::PathFileExistsW(fpath.data())) {
            setlog("path '%s' not exists", fpath.data());
            internal::set_result(ret, 0L);
            return;
        }
        resolved = fpath;
    }

    m_context->curr_path = absolute_path(resolved);
    m_context->image_proc._curr_path = m_context->curr_path;
    m_context->bkproc._curr_path = m_context->curr_path;
    internal::set_result(ret, 1L);
}

void op::Op::GetPath(std::wstring &path) {
    path = m_context->curr_path;
}

void op::Op::GetBasePath(std::wstring &path) {
    path = RuntimeEnvironment::getBasePath();
}

void op::Op::GetID(long *ret) {
    internal::set_result(ret, m_context->id);
}

void op::Op::GetLastError(long *ret) {
    internal::set_result(ret, ::GetLastError());
}

void op::Op::SetShowErrorMsg(long show_type, long *ret) {
    RuntimeEnvironment::m_showErrorMsg = show_type;
    internal::set_result(ret, 1L);
}

void op::Op::Sleep(long millseconds, long *ret) {
    ::Sleep(millseconds);
    internal::set_result(ret, 1L);
}

void op::Op::Delay(long mis, long *ret) {
    internal::set_result(ret, ::Delay(mis));
}

void op::Op::Delays(long mis_min, long mis_max, long *ret) {
    internal::set_result(ret, ::Delays(mis_min, mis_max));
}
