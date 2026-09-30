#include "file_dialog.hpp"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

namespace paulascape {

static std::string winDialog(bool save, const std::string& title, const std::string& defaultName, const std::string& filterName, const std::string& filterExt) {
    std::string filter = filterName + " (*." + filterExt + ")";
    filter.push_back('\0');
    filter += "*." + filterExt;
    filter.push_back('\0');
    filter += "All files";
    filter.push_back('\0');
    filter += "*.*";
    filter.push_back('\0');

    char file[MAX_PATH] = {};
    defaultName.copy(file, sizeof(file) - 1);
    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrDefExt = filterExt.c_str();
    ofn.Flags = OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL ok = save ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn);
    return ok ? std::string(file) : std::string();
}

std::string openFileDialog(const std::string& title, const std::string& filterName, const std::string& filterExt) {
    return winDialog(false, title, "", filterName, filterExt);
}

std::string saveFileDialog(const std::string& title, const std::string& defaultName, const std::string& filterName, const std::string& filterExt) {
    return winDialog(true, title, defaultName, filterName, filterExt);
}

} // namespace paulascape

#else

#include <cstdio>
#include <cstdlib>

namespace paulascape {

namespace {

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''"; else out.push_back(c);
    }
    return out + "'";
}

std::string runCommand(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {};
    std::string result;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), pipe)) result += buf;
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) result.pop_back();
    return result;
}

} // namespace

#if defined(__APPLE__)

std::string openFileDialog(const std::string& title, const std::string&, const std::string&) {
    const std::string script = "POSIX path of (choose file with prompt \"" + title + "\")";
    return runCommand("osascript -e " + shellQuote(script) + " 2>/dev/null");
}

std::string saveFileDialog(const std::string& title, const std::string& defaultName, const std::string&, const std::string&) {
    const std::string script = "POSIX path of (choose file name with prompt \"" + title + "\" default name \"" + defaultName + "\")";
    return runCommand("osascript -e " + shellQuote(script) + " 2>/dev/null");
}

#else

// Linux: use zenity, then kdialog, whichever is installed.
std::string openFileDialog(const std::string& title, const std::string& filterName, const std::string& filterExt) {
    std::string r = runCommand("zenity --file-selection --title=" + shellQuote(title) +
                               " --file-filter=" + shellQuote(filterName + " | *." + filterExt + " *." + filterExt) + " 2>/dev/null");
    if (r.empty()) r = runCommand("kdialog --getopenfilename . " + shellQuote("*." + filterExt) + " 2>/dev/null");
    return r;
}

std::string saveFileDialog(const std::string& title, const std::string& defaultName, const std::string&, const std::string& filterExt) {
    std::string r = runCommand("zenity --file-selection --save --confirm-overwrite --title=" + shellQuote(title) +
                               " --filename=" + shellQuote(defaultName) + " 2>/dev/null");
    if (r.empty()) r = runCommand("kdialog --getsavefilename " + shellQuote(defaultName) + " " + shellQuote("*." + filterExt) + " 2>/dev/null");
    return r;
}

#endif

} // namespace paulascape

#endif
