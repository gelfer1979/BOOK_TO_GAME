// book_summarizer.cpp
// Standalone Windows console utility to summarize large books using the project's AI engines.

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <memory>
#include <filesystem>

#define NOMINMAX
#include <windows.h>
#include <commdlg.h>

#include "book_converter.h"
#include "modelapi.h"

// Forward declaration of GetUiText so modelapi.h compiles cleanly
std::string GetUiText(const std::string& key) {
    if (key == "pacing_critical_title") return "Pacing Critical:";
    if (key == "pacing_turn_status") return "Turns: {turns}/{min}-{max}";
    if (key == "pacing_rule_early") return "Early pacing rule.";
    if (key == "pacing_rule_mid") return "Mid pacing rule.";
    if (key == "pacing_rule_limit") return "Pacing limit reached.";
    return key;
}

std::string CleanRawSummary(std::string text) {
    size_t pos;
    // Strip JSON keys like "summary": "
    while ((pos = text.find("\"summary\": \"")) != std::string::npos) {
        text.replace(pos, 12, "");
    }
    while ((pos = text.find("\"summary\" : \"")) != std::string::npos) {
        text.replace(pos, 13, "");
    }
    while ((pos = text.find("\"summary\":")) != std::string::npos) {
        text.replace(pos, 10, "");
    }
    while ((pos = text.find("\"summary\" :")) != std::string::npos) {
        text.replace(pos, 11, "");
    }
    
    // Strip double quotes, curly braces, and backslashes
    std::string cleanText = "";
    for (char c : text) {
        if (c != '{' && c != '}' && c != '"' && c != '\\') {
            cleanText += c;
        }
    }
    
    return Trim(cleanText);
}

