
#include "common.h"

#include <shlwapi.h>
#include <ctime>

namespace r00t {

int IMin(int a, int b) { return a < b ? a : b; }
int IMax(int a, int b) { return a > b ? a : b; }

int IClamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

std::wstring ExePath()
{
    wchar_t buffer[MAX_PATH * 2] = { 0 };
    DWORD len = GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
    if (len == 0) return std::wstring();
    return std::wstring(buffer, len);
}

std::wstring ExeDir()
{
    std::wstring path = ExePath();
    size_t cut = path.find_last_of(L"\\/");
    if (cut == std::wstring::npos) return path;
    return path.substr(0, cut);
}

bool PathExists(const std::wstring& path)
{
    if (path.empty()) return false;
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring out = a;
    if (out.back() != L'\\' && out.back() != L'/') out.push_back(L'\\');
    out += b;
    return out;
}

bool MakeDirectoryTree(const std::wstring& dir)
{
    if (dir.empty()) return false;
    if (PathExists(dir)) return true;

    std::wstring current;
    for (size_t i = 0; i < dir.size(); ++i) {
        current.push_back(dir[i]);
        bool last = (i + 1 == dir.size());
        bool separator = (dir[i] == L'\\' || dir[i] == L'/');
        if (separator && current.size() <= 3) continue;
        if (last || separator) {
            if (!current.empty() && !PathExists(current)) {
                if (!CreateDirectoryW(current.c_str(), nullptr)) {
                    if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
                }
            }
        }
    }
    return PathExists(dir);
}

std::wstring KnownFolderPath(REFKNOWNFOLDERID id)
{
    PWSTR raw = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &raw)) && raw) {
        result.assign(raw);
    }
    if (raw) CoTaskMemFree(raw);
    return result;
}

std::wstring OutputDirectory()
{
    std::wstring pictures = KnownFolderPath(FOLDERID_Pictures);
    if (pictures.empty()) pictures = ExeDir();
    return JoinPath(pictures, L"r00tshot");
}

std::wstring OutputFileName()
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t name[64] = { 0 };
    wsprintfW(name, L"r00tshot_%04u-%02u-%02u_%02u-%02u-%02u.png",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(name);
}

}
