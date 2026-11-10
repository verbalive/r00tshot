
#include "common.h"

#include <shlwapi.h>
#include <iostream>
#include <string>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace {

std::string ToUtf8(const std::wstring& text)
{
    if (text.empty()) return std::string();
    const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                           nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return std::string();
    std::string out((size_t)needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], needed, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& text)
{
    if (text.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0);
    if (needed <= 0) return std::wstring();
    std::wstring out((size_t)needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &out[0], needed);
    return out;
}

void Say(const std::string& text) { std::cout << text << std::endl; }

void SayStep(const std::string& what, bool ok)
{
    std::cout << "  [" << (ok ? "ok" : "!!") << "] " << what << std::endl;
}

std::string Ask(const std::string& prompt, const std::string& fallback)
{
    std::cout << prompt << " [" << fallback << "]: " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return fallback;
    if (line.empty()) return fallback;

    const size_t first = line.find_first_not_of(" \t\r\n\"");
    if (first == std::string::npos) return fallback;
    const size_t last = line.find_last_not_of(" \t\r\n\"");
    return line.substr(first, last - first + 1);
}

bool AskYesNo(const std::string& prompt, bool fallback)
{
    std::cout << prompt << (fallback ? " [Y/n]: " : " [y/N]: ") << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return fallback;
    if (line.empty()) return fallback;
    return line[0] == 'y' || line[0] == 'Y';
}

struct Options {
    bool                uninstall = false;
    bool                silent = false;
    bool                dirSet = false;
    bool                startupSet = false;
    bool                startup = true;
    std::wstring        dir;
};

void PrintUsage()
{
    Say("r00tshot setup " + ToUtf8(R00TSHOT_APP_VERSION));
    Say("");
    Say("Usage: r00tshot-setup.exe [options]");
    Say("");
    Say("  --dir <path>      folder to install into");
    Say("  --startup         add r00tshot to the Windows startup list");
    Say("  --no-startup      leave the Windows startup list untouched");
    Say("  --silent          do not ask anything, use the defaults");
    Say("  --uninstall       remove r00tshot from this account");
    Say("  --help            show this message");
    Say("");
}

bool ParseOptions(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h" || arg == "/?") {
            PrintUsage();
            exit(0);
        } else if (arg == "--uninstall" || arg == "-u") {
            options.uninstall = true;
        } else if (arg == "--silent" || arg == "-s") {
            options.silent = true;
        } else if (arg == "--startup") {
            options.startupSet = true;
            options.startup = true;
        } else if (arg == "--no-startup") {
            options.startupSet = true;
            options.startup = false;
        } else if (arg == "--dir") {
            if (i + 1 >= argc) {
                Say("error: --dir needs a path");
                return false;
            }
            options.dir = FromUtf8(argv[++i]);
            options.dirSet = true;
        } else {
            Say("error: unknown option " + arg);
            return false;
        }
    }
    return true;
}

bool SetStartupEntry(const std::wstring& exePath, bool enabled)
{
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, R00TSHOT_RUN_KEY, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    bool ok = true;
    if (enabled) {
        const std::wstring value = L"\"" + exePath + L"\"";
        ok = RegSetValueExW(key, R00TSHOT_RUN_VALUE, 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(value.c_str()),
                            (DWORD)((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        const LONG result = RegDeleteValueW(key, R00TSHOT_RUN_VALUE);
        ok = (result == ERROR_SUCCESS || result == ERROR_FILE_NOT_FOUND);
    }
    RegCloseKey(key);
    return ok;
}

std::wstring ShortcutPath()
{
    const std::wstring programs = r00t::KnownFolderPath(FOLDERID_Programs);
    if (programs.empty()) return std::wstring();
    return r00t::JoinPath(programs, L"r00tshot.lnk");
}

bool CreateShortcut(const std::wstring& linkPath, const std::wstring& target)
{
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, reinterpret_cast<void**>(&link)))) {
        return false;
    }

    link->SetPath(target.c_str());
    link->SetDescription(L"r00tshot screen capture");
    link->SetWorkingDirectory(r00t::ExeDir().c_str());
    link->SetIconLocation(target.c_str(), 0);

    bool ok = false;
    IPersistFile* file = nullptr;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) {
        ok = SUCCEEDED(file->Save(linkPath.c_str(), TRUE));
        file->Release();
    }
    link->Release();
    return ok;
}

std::wstring DefaultInstallDir()
{
    std::wstring base = r00t::KnownFolderPath(FOLDERID_LocalAppData);
    if (base.empty()) base = r00t::ExeDir();
    return r00t::JoinPath(r00t::JoinPath(base, L"Programs"), L"r00tshot");
}

std::wstring FindPayload()
{
    const wchar_t* name = L"r00tshot.exe";

    std::vector<std::wstring> candidates;
    candidates.push_back(r00t::JoinPath(r00t::ExeDir(), name));

    wchar_t cwd[MAX_PATH * 2] = { 0 };
    if (GetCurrentDirectoryW(ARRAYSIZE(cwd), cwd) > 0) {
        candidates.push_back(r00t::JoinPath(cwd, name));
    }

    for (size_t i = 0; i < candidates.size(); ++i) {
        if (r00t::PathExists(candidates[i])) return candidates[i];
    }
    return std::wstring();
}