int main() {
    // Set console output code page to UTF-8 to correctly display Russian/Cyrillic characters
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::cout << "=====================================================" << std::endl;
    std::cout << "        BOOK TO GAME - STANDALONE SUMMARIZER         " << std::endl;
    std::cout << "=====================================================" << std::endl;
    std::cout << "Opening file dialog... Please select a book file." << std::endl;

    // 1. Open standard Windows file selection dialog
    OPENFILENAMEW ofn;
    wchar_t szFile[512] = { 0 };
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = BookConverter::GetWindowsDialogFilterW();
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn) != TRUE) {
        std::cout << "[Info] File dialog cancelled or closed. Exiting." << std::endl;
        return 0;
    }

    std::string filePath = BookConverter::WideToUTF8(szFile);
    std::cout << "[File] Selected: " << filePath << std::endl;

    // 2. Convert book to text using BookConverter
    std::cout << "[BookConverter] Parsing and converting file..." << std::endl;
    std::string ext = BookConverter::GetExt(filePath);
    std::string content = "";

    if (ext == ".txt") {
        std::ifstream txtFile(szFile);
        if (txtFile.is_open()) {
            std::stringstream ss;
            ss << txtFile.rdbuf();
            content = ss.str();
            txtFile.close();
        }
    } else {
        content = BookConverter::ConvertBookToText(filePath);
    }

    content = Trim(content);
    if (content.empty()) {
        std::cerr << "[Error] Failed to read or convert file. Format might be unsupported or file is empty." << std::endl;
        return 1;
    }

    std::cout << "[BookConverter] Successfully converted book! Length: " << content.length() << " characters." << std::endl;

    // 3. Load configurations from settings.json
    std::string aiModel = "ai_summarizer.json";
    std::string gameLanguage = "Russian";
    int maxRetries = 3;
    int retryDelayMs = 1000;
    int connectTimeout = 15;
    int requestTimeout = 45;

    std::ifstream settingsFile("settings.json");
    if (!settingsFile.is_open()) {
        settingsFile.open("../settings.json");
    }
    if (settingsFile.is_open()) {
        try {
            nlohmann::json j;
            settingsFile >> j;
            if (j.contains("Language") && j["Language"].is_string()) {
                gameLanguage = j["Language"].get<std::string>();
            }
            if (j.contains("maxRetries") && j["maxRetries"].is_number()) {
                maxRetries = j["maxRetries"].get<int>();
            }
            if (j.contains("retryDelayMs") && j["retryDelayMs"].is_number()) {
                retryDelayMs = j["retryDelayMs"].get<int>();
            }
            if (j.contains("CURLOPT_CONNECTTIMEOUT") && j["CURLOPT_CONNECTTIMEOUT"].is_number()) {
                connectTimeout = j["CURLOPT_CONNECTTIMEOUT"].get<int>();
            }
            if (j.contains("CURLOPT_TIMEOUT") && j["CURLOPT_TIMEOUT"].is_number()) {
                requestTimeout = j["CURLOPT_TIMEOUT"].get<int>();
            }
        } catch (...) {
            std::cout << "[Warning] Failed to parse settings.json. Using default settings." << std::endl;
        }
        settingsFile.close();
    } else {
        std::cout << "[Warning] settings.json not found. Using default settings." << std::endl;
    }

    std::cout << "[Config] Using Model Profile: " << aiModel << std::endl;
    std::cout << "[Config] Target Language: " << gameLanguage << std::endl;

    // 4. Initialize AI client
    std::unique_ptr<AskAiExternal> aiClient = std::make_unique<AskAiExternal>(aiModel);
    aiClient->setRetrySettings(maxRetries, retryDelayMs);
    aiClient->setTimeoutSettings(connectTimeout, requestTimeout);

    // 5. Chunk and summarize exactly matching game actions
    std::string consolidatedSummary = "";
    if (content.length() > 20000) {
        std::vector<std::string> chunks = SplitIntoChunks(content, 20000);
        std::cout << "[AI Summarizer] Large book detected. Splitting into " << chunks.size() << " chunks..." << std::endl;

        int stepNum = 1;
        for (const auto& chunk : chunks) {
            std::cout << "[Progress] Processing Part " << stepNum << " of " << chunks.size() << "..." << std::endl;

            aiClient->setSystemPrompt("You are a professional summarizing assistant. Summarize the text as requested. Do NOT output any options tags, XML, or choice suggestions.");

            std::string summaryPrompt;
            if (gameLanguage == "Russian") {
                summaryPrompt = "Пожалуйста, напиши подробный и точный пересказ следующей части книги. "
                                "Сохрани ключевые события, имена персонажей, локации и важные детали сюжета. "
                                "Напиши пересказ строго на русском языке:\n\n" + chunk;
            } else {
                summaryPrompt = "Please write a detailed and accurate plot summary of the following part of the book. "
                                "Preserve all key events, character names, locations, and important plot details. "
                                "Write the summary strictly in the target language (" + gameLanguage + "):\n\n" + chunk;
            }

            std::string chunkSummary = "";
            int attempt = 0;
            while (attempt < 3) {
                chunkSummary = aiClient->ask(summaryPrompt, gameLanguage);
                chunkSummary = Trim(chunkSummary);
                if (chunkSummary.empty() || chunkSummary.find("Error") != std::string::npos) {
                    attempt++;
                    std::cout << "[Retry] Attempt " << attempt << " failed for part " << stepNum << std::endl;
                    Sleep(retryDelayMs);
                } else {
                    break;
                }
            }

            if (chunkSummary.empty() || chunkSummary.find("Error") != std::string::npos) {
                std::cerr << "[Fatal] Summarization failed for part " << stepNum << ": " << chunkSummary << std::endl;
                return 1;
            }

            // Clean raw chunk text (remove json keys, braces, quotes, backslashes)
            std::string cleanChunk = CleanRawSummary(chunkSummary);

            consolidatedSummary += cleanChunk + "\n\n";
            stepNum++;
        }
    } else {
        std::cout << "[AI Summarizer] Summarizing book as a single chunk..." << std::endl;
        aiClient->setSystemPrompt("You are a professional summarizing assistant. Summarize the text as requested. Do NOT output any options tags, XML, or choice suggestions.");

        std::string summaryPrompt;
        if (gameLanguage == "Russian") {
            summaryPrompt = "Пожалуйста, напиши подробный и точный пересказ следующей части книги. "
                            "Сохрани ключевые события, имена персонажей, локации и важные детали сюжета. "
                            "Напиши пересказ строго на русском языке:\n\n" + content;
        } else {
            summaryPrompt = "Please write a detailed and accurate plot summary of the following part of the book. "
                            "Preserve all key events, character names, locations, and important plot details. "
                            "Write the summary strictly in the target language (" + gameLanguage + "):\n\n" + content;
        }

        std::string rawSummary = aiClient->ask(summaryPrompt, gameLanguage);
        rawSummary = Trim(rawSummary);

        if (rawSummary.empty() || rawSummary.find("Error") != std::string::npos) {
            std::cerr << "[Fatal] Summarization failed: " << rawSummary << std::endl;
            return 1;
        }

        // Clean raw summary text (remove json keys, braces, quotes, backslashes)
        consolidatedSummary = CleanRawSummary(rawSummary);
    }

    // 6. Save consolidated summary to newbook.txt
    std::ofstream outFile("newbook.txt");
    if (!outFile.is_open()) {
        std::cerr << "[Error] Could not open newbook.txt for writing." << std::endl;
        return 1;
    }

    outFile << consolidatedSummary;
    outFile.close();

    std::cout << "=====================================================" << std::endl;
    std::cout << "SUCCESS: Summarization completed successfully!" << std::endl;
    std::cout << "The summarized text has been saved to: newbook.txt" << std::endl;
    std::cout << "=====================================================" << std::endl;

    return 0;
}
