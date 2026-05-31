// book_converter.h
// Public API for converting epub, fb2, docx, mobi files to plain text.
// Implementation is in book_converter.cpp (miniz/pugixml kept isolated there).

#pragma once
#include <string>

namespace BookConverter {

// Returns lowercase extension of a file path (e.g. ".epub")
std::string GetExt(const std::string& path);

// Returns true if the extension is a supported book format
bool IsSupportedBookFormat(const std::string& ext);

// Convert any supported book format to plain text.
// Returns empty string on error or unsupported format (.txt/.json not converted).
std::string ConvertBookToText(const std::string& filePath);

// Convert and save to a temporary .txt file.
// Returns path to temp file, or empty string on failure.
std::string ConvertBookToTempTxt(const std::string& filePath, const std::string& tempDir = "");

// File dialog filter strings for each platform
const char* GetWindowsDialogFilter();
const char* GetZenityFilter();
const char* GetMacOSScript();

#if defined(_WIN32)
std::wstring UTF8ToWide(const std::string& str);
std::string WideToUTF8(const std::wstring& wstr);
const wchar_t* GetWindowsDialogFilterW();
#endif

} // namespace BookConverter
