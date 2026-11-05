// #include "stdafx.h"
#include "ImageSearchService.h"
#include "../base/Utils.h"
#include "../ocr/OcrService.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <shared_mutex>
#include <sstream>

namespace op::image {

using op::ocr::HttpOcrService;

namespace {
// 普通找图模板缓存。放在进程级别，多个 Op 实例共享同一份已解码图片和预处理模板。
// 缓存项记录文件 (mtime,size)：同名文件被覆盖后自动失效（Twinkle 图色库会原地更新 PNG）。
// 显式 LoadPic/LoadMemPic 标记 explicit_load；EnablePicCache(0) 只影响 FindPic 的自动缓存。
struct PicCacheEntry {
    std::shared_ptr<Image> image;
    std::shared_ptr<PicMatchTemplate> match;
    bool explicit_load = false;
    bool has_file_stamp = false;
    unsigned long long file_size = 0;
    FILETIME file_mtime{};
};

std::map<wstring, PicCacheEntry> g_pic_cache;
std::shared_mutex g_pic_cache_mutex;

template <typename Target, typename Value> bool set_out(Target *target, Value value) {
    if (!target)
        return false;
    *target = static_cast<Target>(value);
    return true;
}

color_t sim_to_point_color_diff(double sim) {
    if (sim < 0.0 || sim > 1.0)
        sim = 1.0;

    const auto diff = static_cast<uchar>(std::ceil((1.0 - sim) * 255.0));
    color_t color_diff;
    color_diff.b = diff;
    color_diff.g = diff;
    color_diff.r = diff;
    return color_diff;
}

std::shared_ptr<PicMatchTemplate> make_pic_match(std::shared_ptr<Image> image) {
    if (!image || image->empty())
        return nullptr;

    auto match = std::make_shared<PicMatchTemplate>();
    build_pic_match_template(*image, *match);
    return match;
}

// 读取文件时间戳/大小；文件不存在返回 false。
bool query_file_stamp(const wstring &path, unsigned long long &size, FILETIME &mtime) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    size = (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    mtime = data.ftLastWriteTime;
    return true;
}

bool same_stamp(const PicCacheEntry &entry, unsigned long long size, const FILETIME &mtime) {
    return entry.file_size == size && entry.file_mtime.dwLowDateTime == mtime.dwLowDateTime &&
           entry.file_mtime.dwHighDateTime == mtime.dwHighDateTime;
}

// allow_auto=false 时忽略 FindPic 自动缓存（EnablePicCache(0)），显式 LoadPic 仍可用。
bool find_cached_pic(const wstring &key, std::shared_ptr<Image> &image, std::shared_ptr<PicMatchTemplate> &match,
                     bool allow_auto = true) {
    std::shared_lock<std::shared_mutex> lock(g_pic_cache_mutex);
    auto it = g_pic_cache.find(key);
    if (it == g_pic_cache.end())
        return false;

    const PicCacheEntry &entry = it->second;
    if (!entry.image || !entry.match)
        return false;
    if (!entry.explicit_load && !allow_auto)
        return false;
    if (entry.has_file_stamp) {
        unsigned long long size = 0;
        FILETIME mtime{};
        // 文件被覆盖/删除时视为未命中，下一轮按新文件重建。
        if (!query_file_stamp(key, size, mtime) || !same_stamp(entry, size, mtime))
            return false;
    }

    image = entry.image;
    match = entry.match;
    return true;
}

bool store_cached_pic(const wstring &key, std::shared_ptr<Image> image, std::shared_ptr<PicMatchTemplate> match,
                      bool explicit_load, const wstring *file_path) {
    if (key.empty() || !image || image->empty() || !match)
        return false;

    PicCacheEntry entry;
    entry.explicit_load = explicit_load;
    if (file_path) {
        unsigned long long size = 0;
        FILETIME mtime{};
        if (query_file_stamp(*file_path, size, mtime)) {
            entry.has_file_stamp = true;
            entry.file_size = size;
            entry.file_mtime = mtime;
        }
    }
    entry.image = std::move(image);
    entry.match = std::move(match);

    std::unique_lock<std::shared_mutex> lock(g_pic_cache_mutex);
    g_pic_cache[key] = std::move(entry);
    return true;
}

bool store_cached_pic(const wstring &key, std::shared_ptr<Image> image, bool explicit_load, const wstring *file_path) {
    auto match = make_pic_match(image);
    if (!match)
        return false;
    return store_cached_pic(key, std::move(image), std::move(match), explicit_load, file_path);
}

bool erase_cached_pic(const wstring &key) {
    std::unique_lock<std::shared_mutex> lock(g_pic_cache_mutex);
    return g_pic_cache.erase(key) > 0;
}

std::shared_ptr<Image> read_pic_file(const wstring &path) {
    auto image = std::make_shared<Image>();
    if (!image->read(path.data()) || image->empty())
        return nullptr;
    return image;
}

std::shared_ptr<Image> read_mem_pic(void *data, long size) {
    if (!data || size <= 0)
        return nullptr;

    auto image = std::make_shared<Image>();
    if (!image->read(data, size) || image->empty())
        return nullptr;
    return image;
}

} // namespace

ImageSearchService::ImageSearchService() {
    _enable_cache = 1;
}

ImageSearchService::~ImageSearchService() {
}

long ImageSearchService::Capture(const std::wstring &file) {
    std::filesystem::path fpath(file);
    if (!fpath.is_absolute())
        fpath = std::filesystem::path(_curr_path) / fpath;

    return _src.write(fpath.c_str());
}

long ImageSearchService::CmpColor(long x, long y, const std::wstring &scolor, double sim) {
    std::vector<color_df_t> vcolor;
    str2colordfs(scolor, vcolor);
    color_t color;
    if (!ImageSearchAlgorithms::GetPixel(x, y, color))
        return 0;
    return ImageSearchAlgorithms::CmpColor(color, vcolor, sim);
}

long ImageSearchService::FindColor(const wstring &color, double sim, long dir, long &x, long &y) {
    std::vector<color_df_t> colors;
    str2colordfs(color, colors);
    // setlog("%s cr size=%d",colors[0].color.tostr().data(), colors.size());
    // setlog("sim:,dir:%d", dir);
    return ImageSearchAlgorithms::FindColor(colors, sim, dir, x, y);
}

long ImageSearchService::FindColorEx(const wstring &color, double sim, long dir, wstring &retstr) {
    std::vector<color_df_t> colors;
    str2colordfs(color, colors);
    return ImageSearchAlgorithms::FindColorEx(colors, sim, dir, retstr);
}

void ImageSearchService::parse_multi_color_args(const wstring &first_color, const wstring &offset_color,
                                                std::vector<color_df_t> &vfirst_color,
                                                std::vector<pt_cr_df_t> &voffset_cr) {
    str2colordfs(first_color, vfirst_color);

    // offset_color 兼容旧格式: x|y|颜色描述，多段之间用英文逗号分隔。
    std::vector<wstring> vseconds;
    split(offset_color, vseconds, L",");
    voffset_cr.clear();
    for (auto &it : vseconds) {
        size_t id1, id2;
        id1 = it.find(L'|');
        id2 = (id1 == wstring::npos ? wstring::npos : it.find(L'|', id1 + 1));
        if (id2 != wstring::npos) {
            pt_cr_df_t tp;
            swscanf(it.c_str(), L"%d|%d", &tp.x, &tp.y);
            if (id2 + 1 != it.length())
                str2colordfs(it.substr(id2 + 1), tp.crdfs);
            else
                break;
            voffset_cr.push_back(tp);
        }
    }
}

long ImageSearchService::FindMultiColor(const wstring &first_color, const wstring &offset_color, double sim, long dir,
                                        long &x, long &y) {
    std::vector<color_df_t> vfirst_color;
    std::vector<pt_cr_df_t> voffset_cr;
    parse_multi_color_args(first_color, offset_color, vfirst_color, voffset_cr);
    return ImageSearchAlgorithms::FindMultiColor(vfirst_color, voffset_cr, sim, dir, x, y);
}

long ImageSearchService::FindMultiColorEx(const wstring &first_color, const wstring &offset_color, double sim, long dir,
                                          wstring &retstr) {
    std::vector<color_df_t> vfirst_color;
    std::vector<pt_cr_df_t> voffset_cr;
    parse_multi_color_args(first_color, offset_color, vfirst_color, voffset_cr);
    return ImageSearchAlgorithms::FindMultiColorEx(vfirst_color, voffset_cr, sim, dir, retstr);
}
// 图形定位
long ImageSearchService::FindPic(const std::wstring &files, const wstring &delta_colors, double sim, long dir, long &x,
                        long &y) {
    vector<PicMatchTemplate *> vmatches;
    // 算法层沿用裸指针入参，这里用 holders 保证匹配期间图片对象仍然存活。
    vector<std::shared_ptr<Image>> holders;
    vector<std::shared_ptr<PicMatchTemplate>> match_holders;
    color_t dfcolor;
    vector<std::wstring> vpic_name;
    files2mats(files, vmatches, vpic_name, holders, match_holders);
    dfcolor.str2color(delta_colors);
    sim = 0.5 + sim / 2;
    // 线程版 FindPicTh/FindPicExTh 已整体移除：该路径在 MuMu 子窗口/服务进程里恒返回 -1，
    // 且每个 Op 实例会预建 hardware_concurrency() 个空闲线程（见 REVIEW_OPTIMIZATION.md O1）。
    long ret = ImageSearchAlgorithms::FindPic(vmatches, dfcolor, sim, dir, x, y);
    return ret;
}
//
long ImageSearchService::FindPicEx(const std::wstring &files, const wstring &delta_colors, double sim, long dir, wstring &retstr,
                          bool returnID) {
    vector<PicMatchTemplate *> vmatches;
    // 算法层沿用裸指针入参，这里用 holders 保证匹配期间图片对象仍然存活。
    vector<std::shared_ptr<Image>> holders;
    vector<std::shared_ptr<PicMatchTemplate>> match_holders;
    vpoint_desc_t vpd;
    color_t dfcolor;
    vector<std::wstring> vpic_name;
    files2mats(files, vmatches, vpic_name, holders, match_holders);
    dfcolor.str2color(delta_colors);
    sim = 0.5 + sim / 2;
    long ret = ImageSearchAlgorithms::FindPicEx(vmatches, dfcolor, sim, dir, vpd);
    std::wstringstream ss(std::wstringstream::in | std::wstringstream::out);
    if (returnID) {
        for (auto &it : vpd) {
            ss << it.id << L"," << it.pos << L"|";
        }
    } else {
        for (auto &it : vpd) {
            ss << vpic_name[it.id] << L"," << it.pos << L"|";
        }
    }
    retstr = ss.str();
    if (vpd.size())
        retstr.pop_back();
    return ret;
}

long ImageSearchService::FindColorBlock(const wstring &color, double sim, long count, long height, long width, long &x,
                               long &y) {
    str2binaryfbk(color, sim);
    return ImageSearchAlgorithms::FindColorBlock(count, height, width, x, y);
}

long ImageSearchService::FindColorBlockEx(const wstring &color, double sim, long count, long height, long width,
                                 wstring &retstr) {
    str2binaryfbk(color, sim);
    return ImageSearchAlgorithms::FindColorBlockEx(count, height, width, retstr);
}

long ImageSearchService::GetColorNum(const wstring &color, double sim) {
    std::vector<color_df_t> colors;
    str2colordfs(color, colors);
    return ImageSearchAlgorithms::FindColorNum(colors, sim);
}

long ImageSearchService::OCR(const wstring &color, double sim, std::wstring &out_str) {
    // 统一走 HTTP OCR 服务；color 参数只为兼容既有调用方保留。
    (void)color;
    out_str.clear();
    if (sim < 0. || sim > 1.)
        sim = 1.;

    vocr_rec_t res;
    const int ocr_ret = HttpOcrService::getInstance()->ocr(_src.pdata, _src.width, _src.height, 4, res);
    if (ocr_ret < 0) {
        // 与“识别成功但没有文字”区分：后端不可用/超时/解析失败时返回 0，
        // 让上层能停止轮询，而不是把失败伪装成空结果。
        setlog("OCR HTTP request failed: ret=%d", ocr_ret);
        return 0;
    }
    // OCR 结果是框列表；Ocr/OcrEx/FindStr* 统一按 (9px 行带, x) 的阅读顺序输出，
    // 否则同一批结果在两套 API 里可能拼出不同的字符串。
    std::stable_sort(res.begin(), res.end(), [](const ocr_rec_t &a, const ocr_rec_t &b) {
        return a.left_top < b.left_top;
    });
    for (auto &it : res) {
        if (it.confidence >= sim - 1e-9) {
            out_str += it.text;
        }
    }
    return 1;
}

wstring ImageSearchService::GetColor(long x, long y) {
    color_t cr;
    if (ImageSearchAlgorithms::GetPixel(x, y, cr)) {
        return _s2wstring(cr.tostr());
    } else {
        return L"";
    }
}

int ImageSearchService::str2colordfs(const wstring &color_str, std::vector<color_df_t> &colors) {
    return str2colordfs(color_str, colors, nullptr);
}

int ImageSearchService::str2colordfs(const wstring &color_str, std::vector<color_df_t> &colors,
                                     std::vector<bool> *explicit_dfs) {
    colors.clear();
    if (explicit_dfs)
        explicit_dfs->clear();
    if (color_str.empty()) { // default
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(_color_cache_mutex);
        auto it = _color_cache.find(color_str);
        if (it != _color_cache.end()) {
            colors = it->second.colors;
            if (explicit_dfs)
                *explicit_dfs = it->second.explicit_dfs;
            return it->second.kind;
        }
    }

    ColorParseCache parsed;
    if (color_str[0] == L'@') { // bk color info
        parsed.kind = 1;
    }

    const wstring body = parsed.kind ? color_str.substr(1) : color_str;
    std::vector<wstring> vstr, vstr2;
    color_df_t cr;
    split(body, vstr, L"|");
    for (auto &it : vstr) {
        split(it, vstr2, L"-");
        cr.color.str2color(vstr2[0]);
        const bool has_explicit_df = vstr2.size() == 2;
        cr.df.str2color(has_explicit_df ? vstr2[1] : L"000000");
        parsed.colors.push_back(cr);
        parsed.explicit_dfs.push_back(has_explicit_df);
    }

    {
        std::lock_guard<std::mutex> lock(_color_cache_mutex);
        if (_color_cache.size() >= kColorCacheLimit)
            _color_cache.clear(); // 颜色串通常远少于上限，整体清空即可。
        _color_cache.emplace(color_str, parsed);
    }

    colors = parsed.colors;
    if (explicit_dfs)
        *explicit_dfs = parsed.explicit_dfs;
    return parsed.kind;
}

void ImageSearchService::str2colors(const wstring &color, std::vector<color_t> &vcolor) {
    std::vector<wstring> vstr, vstr2;
    color_t cr;
    vcolor.clear();
    split(color, vstr, L"|");
    for (auto &it : vstr) {
        cr.str2color(it);
        vcolor.push_back(cr);
    }
}

long ImageSearchService::LoadPic(const wstring &files) {
    std::vector<wstring> vstr;
    int loaded = 0;
    split(files, vstr, L"|");
    wstring tp;
    for (auto &it : vstr) {
        // 显式加载会按当前文件内容刷新全局缓存，EnablePicCache 只影响 FindPic 的自动缓存。
        if (!Path2GlobalPath(it, _curr_path, tp))
            continue;

        auto image = read_pic_file(tp);
        if (!image)
            continue;

        store_cached_pic(tp, std::move(image), /*explicit_load=*/true, &tp);
        loaded++;
    }
    return loaded;
}

long ImageSearchService::FreePic(const wstring &files) {
    std::vector<wstring> vstr;
    int loaded = 0;
    split(files, vstr, L"|");
    wstring tp;
    for (auto &it : vstr) {
        // 先按内存图片名或完整 key 删除。
        if (erase_cached_pic(it)) {
            loaded++;
            continue;
        }

        // 没查到再按资源目录解析后的文件路径删除。
        if (Path2GlobalPath(it, _curr_path, tp) && tp != it && erase_cached_pic(tp)) {
            loaded++;
        }
    }
    return loaded;
}

long ImageSearchService::LoadMemPic(const wstring &file_name, void *data, long size) {
    try {
        if (file_name.empty())
            return 0;

        auto image = read_mem_pic(data, size);
        if (!image)
            return 0;

        // 同名内存图按新内容覆盖，避免全局缓存复用到旧模板。
        return store_cached_pic(file_name, std::move(image), /*explicit_load=*/true, nullptr) ? 1 : 0;
    } catch (...) {
        return 0;
    }
}

long ImageSearchService::GetPicSize(const wstring &file_name, long *width, long *height) {
    set_out(width, 0L);
    set_out(height, 0L);

    std::shared_ptr<Image> image;
    std::shared_ptr<PicMatchTemplate> match;
    find_cached_pic(file_name, image, match, _enable_cache != 0);
    if (!image) {
        wstring tp;
        if (Path2GlobalPath(file_name, _curr_path, tp))
            find_cached_pic(tp, image, match, _enable_cache != 0);
    }

    if (image) {
        if (!set_out(width, image->width) || !set_out(height, image->height))
            return 0;
        return 1;
    }

    // 缓存未命中时按文件读尺寸（只解析文件头，不驻留缓存），与 FindPic 语义对齐。
    wstring tp;
    if (!Path2GlobalPath(file_name, _curr_path, tp))
        return 0;
    auto disk = read_pic_file(tp);
    if (!disk)
        return 0;
    if (!set_out(width, disk->width) || !set_out(height, disk->height))
        return 0;
    return 1;
}

// FindColorBlock/FindColorBlockEx/FindLine 共用的二值化入口：按颜色描述生成二值图，
// 未显式给出色差时用 sim 换算的隐式容差。
void ImageSearchService::str2binaryfbk(const wstring &color, double sim) {
    vector<color_df_t> colors;
    vector<bool> explicit_dfs;
    if (str2colordfs(color, colors, &explicit_dfs) == 0) {
        const auto implicit_df = sim_to_point_color_diff(sim);
        for (size_t i = 0; i < colors.size() && i < explicit_dfs.size(); ++i) {
            if (!explicit_dfs[i])
                colors[i].df = implicit_df;
        }
        bgr2binary(colors);
    } else {
        bgr2binarybk(colors);
    }
}

void ImageSearchService::files2mats(const wstring &files, std::vector<PicMatchTemplate *> &vmatches,
                                    std::vector<wstring> &vstr, std::vector<std::shared_ptr<Image>> &holders,
                                    std::vector<std::shared_ptr<PicMatchTemplate>> &match_holders) {
    // std::vector<wstring>vstr, vstr2;
    vmatches.clear();
    vstr.clear();
    holders.clear();
    match_holders.clear();
    std::vector<wstring> names;
    split(files, names, L"|");
    wstring tp;
    for (auto &it : names) {
        // 先按原始名字查找，覆盖 LoadMemPic 名称和完整路径两种情况。
        std::shared_ptr<Image> image;
        std::shared_ptr<PicMatchTemplate> match;
        find_cached_pic(it, image, match, _enable_cache != 0);
        if (!image) {
            if (!Path2GlobalPath(it, _curr_path, tp))
                continue;
            find_cached_pic(tp, image, match, _enable_cache != 0);
            if (!image) {
                image = read_pic_file(tp);
                if (!image)
                    continue;
                match = make_pic_match(image);
                if (!match)
                    continue;
                // 自动读取本地文件时，只有开启缓存才写入全局缓存。
                if (_enable_cache)
                    store_cached_pic(tp, image, match, /*explicit_load=*/false, &tp);
            }
        }

        holders.push_back(image);
        match_holders.push_back(match);
        vmatches.push_back(match.get());
        vstr.push_back(it);
    }
}

long ImageSearchService::OcrEx(const wstring &color, double sim, std::wstring &retstr) {
    // 统一走 HTTP OCR 服务；color 参数只为兼容既有调用方保留。
    (void)color;
    retstr.clear();
    if (sim < 0. || sim > 1.)
        sim = 1.;

    vocr_rec_t res;
    int find_ct = 0;
    const int ocr_ret = HttpOcrService::getInstance()->ocr(_src.pdata, _src.width, _src.height, 4, res);
    if (ocr_ret < 0)
        return 0;
    std::stable_sort(res.begin(), res.end(), [](const ocr_rec_t &a, const ocr_rec_t &b) {
        return a.left_top < b.left_top;
    });
    for (auto &it : res) {
        if (it.confidence >= sim - 1e-9) {
            retstr += std::to_wstring(it.left_top.x + _x1 + _dx);
            retstr += L",";
            retstr += std::to_wstring(it.left_top.y + _y1 + _dy);
            retstr += L",";
            retstr += it.text;
            retstr += L"|";
            ++find_ct;
            if (find_ct >= _max_return_obj_ct)
                break;
        }
    }
    if (!retstr.empty() && retstr.back() == L'|')
        retstr.pop_back();
    return find_ct;
}

long ImageSearchService::FindStr(const wstring &str, const wstring &color, double sim, long &retx, long &rety) {
    // 统一走 HTTP OCR 服务；color 参数只为兼容既有调用方保留。
    (void)color;
    vector<wstring> vstr;
    split(str, vstr, L"|");
    if (sim < 0. || sim > 1.)
        sim = 1.;

    std::vector<ocr_rec_t> ocr_res;
    vocr_rec_t res;
    HttpOcrService::getInstance()->ocr(_src.pdata, _src.width, _src.height, 4, res);
    for (auto &it : res) {
        if (it.confidence >= sim - 1e-9) {
            ocr_res.push_back(it);
        }
    }
    return ImageSearchAlgorithms::FindStr(ocr_res, vstr, retx, rety);
}

long ImageSearchService::FindStrEx(const wstring &str, const wstring &color, double sim, std::wstring &out_str) {
    // 统一走 HTTP OCR 服务；color 参数只为兼容既有调用方保留。
    (void)color;
    out_str.clear();
    vector<wstring> vstr;
    split(str, vstr, L"|");
    if (sim < 0. || sim > 1.)
        sim = 1.;

    std::vector<ocr_rec_t> ocr_res;
    vocr_rec_t res;
    HttpOcrService::getInstance()->ocr(_src.pdata, _src.width, _src.height, 4, res);
    for (auto &it : res) {
        if (it.confidence >= sim - 1e-9) {
            ocr_res.push_back(it);
        }
    }
    return ImageSearchAlgorithms::FindStrEx(ocr_res, vstr, out_str);
}

long ImageSearchService::OcrAuto(double sim, std::wstring &retstr) {
    return OCR(L"", sim, retstr);
}

long ImageSearchService::OcrFromFile(const wstring &files, const wstring &color, double sim, std::wstring &retstr) {
    retstr.clear();
    if (sim < 0. || sim > 1.)
        sim = 1.;
    wstring fullpath;
    if (Path2GlobalPath(files, _curr_path, fullpath)) {
        if (!_src.read(fullpath.data()))
            return 0;
        invalidate_frame_cache();
        return OCR(color, sim, retstr);
    }
    return 0;
}

long ImageSearchService::OcrAutoFromFile(const wstring &files, double sim, std::wstring &retstr) {
    retstr.clear();
    if (sim < 0. || sim > 1.)
        sim = 1.;
    wstring fullpath;

    if (Path2GlobalPath(files, _curr_path, fullpath)) {
        if (!_src.read(fullpath.data()))
            return 0;
        invalidate_frame_cache();
        return OCR(L"", sim, retstr);
    }
    return 0;
}

long long ImageSearchService::FindLine(const wstring &color, double sim, wstring &retStr) {
    retStr.clear();
    if (sim < 0. || sim > 1.)
        sim = 1.;
    // FindLine 只需要二值图，不依赖字库。二值预处理固定使用默认值（关闭），
    // 与历史默认状态一致。
    str2binaryfbk(color, sim);
    return ImageSearchAlgorithms::FindLine(retStr);
}

} // namespace op::image
