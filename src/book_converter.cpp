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

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

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

#if defined(_WIN32)
std::wstring UTF8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string WideToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}
#endif

// ==========================================================================
//  Internal utilities
// ==========================================================================

static std::string ToLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

static std::string CP1251ToUTF8(const std::string& str) {
    std::string out;
    out.reserve(str.size() * 2);
    for (unsigned char c : str) {
        if (c < 0x80) {
            out += (char)c;
        } else {
            if (c >= 0xC0 && c <= 0xDF) {
                out += (char)0xD0;
                out += (char)(c - 0xC0 + 0x90);
            } else if (c >= 0xE0 && c <= 0xEF) {
                out += (char)0xD0;
                out += (char)(c - 0xE0 + 0xB0);
            } else if (c >= 0xF0 && c <= 0xFF) {
                out += (char)0xD1;
                out += (char)(c - 0xF0 + 0x80);
            } else if (c == 0xA8) { // Ё
                out += (char)0xD0;
                out += (char)0x81;
            } else if (c == 0xB8) { // ё
                out += (char)0xD1;
                out += (char)0x91;
            } else {
                switch(c) {
                    case 0x82: out += "\xE2\x80\x9A"; break; // Single low-9 quotation mark
                    case 0x84: out += "\xE2\x80\x9E"; break; // Double low-9 quotation mark
                    case 0x85: out += "\xE2\x80\xA6"; break; // Horizontal ellipsis
                    case 0x86: out += "\xE2\x80\xA0"; break; // Dagger
                    case 0x87: out += "\xE2\x80\xA1"; break; // Double dagger
                    case 0x89: out += "\xE2\x80\xB0"; break; // Per mille sign
                    case 0x8A: out += "\xD0\x89"; break;     // LJE
                    case 0x8B: out += "\xE2\x80\xB9"; break; // Single left-pointing angle quotation mark
                    case 0x8C: out += "\xD0\x8A"; break;     // NJE
                    case 0x8D: out += "\xD0\x8B"; break;     // KJE
                    case 0x8E: out += "\xD0\x8C"; break;     // TSHE
                    case 0x8F: out += "\xD0\x8F"; break;     // DZHE
                    case 0x91: out += "\xE2\x80\x98"; break; // Left single quotation mark
                    case 0x92: out += "\xE2\x80\x99"; break; // Right single quotation mark
                    case 0x93: out += "\xE2\x80\x9C"; break; // Left double quotation mark
                    case 0x94: out += "\xE2\x80\x9D"; break; // Right double quotation mark
                    case 0x95: out += "\xE2\x80\xA2"; break; // Bullet
                    case 0x96: out += "\xE2\x80\x93"; break; // En dash
                    case 0x97: out += "\xE2\x80\x94"; break; // Em dash
                    case 0x99: out += "\xE2\x84\xA2"; break; // Trademark sign
                    case 0x9A: out += "\xD1\x89"; break;     // lje
                    case 0x9B: out += "\xE2\x80\xBA"; break; // Single right-pointing angle quotation mark
                    case 0x9C: out += "\xD1\x8A"; break;     // nje
                    case 0x9D: out += "\xD1\x8B"; break;     // kje
                    case 0x9E: out += "\xD1\x8C"; break;     // tshe
                    case 0x9F: out += "\xD1\x8F"; break;     // dzhe
                    case 0xA0: out += "\xC2\xA0"; break;     // Non-breaking space
                    case 0xA1: out += "\xD0\x8E"; break;     // SHORT U
                    case 0xA2: out += "\xD1\x8E"; break;     // short u
                    case 0xA3: out += "\xD0\x88"; break;     // JE
                    case 0xA4: out += "\xC2\xA4"; break;     // Currency sign
                    case 0xA5: out += "\xD2\x90"; break;     // GHE WITH UPTURN
                    case 0xA6: out += "\xC2\xA6"; break;     // Broken bar
                    case 0xA7: out += "\xC2\xA7"; break;     // Section sign
                    case 0xA9: out += "\xC2\xA9"; break;     // Copyright sign
                    case 0xAA: out += "\xD0\x84"; break;     // UKRAINIAN IE
                    case 0xAB: out += "\xC2\xAB"; break;     // Left-pointing double angle quotation mark
                    case 0xAC: out += "\xC2\xAC"; break;     // Not sign
                    case 0xAD: out += "\xC2\xAD"; break;     // Soft hyphen
                    case 0xAE: out += "\xC2\xAE"; break;     // Registered sign
                    case 0xAF: out += "\xD0\x87"; break;     // YI
                    case 0xB0: out += "\xC2\xB0"; break;     // Degree sign
                    case 0xB1: out += "\xC2\xB1"; break;     // Plus-minus sign
                    case 0xB2: out += "\xD0\x86"; break;     // BYELORUSSIAN-UKRAINIAN I
                    case 0xB3: out += "\xD1\x96"; break;     // byelorussian-ukrainian i
                    case 0xB4: out += "\xD2\x91"; break;     // ghe with upturn
                    case 0xB5: out += "\xC2\xB5"; break;     // Micro sign
                    case 0xB6: out += "\xC2\xB6"; break;     // Pilcrow sign
                    case 0xB7: out += "\xC2\xB7"; break;     // Middle dot
                    case 0xB9: out += "\xD1\x98"; break;     // je
                    case 0xBA: out += "\xD1\x94"; break;     // ukrainian ie
                    case 0xBB: out += "\xC2\xBB"; break;     // Right-pointing double angle quotation mark
                    case 0xBC: out += "\xD1\x9F"; break;     // dze
                    case 0xBD: out += "\xD0\x85"; break;     // DZE
                    case 0xBE: out += "\xD1\x95"; break;     // yi
                    case 0xBF: out += "\xD1\x97"; break;     // yi
                    default:   out += '?'; break;            // Fallback for others
                }
            }
        }
    }
    return out;
}

