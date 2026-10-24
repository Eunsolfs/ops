#pragma once
#include "../base/Types.h"
#include <condition_variable>
#include <mutex>

namespace op::ocr {

class HttpOcrService {
  private:
    HttpOcrService();

  public:
    HttpOcrService(const HttpOcrService &) = delete;
    HttpOcrService &operator=(const HttpOcrService &) = delete;
    static HttpOcrService *getInstance();
    ~HttpOcrService();
    int init(const std::wstring &enginePath, const std::wstring &dllName, const vector<string> &argv);
    int release();
    int ocr(byte *data, int w, int h, int bpp, vocr_rec_t &result);

  private:
    // 配置锁只保护 endpoint/timeout 的读写；HTTP 请求必须在锁外执行，
    // 否则一个后端卡死会把全进程所有 OCR/SetOcrEngine 一起阻塞。
    std::mutex m_config_mutex;
    std::string m_endpoint;
    int m_timeout_ms;

    // 允许少量并发，但限制在 4 个同时在途请求，避免打爆外部 OCR 服务。
    static constexpr int kMaxConcurrentOcr = 4;
    std::mutex m_slot_mutex;
    std::condition_variable m_slot_cv;
    int m_in_flight = 0;

    class SlotGuard {
      public:
        explicit SlotGuard(HttpOcrService *owner);
        ~SlotGuard();
        SlotGuard(const SlotGuard &) = delete;
        SlotGuard &operator=(const SlotGuard &) = delete;

      private:
        HttpOcrService *m_owner;
    };
};

} // namespace op::ocr
