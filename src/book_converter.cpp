// book_converter.cpp
// Implementation of book format converters.
// Includes miniz, pugixml, libmobi — isolated from main.cpp to avoid header conflicts.

#include "book_converter.h"

#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <functional>
#include <unordered_map>
#include <iostream>
#include <filesystem>

// --------------------------------------------------------------------------
// pugixml — XML parser for FB2, EPUB (XHTML), DOCX
// --------------------------------------------------------------------------
#include <pugixml.hpp>

// --------------------------------------------------------------------------
// miniz — ZIP decompressor for EPUB and DOCX (both are ZIP archives)
// --------------------------------------------------------------------------
#include <miniz.h>
#include <miniz_zip.h>

// --------------------------------------------------------------------------
// libmobi — MOBI/AZW reader (desktop platforms only)
// --------------------------------------------------------------------------
#if defined(MOBI_SUPPORT)
#include <mobi.h>
#endif

namespace BookConverter {

// ==========================================================================
//  Internal utilities
// ==========================================================================

static std::string ToLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

std::string GetExt(const std::string& path) {
    size_t pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    return ToLower(path.substr(pos));
}

static std::string ReadBinaryFile(const std::string& path) {
#if defined(_WIN32)
    std::ifstream f(std::filesystem::path(std::filesystem::u8path(path)), std::ios::binary);
#else
    std::ifstream f(path, std::ios::binary);
#endif
    if (!f.is_open()) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static bool WriteTextFile(const std::string& path, const std::string& content) {
#if defined(_WIN32)
    std::ofstream f(std::filesystem::path(std::filesystem::u8path(path)));
#else
    std::ofstream f(path);
#endif
    if (!f.is_open()) return false;
    f << content;
    return true;
}

static std::string NormalizeWhitespace(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool lastNewline = false;
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\n') {
            if (!lastNewline) { out += '\n'; lastNewline = true; }
        } else {
            lastNewline = false;
            out += c;
        }
    }
    return out;
}

// ==========================================================================
//  FB2 converter (FictionBook 2.0 XML)
// ==========================================================================

static std::string ExtractTextFromFB2(const std::string& path) {
    std::string xmlData = ReadBinaryFile(path);
    if (xmlData.empty()) return "";

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(xmlData.data(), xmlData.size(),
        pugi::parse_default | pugi::parse_declaration);
    if (!result) return "";

    std::string out;

    // Recursive walker — collects text from <p>, <v>, <title>, <subtitle>
    std::function<void(pugi::xml_node)> walk = [&](pugi::xml_node node) {
        std::string name = ToLower(node.name());
        if (name == "binary") return;

        bool isParagraph = (name == "p" || name == "v");
        bool isTitle     = (name == "title" || name == "subtitle");
        bool isSection   = (name == "section" || name == "body");

        if (isParagraph || isTitle) {
            std::string line;
            for (pugi::xml_node child : node.children()) {
                if (child.type() == pugi::node_pcdata ||
                    child.type() == pugi::node_cdata) {
                    line += child.value();
                } else {
                    for (pugi::xml_node sub : child.children()) {
                        if (sub.type() == pugi::node_pcdata) line += sub.value();
                    }
                }
            }
            if (!line.empty()) {
                out += line + "\n";
                if (isTitle) out += "\n";
            }
        } else {
            for (pugi::xml_node child : node.children()) walk(child);
            if (isSection && !out.empty() && out.back() != '\n') out += "\n";
        }
    };

    pugi::xml_node fictionBook = doc.child("FictionBook");
    if (!fictionBook) fictionBook = doc.document_element();
    for (pugi::xml_node body : fictionBook.children("body")) walk(body);

    return NormalizeWhitespace(out);
}

// ==========================================================================
//  ZIP extraction helper (used by EPUB and DOCX)
// ==========================================================================

struct ZipEntry {
    std::string name;
    std::string data;
};

using ZipFilter = std::function<bool(const std::string&)>;

static std::vector<ZipEntry> ExtractFromZipBlob(const std::string& zipData,
                                                 const ZipFilter& filter)
{
    std::vector<ZipEntry> results;
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_mem(&zip, zipData.data(), zipData.size(), 0))
        return results;

    mz_uint numFiles = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < numFiles; i++) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) continue;
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;

        std::string fname = stat.m_filename;
        if (!filter(fname)) continue;

        size_t sz = (size_t)stat.m_uncomp_size;
        std::string buf(sz, '\0');
        if (!mz_zip_reader_extract_to_mem(&zip, i, &buf[0], sz, 0)) continue;
        results.push_back({ fname, std::move(buf) });
    }

    mz_zip_reader_end(&zip);
    return results;
}

// ==========================================================================
//  EPUB converter
// ==========================================================================

