#pragma once
#ifndef OP_BASE_UTILS_H_
#define OP_BASE_UTILS_H_
#include "Types.h"
std::wstring _s2wstring(const std::string &s);
std::string _ws2string(const std::wstring &s);

std::string utf8_to_ansi(std::string strUTF8);
// UTF-8 -> 宽字符（不走 CP936/CP_ACP 中转，保留中文/emoji/日文等非 ANSI 字符）。
// 放在头文件里是为了让 op_test 等不导出内部符号的调用方也能直接使用。
inline std::wstring utf8_to_wstring(const std::string &strUTF8) {
    if (strUTF8.empty())
        return {};

    const int length = static_cast<int>(strUTF8.size());
    int wide_length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, strUTF8.data(), length, nullptr, 0);
    DWORD flags = MB_ERR_INVALID_CHARS;
    if (wide_length <= 0) {
        flags = 0;
        wide_length = MultiByteToWideChar(CP_UTF8, flags, strUTF8.data(), length, nullptr, 0);
    }
    if (wide_length <= 0)
        return {};

    std::wstring wide(static_cast<size_t>(wide_length), L'\0');
    MultiByteToWideChar(CP_UTF8, flags, strUTF8.data(), length, wide.data(), wide_length);
    return wide;
}
// 将路径转化为全局路径
long Path2GlobalPath(const std::wstring &file, const std::wstring &curr_path, std::wstring &out);

void split(const std::wstring &s, std::vector<std::wstring> &v, const std::wstring &c);
void split(const std::string &s, std::vector<std::string> &v, const std::string &c);

void wstring2upper(std::wstring &s);
void string2upper(std::string &s);

void wstring2lower(std::wstring &s);
void string2lower(std::string &s);

void replacea(std::string &str, const std::string &oldval, const std::string &newval);
void replacew(std::wstring &str, const std::wstring &oldval, const std::wstring &newval);

// for debug
long setlog(const wchar_t *format, ...);
//
long setlog(const char *format, ...);

// Returns the last Win32 error, in string format. Returns an empty string if there is no error.
std::string GetLastErrorAsString();

int inline hex2bin(int c) {
    return c <= L'9' ? c - L'0' : c - L'A' + 10;
};

int inline bin2hex(int c) {
    int ans = 0;
    int c1 = c >> 4 & 0xf;
    int c2 = c & 0xf;
    ans |= (c1 <= 9 ? c1 + L'0' : c1 + 'A' - 10) << 8;
    ans |= c2 <= 9 ? c2 + L'0' : c2 + 'A' - 10;
    return ans;
};

constexpr int PTY(op::uint pt) {
    return pt >> 16;
}

constexpr int PTX(op::uint pt) {
    return pt & 0xffff;
}

template <typename T> void nextVal(const T &t, int *next) {
    next[0] = -1;
    int k = -1, j = 0;
    while (j < (int)t.size() - 1) {
        if (k == -1 || t[k] == t[j]) {
            k++;
            j++;
            next[j] = k;
        } else {
            k = next[k];
        }
    }
}
template <typename T> int kmp(const T &s, const T &t) {
    std::vector<int> next(t.size());
    nextVal(t, next.data());
    int i = 0, j = 0;
    while (i < (int)s.size() && j < (int)t.size()) {
        if (j == -1 || s[i] == t[j]) {
            i++;
            j++;
        } else {
            j = next[j];
        }
    }
    return j == s.size() ? i - j : -1;
}

namespace op {
std::ostream &operator<<(std::ostream &o, point_t const &rhs);
std::wostream &operator<<(std::wostream &o, point_t const &rhs);
} // namespace op

bool Delay(long mis);
bool Delays(long mis_min, long mis_max);

#endif // OP_BASE_UTILS_H_
