// #include "stdafx.h"
#include "GdiCapture.h"

#include <atlimage.h>

#include <cstddef>
#include <fstream>
#include <span>

#include "../../window/WindowService.h"
#include "../../image/Image.h"
#include "../../base/AutomationModes.h"
#include "../../base/Utils.h"

namespace {

void release_capture_bitmap(HDC memory_dc, HBITMAP &capture_bitmap, HBITMAP &original_bitmap) {
    if (!capture_bitmap)
        return;

    // 删除位图前先把内存 DC 恢复到原始对象，避免删除仍被选中的 GDI 对象。
    if (memory_dc && original_bitmap) {
        SelectObject(memory_dc, original_bitmap);
    }

    DeleteObject(capture_bitmap);
    capture_bitmap = NULL;
    original_bitmap = NULL;
}

void release_memory_dc(HDC &memory_dc) {
    if (!memory_dc)
        return;

    DeleteDC(memory_dc);
    memory_dc = NULL;
}

std::span<std::byte> shared_pixels(op::SharedMemory *shared_memory, int width, int height) {
    const auto pixelBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    return {shared_memory->data<std::byte>() + sizeof(op::capture::FrameInfo), pixelBytes};
}

std::span<const std::byte> gdi_row(std::span<const std::byte> pixels, int src_width, int row, int x, int width) {
    const auto offset = (static_cast<size_t>(row) * static_cast<size_t>(src_width) + static_cast<size_t>(x)) * 4;
    return pixels.subspan(offset, static_cast<size_t>(width) * 4);
}

} // namespace

