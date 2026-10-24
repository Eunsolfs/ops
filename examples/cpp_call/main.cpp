// OP 纯 C++ 调用示例：直接使用 op::Op（静态链接 libop.lib）。
// 对应工程：examples/cpp_call/cpp_call.vcxproj
#include <libop.h>

#include <iostream>
#include <string>

int wmain() {
    try {
        op::Op op;

        std::wcout << L"Ver      = " << op.Ver() << L"\n";

        std::wstring base_path;
        op.GetBasePath(base_path);
        std::wcout << L"BasePath = " << base_path << L"\n";

        long id = 0;
        op.GetID(&id);
        std::wcout << L"ID       = " << id << L"\n";

        std::wstring opencv_version;
        op.CvGetOpenCvVersion(opencv_version);
        std::wcout << L"OpenCV   = " << opencv_version << L"\n";

        return 0;
    } catch (const std::exception &e) {
        std::cerr << "op::Op failed: " << e.what() << "\n";
        return 1;
    } catch (...) {
        std::cerr << "op::Op failed: unknown exception\n";
        return 1;
    }
}