int DoInstall(const Options& options)
{
    const std::wstring payload = FindPayload();
    if (payload.empty()) {
        Say("");
        Say("error: r00tshot.exe was not found next to the setup program.");
        Say("       Keep both files in the same folder and run the setup again.");
        return 1;
    }

    Say("");
    std::wstring targetDir = options.dirSet ? options.dir : DefaultInstallDir();
    if (!options.silent) {
        targetDir = FromUtf8(Ask("Install folder", ToUtf8(targetDir)));
    }
    if (targetDir.empty()) {
        Say("error: the install folder cannot be empty.");
        return 1;
    }

    bool startup = options.startupSet ? options.startup : true;
    if (!options.silent && !options.startupSet) {
        startup = AskYesNo("Run r00tshot automatically when Windows starts?", true);
    }

    bool shortcut = true;
    if (!options.silent) {
        shortcut = AskYesNo("Create a Start menu shortcut?", true);
    }

    bool launch = true;
    if (!options.silent) {
        launch = AskYesNo("Start r00tshot now?", true);
    }

    Say("");
    Say("Installing r00tshot " + ToUtf8(R00TSHOT_APP_VERSION));
    Say("");

    if (!r00t::MakeDirectoryTree(targetDir)) {
        SayStep("create " + ToUtf8(targetDir), false);
        Say("error: the install folder could not be created.");
        return 1;
    }

    const std::wstring targetExe = r00t::JoinPath(targetDir, L"r00tshot.exe");

    bool copied = true;
    if (_wcsicmp(payload.c_str(), targetExe.c_str()) != 0) {
        copied = CopyFileW(payload.c_str(), targetExe.c_str(), FALSE) != FALSE;
    }
    SayStep("install r00tshot.exe", copied);
    if (!copied) {
        Say("error: copying the program failed. Close a running r00tshot and retry.");
        return 1;
    }

    if (startup) {
        SayStep("add to Windows startup", SetStartupEntry(targetExe, true));
    } else {
        SetStartupEntry(targetExe, false);
        SayStep("Windows startup entry disabled", true);
    }

    if (shortcut) {
        const std::wstring link = ShortcutPath();
        if (link.empty()) {
            SayStep("Start menu shortcut", false);
        } else {
            SayStep("Start menu shortcut", CreateShortcut(link, targetExe));
        }
    }

    if (launch) {
        HINSTANCE result = ShellExecuteW(nullptr, L"open", targetExe.c_str(), nullptr,
                                         targetDir.c_str(), SW_SHOWNORMAL);
        SayStep("start r00tshot", (INT_PTR)result > 32);
    }

    Say("");
    Say("r00tshot is installed in " + ToUtf8(targetDir));
    Say("Press Page Down anywhere to capture the screen.");
    Say("Screenshots are written to " + ToUtf8(r00t::OutputDirectory()));
    Say("Run this setup with --uninstall to remove it again.");
    Say("");
    return 0;
}

int DoUninstall()
{
    Say("");
    Say("Removing r00tshot");
    Say("");

    SayStep("remove Windows startup entry", SetStartupEntry(std::wstring(), false));

    const std::wstring link = ShortcutPath();
    if (!link.empty() && r00t::PathExists(link)) {
        SayStep("remove Start menu shortcut", DeleteFileW(link.c_str()) != FALSE);
    }

    const std::wstring thisExe = r00t::ExePath();
    const std::wstring installDir = r00t::ExeDir();
    const std::wstring targetExe = r00t::JoinPath(installDir, L"r00tshot.exe");

    if (_wcsicmp(thisExe.c_str(), targetExe.c_str()) != 0 && r00t::PathExists(targetExe)) {
        bool removed = DeleteFileW(targetExe.c_str()) != FALSE;
        if (!removed && MoveFileExW(targetExe.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT)) {
            removed = true;
        }
        SayStep("remove r00tshot.exe", removed);
    }

    if (_wcsicmp(thisExe.c_str(), targetExe.c_str()) == 0) {
        MoveFileExW(thisExe.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT);
        SayStep("remove the setup program", true);
    }

    if (RemoveDirectoryW(installDir.c_str())) {
        SayStep("remove " + ToUtf8(installDir), true);
    } else {
        SayStep("remove " + ToUtf8(installDir), false);
        Say("note: the folder is busy or not empty, it was left in place.");
    }

    Say("");
    Say("r00tshot has been removed. Screenshots in "
        + ToUtf8(r00t::OutputDirectory()) + " were kept.");
    Say("");
    return 0;
}

}

int main(int argc, char** argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Options options;
    if (!ParseOptions(argc, argv, options)) {
        CoUninitialize();
        return 2;
    }

    int result;
    if (options.uninstall) {
        result = DoUninstall();
    } else {
        Say("");
        Say("r00tshot " + ToUtf8(R00TSHOT_APP_VERSION) + " - screen capture tool");
        Say("---------------------------------------------");
        if (options.silent) {
            Say("Running in silent mode, defaults are used.");
        } else {
            Say("Press Enter to accept the value between brackets.");
        }
        result = DoInstall(options);
    }

    CoUninitialize();
    return result;
}