namespace op::capture {

GdiCapture::GdiCapture() {
    _render_type = 0;
    dx_ = 0;
    dy_ = 0;
    // 4*2^22=16*2^20=16MB
    //_image_data = new byte[MAX_IMAGE_WIDTH*MAX_IMAGE_WIDTH * 4];
}

GdiCapture::~GdiCapture() {
    // SAFE_DELETE_ARRAY(_image_data);
}

long GdiCapture::BindEx(HWND hwnd, long render_type) {
    if (!::IsWindow(hwnd))
        return 0;
    _hwnd = hwnd;
    _render_type = render_type;

    if (render_type == RDT_NORMAL) {
        RECT rc, rc2;
        ::GetWindowRect(_hwnd, &rc);
        ::GetClientRect(hwnd, &rc2);

        _width = rc2.right - rc2.left;
        _height = rc2.bottom - rc2.top;
        POINT pt = {0};
        ::ClientToScreen(hwnd, &pt);
        dx_ = pt.x - rc.left;
        dy_ = pt.y - rc.top;
        _hdc = ::GetDC(NULL);
    } else { // client size
        RECT rc, rc2;
        ::GetWindowRect(_hwnd, &rc);
        ::GetClientRect(hwnd, &rc2);
        _width = rc2.right - rc2.left;
        _height = rc2.bottom - rc2.top;
        POINT pt = {0};
        ::ClientToScreen(hwnd, &pt);
        dx_ = pt.x - rc.left;
        dy_ = pt.y - rc.top;
        /*setlog("dx=%d dy=%d", dx_, dy_);*/
        if (_render_type == RDT_GDI) {
            _hdc = ::GetDC(_hwnd);
            // SetWindowLongA(topHwnd, GWL_EXSTYLE, dwExStyle | WS_EX_LAYERED);
            // UpdateWindow(topHwnd);

        } else if (RDT_GDI_DX2 == render_type) {
            _hdc = ::GetDC(_hwnd);
            _device_caps = GetDeviceCaps(_hdc, BITSPIXEL);
        } else {
            /*  HWND dx2TopHwnd = WindowService::GetTopWindowSp(_hwnd);
              GetPropA(dx2TopHwnd, "opstyle");
              long dx2ExStyle = GetWindowLongA(dx2TopHwnd, GWL_EXSTYLE);*/
            /*if (GetPropA(dx2TopHwnd, "opstyle_flag")) {
              dx2ExStyle = GetWindowLongA(dx2TopHwnd, GWL_EXSTYLE);
            } else {
              dx2ExStyle = GetWindowLongA(dx2TopHwnd, GWL_EXSTYLE);
              SetPropA(dx2TopHwnd, "opstyle", (HANDLE)dx2ExStyle);
              SetPropA(dx2TopHwnd, "opstyle_flag", (HANDLE)HANDLE_FLAG_INHERIT);
            }
          */
            /* SetWindowLongA(dx2TopHwnd, GWL_EXSTYLE, dx2ExStyle | WS_EX_LAYERED);
             UpdateWindow(dx2TopHwnd);*/
            _hdc = ::GetDC(_hwnd);
        }
    }

    if (_hdc == NULL) {
        setlog("hdc == NULL", _hdc);
        return 0;
    }

    // 创建一个与指定设备兼容的内存设备上下文环境
    _hmdc = CreateCompatibleDC(_hdc);
    if (_hmdc == NULL) {
        setlog("CreateCompatibleDC false");
        release_device_context();
        return -2;
    }

    // updata_screen();
    return 1;
}

long GdiCapture::UnBindEx() {
    // setlog("bkgdi::UnBindEx()");
    release_capture_bitmap(_hmdc, _hbmpscreen, _hbmp_old);
    _bmp_width = 0;
    _bmp_height = 0;

    // GetDC 得到的窗口/桌面 DC 必须用 ReleaseDC 归还。
    if (_hdc) {
        release_device_context();
    }
    release_memory_dc(_hmdc);

    return 1;
}

bool GdiCapture::requestCapture(int x1, int y1, int w, int h, Image &img) {
    // step 1.判断 窗口是否存在
    if (!::IsWindow(_hwnd))
        return 0;
    if (!_hdc || !_hmdc || !_shmem) {
        setlog("GdiCapture::requestCapture resources not initialized");
        return false;
    }
    if (w <= 0 || h <= 0) {
        setlog("GdiCapture::requestCapture invalid size %dx%d", w, h);
        return false;
    }

    img.create(w, h);
    if (!ensure_shared_capacity(sizeof(FrameInfo) + static_cast<size_t>(w) * static_cast<size_t>(h) * 4)) {
        return false;
    }
    if (_render_type == RDT_NORMAL) { // normal 拷贝的大小为实际需要的大小

        //
        /*	int w = rect.right - rect.left;
                int h = rect.bottom - rect.top;*/
        // setlog("_w w=%d %d _h h=%d %d,dx=%d dy=%d", _width, w, _height, h, dx_,
        // dy_);
        if (!ensure_capture_bitmap(w, h))
            return false;

        _bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        _bfh.bfSize = _bfh.bfOffBits + w * h * 4;
        _bfh.bfType = static_cast<WORD>(0x4d42);

        _bih.biBitCount = 32; // 每个像素字节大小
        _bih.biCompression = BI_RGB;
        _bih.biHeight = h; // 高度
        _bih.biPlanes = 1;
        _bih.biSize = sizeof(BITMAPINFOHEADER);
        _bih.biSizeImage = w * h * 4; // 图像数据大小
        _bih.biWidth = w;             // 宽度

        // 对指定的源设备环境区域中的像素进行位块（bit_block）转换

        RECT rc;
        ::GetWindowRect(_hwnd, &rc);
        // setlog("rect left =%d top =%d, dx =%d, dy = %d", rc.left, rc.top, dx_,
        // dy_);
        int src_x = x1 + rc.left + dx_;
        int src_y = y1 + rc.top + dy_;
        if (BitBlt(_hmdc, 0, 0, w, h, _hdc, src_x, src_y, SRCCOPY)) {
            // ok
        } else {
            setlog("error in bitbit");
            release_capture_bitmap(_hmdc, _hbmpscreen, _hbmp_old);
            return false;
        }

        // 函数获取指定兼容位图的位，然后将其作一个DIB—设备无关位图（Device-Independent
        //  Bitmap）使用的指定格式复制到一个缓冲区中 _pmutex->lock();
        uchar *pshare = _shmem->data<byte>();
        fmtFrameInfo(pshare, _hwnd, w, h);
        GetDIBits(_hmdc, _hbmpscreen, 0L, (DWORD)h, pshare + sizeof(FrameInfo), (LPBITMAPINFO)&_bih,
                  (DWORD)DIB_RGB_COLORS);

        //_pmutex->unlock();

        // 将数据拷贝到目标注意实际数据是反的
        auto pixels = shared_pixels(_shmem, w, h);

        for (int i = 0; i < h; i++) {
            // GDI DIB 默认自底向上，span 行视图让翻转行拷贝的偏移更直观。
            const auto row = gdi_row(pixels, w, h - 1 - i, 0, w);
            memcpy(img.ptr<uchar>(i), row.data(), row.size());
        }
    } else if (RDT_GDI_DX2 == _render_type) {
        ATL::CImage image;
        image.Create(w, h, _device_caps);
        BitBlt(image.GetDC(), 0, 0, w, h, _hdc, x1, y1, SRCCOPY);
        img.read(&image);
        image.ReleaseDC();
        // 该分支不写像素到共享内存，但仍需推进 frameId，避免 FindPic 灰度缓存跨帧复用。
        fmtFrameInfo(_shmem->data<byte>(), _hwnd, w, h);
    } else { // gdi ... 由于printwindow 函数的原因
             // 截取大小为实际的窗口大小，在后续的处理中，需要转化成客户区大小
        //
        RECT rc;
        ::GetWindowRect(_hwnd, &rc);
        int ww = rc.right - rc.left;
        int wh = rc.bottom - rc.top;
        if (ww <= 0 || wh <= 0) {
            setlog("GdiCapture::requestCapture invalid window size %dx%d", ww, wh);
            return false;
        }
        // 窗口可能在 Bind 后被放大：共享内存仍是绑定时尺寸，先校验再 GetDIBits，
        // 否则 ww*wh*4 会越过映射末尾。窗口变化后应重新 BindWindow。
        if (!ensure_shared_capacity(sizeof(FrameInfo) + static_cast<size_t>(ww) * static_cast<size_t>(wh) * 4)) {
            return false;
        }
        // 客户区请求区域映射回窗口位图后必须仍在 [0,ww)x[0,wh) 内，
        // 否则 gdi_row 的行号会下溢成巨大的 size_t。
        const int src_x = x1 + dx_;
        const int src_y = y1 + dy_;
        if (src_x < 0 || src_y < 0 || w > ww - src_x || h > wh - src_y) {
            setlog("GdiCapture::requestCapture region out of window: src=%d,%d size=%dx%d window=%dx%d", src_x, src_y, w,
                   h, ww, wh);
            return false;
        }
        // setlog("_w w=%d %d _h h=%d %d,dx=%d dy=%d", _width, w, _height, h, dx_,
        // dy_);
        if (!ensure_capture_bitmap(ww, wh))
            return false;

        _bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        _bfh.bfSize = _bfh.bfOffBits + ww * wh * 4;
        _bfh.bfType = static_cast<WORD>(0x4d42);

        _bih.biBitCount = 32; // 每个像素字节大小
        _bih.biCompression = BI_RGB;
        _bih.biHeight = wh; // 高度
        _bih.biPlanes = 1;
        _bih.biSize = sizeof(BITMAPINFOHEADER);
        _bih.biSizeImage = ww * wh * 4; // 图像数据大小
        _bih.biWidth = ww;              // 宽度

        // 对指定的源设备环境区域中的像素进行位块（bit_block）转换

        if (_render_type == RDT_GDI) {
            ::PrintWindow(_hwnd, _hmdc, 0);

        } else {
            ::UpdateWindow(_hwnd);
            //::RedrawWindow(_hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE |
            //: RDW_ALLCHILDREN | RDW_FRAME);
            ::PrintWindow(_hwnd, _hmdc, 0);
        }
        // 函数获取指定兼容位图的位，然后将其作一个DIB—设备无关位图（Device-Independent
        //  Bitmap）使用的指定格式复制到一个缓冲区中 _pmutex->lock();
        uchar *pshare = _shmem->data<byte>();
        fmtFrameInfo(pshare, _hwnd, w, h);
        GetDIBits(_hmdc, _hbmpscreen, 0L, (DWORD)wh, pshare + sizeof(FrameInfo), (LPBITMAPINFO)&_bih,
                  (DWORD)DIB_RGB_COLORS);

        // 将数据拷贝到目标注意实际数据是反的(注意偏移)
        auto pixels = shared_pixels(_shmem, ww, wh);
        for (int i = 0; i < h; i++) {
            const int rowIndex = wh - 1 - i - src_y;
            const auto row = gdi_row(pixels, ww, rowIndex, src_x, w);
            memcpy(img.ptr<uchar>(i), row.data(), row.size());
        }
    }
    return 1;
}

bool GdiCapture::ensure_capture_bitmap(int w, int h) {
    if (w <= 0 || h <= 0)
        return false;

    // 同尺寸复用：截图循环里不再每次 CreateCompatibleBitmap/DeleteObject。
    if (_hbmpscreen && _bmp_width == w && _bmp_height == h)
        return true;

    release_capture_bitmap(_hmdc, _hbmpscreen, _hbmp_old);
    _bmp_width = 0;
    _bmp_height = 0;

    _hbmpscreen = CreateCompatibleBitmap(_hdc, w, h);
    if (!_hbmpscreen) {
        setlog("CreateCompatibleBitmap false");
        return false;
    }
    _hbmp_old = static_cast<HBITMAP>(SelectObject(_hmdc, _hbmpscreen));
    if (!_hbmp_old) {
        setlog("SelectObject false");
        release_capture_bitmap(_hmdc, _hbmpscreen, _hbmp_old);
        return false;
    }
    _bmp_width = w;
    _bmp_height = h;
    return true;
}

bool GdiCapture::ensure_shared_capacity(size_t required) {
    if (!_shmem) {
        setlog("GdiCapture shared frame not initialized");
        return false;
    }
    if (_shmem->size() < required) {
        setlog(L"GdiCapture shared frame too small: required=%llu capacity=%llu (window resized? rebind)",
               static_cast<unsigned long long>(required), static_cast<unsigned long long>(_shmem->size()));
        return false;
    }
    return true;
}

void GdiCapture::release_device_context() {
    if (!_hdc)
        return;

    ::ReleaseDC(_render_type == RDT_NORMAL ? NULL : _hwnd, _hdc);
    _hdc = NULL;
}

void GdiCapture::fmtFrameInfo(void *dst, HWND hwnd, int w, int h) {
    m_frameInfo.hwnd = (unsigned __int64)hwnd;
    m_frameInfo.frameId++;
    m_frameInfo.time = static_cast<unsigned int>(::GetTickCount64());
    m_frameInfo.width = w;
    m_frameInfo.height = h;
    m_frameInfo.fmtChk();
    memcpy(dst, &m_frameInfo, sizeof(m_frameInfo));
}

} // namespace op::capture
