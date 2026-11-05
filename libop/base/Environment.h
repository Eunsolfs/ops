#ifndef OP_BASE_ENVIRONMENT_H_
#define OP_BASE_ENVIRONMENT_H_
#include <atomic>
#include <string>
class RuntimeEnvironment {
  public:
    static void setInstance(void *instance);
    static void *getInstance();
    static std::wstring getBasePath();
    static std::wstring getOpName();
    // 进程级日志开关：0=静默（默认），1=MessageBox，2=文件，3=stdout。
    // 用 atomic 避免多线程读写竞争（OpSetShowErrorMsg 可从任意句柄调用）。
    static std::atomic<int> m_showErrorMsg;

  private:
    static void *m_instance;
    static std::wstring m_basePath;
    static std::wstring m_opName;
};
#endif // OP_BASE_ENVIRONMENT_H_
