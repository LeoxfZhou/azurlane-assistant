#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

inline void replaceCatFileWithBackup(const std::filesystem::path& temporary,
                                     const std::filesystem::path& destination) {
    std::error_code error;
    if (std::filesystem::exists(destination)) {
        static std::atomic<unsigned long long> sequence{0};
        const auto tick = std::chrono::system_clock::now().time_since_epoch().count();
        auto backup = destination;
        backup += ".backup-" + std::to_string(tick) + "-" +
                  std::to_string(sequence.fetch_add(1));
        {
            std::ifstream input(destination, std::ios::binary);
            std::ofstream output(backup, std::ios::binary | std::ios::trunc);
            if (!input || !output) throw std::runtime_error("无法打开旧文件或备份文件");
            output << input.rdbuf();
            output.flush();
            if (!output) throw std::runtime_error("备份旧文件失败");
        }
    }
#ifdef _WIN32
    const bool destinationExists = std::filesystem::exists(destination);
    BOOL replaced = FALSE;
    DWORD replaceError = ERROR_SUCCESS;
    for (int attempt = 0; attempt < 10; ++attempt) {
        replaced = destinationExists
            ? ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr,
                           REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)
            : MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
        if (replaced) break;
        replaceError = GetLastError();
        if (replaceError != ERROR_SHARING_VIOLATION && replaceError != ERROR_ACCESS_DENIED)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!replaced)
        throw std::runtime_error("替换文件失败，Windows 错误码：" +
                                 std::to_string(replaceError));
#else
    std::filesystem::rename(temporary, destination, error);
    if (error) throw std::runtime_error("替换文件失败：" + error.message());
#endif
}