static std::string XhtmlBodyToText(const std::string& xhtmlData) {
    pugi::xml_document doc;
    doc.load_string(xhtmlData.c_str(),
        pugi::parse_default | pugi::parse_fragment);

    std::string out;
    std::function<void(pugi::xml_node)> walk = [&](pugi::xml_node node) {
        std::string name = ToLower(node.name());
        if (name == "script" || name == "style") return;

        bool isBlock = (name == "p" || name == "div" || name == "h1" ||
                        name == "h2" || name == "h3" || name == "h4" ||
                        name == "h5" || name == "h6" || name == "li" ||
                        name == "br" || name == "tr");

        if (node.type() == pugi::node_pcdata) {
            out += node.value();
        } else {
            for (pugi::xml_node child : node.children()) walk(child);
        }

        if (isBlock && !out.empty() && out.back() != '\n') out += "\n";
    };

    auto body = doc.select_node("//body");
    if (body) walk(body.node());
    else      walk(doc.document_element());

    return out;
}

static std::string ExtractTextFromEPUB(const std::string& path) {
    std::string zipData = ReadBinaryFile(path);
    if (zipData.empty()) return "";

    // Step 1: Find OPF path from META-INF/container.xml
    std::string opfPath;
    {
        auto files = ExtractFromZipBlob(zipData, [](const std::string& n) {
            std::string ln = n;
            std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
            return ln == "meta-inf/container.xml";
        });
        if (!files.empty()) {
            pugi::xml_document d;
            d.load_string(files[0].data.c_str());
            auto rf = d.select_node("//rootfile");
            if (rf) opfPath = rf.node().attribute("full-path").value();
        }
    }

    // Step 2: Parse OPF manifest + spine
    std::vector<std::string> spineHrefs;
    std::string opfDir;
    if (!opfPath.empty()) {
        size_t slash = opfPath.rfind('/');
        if (slash != std::string::npos) opfDir = opfPath.substr(0, slash + 1);

        auto opfFiles = ExtractFromZipBlob(zipData, [&](const std::string& n) {
            return n == opfPath;
        });

        if (!opfFiles.empty()) {
            pugi::xml_document opfDoc;
            opfDoc.load_string(opfFiles[0].data.c_str());

            std::unordered_map<std::string, std::string> idToHref;
            for (auto& item : opfDoc.select_nodes("//manifest/item")) {
                std::string id   = item.node().attribute("id").value();
                std::string href = item.node().attribute("href").value();
                std::string mt   = item.node().attribute("media-type").value();
                if (!id.empty() && !href.empty() &&
                    (mt.find("html") != std::string::npos ||
                     mt.find("xhtml") != std::string::npos)) {
                    idToHref[id] = href;
                }
            }

            for (auto& itemref : opfDoc.select_nodes("//spine/itemref")) {
                std::string idref = itemref.node().attribute("idref").value();
                auto it = idToHref.find(idref);
                if (it != idToHref.end()) {
                    std::string href = it->second;
                    if (href.find("://") == std::string::npos && href[0] != '/')
                        href = opfDir + href;
                    size_t frag = href.find('#');
                    if (frag != std::string::npos) href = href.substr(0, frag);
                    spineHrefs.push_back(href);
                }
            }
        }
    }

    // Step 3: Extract text from each spine item
    std::string result;
    if (!spineHrefs.empty()) {
        for (const auto& href : spineHrefs) {
            auto files = ExtractFromZipBlob(zipData, [&](const std::string& n) {
                return n == href || ToLower(n) == ToLower(href);
            });
            if (!files.empty()) result += XhtmlBodyToText(files[0].data) + "\n";
        }
    } else {
        // Fallback: all XHTML files
        auto files = ExtractFromZipBlob(zipData, [](const std::string& n) {
            size_t dot = n.rfind('.');
            if (dot == std::string::npos) return false;
            std::string e = n.substr(dot + 1);
            std::transform(e.begin(), e.end(), e.begin(), ::tolower);
            return e == "html" || e == "xhtml" || e == "htm";
        });
        for (const auto& f : files) result += XhtmlBodyToText(f.data) + "\n";
    }

    return NormalizeWhitespace(result);
}

// ==========================================================================
//  DOCX converter (ZIP + word/document.xml)
// ==========================================================================

static std::string ExtractTextFromDOCX(const std::string& path) {
    std::string zipData = ReadBinaryFile(path);
    if (zipData.empty()) return "";

    auto docFiles = ExtractFromZipBlob(zipData, [](const std::string& n) {
        std::string ln = n;
        std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
        return ln == "word/document.xml";
    });
    if (docFiles.empty()) return "";

    pugi::xml_document doc;
    doc.load_string(docFiles[0].data.c_str());

    std::string out;
    for (auto& para : doc.select_nodes("//w:p")) {
        std::string line;
        for (auto& t : para.node().select_nodes(".//w:t"))
            line += t.node().text().get();
        if (!line.empty()) out += line + "\n";
    }
    return NormalizeWhitespace(out);
}

// ==========================================================================
//  MOBI converter (via libmobi)
// ==========================================================================

