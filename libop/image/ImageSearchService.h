#pragma once
#include "ImageSearchAlgorithms.h"
#include <memory>
#include <string>

namespace op::image {

/*
此类为图像处理，包含以下工作
1.像素比较，查找
2.颜色转化
3.图像定位
4.OCR（统一走 HTTP OCR 服务）
5....

同一个 ImageSearchService 实例会复用截图和算法工作区，不支持并发调用；
多线程场景应保持每个线程使用独立 Op 对象。
*/
class ImageSearchService : public ImageSearchAlgorithms {
  public:
    ImageSearchService();
    ~ImageSearchService();
    //
    long Capture(const std::wstring &file);

    long CmpColor(long x, long y, const std::wstring &scolor, double sim);

    long FindColor(const wstring &color, double sim, long dir, long &x, long &y);

    long FindColorEx(const wstring &color, double sim, long dir, wstring &retstr);

    long FindMultiColor(const wstring &first_color, const wstring &offset_color, double sim, long dir, long &x,
                        long &y);

    long FindMultiColorEx(const wstring &first_color, const wstring &offset_color, double sim, long dir,
                          wstring &retstr);
    // 图形定位
    long FindPic(const std::wstring &files, const wstring &delta_colors, double sim, long dir, long &x, long &y);
    //
    long FindPicEx(const std::wstring &files, const wstring &delta_colors, double sim, long dir, wstring &retstr,
                   bool returnID = true);

    long FindColorBlock(const wstring &color, double sim, long count, long height, long width, long &x, long &y);

    long FindColorBlockEx(const wstring &color, double sim, long count, long height, long width, wstring &retstr);

    std::wstring GetColor(long x, long y);

    long GetColorNum(const wstring &color, double sim);

    // 以下 OCR 接口统一调用 HTTP OCR 服务（HttpOcrService），不再支持本地点阵字库。
    // color/sim 参数保留是为了兼容既有调用方；HTTP OCR 只使用 sim 作为置信度过滤阈值。
    long OCR(const wstring &color, double sim, std::wstring &out_str);

    long OcrEx(const wstring &color, double sim, std::wstring &out_str);

    long FindStr(const wstring &str, const wstring &color, double sim, long &retx, long &rety);

    long FindStrEx(const wstring &str, const wstring &color, double sim, std::wstring &out_str);

    long OcrAuto(double sim, std::wstring &retstr);

    long OcrFromFile(const wstring &files, const wstring &color, double sim, std::wstring &retstr);

    long OcrAutoFromFile(const wstring &files, double sim, std::wstring &retstr);

    // 直线检测不依赖字库，只需要二值图。
    long FindLine(const wstring &color, double sim, wstring &retStr);

    long LoadPic(const wstring &files);

    long FreePic(const wstring &files);

    long LoadMemPic(const wstring &file_name, void *data, long size);

    long GetPicSize(const wstring &file_name, long *x, long *y);

  public:
    // 当前目录
    wstring _curr_path;
    // 是否允许 FindPic 自动缓存本地文件；LoadPic/LoadMemPic 属于显式加载，不受此开关影响。
    int _enable_cache;

  private:
    // RETURN TYPE 0:word colors info; 1:bk color info
    int str2colordfs(const wstring &color_str, std::vector<color_df_t> &colors);
    int str2colordfs(const wstring &color_str, std::vector<color_df_t> &colors, std::vector<bool> *explicit_dfs);
    void parse_multi_color_args(const wstring &first_color, const wstring &offset_color,
                                std::vector<color_df_t> &vfirst_color, std::vector<pt_cr_df_t> &voffset_cr);
    // 按颜色描述生成二值图；FindColorBlock 与 FindLine 共用。
    void str2binaryfbk(const wstring &color, double sim);
    void str2colors(const wstring &color, std::vector<color_t> &vcolor);
    // vmatches 是算法层使用的裸指针视图，holders 负责延长共享缓存对象的生命周期。
    void files2mats(const wstring &files, std::vector<PicMatchTemplate *> &vmatches, std::vector<wstring> &vstr,
                    std::vector<std::shared_ptr<Image>> &holders,
                    std::vector<std::shared_ptr<PicMatchTemplate>> &match_holders);
};

} // namespace op::image