std::string GetExt(const std::string& path) {
    size_t pos = path.rfind('.');
    if (pos == std::string::npos) return "";
    return ToLower(path.substr(pos));
}

static std::string ReadBinaryFile(const std::string& path) {
#if defined(_WIN32)
    std::ifstream f(std::filesystem::path(UTF8ToWide(path)), std::ios::binary);
#else
    std::ifstream f(path, std::ios::binary);
#endif
    if (!f.is_open()) return "";
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

static bool WriteTextFile(const std::string& path, const std::string& content) {
#if defined(_WIN32)
    std::ofstream f(std::filesystem::path(UTF8ToWide(path)));
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

static std::string GetLocalName(const char* name) {
    if (!name) return "";
    const char* colon = strchr(name, ':');
    return colon ? std::string(colon + 1) : std::string(name);
}

static pugi::xml_node FindDescendantByLocalName(pugi::xml_node parent, const std::string& localName) {
    if (ToLower(GetLocalName(parent.name())) == ToLower(localName)) {
        return parent;
    }
    for (pugi::xml_node child : parent.children()) {
        pugi::xml_node res = FindDescendantByLocalName(child, localName);
        if (res) return res;
    }
    return pugi::xml_node();
}

static void FindAllDescendantsByLocalName(pugi::xml_node parent, const std::string& localName, std::vector<pugi::xml_node>& results) {
    if (ToLower(GetLocalName(parent.name())) == ToLower(localName)) {
        results.push_back(parent);
    }
    for (pugi::xml_node child : parent.children()) {
        FindAllDescendantsByLocalName(child, localName, results);
    }
}

static void GetTextContent(pugi::xml_node node, std::string& out) {
    if (node.type() == pugi::node_pcdata || node.type() == pugi::node_cdata) {
        out += node.value();
    } else {
        for (pugi::xml_node child : node.children()) {
            GetTextContent(child, out);
        }
    }
}

// ==========================================================================
//  FB2 converter (FictionBook 2.0 XML)
// ==========================================================================

static std::string ExtractTextFromFB2(const std::string& path) {
    std::string xmlData = ReadBinaryFile(path);
    if (xmlData.empty()) return "";

    // Check if XML is encoded in windows-1251 or cp1251
    bool isCp1251 = false;
    size_t declEnd = xmlData.find('>');
    if (declEnd != std::string::npos && declEnd < 500) {
        std::string header = xmlData.substr(0, declEnd);
        std::transform(header.begin(), header.end(), header.begin(), ::tolower);
        if (header.find("windows-1251") != std::string::npos ||
            header.find("cp1251") != std::string::npos) {
            isCp1251 = true;
        }
    }
    if (isCp1251) {
        xmlData = CP1251ToUTF8(xmlData);
    }

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(xmlData.data(), xmlData.size(),
        pugi::parse_default | pugi::parse_declaration);
    if (!result) return "";

    std::string out;

    // Recursive walker — collects text from <p>, <v>, <title>, <subtitle>
    std::function<void(pugi::xml_node)> walk = [&](pugi::xml_node node) {
        std::string name = ToLower(GetLocalName(node.name()));
        if (name == "binary") return;

        bool isParagraph = (name == "p" || name == "v");
        bool isTitle     = (name == "title" || name == "subtitle");
        bool isSection   = (name == "section" || name == "body");

        if (isParagraph || isTitle) {
            std::string line;
            GetTextContent(node, line);
            if (!line.empty()) {
                out += line + "\n";
                if (isTitle) out += "\n";
            }
        } else {
            for (pugi::xml_node child : node.children()) walk(child);
            if (isSection && !out.empty() && out.back() != '\n') out += "\n";
        }
    };

    pugi::xml_node fictionBook = FindDescendantByLocalName(doc, "FictionBook");
    if (!fictionBook) fictionBook = doc.document_element();
    std::vector<pugi::xml_node> bodies;
    FindAllDescendantsByLocalName(fictionBook, "body", bodies);
    for (pugi::xml_node body : bodies) walk(body);

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
        std::string name = ToLower(GetLocalName(node.name()));
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

    pugi::xml_node body = FindDescendantByLocalName(doc, "body");
    if (body) walk(body);
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
            pugi::xml_node rf = FindDescendantByLocalName(d, "rootfile");
            if (rf) opfPath = rf.attribute("full-path").value();
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
            std::vector<pugi::xml_node> items;
            FindAllDescendantsByLocalName(opfDoc, "item", items);
            for (auto& item : items) {
                std::string id   = item.attribute("id").value();
                std::string href = item.attribute("href").value();
                std::string mt   = item.attribute("media-type").value();
                if (!id.empty() && !href.empty() &&
                    (mt.find("html") != std::string::npos ||
                     mt.find("xhtml") != std::string::npos)) {
                    idToHref[id] = href;
                }
            }

            std::vector<pugi::xml_node> itemrefs;
            FindAllDescendantsByLocalName(opfDoc, "itemref", itemrefs);
            for (auto& itemref : itemrefs) {
                std::string idref = itemref.attribute("idref").value();
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
    std::vector<pugi::xml_node> paras;
    FindAllDescendantsByLocalName(doc, "p", paras);
    for (auto& para : paras) {
        std::string line;
        std::vector<pugi::xml_node> ts;
        FindAllDescendantsByLocalName(para, "t", ts);
        for (auto& t : ts) {
            line += t.text().get();
        }
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
    std::wstring wpath = UTF8ToWide(path);
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

#if defined(_WIN32)
const wchar_t* GetWindowsDialogFilterW() {
    return L"Book Files (*.txt;*.fb2;*.epub;*.docx;*.mobi)\0"
           L"*.txt;*.fb2;*.epub;*.docx;*.mobi\0"
           L"Text Files (*.txt)\0*.txt\0"
           L"FictionBook (*.fb2)\0*.fb2\0"
           L"EPUB (*.epub)\0*.epub\0"
           L"Word Document (*.docx)\0*.docx\0"
           L"Mobipocket (*.mobi)\0*.mobi\0"
           L"All Files (*.*)\0*.*\0";
}
#endif

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

    std::string tmpPath = "book.txt";
    if (!WriteTextFile(tmpPath, text)) {
        tmpPath = "../book.txt";
        if (!WriteTextFile(tmpPath, text)) return "";
    }
    return tmpPath;
}

} // namespace BookConverter