static std::string ExtractTextFromMOBI(const std::string& path) {
#if defined(MOBI_SUPPORT)
    MOBIData* m = mobi_init();
    if (!m) return "";

#if defined(_WIN32)
    std::wstring wpath = std::filesystem::path(std::filesystem::u8path(path)).wstring();
    FILE* fp = _wfopen(wpath.c_str(), L"rb");
#else
    FILE* fp = fopen(path.c_str(), "rb");
#endif
    if (!fp) { mobi_free(m); return ""; }

    MOBI_RET ret = mobi_load_file(m, fp);
    fclose(fp);
    if (ret != MOBI_SUCCESS) { mobi_free(m); return ""; }

    MOBIRawml* rawml = mobi_init_rawml(m);
    if (!rawml) { mobi_free(m); return ""; }

    ret = mobi_parse_rawml(rawml, m);
    if (ret != MOBI_SUCCESS) {
        mobi_free_rawml(rawml);
        mobi_free(m);
        return "";
    }

    // Collect HTML parts
    std::string allHtml;
    MOBIPart* part = rawml->markup;
    while (part) {
        if (part->data && part->size > 0)
            allHtml += std::string(reinterpret_cast<const char*>(part->data), part->size);
        part = part->next;
    }
    mobi_free_rawml(rawml);
    mobi_free(m);

    // Strip HTML tags
    std::string text;
    bool inTag = false;
    for (size_t i = 0; i < allHtml.size(); i++) {
        char c = allHtml[i];
        if (c == '<') {
            inTag = true;
            // Block-level elements → newline
            if (i + 2 < allHtml.size()) {
                char n1 = ::tolower(allHtml[i+1]);
                char n2 = (i+2 < allHtml.size()) ? ::tolower(allHtml[i+2]) : 0;
                bool isBlock = (n1 == 'p' && (allHtml[i+2] == '>' || allHtml[i+2] == ' ')) ||
                               (n1 == 'b' && n2 == 'r') ||
                               (n1 == 'h' && n2 >= '1' && n2 <= '6') ||
                               (n1 == 'l' && n2 == 'i');
                if (isBlock && !text.empty() && text.back() != '\n') text += '\n';
            }
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            if (c == '&') {
                // Basic entity decoding
                size_t semi = allHtml.find(';', i);
                if (semi != std::string::npos && semi - i < 8) {
                    std::string ent = allHtml.substr(i+1, semi-i-1);
                    if (ent == "amp")  { text += '&'; i = semi; continue; }
                    if (ent == "lt")   { text += '<'; i = semi; continue; }
                    if (ent == "gt")   { text += '>'; i = semi; continue; }
                    if (ent == "nbsp") { text += ' '; i = semi; continue; }
                    if (ent == "quot") { text += '"'; i = semi; continue; }
                }
            }
            text += c;
        }
    }
    return NormalizeWhitespace(text);
#else
    (void)path;
    return "";
#endif
}

// ==========================================================================
//  Public API implementation
// ==========================================================================

bool IsSupportedBookFormat(const std::string& ext) {
    std::string e = ToLower(ext);
    return e == ".txt" || e == ".json" ||
           e == ".fb2" || e == ".epub" ||
           e == ".docx" || e == ".mobi";
}

const char* GetWindowsDialogFilter() {
    return "Book Files (*.txt;*.fb2;*.epub;*.docx;*.mobi)\0"
           "*.txt;*.fb2;*.epub;*.docx;*.mobi\0"
           "Text Files (*.txt)\0*.txt\0"
           "FictionBook (*.fb2)\0*.fb2\0"
           "EPUB (*.epub)\0*.epub\0"
           "Word Document (*.docx)\0*.docx\0"
           "Mobipocket (*.mobi)\0*.mobi\0"
           "All Files (*.*)\0*.*\0";
}

const char* GetZenityFilter() {
    return "Book Files | *.txt *.fb2 *.epub *.docx *.mobi";
}

const char* GetMacOSScript() {
    return "osascript -e 'POSIX path of (choose file of type "
           "{\"public.plain-text\",\"org.idpf.epub-container\","
           "\"com.amazon.mobipocket-ebook\",\"public.data\"})' 2>/dev/null";
}

std::string ConvertBookToText(const std::string& filePath) {
    std::string ext = GetExt(filePath);
    if (ext == ".txt") return ReadBinaryFile(filePath);
    if (ext == ".fb2")  return ExtractTextFromFB2(filePath);
    if (ext == ".epub") return ExtractTextFromEPUB(filePath);
    if (ext == ".docx") return ExtractTextFromDOCX(filePath);
    if (ext == ".mobi") return ExtractTextFromMOBI(filePath);
    return "";
}

std::string ConvertBookToTempTxt(const std::string& filePath) {
    std::string text = ConvertBookToText(filePath);
    if (text.empty()) return "";

    std::string tmpPath = "book_converted_temp.txt";
    if (!WriteTextFile(tmpPath, text)) {
        tmpPath = "../book_converted_temp.txt";
        if (!WriteTextFile(tmpPath, text)) return "";
    }
    return tmpPath;
}

} // namespace BookConverter
