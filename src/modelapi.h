#pragma once
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <algorithm>
#include <cstdint>
#include <functional>
#ifndef __EMSCRIPTEN__
#include <curl/curl.h>
#endif
#include <nlohmann/json.hpp>
#include <filesystem>
#include "book_converter.h"


inline void SaveBookErrorLog(const std::string& rawResponse, const std::string& errorMsg);
inline std::string Trim(const std::string& str);

struct PipelineStep {
    std::string action;      // "replace", "remove_chars", "trim"
    std::string target;      // target string or characters to remove
    std::string replacement; // replacement string (for "replace")
};

// Pure UI-independent Chat Message representation
struct ChatMessageData {
    std::string sender; // "User" or "AI"
    std::string text;   // Raw full text content
};

// Abstract base class for asking AI questions
class AskAi {
public:
    virtual std::string ask(const std::string& question, const std::string& language = "English") = 0;   
    virtual std::string askChat(const std::vector<ChatMessageData>& history, const std::string& language) {
        if (history.empty()) return "";
        return ask(history.back().text, language);
    }
    virtual void setSystemPrompt(const std::string& prompt) {}
    virtual ~AskAi() = default;
};

// Universal concrete implementation for external AI services (LM Studio, Google Gemini, etc.)
class AskAiExternal : public AskAi {
public:
    // Initialize by loading options from the specified JSON config file
    AskAiExternal(const std::string& configFilePath) {
        std::ifstream file(configFilePath);
        std::string loadedPath = configFilePath;
        if (!file.is_open()) {
            std::string parentPath = "../" + configFilePath;
            file.open(parentPath);
            if (file.is_open()) {
                loadedPath = parentPath;
            }
        }
        
        if (file.is_open()) {
            try {
                nlohmann::json j;
                file >> j;
                
                if (j.contains("baseUrl") && j["baseUrl"].is_string()) {
                    baseUrl_ = j["baseUrl"].get<std::string>();
                }
                if (j.contains("modelName") && j["modelName"].is_string()) {
                    modelName_ = j["modelName"].get<std::string>();
                }
                if (j.contains("apiKey") && j["apiKey"].is_string()) {
                    apiKey_ = j["apiKey"].get<std::string>();
                }
                if (j.contains("apiKeyEnvVar") && j["apiKeyEnvVar"].is_string()) {
                    apiKeyEnvVar_ = j["apiKeyEnvVar"].get<std::string>();
                }
                if (j.contains("format") && j["format"].is_string()) {
                    format_ = j["format"].get<std::string>();
                }
                if (j.contains("maxRetries") && j["maxRetries"].is_number()) {
                    maxRetries_ = j["maxRetries"].get<int>();
                }
                if (j.contains("retryDelayMs") && j["retryDelayMs"].is_number()) {
                    retryDelayMs_ = j["retryDelayMs"].get<int>();
                }
                
                std::cout << "[Config] Loaded configuration from: " << loadedPath << std::endl;
                std::cout << "[Config] Base URL: " << baseUrl_ << ", Format: " << format_ << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[Config] Parse error: " << e.what() << std::endl;
            }
            file.close();
        } else {
            std::cerr << "[Config] Failed to open configuration file: " << configFilePath << " (also tried parent directory)." << std::endl;
        }

        if (baseUrl_.empty() && configFilePath != "ai_gemini.json") {
            std::cout << "[Config] Attempting robust fallback to default 'ai_gemini.json'..." << std::endl;
            std::ifstream fallbackFile("ai_gemini.json");
            std::string fallbackLoadedPath = "ai_gemini.json";
            if (!fallbackFile.is_open()) {
                fallbackFile.open("../ai_gemini.json");
                fallbackLoadedPath = "../ai_gemini.json";
            }
            if (fallbackFile.is_open()) {
                try {
                    nlohmann::json j;
                    fallbackFile >> j;
                    if (j.contains("baseUrl") && j["baseUrl"].is_string()) {
                        baseUrl_ = j["baseUrl"].get<std::string>();
                    }
                    if (j.contains("modelName") && j["modelName"].is_string()) {
                        modelName_ = j["modelName"].get<std::string>();
                    }
                    if (j.contains("apiKey") && j["apiKey"].is_string()) {
                        apiKey_ = j["apiKey"].get<std::string>();
                    }
                    if (j.contains("apiKeyEnvVar") && j["apiKeyEnvVar"].is_string()) {
                        apiKeyEnvVar_ = j["apiKeyEnvVar"].get<std::string>();
                    }
                    if (j.contains("format") && j["format"].is_string()) {
                        format_ = j["format"].get<std::string>();
                    }
                    if (j.contains("maxRetries") && j["maxRetries"].is_number()) {
                        maxRetries_ = j["maxRetries"].get<int>();
                    }
                    if (j.contains("retryDelayMs") && j["retryDelayMs"].is_number()) {
                        retryDelayMs_ = j["retryDelayMs"].get<int>();
                    }
                    std::cout << "[Config] Robust fallback successful. Loaded default config from: " << fallbackLoadedPath << std::endl;
                } catch (...) {}
                fallbackFile.close();
            }
        }


        // Apply environment override if defined to ensure key security
        if (!apiKeyEnvVar_.empty()) {
            const char* envVal = std::getenv(apiKeyEnvVar_.c_str());
            if (envVal != nullptr && std::string(envVal).length() > 0) {
                apiKey_ = std::string(envVal);
                std::cout << "[Security] Override API key loaded from environment: " << apiKeyEnvVar_ << std::endl;
            }
        }
    }

    void setSystemPrompt(const std::string& prompt) override {
        systemPrompt_ = prompt;
    }

#ifdef __EMSCRIPTEN__
    std::string ask(const std::string& question, const std::string& language = "English") override {
        // Construct formatting based on custom format configuration
        std::string jsonPayload;
        if (format_ == "gemini") {
            // Google Gemini request body format
            nlohmann::json requestBody;
            requestBody["contents"] = nlohmann::json::array();
            nlohmann::json part;
            part["text"] = question;
            nlohmann::json content;
            content["parts"] = nlohmann::json::array({part});
            requestBody["contents"].push_back(content);
            
            if (!systemPrompt_.empty()) {
                nlohmann::json sysInst;
                nlohmann::json sysPart;
                sysPart["text"] = systemPrompt_;
                sysInst["parts"] = nlohmann::json::array({sysPart});
                requestBody["systemInstruction"] = sysInst;
            }

            nlohmann::json safetySettings = nlohmann::json::array({
                {{"category", "HARM_CATEGORY_HARASSMENT"}, {"threshold", "BLOCK_NONE"}},
                {{"category", "HARM_CATEGORY_HATE_SPEECH"}, {"threshold", "BLOCK_NONE"}},
                {{"category", "HARM_CATEGORY_SEXUALLY_EXPLICIT"}, {"threshold", "BLOCK_NONE"}},
                {{"category", "HARM_CATEGORY_DANGEROUS_CONTENT"}, {"threshold", "BLOCK_NONE"}}
            });
            requestBody["safetySettings"] = safetySettings;

            nlohmann::json genConfig;
            genConfig["responseMimeType"] = "application/json";
            requestBody["generationConfig"] = genConfig;
            
            jsonPayload = requestBody.dump();
        } else {
            // Default OpenAI-compatible format
            nlohmann::json requestBody;
            requestBody["model"] = modelName_;
            requestBody["messages"] = nlohmann::json::array();
            if (!systemPrompt_.empty()) {
                requestBody["messages"].push_back({{"role", "system"}, {"content", systemPrompt_}});
            }
            requestBody["messages"].push_back({{"role", "user"}, {"content", question}});
            requestBody["temperature"] = 0.7;
            jsonPayload = requestBody.dump();
        }

        // Perform synchronous XHR in JavaScript
        char* responsePtr = nullptr;
        int responseCode = 0;
        
        EM_ASM({
            var url = UTF8ToString($0);
            var payload = UTF8ToString($1);
            var apiKey = UTF8ToString($2);
            var format = UTF8ToString($3);
            var pResponseOut = $4;
            var pResponseCodeOut = $5;
            
            try {
                var xhr = new XMLHttpRequest();
                xhr.open("POST", url, false); // SYNCHRONOUS
                xhr.setRequestHeader("Content-Type", "application/json; charset=utf-8");
                if (apiKey) {
                    if (format === "gemini") {
                        xhr.setRequestHeader("x-goog-api-key", apiKey);
                    } else {
                        xhr.setRequestHeader("Authorization", "Bearer " + apiKey);
                    }
                }
                xhr.send(payload);
                
                HEAP32[pResponseCodeOut >> 2] = xhr.status;
                var responseText = xhr.responseText || "";
                var lengthBytes = lengthBytesUTF8(responseText) + 1;
                var stringOnWasmHeap = _malloc(lengthBytes);
                stringToUTF8(responseText, stringOnWasmHeap, lengthBytes);
                HEAP32[pResponseOut >> 2] = stringOnWasmHeap;
            } catch (err) {
                HEAP32[pResponseCodeOut >> 2] = 0;
                var errText = "Error: " + err.message;
                var lengthBytes = lengthBytesUTF8(errText) + 1;
                var stringOnWasmHeap = _malloc(lengthBytes);
                stringToUTF8(errText, stringOnWasmHeap, lengthBytes);
                HEAP32[pResponseOut >> 2] = stringOnWasmHeap;
            }
        }, baseUrl_.c_str(), jsonPayload.c_str(), apiKey_.c_str(), format_.c_str(), &responsePtr, &responseCode);
        
        std::string readBuffer;
        if (responsePtr != nullptr) {
            readBuffer = std::string(responsePtr);
            free(responsePtr);
        }
        
        if (responseCode == 401) {
            return "Error 401: Unauthorized. Check API key.";
        } else if (responseCode == 429) {
            return "Error 429: Too Many Requests. Rate limit exceeded.";
        } else if (responseCode != 200) {
            if (!readBuffer.empty() && readBuffer.find("Error") != std::string::npos) {
                return readBuffer;
            }
            return "Error: API returned status code " + std::to_string(responseCode);
        }

        // Parse format-specific response
        try {
            nlohmann::json responseJson = nlohmann::json::parse(readBuffer);
            std::string aiResponse;
            
            if (format_ == "gemini") {
                if (responseJson.contains("candidates") && !responseJson["candidates"].empty() &&
                    responseJson["candidates"][0].contains("content") &&
                    responseJson["candidates"][0]["content"].contains("parts") &&
                    !responseJson["candidates"][0]["content"]["parts"].empty() &&
                    responseJson["candidates"][0]["content"]["parts"][0].contains("text")) {
                    
                    aiResponse = responseJson["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                } else {
                    bool blocked = false;
                    if (responseJson.contains("promptFeedback") && responseJson["promptFeedback"].contains("blockReason")) {
                        blocked = true;
                    } else if (responseJson.contains("candidates") && !responseJson["candidates"].empty()) {
                        std::string finishReason = responseJson["candidates"][0].value("finishReason", "");
                        if (finishReason == "SAFETY" || finishReason == "RECITATION" || finishReason == "OTHER") {
                            blocked = true;
                        }
                    }
                    
                    if (blocked) {
                        bool isEn = (language == "English" || language == "en" || language == "EN");
                        if (isEn) {
                            aiResponse = "Error: The book content was blocked by Google Gemini's safety filters (PROHIBITED_CONTENT). Please try a different book or select a local/unfiltered AI model in Settings.";
                        } else {
                            aiResponse = "Ошибка: книга отклонена фильтрами безопасности Google Gemini (запрещенный контент). Пожалуйста, выберите другую книгу или используйте локальную/безцензурную модель ИИ в Настройках.";
                        }
                    } else {
                        aiResponse = "Error: Invalid Gemini response format.";
                    }
                }
            } else {
                if (responseJson.contains("choices") && !responseJson["choices"].empty() &&
                    responseJson["choices"][0].contains("message") &&
                    responseJson["choices"][0]["message"].contains("content")) {
                    
                    aiResponse = responseJson["choices"][0]["message"]["content"].get<std::string>();
                } else {
                    aiResponse = "Error: Invalid OpenAI response format.";
                }
            }
            
            return aiResponse;
        } catch (const std::exception& e) {
            return "Error: Failed to parse API response JSON.";
        }
    }
#else
    std::string ask(const std::string& question, const std::string& language = "English") override {
        int attempt = 0;
        int delayMs = retryDelayMs_;
        
        while (true) {
            CURL* curl;
            CURLcode res;
            std::string readBuffer; // Stores the response payload

            curl = curl_easy_init();
            if (!curl) {
                std::cerr << "[API] Failed to initialize cURL." << std::endl;
                return "Error: Failed to initialize cURL.";
            }

            // Construct formatting based on custom format configuration
            std::string jsonPayload;
            if (format_ == "gemini") {
                // Google Gemini request body format
                nlohmann::json requestBody;
                requestBody["contents"] = nlohmann::json::array();
                nlohmann::json part;
                part["text"] = question;
                nlohmann::json content;
                content["parts"] = nlohmann::json::array({part});
                requestBody["contents"].push_back(content);
                
                if (!systemPrompt_.empty()) {
                    nlohmann::json sysInst;
                    nlohmann::json sysPart;
                    sysPart["text"] = systemPrompt_;
                    sysInst["parts"] = nlohmann::json::array({sysPart});
                    requestBody["systemInstruction"] = sysInst;
                }

                nlohmann::json safetySettings = nlohmann::json::array({
                    {{"category", "HARM_CATEGORY_HARASSMENT"}, {"threshold", "BLOCK_NONE"}},
                    {{"category", "HARM_CATEGORY_HATE_SPEECH"}, {"threshold", "BLOCK_NONE"}},
                    {{"category", "HARM_CATEGORY_SEXUALLY_EXPLICIT"}, {"threshold", "BLOCK_NONE"}},
                    {{"category", "HARM_CATEGORY_DANGEROUS_CONTENT"}, {"threshold", "BLOCK_NONE"}}
                });
                requestBody["safetySettings"] = safetySettings;

                nlohmann::json genConfig;
                genConfig["responseMimeType"] = "application/json";
                requestBody["generationConfig"] = genConfig;
                
                jsonPayload = requestBody.dump();
            } else {
                // Default OpenAI-compatible format (LM Studio, etc.)
                nlohmann::json requestBody;
                requestBody["model"] = modelName_;
                requestBody["messages"] = nlohmann::json::array();
                if (!systemPrompt_.empty()) {
                    requestBody["messages"].push_back({{"role", "system"}, {"content", systemPrompt_}});
                }
                requestBody["messages"].push_back({{"role", "user"}, {"content", question}});
                requestBody["temperature"] = 0.7;
                jsonPayload = requestBody.dump();
            }

            // Configure general curl options
            curl_easy_setopt(curl, CURLOPT_URL, baseUrl_.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, jsonPayload.length());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)connectTimeout_);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)requestTimeout_);

            // Set callback function to write received data into readBuffer
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            // Configure specific headers based on API format
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
            
            if (!apiKey_.empty()) {
                if (format_ == "gemini") {
                    std::string geminiHeader = "x-goog-api-key: " + apiKey_;
                    headers = curl_slist_append(headers, geminiHeader.c_str());
                } else {
                    std::string authHeader = "Authorization: Bearer " + apiKey_;
                    headers = curl_slist_append(headers, authHeader.c_str());
                }
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            std::cout << "[API] Sending request to: " << baseUrl_ << " (Attempt " << (attempt + 1) << ")" << std::endl;
            res = curl_easy_perform(curl);

            if (res != CURLE_OK) {
                std::string errStr = curl_easy_strerror(res);
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                
                attempt++;
                if (attempt <= maxRetries_) {
                    std::cout << "[API Retry] Connection failed (" << errStr << "). Retrying in " << delayMs << "ms..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    delayMs *= 2;
                    continue;
                } else {
                    return "Error: Connection failed or request timed out (" + errStr + ").";
                }
            }

            // Retrieve and log HTTP response code
            long httpResponseCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpResponseCode);
            std::cout << "[API Response] HTTP status code: " << httpResponseCode << std::endl;

            if (httpResponseCode == 429 || (httpResponseCode >= 500 && httpResponseCode <= 599)) {
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                
                attempt++;
                if (attempt <= maxRetries_) {
                    std::cout << "[API Retry] HTTP " << httpResponseCode << " (Transient Error). Retrying in " << delayMs << "ms..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    delayMs *= 2;
                    continue;
                } else {
                    if (httpResponseCode == 429) {
                        return "Error 429: Too Many Requests. Rate limit exceeded.";
                    } else {
                        return "Error " + std::to_string(httpResponseCode) + ": Transient server error.";
                    }
                }
            }

            if (httpResponseCode == 401) {
                std::cerr << "[API Response] Error 401: Unauthorized. Please check your API key." << std::endl;
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return "Error 401: Unauthorized. Check API key.";
            } else if (httpResponseCode != 200) {
                std::cerr << "[API Response] Warning: Received non-200 HTTP status code: " << httpResponseCode << std::endl;
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return "Error: API returned HTTP status code " + std::to_string(httpResponseCode);
            }

            // Parse format-specific response
            try {
                nlohmann::json responseJson = nlohmann::json::parse(readBuffer);
                std::string aiResponse;
                
                if (format_ == "gemini") {
                    // Parse Google Gemini response format
                    if (responseJson.contains("candidates") && !responseJson["candidates"].empty() &&
                        responseJson["candidates"][0].contains("content") &&
                        responseJson["candidates"][0]["content"].contains("parts") &&
                        !responseJson["candidates"][0]["content"]["parts"].empty() &&
                        responseJson["candidates"][0]["content"]["parts"][0].contains("text")) {
                        
                        aiResponse = responseJson["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                    } else {
                        std::cerr << "[API Response] Invalid Gemini response structure: " << readBuffer << std::endl;
                        SaveBookErrorLog(readBuffer, "Invalid Gemini response structure.");
                        
                        bool blocked = false;
                        if (responseJson.contains("promptFeedback") && responseJson["promptFeedback"].contains("blockReason")) {
                            blocked = true;
                        } else if (responseJson.contains("candidates") && !responseJson["candidates"].empty()) {
                            std::string finishReason = responseJson["candidates"][0].value("finishReason", "");
                            if (finishReason == "SAFETY" || finishReason == "RECITATION" || finishReason == "OTHER") {
                                blocked = true;
                            }
                        }
                        
                        if (blocked) {
                            bool isEn = (language == "English" || language == "en" || language == "EN");
                            if (isEn) {
                                aiResponse = "Error: The book content was blocked by Google Gemini's safety filters (PROHIBITED_CONTENT). Please try a different book or select a local/unfiltered AI model in Settings.";
                            } else {
                                std::string englishError = "The book content was blocked by Google Gemini's safety filters (PROHIBITED_CONTENT). Please try a different book or select a local/unfiltered AI model in Settings.";
                                std::string translatePrompt = "You are a professional translator. Translate the following error message into the language: '" + language + "'. Return ONLY the translated text, with no extra formatting, quotes, or conversational phrases:\n\n" + englishError;
                                std::cout << "[API Error Translation] Translating error to: " << language << "..." << std::endl;
                                std::string translated = ask(translatePrompt, "English");
                                translated = Trim(translated);
                                if (!translated.empty() && translated.find("Error") == std::string::npos) {
                                    aiResponse = "Error: " + translated;
                                } else {
                                    if (language == "Russian" || language == "ru" || language == "RU") {
                                        aiResponse = "Ошибка: книга отклонена фильтрами безопасности Google Gemini (запрещенный контент). Пожалуйста, выберите другую книгу или используйте локальную/безцензурную модель ИИ в Настройках.";
                                    } else {
                                        aiResponse = "Error: " + englishError;
                                    }
                                }
                            }
                        } else {
                            aiResponse = "Error: Invalid Gemini response format.";
                        }
                    }
                } else {
                    // Parse standard OpenAI response format
                    if (responseJson.contains("choices") && !responseJson["choices"].empty() &&
                        responseJson["choices"][0].contains("message") &&
                        responseJson["choices"][0]["message"].contains("content")) {
                        
                        aiResponse = responseJson["choices"][0]["message"]["content"].get<std::string>();
                    } else {
                        std::cerr << "[API Response] Invalid OpenAI response format: " << readBuffer << std::endl;
                        aiResponse = "Error: Invalid OpenAI response format.";
                    }
                }
                
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return aiResponse;
            } catch (const std::exception& e) {
                std::cerr << "[API Response] Parsing error: " << e.what() << std::endl;
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return "Error: Failed to parse API response JSON.";
            }
        }
    }
#endif

#ifdef __EMSCRIPTEN__
    std::string askChat(const std::vector<ChatMessageData>& history, const std::string& language) override {
        // Construct formatting based on custom format configuration
        std::string jsonPayload;
        if (format_ == "gemini") {
            // Google Gemini request body format
            nlohmann::json requestBody;
            nlohmann::json contents = nlohmann::json::array();
            
            for (const auto& msg : history) {
                std::string role = (msg.sender == "User") ? "user" : "model";
                if (!contents.empty() && contents.back()["role"] == role) {
                    std::string existingText = contents.back()["parts"][0]["text"].get<std::string>();
                    contents.back()["parts"][0]["text"] = existingText + "\n\n" + msg.text;
                } else {
                    contents.push_back({
                        {"role", role},
                        {"parts", nlohmann::json::array({{{"text", msg.text}}})}
                    });
                }
            }
            requestBody["contents"] = contents;
            
            if (!systemPrompt_.empty()) {
                nlohmann::json sysInst;
                nlohmann::json sysPart;
                sysPart["text"] = systemPrompt_;
                sysInst["parts"] = nlohmann::json::array({sysPart});
                requestBody["systemInstruction"] = sysInst;
            }

            nlohmann::json safetySettings = nlohmann::json::array({
                {{"category", "HARM_CATEGORY_HARASSMENT"}, {"threshold", "BLOCK_NONE"}},
                {{"category", "HARM_CATEGORY_HATE_SPEECH"}, {"threshold", "BLOCK_NONE"}},
                {{"category", "HARM_CATEGORY_SEXUALLY_EXPLICIT"}, {"threshold", "BLOCK_NONE"}},
                {{"category", "HARM_CATEGORY_DANGEROUS_CONTENT"}, {"threshold", "BLOCK_NONE"}}
            });
            requestBody["safetySettings"] = safetySettings;

            nlohmann::json genConfig;
            genConfig["responseMimeType"] = "application/json";
            requestBody["generationConfig"] = genConfig;
            
            jsonPayload = requestBody.dump();
        } else {
            // Default OpenAI-compatible format
            nlohmann::json requestBody;
            requestBody["model"] = modelName_;
            requestBody["messages"] = nlohmann::json::array();
            if (!systemPrompt_.empty()) {
                requestBody["messages"].push_back({{"role", "system"}, {"content", systemPrompt_}});
            }
            
            for (const auto& msg : history) {
                std::string role = (msg.sender == "User") ? "user" : "assistant";
                if (!requestBody["messages"].empty() && requestBody["messages"].back()["role"] == role) {
                    std::string existingText = requestBody["messages"].back()["content"].get<std::string>();
                    requestBody["messages"].back()["content"] = existingText + "\n\n" + msg.text;
                } else {
                    requestBody["messages"].push_back({{"role", role}, {"content", msg.text}});
                }
            }
            requestBody["temperature"] = 0.7;
            jsonPayload = requestBody.dump();
        }

        // Perform synchronous XHR in JavaScript
        char* responsePtr = nullptr;
        int responseCode = 0;
        
        EM_ASM({
            var url = UTF8ToString($0);
            var payload = UTF8ToString($1);
            var apiKey = UTF8ToString($2);
            var format = UTF8ToString($3);
            var pResponseOut = $4;
            var pResponseCodeOut = $5;
            
            try {
                var xhr = new XMLHttpRequest();
                xhr.open("POST", url, false); // SYNCHRONOUS
                xhr.setRequestHeader("Content-Type", "application/json; charset=utf-8");
                if (apiKey) {
                    if (format === "gemini") {
                        xhr.setRequestHeader("x-goog-api-key", apiKey);
                    } else {
                        xhr.setRequestHeader("Authorization", "Bearer " + apiKey);
                    }
                }
                xhr.send(payload);
                
                HEAP32[pResponseCodeOut >> 2] = xhr.status;
                var responseText = xhr.responseText || "";
                var lengthBytes = lengthBytesUTF8(responseText) + 1;
                var stringOnWasmHeap = _malloc(lengthBytes);
                stringToUTF8(responseText, stringOnWasmHeap, lengthBytes);
                HEAP32[pResponseOut >> 2] = stringOnWasmHeap;
            } catch (err) {
                HEAP32[pResponseCodeOut >> 2] = 0;
                var errText = "Error: " + err.message;
                var lengthBytes = lengthBytesUTF8(errText) + 1;
                var stringOnWasmHeap = _malloc(lengthBytes);
                stringToUTF8(errText, stringOnWasmHeap, lengthBytes);
                HEAP32[pResponseOut >> 2] = stringOnWasmHeap;
            }
        }, baseUrl_.c_str(), jsonPayload.c_str(), apiKey_.c_str(), format_.c_str(), &responsePtr, &responseCode);
        
        std::string readBuffer;
        if (responsePtr != nullptr) {
            readBuffer = std::string(responsePtr);
            free(responsePtr);
        }
        
        if (responseCode == 401) {
            return "Error 401: Unauthorized. Check API key.";
        } else if (responseCode == 429) {
            return "Error 429: Too Many Requests. Rate limit exceeded.";
        } else if (responseCode != 200) {
            if (!readBuffer.empty() && readBuffer.find("Error") != std::string::npos) {
                return readBuffer;
            }
            return "Error: API returned status code " + std::to_string(responseCode);
        }

        // Parse format-specific response
        try {
            nlohmann::json responseJson = nlohmann::json::parse(readBuffer);
            std::string aiResponse;
            
            if (format_ == "gemini") {
                if (responseJson.contains("candidates") && !responseJson["candidates"].empty() &&
                    responseJson["candidates"][0].contains("content") &&
                    responseJson["candidates"][0]["content"].contains("parts") &&
                    !responseJson["candidates"][0]["content"]["parts"].empty() &&
                    responseJson["candidates"][0]["content"]["parts"][0].contains("text")) {
                    
                    aiResponse = responseJson["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                } else {
                    bool blocked = false;
                    if (responseJson.contains("promptFeedback") && responseJson["promptFeedback"].contains("blockReason")) {
                        blocked = true;
                    } else if (responseJson.contains("candidates") && !responseJson["candidates"].empty()) {
                        std::string finishReason = responseJson["candidates"][0].value("finishReason", "");
                        if (finishReason == "SAFETY" || finishReason == "RECITATION" || finishReason == "OTHER") {
                            blocked = true;
                        }
                    }
                    
                    if (blocked) {
                        bool isEn = (language == "English" || language == "en" || language == "EN");
                        if (isEn) {
                            aiResponse = "Error: The prompt or narrative was blocked by Google Gemini's safety filters (PROHIBITED_CONTENT). Please try a different choice or select a local/unfiltered AI model in Settings.";
                        } else {
                            aiResponse = "Ошибка: ответ заблокирован фильтрами безопасности Google Gemini (запрещенный контент). Пожалуйста, выберите другое действие или используйте локальную/безцензурную модель ИИ в Настройках.";
                        }
                    } else {
                        aiResponse = "Error: Invalid Gemini response format.";
                    }
                }
            } else {
                if (responseJson.contains("choices") && !responseJson["choices"].empty() &&
                    responseJson["choices"][0].contains("message") &&
                    responseJson["choices"][0]["message"].contains("content")) {
                    
                    aiResponse = responseJson["choices"][0]["message"]["content"].get<std::string>();
                } else {
                    aiResponse = "Error: Invalid OpenAI response format.";
                }
            }
            
            return aiResponse;
        } catch (const std::exception& e) {
            return "Error: Failed to parse API response JSON.";
        }
    }
#else
    std::string askChat(const std::vector<ChatMessageData>& history, const std::string& language) override {
        int attempt = 0;
        int delayMs = retryDelayMs_;
        
        while (true) {
            CURL* curl;
            CURLcode res;
            std::string readBuffer; // Stores the response payload

            curl = curl_easy_init();
            if (!curl) {
                std::cerr << "[API] Failed to initialize cURL." << std::endl;
                return "Error: Failed to initialize cURL.";
            }

            // Construct formatting based on custom format configuration
            std::string jsonPayload;
            if (format_ == "gemini") {
                // Google Gemini request body format
                nlohmann::json requestBody;
                nlohmann::json contents = nlohmann::json::array();
                
                for (const auto& msg : history) {
                    std::string role = (msg.sender == "User") ? "user" : "model";
                    if (!contents.empty() && contents.back()["role"] == role) {
                        std::string existingText = contents.back()["parts"][0]["text"].get<std::string>();
                        contents.back()["parts"][0]["text"] = existingText + "\n\n" + msg.text;
                    } else {
                        contents.push_back({
                            {"role", role},
                            {"parts", nlohmann::json::array({{{"text", msg.text}}})}
                        });
                    }
                }
                requestBody["contents"] = contents;
                
                if (!systemPrompt_.empty()) {
                    nlohmann::json sysInst;
                    nlohmann::json sysPart;
                    sysPart["text"] = systemPrompt_;
                    sysInst["parts"] = nlohmann::json::array({sysPart});
                    requestBody["systemInstruction"] = sysInst;
                }

                nlohmann::json safetySettings = nlohmann::json::array({
                    {{"category", "HARM_CATEGORY_HARASSMENT"}, {"threshold", "BLOCK_NONE"}},
                    {{"category", "HARM_CATEGORY_HATE_SPEECH"}, {"threshold", "BLOCK_NONE"}},
                    {{"category", "HARM_CATEGORY_SEXUALLY_EXPLICIT"}, {"threshold", "BLOCK_NONE"}},
                    {{"category", "HARM_CATEGORY_DANGEROUS_CONTENT"}, {"threshold", "BLOCK_NONE"}}
                });
                requestBody["safetySettings"] = safetySettings;

                nlohmann::json genConfig;
                genConfig["responseMimeType"] = "application/json";
                requestBody["generationConfig"] = genConfig;
                
                jsonPayload = requestBody.dump();
            } else {
                // Default OpenAI-compatible format (LM Studio, etc.)
                nlohmann::json requestBody;
                requestBody["model"] = modelName_;
                requestBody["messages"] = nlohmann::json::array();
                if (!systemPrompt_.empty()) {
                    requestBody["messages"].push_back({{"role", "system"}, {"content", systemPrompt_}});
                }
                
                for (const auto& msg : history) {
                    std::string role = (msg.sender == "User") ? "user" : "assistant";
                    if (!requestBody["messages"].empty() && requestBody["messages"].back()["role"] == role) {
                        std::string existingText = requestBody["messages"].back()["content"].get<std::string>();
                        requestBody["messages"].back()["content"] = existingText + "\n\n" + msg.text;
                    } else {
                        requestBody["messages"].push_back({{"role", role}, {"content", msg.text}});
                    }
                }
                requestBody["temperature"] = 0.7;
                jsonPayload = requestBody.dump();
            }

            // Configure general curl options
            curl_easy_setopt(curl, CURLOPT_URL, baseUrl_.c_str());
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, jsonPayload.length());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, (long)connectTimeout_);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)requestTimeout_);

            // Set callback function to write received data into readBuffer
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);

            // Configure specific headers based on API format
            struct curl_slist* headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/json; charset=utf-8");
            
            if (!apiKey_.empty()) {
                if (format_ == "gemini") {
                    std::string geminiHeader = "x-goog-api-key: " + apiKey_;
                    headers = curl_slist_append(headers, geminiHeader.c_str());
                } else {
                    std::string authHeader = "Authorization: Bearer " + apiKey_;
                    headers = curl_slist_append(headers, authHeader.c_str());
                }
            }
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            std::cout << "[API] Sending chat request to: " << baseUrl_ << " (Attempt " << (attempt + 1) << ")" << std::endl;
            res = curl_easy_perform(curl);

            if (res != CURLE_OK) {
                std::string errStr = curl_easy_strerror(res);
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                
                attempt++;
                if (attempt <= maxRetries_) {
                    std::cout << "[API Retry] Connection failed (" << errStr << "). Retrying in " << delayMs << "ms..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    delayMs *= 2;
                    continue;
                } else {
                    return "Error: Connection failed or request timed out (" + errStr + ").";
                }
            }

            // Retrieve and log HTTP response code
            long httpResponseCode = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpResponseCode);
            std::cout << "[API Response] HTTP status code: " << httpResponseCode << std::endl;

            if (httpResponseCode == 429 || (httpResponseCode >= 500 && httpResponseCode <= 599)) {
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                
                attempt++;
                if (attempt <= maxRetries_) {
                    std::cout << "[API Retry] HTTP " << httpResponseCode << " (Transient Error). Retrying in " << delayMs << "ms..." << std::endl;
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    delayMs *= 2;
                    continue;
                } else {
                    if (httpResponseCode == 429) {
                        return "Error 429: Too Many Requests. Rate limit exceeded.";
                    } else {
                        return "Error " + std::to_string(httpResponseCode) + ": Transient server error.";
                    }
                }
            }

            if (httpResponseCode == 401) {
                std::cerr << "[API Response] Error 401: Unauthorized. Please check your API key." << std::endl;
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return "Error 401: Unauthorized. Check API key.";
            } else if (httpResponseCode != 200) {
                std::cerr << "[API Response] Warning: Received non-200 HTTP status code: " << httpResponseCode << std::endl;
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return "Error: API returned HTTP status code " + std::to_string(httpResponseCode);
            }

            // Parse format-specific response
            try {
                nlohmann::json responseJson = nlohmann::json::parse(readBuffer);
                std::string aiResponse;
                
                if (format_ == "gemini") {
                    if (responseJson.contains("candidates") && !responseJson["candidates"].empty() &&
                        responseJson["candidates"][0].contains("content") &&
                        responseJson["candidates"][0]["content"].contains("parts") &&
                        !responseJson["candidates"][0]["content"]["parts"].empty() &&
                        responseJson["candidates"][0]["content"]["parts"][0].contains("text")) {
                        
                        aiResponse = responseJson["candidates"][0]["content"]["parts"][0]["text"].get<std::string>();
                    } else {
                        std::cerr << "[API Response] Invalid Gemini response structure: " << readBuffer << std::endl;
                        SaveBookErrorLog(readBuffer, "Invalid Gemini response structure in chat.");
                        
                        bool blocked = false;
                        if (responseJson.contains("promptFeedback") && responseJson["promptFeedback"].contains("blockReason")) {
                            blocked = true;
                        } else if (responseJson.contains("candidates") && !responseJson["candidates"].empty()) {
                            std::string finishReason = responseJson["candidates"][0].value("finishReason", "");
                            if (finishReason == "SAFETY" || finishReason == "RECITATION" || finishReason == "OTHER") {
                                blocked = true;
                            }
                        }
                        
                        if (blocked) {
                            bool isEn = (language == "English" || language == "en" || language == "EN");
                            if (isEn) {
                                aiResponse = "Error: The prompt or narrative was blocked by Google Gemini's safety filters (PROHIBITED_CONTENT). Please try a different choice or select a local/unfiltered AI model in Settings.";
                            } else {
                                std::string englishError = "The prompt or narrative was blocked by Google Gemini's safety filters (PROHIBITED_CONTENT). Please try a different choice or select a local/unfiltered AI model in Settings.";
                                std::string translatePrompt = "You are a professional translator. Translate the following error message into the language: '" + language + "'. Return ONLY the translated text, with no extra formatting, quotes, or conversational phrases:\n\n" + englishError;
                                std::cout << "[API Error Translation] Translating chat error to: " << language << "..." << std::endl;
                                std::string translated = ask(translatePrompt, "English");
                                translated = Trim(translated);
                                if (!translated.empty() && translated.find("Error") == std::string::npos) {
                                    aiResponse = "Error: " + translated;
                                } else {
                                    if (language == "Russian" || language == "ru" || language == "RU") {
                                        aiResponse = "Ошибка: ответ заблокирован фильтрами безопасности Google Gemini (запрещенный контент). Пожалуйста, выберите другое действие или используйте локальную/безцензурную модель ИИ в Настройках.";
                                    } else {
                                        aiResponse = "Error: " + englishError;
                                    }
                                }
                            }
                        } else {
                            aiResponse = "Error: Invalid Gemini response format.";
                        }
                    }
                } else {
                    if (responseJson.contains("choices") && !responseJson["choices"].empty() &&
                        responseJson["choices"][0].contains("message") &&
                        responseJson["choices"][0]["message"].contains("content")) {
                        
                        aiResponse = responseJson["choices"][0]["message"]["content"].get<std::string>();
                    } else {
                        std::cerr << "[API Response] Invalid OpenAI response structure: " << readBuffer << std::endl;
                        aiResponse = "Error: Invalid OpenAI response format.";
                    }
                }
                
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return aiResponse;
            } catch (const std::exception& e) {
                std::cerr << "[API Response] Parsing error: " << e.what() << std::endl;
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                return "Error: Failed to parse API response JSON.";
            }
        }
    }
#endif

    void setRetrySettings(int maxRetries, int retryDelayMs) {
        if (maxRetries >= 0) maxRetries_ = maxRetries;
        if (retryDelayMs > 0) retryDelayMs_ = retryDelayMs;
    }

    void setTimeoutSettings(int connectTimeout, int requestTimeout) {
        if (connectTimeout > 0) connectTimeout_ = connectTimeout;
        if (requestTimeout > 0) requestTimeout_ = requestTimeout;
    }

    int getConnectTimeout() const { return connectTimeout_; }
    int getRequestTimeout() const { return requestTimeout_; }

private:
    std::string baseUrl_;
    std::string modelName_;
    std::string apiKey_;
    std::string apiKeyEnvVar_;
    std::string format_ = "openai";
    std::string systemPrompt_;
    int maxRetries_ = 3;
    int retryDelayMs_ = 1000;
    int connectTimeout_ = 5;
    int requestTimeout_ = 15;

    // Static callback function to write received data into a std::string
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};

// Chapter progression details
struct ChapterData {
    int number = 1;
    std::string title = "";
    std::string description = "";
};

// Global Model State representation
struct GameState {
    int currentChapter = 1;
    std::vector<std::string> chapterSummaries;
    std::vector<ChapterData> chapters;
    bool gameOver = false;
    bool gameWon = false;
    int pendingNextChapter = -1;
    int maxTurnsForce = 10;
    std::vector<ChatMessageData> messages;
    std::vector<std::string> activeChoices;
    std::string lastQuery = "";
    
    // Core game setup prompts
    std::string systemPrompt = "";
    std::string bookWorld = "";
    std::string bookTitle = "BOOK_TO_GAME";
    std::string bookStartPrompt = "";
    std::string gameLanguage = "Russian";
    std::string pacingForcedConclusionPrompt = "CRITICAL TURN LIMIT REACHED: You MUST conclude this chapter on this turn. Transition smoothly and append '<next_chapter>{next_chapter}</next_chapter>' right after the options tags.";
    
    // AI prompt templates
    std::string promptGameWorldHeader = "Game World:";
    std::string promptGameStateHeader = "Current Game State:";
    std::string promptCurrentChapterLabel = "Current Chapter: ";
    std::string promptPreviousChaptersHeader = "Brief history of previous chapters:";
    std::string promptChapterSummaryItem = "- Chapter {chapter}: {summary}\n";
    std::string promptChapterDetailsHeader = "Objectives and description of the current chapter (Chapter {chapter}: {title}):\n{description}\n";
    std::string promptAiRulesHeader = "AI GAMEPLAY RULES:";
    std::string promptAiRuleOptionsFormat = "1. OPTIONS FORMAT: At the end of every response, you MUST offer between 2 and 4 interactive choices wrapped strictly in XML-like tags using the following format: <options><option>First choice</option><option>Second choice</option></options>. Do not output choices in normal text or in any other format.\n";
    std::string promptAiRuleChapterTransition = "2. CHAPTER TRANSITION: When the player successfully resolves the objectives of the current chapter, you MUST append the '<next_chapter>{next_chapter}</next_chapter>' tag right after the closing </options> tag to transition to the next chapter.\n";
    std::string promptAiRuleLanguageEnforcement = "4. LANGUAGE ENFORCEMENT: All generated narration (story) and choices inside <option> tags MUST be strictly written in the target language: '{language}', regardless of the language of the source book lore or chapter plots.\n";
    std::string promptAiFinalChapterWarning = "IMPORTANT: This is the final chapter of the entire book! Resolve all major story conflicts, bring the plot to a grand finale and a satisfying logical conclusion of the entire book. After the </options> tags, you MUST append the transition tag to the epilogue: <next_chapter>{epilogue_chapter}</next_chapter>.\n";
    std::string promptAiEpilogueWriter = "You are a professional epilogue writer. Write a beautiful, brief, and satisfying final conclusion. Do NOT output any choices, options inside <options>, or XML tags. Respond strictly in the target language: '{language}'.";
    std::string promptAiBookGenerator = "You are an AI interactive game book generator. Analyze the following raw book/story text and transform it into a structured adventure game in JSON format. The JSON MUST strictly conform to the following schema:\n{\n    \"title\": \"[A short, engaging title for the quest game]\",\n    \"world\": \"[A detailed description of the game world, lore, rules, and faction details based on the text. 2-3 paragraphs]\",\n    \"plot\": [\n        {\n            \"chapter\": 1,\n            \"title\": \"[Title of Chapter 1]\",\n            \"description\": \"[Detailed description of what the player must achieve in Chapter 1, characters to meet, items to find, and dangers to avoid]\"\n        },\n        {\n            \"chapter\": 2,\n            \"title\": \"[Title of Chapter 2]\",\n            \"description\": \"[Detailed description of Chapter 2 objectives...]\"\n        }\n    ],\n    \"startPrompt\": \"[Introductory prompt that starts the game in Chapter 1, setting the scene, giving initial inventory, and prompting first choices]\"\n}";
    std::string promptAiBookGenLength = "Generate a logical sequential series of chapters mapping out the story arc according to the user's custom length wishes: \"{wishes}\". Follow this number/range of chapters exactly.";
    std::string promptAiBookGenLengthDefault = "Generate between 3 and 5 logical sequential chapters mapping out the story arc.";
    std::string promptAiBookGenGenre = "\n- Genre and atmosphere constraint: \"{wishes}\". You MUST adapt the quest environment, vocabulary, tropes, and thematic elements to match this chosen genre/atmosphere.";
    std::string promptAiBookGenFidelity = "\n- Story progression rules: \"{wishes}\". If the user wants to follow the book's canon, recreate the main events. If they want a free alternative timeline, diverge dynamically and allow total narrative freedom based on the book's starting point.";
    std::string promptAiBookGenCustom = "\n- CRITICAL USER CUSTOM WISHES/PREFERENCES: \"{wishes}\". You MUST absolutely integrate these specific wishes, companions, items, mechanics, or starting conditions into the game's world description and chapter objective specifications.";
    std::string promptAiBookGenRules = "\nCRITICAL RULES:\n1. All narrative text in the generated JSON (title, world, plot descriptions, startPrompt) MUST be written strictly in the language: '{language}'.\n2. Your response must be ONLY valid JSON content. Do not include any explanations, markdown formatting, preamble, or extra text. Output only the pure JSON structure.\n\nStory text:\n{content}";
    std::string promptAiTranslator = "You are a professional translator. Respond ONLY with the requested translation, absolutely no conversational text, formatting, or XML/options tags.";
    std::string promptAiLocalizer = "You are a professional software localizer. Translate all values in the provided JSON to the requested language. Return ONLY valid JSON, with absolutely no markdown, comments, formatting, or options tags.";
    std::string promptAiSummarizer = "You are a professional summarizing assistant. Summarize the text as requested. Do NOT output any options tags, XML, or choice suggestions.";
    std::string promptAiLanguageNormalizer = "You are a standard language name normalizer. Return only the single-word English name of the language, with no other text, options, or tags.";
    std::string promptAiBookBlueprintGen = "You are an AI interactive game book generator. Analyze the following raw book/story text and transform it into a structured adventure game outline in JSON format. The JSON MUST strictly conform to the following schema...";
    std::string promptAiBookBlockHydration = "You are an AI interactive game book generator. Your task is to write detailed descriptions and objectives for a specific block of chapters in the game book outline...";
    std::string promptAiSummaryCompressor = "You are a professional editor. Your task is to combine and compress multiple sequential chapter summaries into a single, cohesive, and extremely concise paragraph (at most 3-4 sentences) that highlights the most important plot events, character progress, and key items discovered. Write strictly in the target language: '{language}'.";
    std::string uiChaptersRangeLabel = "Chapters {range}";
    
    // AI Preprocessing & Parsing settings
    std::string choicesSeparator = "\x1F";
    std::vector<PipelineStep> preprocessingPipeline = {
        {"replace", "\\n", "\n"},
        {"replace", "\"narrative\":", ""},
        {"replace", "\"narrative\" :", ""},
        {"replace", "\"options\":", "<separator>"},
        {"replace", "\"choices\":", "<separator>"},
        {"replace", "\"options\" :", "<separator>"},
        {"replace", "\"choices\" :", "<separator>"},
        {"remove_chars", "{}[]", ""}
    };
};

// --- Helper Functions ---

inline int ParseChapterCount(const std::string& wishes) {
    if (wishes.empty()) return 5;
    int maxNum = 0;
    std::string currentNum = "";
    for (char c : wishes) {
        if (isdigit(c)) {
            currentNum += c;
        } else {
            if (!currentNum.empty()) {
                int val = std::stoi(currentNum);
                if (val > maxNum) maxNum = val;
                currentNum = "";
            }
        }
    }
    if (!currentNum.empty()) {
        int val = std::stoi(currentNum);
        if (val > maxNum) maxNum = val;
    }
    if (maxNum <= 0) return 5;
    return maxNum;
}

// UTF-8 Helpers
inline std::vector<uint32_t> DecodeUTF8(const std::string& text) {
    std::vector<uint32_t> codePoints;
    int i = 0;
    int len = text.length();
    while (i < len) {
        unsigned char c = text[i];
        uint32_t cp = 0;
        int bytes = 0;
        if (c < 0x80) { cp = c; bytes = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; bytes = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; bytes = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; bytes = 4; }
        else { i++; continue; }
        
        if (i + bytes > len) break;
        for (int j = 1; j < bytes; j++) {
            cp = (cp << 6) | ((unsigned char)text[i + j] & 0x3F);
        }
        codePoints.push_back(cp);
        i += bytes;
    }
    return codePoints;
}

inline bool IsRTLCodePoint(uint32_t cp) {
    return ((cp >= 0x0590 && cp <= 0x05FF) || // Hebrew range
            (cp >= 0x0600 && cp <= 0x06FF) || // Arabic range
            (cp >= 0x0750 && cp <= 0x077F) || // Arabic Supplement
            (cp >= 0x08A0 && cp <= 0x08FF) || // Arabic Extended-A
            (cp >= 0xFB50 && cp <= 0xFDFF) || // Arabic Presentation Forms-A
            (cp >= 0xFE70 && cp <= 0xFEFF));  // Arabic Presentation Forms-B
}

inline bool HasRTLCharacter(const std::string& text) {
    std::vector<uint32_t> codePoints = DecodeUTF8(text);
    for (uint32_t cp : codePoints) {
        if (IsRTLCodePoint(cp)) {
            return true;
        }
    }
    return false;
}

inline bool HasCyrillic(const std::string& text) {
    std::vector<uint32_t> codePoints = DecodeUTF8(text);
    for (uint32_t cp : codePoints) {
        if (cp >= 0x0400 && cp <= 0x04FF) {
            return true;
        }
    }
    return false;
}

// Safe Backspace POP UTF-8 Helper
inline void PopUTF8Character(std::string& s) {
    if (s.empty()) return;
    int last = s.length() - 1;
    while (last >= 0) {
        unsigned char c = s[last];
        if ((c & 0xC0) != 0x80) { // Not a continuation byte
            s.erase(last);
            break;
        }
        last--;
    }
}

// String utility to trim whitespace and newlines from both ends
inline std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

inline std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

inline std::string CleanTextForFont(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    
    size_t i = 0;
    while (i < text.size()) {
        unsigned char c = text[i];
        int len = 1;
        uint32_t codePoint = 0;
        
        if (c < 0x80) {
            len = 1;
            codePoint = c;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
            if (i + 1 < text.size()) {
                codePoint = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F);
            }
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            if (i + 2 < text.size()) {
                codePoint = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F);
            }
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            if (i + 3 < text.size()) {
                codePoint = ((c & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) | ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F);
            }
        } else {
            len = 1;
            codePoint = c;
        }
        
        if (i + len > text.size()) {
            break;
        }
        
        bool keep = true;
        // 1. Plane 1 and beyond (code points >= 0x10000) are mostly emojis, transport, map, CJK symbols
        if (codePoint >= 0x10000) {
            keep = false;
        }
        // 2. Miscellaneous Symbols & Dingbats: 0x2600 to 0x27BF (includes high voltage, swords, stars, cross marks)
        else if (codePoint >= 0x2600 && codePoint <= 0x27BF) {
            keep = false;
        }
        // 3. Miscellaneous Technical: 0x2300 to 0x23FF (includes return/confirm symbols)
        else if (codePoint >= 0x2300 && codePoint <= 0x23FF) {
            keep = false;
        }
        // 4. Variation Selectors: 0xFE00 to 0xFE0F
        else if (codePoint >= 0xFE00 && codePoint <= 0xFE0F) {
            keep = false;
        }
        
        if (keep) {
            result.append(text.substr(i, len));
        }
        i += len;
    }
    
    // Trim leading/trailing spaces and leftover punctuation (e.g. dots, dashes, colons, spaces)
    size_t start = 0;
    while (start < result.size()) {
        unsigned char c = result[start];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '-' || c == '*' || c == '.' || c == ':' || c == ',') {
            start++;
        } else {
            break;
        }
    }
    
    size_t end = result.size();
    while (end > start) {
        unsigned char c = result[end - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            end--;
        } else {
            break;
        }
    }
    
    if (start >= end) {
        return "";
    }
    return result.substr(start, end - start);
}

inline std::string EraseAllSubstrings(std::string str, const std::string& sub) {
    size_t pos;
    while ((pos = str.find(sub)) != std::string::npos) {
        str.erase(pos, sub.length());
    }
    return str;
}

inline void PreprocessRawAiResponse(std::string& aiResponse, const std::string& choicesSeparator = "\x1F", const std::vector<PipelineStep>& pipeline = {}) {
    // If a custom pipeline is passed, execute it step-by-step
    if (!pipeline.empty()) {
        for (const auto& step : pipeline) {
            if (step.action == "replace") {
                std::string target = step.target;
                std::string replacement = step.replacement;
                
                // Replace marker with actual runtime separator if requested
                size_t sepMarker;
                while ((sepMarker = replacement.find("<choicesSeparator>")) != std::string::npos) {
                    replacement.replace(sepMarker, 18, choicesSeparator);
                }
                while ((sepMarker = replacement.find("choicesSeparator")) != std::string::npos) {
                    replacement.replace(sepMarker, 16, choicesSeparator);
                }
                while ((sepMarker = replacement.find("<separator>")) != std::string::npos) {
                    replacement.replace(sepMarker, 11, choicesSeparator);
                }
                
                if (!target.empty()) {
                    size_t pos = 0;
                    while ((pos = aiResponse.find(target, pos)) != std::string::npos) {
                        aiResponse.replace(pos, target.length(), replacement);
                        pos += replacement.length();
                    }
                }
            } else if (step.action == "remove_chars") {
                // Remove all occurrences of any characters specified in target
                aiResponse.erase(
                    std::remove_if(aiResponse.begin(), aiResponse.end(), [&](char c) {
                        return step.target.find(c) != std::string::npos;
                    }),
                    aiResponse.end()
                );
            } else if (step.action == "trim") {
                aiResponse = Trim(aiResponse);
            }
        }
    } else {
        // Fallback/default hardcoded preprocessing if no pipeline is configured
        // 1. Normalize escape sequences
        size_t nPos = 0;
        while ((nPos = aiResponse.find("\\n", nPos)) != std::string::npos) {
            aiResponse.replace(nPos, 2, "\n");
            nPos += 1;
        }
        
        // 2. Preprocess JSON fragments if we detect curly brackets or JSON keys
        if (aiResponse.find("{") != std::string::npos || aiResponse.find("\"narrative\"") != std::string::npos) {
            // Clean up typical JSON properties
            std::vector<std::pair<std::string, std::string>> defaults = {
                {"\"narrative\":", ""},
                {"\"narrative\" :", ""},
                {"\"options\":", choicesSeparator},
                {"\"choices\":", choicesSeparator},
                {"\"options\" :", choicesSeparator},
                {"\"choices\" :", choicesSeparator}
            };
            for (const auto& pair : defaults) {
                size_t pos = 0;
                while ((pos = aiResponse.find(pair.first, pos)) != std::string::npos) {
                    aiResponse.replace(pos, pair.first.length(), pair.second);
                    pos += pair.second.length();
                }
            }
            
            // Remove brackets
            std::string removeChars = "{}[]";
            aiResponse.erase(
                std::remove_if(aiResponse.begin(), aiResponse.end(), [&](char c) {
                    return removeChars.find(c) != std::string::npos;
                }),
                aiResponse.end()
            );
        }
    }
}

inline std::vector<std::string> ExtractAndStripOptions(std::string& aiResponse, const std::string& choicesSeparator = "\x1F", const std::vector<PipelineStep>& pipeline = {}) {
    // 1. Run PreprocessRawAiResponse to clean up escape sequences and JSON-like malformations
    PreprocessRawAiResponse(aiResponse, choicesSeparator, pipeline);

    std::vector<std::string> options;
    
    // 0. Universal choices split separator
    size_t pipePos = aiResponse.find(choicesSeparator);
    size_t separatorLen = choicesSeparator.length();
    if (pipePos == std::string::npos) {
        pipePos = aiResponse.find("\x1F");
        separatorLen = 1;
    }
    if (pipePos == std::string::npos) {
        pipePos = aiResponse.find("[choices_split]");
        separatorLen = 15;
    }
    if (pipePos == std::string::npos) {
        pipePos = aiResponse.find("[choices]");
        separatorLen = 9;
    }
    if (pipePos == std::string::npos) {
        pipePos = aiResponse.find("|");
        separatorLen = 1;
    }
    
    if (pipePos != std::string::npos) {
        std::string narrativePart = aiResponse.substr(0, pipePos);
        std::string optionsPart = aiResponse.substr(pipePos + separatorLen);
        
        // Remove all duplicate/subsequent separators to keep only the first one
        narrativePart = EraseAllSubstrings(narrativePart, choicesSeparator);
        narrativePart = EraseAllSubstrings(narrativePart, "\x1F");
        narrativePart = EraseAllSubstrings(narrativePart, "[choices_split]");
        narrativePart = EraseAllSubstrings(narrativePart, "[choices]");
        narrativePart.erase(std::remove(narrativePart.begin(), narrativePart.end(), '|'), narrativePart.end());
        
        optionsPart = EraseAllSubstrings(optionsPart, choicesSeparator);
        optionsPart = EraseAllSubstrings(optionsPart, "\x1F");
        optionsPart = EraseAllSubstrings(optionsPart, "[choices_split]");
        optionsPart = EraseAllSubstrings(optionsPart, "[choices]");
        optionsPart.erase(std::remove(optionsPart.begin(), optionsPart.end(), '|'), optionsPart.end());
        
        std::vector<std::string> tempOptions;
        std::stringstream ss(optionsPart);
        std::string line;
        while (std::getline(ss, line)) {
            std::string optLine = Trim(line);
            if (optLine.empty()) continue;
            
            // Trim quotes, commas, and formatting characters from option lines (e.g. from JSON arrays)
            bool cleaned = true;
            while (cleaned) {
                cleaned = false;
                std::string trimmed = Trim(optLine);
                if (trimmed.empty()) break;
                
                // Remove trailing comma
                if (trimmed.back() == ',') {
                    trimmed.pop_back();
                    cleaned = true;
                }
                
                // Remove surrounding quotes
                trimmed = Trim(trimmed);
                if (trimmed.length() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
                    trimmed = trimmed.substr(1, trimmed.length() - 2);
                    cleaned = true;
                }
                
                if (cleaned) {
                    optLine = trimmed;
                }
            }
            optLine = Trim(optLine);
            if (optLine.empty()) continue;
            
            // Strip leading bullet markers
            while (!optLine.empty() && (optLine[0] == '-' || optLine[0] == '*' || optLine[0] == '+' || optLine[0] == ' ')) {
                optLine = optLine.substr(1);
                optLine = Trim(optLine);
            }
            
            // Strip alphabetical or numerical prefixes like 'A) ', '1. ', Cyrillic 'А) '
            if (!optLine.empty()) {
                // Check if it starts with digit followed by . or )
                size_t digitLen = 0;
                while (digitLen < optLine.length() && std::isdigit((unsigned char)optLine[digitLen])) {
                    digitLen++;
                }
                if (digitLen > 0 && digitLen < optLine.length()) {
                    char delim = optLine[digitLen];
                    if (delim == '.' || delim == ')') {
                        optLine = optLine.substr(digitLen + 1);
                        optLine = Trim(optLine);
                    }
                }
                
                // Check if it starts with standard English letter followed by . or )
                if (optLine.length() >= 2) {
                    char c = optLine[0];
                    char delim = optLine[1];
                    if (((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) && (delim == '.' || delim == ')')) {
                        optLine = optLine.substr(2);
                        optLine = Trim(optLine);
                    }
                }
            }
            
            if (!optLine.empty()) {
                tempOptions.push_back(CleanTextForFont(optLine));
            }
        }
        
        // Always split and strip, without safety threshold guard
        options = tempOptions;
        aiResponse = Trim(narrativePart);
        
        // Clean trailing/leading newlines, spaces, and leftover characters
        while (!aiResponse.empty() && (aiResponse.back() == '\n' || aiResponse.back() == '\r' || aiResponse.back() == ' ' || aiResponse.back() == '<' || aiResponse.back() == '[' || aiResponse.back() == '`')) {
            aiResponse.pop_back();
        }
        while (!aiResponse.empty() && (aiResponse.front() == '\n' || aiResponse.front() == '\r' || aiResponse.front() == ' ')) {
            aiResponse.erase(aiResponse.begin());
        }
        
        return options;
    }

    std::string lowerResponse = ToLower(aiResponse);
    
    // 1. Direct search for <option>... </option> tags (ignoring outer <options> entirely!)
    size_t searchPos = 0;
    size_t firstOptionPos = std::string::npos;
    bool isXmlFormat = false;
    while (true) {
        size_t optStart = lowerResponse.find("<option", searchPos);
        if (optStart == std::string::npos) break;
        
        // Skip if this is actually the plural "<options" container tag
        if (optStart + 7 < lowerResponse.length() && (lowerResponse[optStart + 7] == 's' || lowerResponse[optStart + 7] == 'S')) {
            searchPos = optStart + 7;
            continue;
        }
        
        size_t tagEnd = lowerResponse.find(">", optStart);
        if (tagEnd == std::string::npos) break;
        
        size_t optEnd = lowerResponse.find("</option>", tagEnd);
        if (optEnd != std::string::npos) {
            std::string optText = aiResponse.substr(tagEnd + 1, optEnd - (tagEnd + 1));
            options.push_back(CleanTextForFont(Trim(optText)));
            searchPos = optEnd + 9;
            if (firstOptionPos == std::string::npos) {
                firstOptionPos = optStart;
                isXmlFormat = true;
            }
        } else {
            searchPos = tagEnd + 1;
        }
    }
    
    // 2. Square bracket [option]...[/option] fallback
    if (options.empty()) {
        searchPos = 0;
        while (true) {
            size_t optStart = lowerResponse.find("[option", searchPos);
            if (optStart == std::string::npos) break;
            
            // Skip if this is actually "[options"
            if (optStart + 7 < lowerResponse.length() && (lowerResponse[optStart + 7] == 's' || lowerResponse[optStart + 7] == 'S')) {
                searchPos = optStart + 7;
                continue;
            }
            
            size_t tagEnd = lowerResponse.find("]", optStart);
            if (tagEnd == std::string::npos) break;
            
            size_t optEnd = lowerResponse.find("[/option]", tagEnd);
            if (optEnd != std::string::npos) {
                std::string optText = aiResponse.substr(tagEnd + 1, optEnd - (tagEnd + 1));
                options.push_back(CleanTextForFont(Trim(optText)));
                searchPos = optEnd + 9;
                if (firstOptionPos == std::string::npos) {
                    firstOptionPos = optStart;
                    isXmlFormat = false;
                }
            } else {
                searchPos = tagEnd + 1;
            }
        }
    }
    
    // 2b. Plain lines inside [options] or <options> block fallback
    if (options.empty()) {
        std::string blockStartTag = "";
        std::string blockEndTag = "";
        size_t startPos = lowerResponse.find("[options]");
        if (startPos != std::string::npos) {
            blockStartTag = "[options]";
            blockEndTag = "[/options]";
        } else {
            startPos = lowerResponse.find("<options>");
            if (startPos != std::string::npos) {
                blockStartTag = "<options>";
                blockEndTag = "</options>";
            }
        }
        
        if (startPos != std::string::npos) {
            size_t endPos = lowerResponse.find(blockEndTag, startPos);
            if (endPos != std::string::npos && endPos > startPos) {
                std::string blockContent = aiResponse.substr(startPos + blockStartTag.length(), endPos - (startPos + blockStartTag.length()));
                
                std::vector<std::string> tempOptions;
                std::stringstream ss(blockContent);
                std::string line;
                while (std::getline(ss, line)) {
                    line = Trim(line);
                    
                    // Clean standard XML/bracket tag fragments inside line
                    size_t firstBr = line.find("[");
                    while (firstBr != std::string::npos) {
                        size_t lastBr = line.find("]", firstBr);
                        if (lastBr != std::string::npos) {
                            line.erase(firstBr, lastBr - firstBr + 1);
                        } else {
                            line.erase(firstBr);
                            break;
                        }
                        firstBr = line.find("[");
                    }
                    size_t firstLt = line.find("<");
                    while (firstLt != std::string::npos) {
                        size_t lastGt = line.find(">", firstLt);
                        if (lastGt != std::string::npos) {
                            line.erase(firstLt, lastGt - firstLt + 1);
                        } else {
                            line.erase(firstLt);
                            break;
                        }
                        firstLt = line.find("<");
                    }
                    
                    line = Trim(line);
                    if (!line.empty()) {
                        tempOptions.push_back(CleanTextForFont(line));
                    }
                }
                
                if (tempOptions.size() >= 2) {
                    options = tempOptions;
                    aiResponse = aiResponse.substr(0, startPos);
                    aiResponse = Trim(aiResponse);
                }
            }
        }
    }
    
    // 3. Narrative stripping logic for tags (dynamic walkback from the first option tag!)
    size_t eraseStart = std::string::npos;
    if (firstOptionPos != std::string::npos) {
        eraseStart = firstOptionPos;
        
        // Walk back up to 120 characters to find the start of any options block wrapper (<options>, [options], <E Options>, etc.)
        size_t limit = (firstOptionPos > 120) ? firstOptionPos - 120 : 0;
        for (size_t i = firstOptionPos; i > limit; i--) {
            char c = aiResponse[i - 1];
            if (isXmlFormat && c == '<') {
                eraseStart = i - 1;
                break;
            } else if (!isXmlFormat && c == '[') {
                eraseStart = i - 1;
                break;
            }
        }
        
        // Also check if there's a prefix like "decide:" just before it
        std::string beforeText = ToLower(aiResponse.substr(limit, eraseStart - limit));
        size_t decPos = beforeText.rfind("decide:");
        if (decPos != std::string::npos) {
            eraseStart = limit + decPos;
        }
    } else {
        // Static lookup fallback for safety if option tags are not found/malformed
        std::vector<std::string> tagsToLook = {
            "<options", "[options", "<option", "[option", 
            "<e options", "<e option", "<opt", "[opt", "decide:"
        };
        for (const auto& tag : tagsToLook) {
            size_t pos = lowerResponse.find(tag);
            if (pos != std::string::npos) {
                if (eraseStart == std::string::npos || pos < eraseStart) {
                    eraseStart = pos;
                }
            }
        }
    }
    
    if (eraseStart != std::string::npos) {
        aiResponse.erase(eraseStart);
    }
    
    // 4. Bulleted list fallback (if no tags are found at all!)
    if (options.empty()) {
        std::vector<std::string> lines;
        std::stringstream ss(aiResponse);
        std::string line;
        while (std::getline(ss, line)) {
            lines.push_back(line);
        }
        
        std::vector<std::string> listOptions;
        int listLinesFound = 0;
        for (int i = (int)lines.size() - 1; i >= 0; i--) {
            std::string trimmedLine = Trim(lines[i]);
            if (trimmedLine.empty()) continue;
            
            bool isOption = false;
            size_t markerEnd = 0;
            
            if (trimmedLine.rfind("- ", 0) == 0 || trimmedLine.rfind("* ", 0) == 0) {
                isOption = true;
                markerEnd = 2;
            }
            
            if (isOption) {
                std::string optText = trimmedLine.substr(markerEnd);
                listOptions.push_back(CleanTextForFont(Trim(optText)));
                listLinesFound++;
            } else {
                if (listLinesFound > 0) {
                    break;
                }
            }
        }
        
        if (!listOptions.empty()) {
            std::reverse(listOptions.begin(), listOptions.end());
            options = listOptions;
            
            // Clean the response by erasing the list lines
            std::string cleanedResponse = "";
            int lastKeepLine = (int)lines.size() - 1 - listLinesFound;
            for (int i = 0; i <= lastKeepLine; i++) {
                cleanedResponse += lines[i] + "\n";
            }
            aiResponse = cleanedResponse;
        }
    }
    
    // 5. Advanced alphabetical/digit marker list and inline fallback
    if (options.empty()) {
        std::string textToParse = aiResponse;
        
        // Lambda to check if a substring is a valid A) / A. / 1) / 1. marker at index idx
        auto isMarker = [](const std::string& txt, size_t idx, char& outLetter, size_t& outMarkerLen) -> bool {
            if (idx + 2 > txt.length()) return false;
            
            char c = txt[idx];
            bool isEng = ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
            bool isCyrStart = ((unsigned char)c == 0xD0 || (unsigned char)c == 0xD1);
            
            if (isEng) {
                char delim = txt[idx + 1];
                if (delim == ')' || delim == '.') {
                    outLetter = std::toupper((unsigned char)c);
                    outMarkerLen = 2;
                    if (idx + 2 < txt.length() && std::isspace((unsigned char)txt[idx + 2])) {
                        outMarkerLen = 3;
                    }
                    return true;
                }
            } else if (isCyrStart && idx + 2 < txt.length()) {
                unsigned char c2 = txt[idx + 1];
                bool isCyrLetter = false;
                char mappedLetter = '\0';
                
                if ((unsigned char)c == 0xD0) {
                    if (c2 >= 0x90 && c2 <= 0x95) { // А-Е
                        isCyrLetter = true;
                        mappedLetter = 'A' + (c2 - 0x90);
                    } else if (c2 >= 0xB0 && c2 <= 0xB5) { // а-е
                        isCyrLetter = true;
                        mappedLetter = 'A' + (c2 - 0xB0);
                    }
                }
                
                if (isCyrLetter) {
                    char delim = txt[idx + 2];
                    if (delim == ')' || delim == '.') {
                        outLetter = mappedLetter;
                        outMarkerLen = 3;
                        if (idx + 3 < txt.length() && std::isspace((unsigned char)txt[idx + 3])) {
                            outMarkerLen = 4;
                        }
                        return true;
                    }
                }
            }
            
            if (std::isdigit((unsigned char)c)) {
                size_t delimIdx = idx + 1;
                while (delimIdx < txt.length() && std::isdigit((unsigned char)txt[delimIdx])) {
                    delimIdx++;
                }
                if (delimIdx < txt.length()) {
                    char delim = txt[delimIdx];
                    if (delim == ')' || delim == '.') {
                        outLetter = '0' + (txt[idx] - '0');
                        outMarkerLen = delimIdx - idx + 1;
                        if (delimIdx + 1 < txt.length() && std::isspace((unsigned char)txt[delimIdx + 1])) {
                            outMarkerLen++;
                        }
                        return true;
                    }
                }
            }
            
            return false;
        };
        
        struct MarkerInfo {
            size_t startPos;
            size_t len;
            char id;
        };
        std::vector<MarkerInfo> foundMarkers;
        size_t scanIdx = 0;
        while (scanIdx < textToParse.length()) {
            char markerId = '\0';
            size_t markerLen = 0;
            if (isMarker(textToParse, scanIdx, markerId, markerLen)) {
                bool validBoundary = (scanIdx == 0 || std::isspace((unsigned char)textToParse[scanIdx - 1]) || textToParse[scanIdx - 1] == '.' || textToParse[scanIdx - 1] == '!' || textToParse[scanIdx - 1] == '?' || textToParse[scanIdx - 1] == ')' || textToParse[scanIdx - 1] == '\"');
                if (validBoundary) {
                    foundMarkers.push_back({scanIdx, markerLen, markerId});
                    scanIdx += markerLen;
                    continue;
                }
            }
            scanIdx++;
        }
        
        if (foundMarkers.size() >= 2) {
            std::vector<std::string> tempOptions;
            for (size_t i = 0; i < foundMarkers.size(); i++) {
                size_t start = foundMarkers[i].startPos + foundMarkers[i].len;
                size_t end = (i + 1 < foundMarkers.size()) ? foundMarkers[i + 1].startPos : textToParse.length();
                
                std::string optionText = textToParse.substr(start, end - start);
                optionText = Trim(optionText);
                if (optionText.length() > 0 && optionText.back() == '.') {
                    optionText.pop_back();
                }
                optionText = Trim(optionText);
                
                if (!optionText.empty()) {
                    tempOptions.push_back(CleanTextForFont(optionText));
                }
            }
            
            if (tempOptions.size() >= 2) {
                options = tempOptions;
                aiResponse = aiResponse.substr(0, foundMarkers[0].startPos);
                aiResponse = Trim(aiResponse);
            }
        }
    }
    
    // Also clean up any leftover markdown code blocks if the AI wrapped options in them
    size_t codeBlockPos = aiResponse.rfind("```");
    if (codeBlockPos != std::string::npos) {
        if (codeBlockPos >= aiResponse.length() - 10) {
            aiResponse.erase(codeBlockPos);
        }
    }
    
    // Strip trailing/leading newlines, spaces, and leftover characters
    while (!aiResponse.empty() && (aiResponse.back() == '\n' || aiResponse.back() == '\r' || aiResponse.back() == ' ' || aiResponse.back() == '<' || aiResponse.back() == '[' || aiResponse.back() == '`')) {
        aiResponse.pop_back();
    }
    while (!aiResponse.empty() && (aiResponse.front() == '\n' || aiResponse.front() == '\r' || aiResponse.front() == ' ')) {
        aiResponse.erase(aiResponse.begin());
    }
    
    return options;
}

inline std::string ReconstructPerfectAiResponse(const std::string& strippedResponse, const std::vector<std::string>& options, const std::string& choicesSeparator = "\x1F") {
    std::string result = strippedResponse;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) {
        result.pop_back();
    }
    if (!options.empty() && options[0] != "Продолжить историю" && options[0] != "Continue story" && options[0] != "Повторить запрос" && options[0] != "Repeat request") {
        result += "\n\n" + choicesSeparator + "\n";
        for (const auto& opt : options) {
            result += "- " + opt + "\n";
        }
    }
    return result;
}

// Serialization
inline void SaveGame(const GameState& state, const std::string& filename = "save.json") {
    nlohmann::json j;
    j["currentChapter"] = state.currentChapter;
    j["chapterSummaries"] = state.chapterSummaries;
    j["gameOver"] = state.gameOver;
    j["gameWon"] = state.gameWon;
    j["pendingNextChapter"] = state.pendingNextChapter;
    j["lastQuery"] = state.lastQuery;
    
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& msg : state.messages) {
        nlohmann::json m;
        m["sender"] = msg.sender;
        m["text"] = msg.text;
        msgs.push_back(m);
    }
    j["messages"] = msgs;
    
    nlohmann::json choices = nlohmann::json::array();
    for (const auto& opt : state.activeChoices) {
        choices.push_back(opt);
    }
    j["activeChoices"] = choices;
    
    std::ofstream file(filename);
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        std::cout << "[SaveGame] Game saved successfully to " << filename << std::endl;
    } else {
        std::cerr << "[SaveGame] Failed to open " << filename << " for writing" << std::endl;
    }

    if (filename == "save.json") {
        std::ifstream parentSettings("../settings.json");
        if (parentSettings.is_open()) {
            parentSettings.close();
            std::ofstream parentFile("../save.json");
            if (parentFile.is_open()) {
                parentFile << j.dump(4);
                parentFile.close();
                std::cout << "[SaveGame] Successfully saved synchronized save.json copy to parent directory." << std::endl;
            }
        }
    }
}

inline bool LoadGame(GameState& state, const std::string& filename = "save.json") {
    std::ifstream file(filename);
    if (!file.is_open()) {
        if (filename == "save.json") {
            file.open("../" + filename);
            if (!file.is_open()) {
                return false;
            }
        } else {
            return false;
        }
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        if (j.contains("currentChapter") && j["currentChapter"].is_number()) {
            state.currentChapter = j["currentChapter"].get<int>();
        }
        if (j.contains("chapterSummaries") && j["chapterSummaries"].is_array()) {
            state.chapterSummaries.clear();
            for (const auto& summary : j["chapterSummaries"]) {
                if (summary.is_string()) {
                    state.chapterSummaries.push_back(summary.get<std::string>());
                }
            }
        }
        if (j.contains("gameOver") && j["gameOver"].is_boolean()) {
            state.gameOver = j["gameOver"].get<bool>();
        }
        if (j.contains("gameWon") && j["gameWon"].is_boolean()) {
            state.gameWon = j["gameWon"].get<bool>();
        } else {
            state.gameWon = false;
        }
        
        if (j.contains("pendingNextChapter") && j["pendingNextChapter"].is_number()) {
            state.pendingNextChapter = j["pendingNextChapter"].get<int>();
        } else {
            state.pendingNextChapter = -1;
        }
        
        if (j.contains("lastQuery") && j["lastQuery"].is_string()) {
            state.lastQuery = j["lastQuery"].get<std::string>();
        } else {
            state.lastQuery = "";
        }
        
        state.messages.clear();
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& m : j["messages"]) {
                if (m.contains("sender") && m["sender"].is_string() &&
                    m.contains("text") && m["text"].is_string()) {
                    ChatMessageData msg;
                    msg.sender = m["sender"].get<std::string>();
                    msg.text = m["text"].get<std::string>();
                    state.messages.push_back(msg);
                }
            }
        }
        
        state.activeChoices.clear();
        if (j.contains("activeChoices") && j["activeChoices"].is_array()) {
            for (const auto& optVal : j["activeChoices"]) {
                if (optVal.is_string()) {
                    state.activeChoices.push_back(optVal.get<std::string>());
                }
            }
        }
        
        std::cout << "[LoadGame] Game loaded successfully from " << filename << ". Current Chapter: " << state.currentChapter << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[LoadGame] Failed to parse " << filename << ": " << e.what() << std::endl;
        return false;
    }
}

// Forward declaration of GetUiText defined in main.cpp
std::string GetUiText(const std::string& key);

inline std::string FormatPacingString(std::string str, int turns, int minVal, int maxVal) {
    size_t pos;
    while ((pos = str.find("{turns}")) != std::string::npos) {
        str.replace(pos, 7, std::to_string(turns));
    }
    while ((pos = str.find("{min}")) != std::string::npos) {
        str.replace(pos, 5, std::to_string(minVal));
    }
    while ((pos = str.find("{max}")) != std::string::npos) {
        str.replace(pos, 5, std::to_string(maxVal));
    }
    return str;
}

inline std::string FormatRetryString(std::string str, int attempt, int maxVal) {
    size_t pos;
    while ((pos = str.find("{attempt}")) != std::string::npos) {
        str.replace(pos, 9, std::to_string(attempt));
    }
    while ((pos = str.find("{max}")) != std::string::npos) {
        str.replace(pos, 5, std::to_string(maxVal));
    }
    return str;
}

inline bool ContainsErrorCaseInsensitive(const std::string& str) {
    if (str.empty()) return false;
    std::string lower = ToLower(str);
    return lower.find("error") != std::string::npos;
}

inline void UpdateSystemPrompt(GameState& state, AskAiExternal* aiClient) {
    std::string combinedPrompt = state.systemPrompt;
    
    if (!state.bookWorld.empty()) {
        combinedPrompt += "\n\n" + state.promptGameWorldHeader + "\n" + state.bookWorld;
    }
    
    combinedPrompt += "\n\n" + state.promptGameStateHeader + "\n";
    combinedPrompt += state.promptCurrentChapterLabel + std::to_string(state.currentChapter) + "\n";
    
    if (!state.chapterSummaries.empty()) {
        combinedPrompt += "\n" + state.promptPreviousChaptersHeader + "\n";
        for (size_t i = 0; i < state.chapterSummaries.size(); i++) {
            std::string raw = state.chapterSummaries[i];
            std::string label;
            std::string text;
            size_t sep = raw.find("::");
            if (sep != std::string::npos) {
                label = raw.substr(0, sep);
                text = raw.substr(sep + 2);
            } else {
                label = std::to_string(i + 1);
                text = raw;
            }
            
            std::string item = state.promptChapterSummaryItem;
            if (label.find('-') != std::string::npos) {
                std::string rangeLabel = state.uiChaptersRangeLabel;
                size_t rPos = rangeLabel.find("{range}");
                if (rPos != std::string::npos) {
                    rangeLabel.replace(rPos, 7, label);
                } else {
                    rangeLabel = "Chapters " + label;
                }
                size_t chPos = item.find("{chapter}");
                if (chPos != std::string::npos) item.replace(chPos, 9, rangeLabel);
            } else {
                size_t chPos = item.find("{chapter}");
                if (chPos != std::string::npos) item.replace(chPos, 9, label);
            }
            
            size_t sumPos = item.find("{summary}");
            if (sumPos != std::string::npos) item.replace(sumPos, 9, text);
            combinedPrompt += item;
        }
    }
    
    std::string activeChapterDesc = "";
    std::string activeChapterTitle = "";
    for (const auto& ch : state.chapters) {
        if (ch.number == state.currentChapter) {
            activeChapterDesc = ch.description;
            activeChapterTitle = ch.title;
            break;
        }
    }
    
    if (!activeChapterDesc.empty()) {
        std::string details = state.promptChapterDetailsHeader;
        size_t chPos = details.find("{chapter}");
        if (chPos != std::string::npos) details.replace(chPos, 9, std::to_string(state.currentChapter));
        size_t tPos = details.find("{title}");
        if (tPos != std::string::npos) details.replace(tPos, 7, activeChapterTitle);
        size_t dPos = details.find("{description}");
        if (dPos != std::string::npos) details.replace(dPos, 13, activeChapterDesc);
        combinedPrompt += details;
    }
    
    combinedPrompt += "\n\n" + state.promptAiRulesHeader + "\n";
    combinedPrompt += state.promptAiRuleOptionsFormat;
    
    std::string rule2 = state.promptAiRuleChapterTransition;
    size_t ncPos = rule2.find("{next_chapter}");
    if (ncPos != std::string::npos) {
        rule2.replace(ncPos, 14, "НОМЕР_СЛЕДУЮЩЕЙ_ГЛАВЫ");
    }
    combinedPrompt += rule2;
    
    combinedPrompt += "3. DEATH RULE: If the player makes a fatal mistake or chooses a deadly option, you MUST end the adventure. Describe their death and append '<player_dead/>' at the very end. DO NOT output any options or choices inside `<options>` when the player dies. Example: 'You died. <player_dead/>'\n";
    
    std::string rule4 = state.promptAiRuleLanguageEnforcement;
    size_t langPos = rule4.find("{language}");
    if (langPos != std::string::npos) {
        rule4.replace(langPos, 10, state.gameLanguage);
    }
    combinedPrompt += rule4;
    
    int maxChapterNum = 0;
    for (const auto& ch : state.chapters) {
        if (ch.number > maxChapterNum) {
            maxChapterNum = ch.number;
        }
    }
    if (maxChapterNum > 0 && state.currentChapter == maxChapterNum) {
        std::string finalWarn = state.promptAiFinalChapterWarning;
        size_t epPos = finalWarn.find("{epilogue_chapter}");
        if (epPos != std::string::npos) {
            finalWarn.replace(epPos, 18, std::to_string(maxChapterNum + 1));
        }
        combinedPrompt += "\n\n" + finalWarn;
    }
    
    int minTurns = 6;
    int maxTurns = 8;
    
    // Parse minTurns and maxTurns from state.systemPrompt if possible
    size_t roughlyPos = state.systemPrompt.find("roughly ");
    if (roughlyPos != std::string::npos) {
        size_t toPos = state.systemPrompt.find(" to ", roughlyPos);
        if (toPos != std::string::npos) {
            std::string minStr = "";
            for (size_t i = roughlyPos + 8; i < toPos; ++i) {
                if (isdigit(state.systemPrompt[i])) minStr += state.systemPrompt[i];
            }
            std::string maxStr = "";
            for (size_t i = toPos + 4; i < state.systemPrompt.length(); ++i) {
                if (isdigit(state.systemPrompt[i])) {
                    maxStr += state.systemPrompt[i];
                } else if (!maxStr.empty()) {
                    break;
                }
            }
            if (!minStr.empty() && !maxStr.empty()) {
                minTurns = std::stoi(minStr);
                maxTurns = std::stoi(maxStr);
            }
        }
    }
    
    int userTurnCount = 0;
    for (const auto& msg : state.messages) {
        if (msg.sender == "User") {
            userTurnCount++;
        }
    }
    
    int transitionTurn = minTurns - 1;
    if (transitionTurn < 1) transitionTurn = 1;
    
    std::string pacingTitle = GetUiText("pacing_critical_title");
    std::string pacingStatus = FormatPacingString(GetUiText("pacing_turn_status"), userTurnCount, minTurns, maxTurns);
    
    combinedPrompt += "\n\n" + pacingTitle + "\n" + pacingStatus + "\n\n";
    
    if (userTurnCount < transitionTurn) {
        combinedPrompt += FormatPacingString(GetUiText("pacing_rule_early"), userTurnCount, minTurns, maxTurns) + "\n";
    } else if (userTurnCount >= transitionTurn && userTurnCount < maxTurns) {
        combinedPrompt += FormatPacingString(GetUiText("pacing_rule_mid"), userTurnCount, minTurns, maxTurns) + "\n";
    } else {
        combinedPrompt += FormatPacingString(GetUiText("pacing_rule_limit"), userTurnCount, minTurns, maxTurns) + "\n";
    }

    if (userTurnCount >= state.maxTurnsForce) {
        std::string forcedPrompt = state.pacingForcedConclusionPrompt;
        size_t pos = forcedPrompt.find("{next_chapter}");
        while (pos != std::string::npos) {
            forcedPrompt.replace(pos, 14, std::to_string(state.currentChapter + 1));
            pos = forcedPrompt.find("{next_chapter}");
        }
        combinedPrompt += "\n" + forcedPrompt + "\n";
    }
    
    if (aiClient) {
        aiClient->setSystemPrompt(combinedPrompt);
    }
}

inline bool IsBookValid(const std::string& filename = "book.json") {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::string parentPath = "../" + filename;
        file.open(parentPath);
        if (!file.is_open()) {
            return false;
        }
    }
    
    try {
        nlohmann::json j;
        file >> j;
        if (!j.contains("title") || !j["title"].is_string() || j["title"].get<std::string>().empty()) return false;
        if (!j.contains("world") || !j["world"].is_string() || j["world"].get<std::string>().empty()) return false;
        if (!j.contains("plot")) return false;
        if (!j["plot"].is_array() && !j["plot"].is_string()) return false;
        if (j["plot"].is_array() && j["plot"].empty()) return false;
        if (!j.contains("startPrompt") || !j["startPrompt"].is_string() || j["startPrompt"].get<std::string>().empty()) return false;
        return true;
    } catch (...) {
        return false;
    }
}

inline void SaveBookErrorLog(const std::string& rawResponse, const std::string& errorMsg) {
    std::ofstream errorFile("book_error.txt");
    if (errorFile.is_open()) {
        errorFile << "AI Raw Response:\n" << rawResponse << "\n\nError:\n" << errorMsg << std::endl;
        errorFile.close();
    }
    
    // Write synchronized copy of book_error.txt to the parent folder if running from a build subdirectory
    std::ifstream parentSettings("../settings.json");
    if (parentSettings.is_open()) {
        parentSettings.close();
        std::ofstream parentErrorFile("../book_error.txt");
        if (parentErrorFile.is_open()) {
            parentErrorFile << "AI Raw Response:\n" << rawResponse << "\n\nError:\n" << errorMsg << std::endl;
            parentErrorFile.close();
        }
    }
}

inline bool CreateBookFromTxt(
    const GameState& modelState,
    AskAi* aiClient, 
    const std::string& txtFilePath, 
    const std::string& gameLanguage, 
    const std::string& lengthWishes,
    const std::string& genreWishes,
    const std::string& fidelityWishes,
    const std::string& customWishes,
    std::string& outError,
    std::function<void(int progress, const std::string& status)> progressCallback = nullptr
) {
    if (aiClient) {
        aiClient->setSystemPrompt(""); // Clear any stale system prompts (e.g. from the old book) to prevent mixing or inheriting old book context
    }

#if defined(_WIN32)
    std::ifstream txtFile(std::filesystem::path(BookConverter::UTF8ToWide(txtFilePath)));
#else
    std::ifstream txtFile(txtFilePath);
#endif
    if (!txtFile.is_open()) {
        outError = "Could not open text file: " + txtFilePath;
        return false;
    }
    
    std::stringstream buffer;
    buffer << txtFile.rdbuf();
    std::string content = buffer.str();
    txtFile.close();
    
    content = Trim(content);
    if (content.empty()) {
        outError = "File is empty: " + txtFilePath;
        return false;
    }

    int totalChapters = ParseChapterCount(lengthWishes);
    
    // Temporarily increase timeouts for large book generation (e.g. 240 seconds for Saga)
    int oldConnect = 5;
    int oldRequest = 15;
    AskAiExternal* extClient = dynamic_cast<AskAiExternal*>(aiClient);
    if (extClient) {
        oldConnect = extClient->getConnectTimeout();
        oldRequest = extClient->getRequestTimeout();
    }

    if (totalChapters <= 10) {
        // --- Route A (Small Book, N <= 10) ---
        std::string lengthInstructions = modelState.promptAiBookGenLengthDefault;
        if (!lengthWishes.empty()) {
            lengthInstructions = modelState.promptAiBookGenLength;
            size_t pos = lengthInstructions.find("{wishes}");
            if (pos != std::string::npos) {
                lengthInstructions.replace(pos, 8, lengthWishes);
            }
        }

        std::string genreInstructions = "";
        if (!genreWishes.empty()) {
            genreInstructions = modelState.promptAiBookGenGenre;
            size_t pos = genreInstructions.find("{wishes}");
            if (pos != std::string::npos) {
                genreInstructions.replace(pos, 8, genreWishes);
            }
        }

        std::string fidelityInstructions = "";
        if (!fidelityWishes.empty()) {
            fidelityInstructions = modelState.promptAiBookGenFidelity;
            size_t pos = fidelityInstructions.find("{wishes}");
            if (pos != std::string::npos) {
                fidelityInstructions.replace(pos, 8, fidelityWishes);
            }
        }

        std::string customInstructions = "";
        if (!customWishes.empty()) {
            customInstructions = modelState.promptAiBookGenCustom;
            size_t pos = customInstructions.find("{wishes}");
            if (pos != std::string::npos) {
                customInstructions.replace(pos, 8, customWishes);
            }
        }

        std::string ruleBlock = modelState.promptAiBookGenRules;
        size_t langPos = ruleBlock.find("{language}");
        if (langPos != std::string::npos) {
            ruleBlock.replace(langPos, 10, gameLanguage);
        }
        size_t cPos = ruleBlock.find("{content}");
        if (cPos != std::string::npos) {
            ruleBlock.replace(cPos, 9, content);
        }

        std::string prompt = modelState.promptAiBookGenerator + "\n" + lengthInstructions + genreInstructions + fidelityInstructions + customInstructions + ruleBlock;
        
        if (extClient) {
            extClient->setTimeoutSettings(15, 240); // 15s connect, 240s request
        }

        std::cout << "[AI Book Gen] Route A: Sending raw story to AI (Length: " << content.length() << " characters)..." << std::endl;
        std::string response = aiClient->ask(prompt, gameLanguage);
        
        if (extClient) {
            extClient->setTimeoutSettings(oldConnect, oldRequest);
        }
        
        std::cout << "[AI Book Gen] AI response received (Length: " << response.length() << " characters)." << std::endl;
        
        response = Trim(response);
        if (response.rfind("Error", 0) == 0) {
            outError = response;
            return false;
        }
        if (response.rfind("```", 0) == 0) {
            size_t start = response.find("{");
            size_t end = response.rfind("}");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                response = response.substr(start, end - start + 1);
            }
        } else {
            size_t start = response.find("{");
            size_t end = response.rfind("}");
            if (start != std::string::npos && end != std::string::npos && end > start) {
                response = response.substr(start, end - start + 1);
            }
        }
        
        try {
            nlohmann::json bj = nlohmann::json::parse(response);
            
            if (!bj.contains("title") || !bj["title"].is_string()) {
                outError = "Missing or invalid 'title' field in generated JSON.";
                SaveBookErrorLog(response, outError);
                return false;
            }
            if (!bj.contains("world") || !bj["world"].is_string()) {
                outError = "Missing or invalid 'world' field in generated JSON.";
                SaveBookErrorLog(response, outError);
                return false;
            }
            if (!bj.contains("plot") || (!bj["plot"].is_array() && !bj["plot"].is_string())) {
                outError = "Missing or invalid 'plot' field in generated JSON.";
                SaveBookErrorLog(response, outError);
                return false;
            }
            if (!bj.contains("startPrompt") || !bj["startPrompt"].is_string()) {
                outError = "Missing or invalid 'startPrompt' field in generated JSON.";
                SaveBookErrorLog(response, outError);
                return false;
            }
            
            std::ofstream outFile("book.json");
            if (!outFile.is_open()) {
                outError = "Could not open book.json for writing.";
                SaveBookErrorLog(response, outError);
                return false;
            }
            outFile << bj.dump(4);
            outFile.close();

            // Overwrite/write copy of book.json to the parent folder if running from a build subdirectory
            std::ifstream parentSettings("../settings.json");
            if (parentSettings.is_open()) {
                parentSettings.close();
                std::ofstream parentOutFile("../book.json");
                if (parentOutFile.is_open()) {
                    parentOutFile << bj.dump(4);
                    parentOutFile.close();
                    std::cout << "[AI Book Gen] Successfully saved synchronized book.json copy to parent directory." << std::endl;
                }
            }
            
            std::cout << "[AI Book Gen] Successfully generated and saved book.json (Route A)!" << std::endl;
            return true;
        } catch (const std::exception& e) {
            outError = std::string("JSON parsing error: ") + e.what() + "\nRaw response starts with: " + response.substr(0, 100);
            std::cerr << "[AI Book Gen] Failed to parse JSON: " << e.what() << std::endl;
            SaveBookErrorLog(response, outError);
            return false;
        }
    } else {
        // --- Route B (Large Book, N > 10) ---
        std::string genreInstructions = "";
        if (!genreWishes.empty()) {
            genreInstructions = modelState.promptAiBookGenGenre;
            size_t pos = genreInstructions.find("{wishes}");
            if (pos != std::string::npos) {
                genreInstructions.replace(pos, 8, genreWishes);
            }
        }

        std::string fidelityInstructions = "";
        if (!fidelityWishes.empty()) {
            fidelityInstructions = modelState.promptAiBookGenFidelity;
            size_t pos = fidelityInstructions.find("{wishes}");
            if (pos != std::string::npos) {
                fidelityInstructions.replace(pos, 8, fidelityWishes);
            }
        }

        std::string customInstructions = "";
        if (!customWishes.empty()) {
            customInstructions = modelState.promptAiBookGenCustom;
            size_t pos = customInstructions.find("{wishes}");
            if (pos != std::string::npos) {
                customInstructions.replace(pos, 8, customWishes);
            }
        }

        std::string ruleBlock = modelState.promptAiBookGenRules;
        size_t langPos = ruleBlock.find("{language}");
        if (langPos != std::string::npos) {
            ruleBlock.replace(langPos, 10, gameLanguage);
        }
        size_t cPos = ruleBlock.find("{content}");
        if (cPos != std::string::npos) {
            ruleBlock.replace(cPos, 9, content);
        }

        std::string blueprintPromptTemplate = modelState.promptAiBookBlueprintGen;
        size_t tcPos = blueprintPromptTemplate.find("{total_chapters}");
        if (tcPos != std::string::npos) {
            blueprintPromptTemplate.replace(tcPos, 16, std::to_string(totalChapters));
        }

        std::string blueprintPrompt = blueprintPromptTemplate + "\n" + genreInstructions + fidelityInstructions + customInstructions + ruleBlock;

        int totalBlocks = (totalChapters + 9) / 10;
        int totalSteps = 1 + totalBlocks;

        nlohmann::json blueprintJson;
        bool blueprintSuccess = false;
        std::string blueprintError = "";

        for (int attempt = 1; attempt <= 3; ++attempt) {
            if (progressCallback) {
                std::string statusMsg = "";
                if (gameLanguage == "Russian") {
                    statusMsg = "Шаг 1 из " + std::to_string(totalSteps) + ": Планирование структуры приключения...";
                    if (attempt > 1) {
                        statusMsg += " (Попытка " + std::to_string(attempt) + " из 3)";
                    }
                } else {
                    statusMsg = "Step 1 of " + std::to_string(totalSteps) + ": Planning the grand quest blueprint...";
                    if (attempt > 1) {
                        statusMsg += " (Attempt " + std::to_string(attempt) + " of 3)";
                    }
                }
                progressCallback(10, statusMsg);
            }

            if (extClient) {
                extClient->setTimeoutSettings(15, 240);
            }
            std::cout << "[AI Book Gen] Route B: Blueprint Attempt " << attempt << "..." << std::endl;
            std::string response = aiClient->ask(blueprintPrompt, gameLanguage);
            if (extClient) {
                extClient->setTimeoutSettings(oldConnect, oldRequest);
            }

            response = Trim(response);
            if (response.rfind("Error", 0) == 0) {
                blueprintError = response;
                continue;
            }
            if (response.rfind("```", 0) == 0) {
                size_t start = response.find("{");
                size_t end = response.rfind("}");
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    response = response.substr(start, end - start + 1);
                }
            } else {
                size_t start = response.find("{");
                size_t end = response.rfind("}");
                if (start != std::string::npos && end != std::string::npos && end > start) {
                    response = response.substr(start, end - start + 1);
                }
            }

            try {
                blueprintJson = nlohmann::json::parse(response);
                if (!blueprintJson.contains("title") || !blueprintJson["title"].is_string() ||
                    !blueprintJson.contains("world") || !blueprintJson["world"].is_string() ||
                    !blueprintJson.contains("startPrompt") || !blueprintJson["startPrompt"].is_string() ||
                    !blueprintJson.contains("plot") || !blueprintJson["plot"].is_array()) {
                    blueprintError = "Blueprint JSON is missing required fields (title, world, startPrompt, plot).";
                    SaveBookErrorLog(response, blueprintError);
                    continue;
                }
                blueprintSuccess = true;
                break;
            } catch (const std::exception& e) {
                blueprintError = std::string("JSON parsing error: ") + e.what();
                SaveBookErrorLog(response, blueprintError);
            }
        }

        if (!blueprintSuccess) {
            outError = "Failed to generate a valid book blueprint outline after 3 attempts. Error: " + blueprintError;
            return false;
        }

        std::string outlineStr = "";
        for (const auto& item : blueprintJson["plot"]) {
            int chNum = item.value("chapter", 0);
            std::string chTitle = item.value("title", "");
            outlineStr += "Chapter " + std::to_string(chNum) + ": " + chTitle + "\n";
        }

        std::map<int, std::string> chapterDescriptions;

        for (int step = 0; step < totalBlocks; ++step) {
            int startCh = step * 10 + 1;
            int endCh = std::min(totalChapters, (step + 1) * 10);
            int activeStep = 2 + step;

            int currentProgress = 20 + (step * 80) / totalBlocks;
            if (currentProgress > 95) currentProgress = 95;

            std::string statusMsg = "";
            if (gameLanguage == "Russian") {
                statusMsg = "Шаг " + std::to_string(activeStep) + " из " + std::to_string(totalSteps) + 
                            ": Создание сюжета для глав с " + std::to_string(startCh) + " по " + std::to_string(endCh) + "...";
            } else {
                statusMsg = "Step " + std::to_string(activeStep) + " of " + std::to_string(totalSteps) + 
                            ": Weaving chapters " + std::to_string(startCh) + " to " + std::to_string(endCh) + "...";
            }

            if (progressCallback) {
                progressCallback(currentProgress, statusMsg);
            }

            std::string previousDetails = "";
            if (step > 0) {
                int startPrev = (step - 1) * 10 + 1;
                int endPrev = step * 10;
                for (int ch = startPrev; ch <= endPrev; ++ch) {
                    std::string chTitle = "";
                    for (const auto& item : blueprintJson["plot"]) {
                        if (item.value("chapter", 0) == ch) {
                            chTitle = item.value("title", "");
                            break;
                        }
                    }
                    std::string desc = chapterDescriptions[ch];
                    previousDetails += "Chapter " + std::to_string(ch) + ": " + chTitle + "\nDescription: " + desc + "\n\n";
                }
            } else {
                previousDetails = "None (This is the first block of chapters).";
            }

            std::string hydrationPrompt = modelState.promptAiBookBlockHydration;

            std::string worldDesc = blueprintJson.value("world", "");
            size_t wPos = hydrationPrompt.find("{world}");
            if (wPos != std::string::npos) {
                hydrationPrompt.replace(wPos, 7, worldDesc);
            }

            size_t oPos = hydrationPrompt.find("{outline}");
            if (oPos != std::string::npos) {
                hydrationPrompt.replace(oPos, 9, outlineStr);
            }

            size_t pdPos = hydrationPrompt.find("{previous_details}");
            if (pdPos != std::string::npos) {
                hydrationPrompt.replace(pdPos, 18, previousDetails);
            }

            size_t scPos = hydrationPrompt.find("{start_chapter}");
            if (scPos != std::string::npos) {
                hydrationPrompt.replace(scPos, 15, std::to_string(startCh));
            }

            size_t ecPos = hydrationPrompt.find("{end_chapter}");
            if (ecPos != std::string::npos) {
                hydrationPrompt.replace(ecPos, 13, std::to_string(endCh));
            }

            size_t lPos = hydrationPrompt.find("{language}");
            if (lPos != std::string::npos) {
                hydrationPrompt.replace(lPos, 10, gameLanguage);
            }

            bool blockSuccess = false;
            std::string blockError = "";
            nlohmann::json blockJson;

            for (int attempt = 1; attempt <= 3; ++attempt) {
                if (progressCallback) {
                    std::string attemptStatus = statusMsg;
                    if (attempt > 1) {
                        if (gameLanguage == "Russian") {
                            attemptStatus += " (Попытка " + std::to_string(attempt) + " из 3)";
                        } else {
                            attemptStatus += " (Attempt " + std::to_string(attempt) + " of 3)";
                        }
                    }
                    progressCallback(currentProgress, attemptStatus);
                }

                if (extClient) {
                    extClient->setTimeoutSettings(15, 240);
                }
                std::cout << "[AI Book Gen] Hydrating chapters " << startCh << " to " << endCh << " (Attempt " << attempt << ")..." << std::endl;
                std::string response = aiClient->ask(hydrationPrompt, gameLanguage);
                if (extClient) {
                    extClient->setTimeoutSettings(oldConnect, oldRequest);
                }

                response = Trim(response);
                if (response.rfind("Error", 0) == 0) {
                    blockError = response;
                    continue;
                }
                if (response.rfind("```", 0) == 0) {
                    size_t start = response.find("{");
                    size_t end = response.rfind("}");
                    if (start != std::string::npos && end != std::string::npos && end > start) {
                        response = response.substr(start, end - start + 1);
                    }
                } else {
                    size_t start = response.find("{");
                    size_t end = response.rfind("}");
                    if (start != std::string::npos && end != std::string::npos && end > start) {
                        response = response.substr(start, end - start + 1);
                    }
                }

                try {
                    blockJson = nlohmann::json::parse(response);
                    if (!blockJson.contains("plot") || !blockJson["plot"].is_array()) {
                        blockError = "Hydration JSON is missing 'plot' array.";
                        SaveBookErrorLog(response, blockError);
                        continue;
                    }

                    bool chaptersFound = true;
                    for (int ch = startCh; ch <= endCh; ++ch) {
                        bool found = false;
                        for (const auto& item : blockJson["plot"]) {
                            if (item.contains("chapter") && item["chapter"].is_number() && item["chapter"].get<int>() == ch) {
                                if (item.contains("description") && item["description"].is_string() && !item["description"].get<std::string>().empty()) {
                                    found = true;
                                    break;
                                }
                            }
                        }
                        if (!found) {
                            chaptersFound = false;
                            break;
                        }
                    }

                    if (!chaptersFound) {
                        blockError = "Hydration JSON plot array does not contain descriptions for all requested chapters in the block.";
                        SaveBookErrorLog(response, blockError);
                        continue;
                    }

                    blockSuccess = true;
                    break;
                } catch (const std::exception& e) {
                    blockError = std::string("JSON parsing error: ") + e.what();
                    SaveBookErrorLog(response, blockError);
                }
            }

            if (!blockSuccess) {
                outError = "Failed to hydrate chapters " + std::to_string(startCh) + " to " + std::to_string(endCh) + " after 3 attempts. Error: " + blockError;
                return false;
            }

            for (const auto& item : blockJson["plot"]) {
                int chNum = item.value("chapter", 0);
                std::string desc = item.value("description", "");
                if (chNum >= startCh && chNum <= endCh) {
                    chapterDescriptions[chNum] = desc;
                }
            }
        }

        nlohmann::json finalPlot = nlohmann::json::array();
        for (const auto& item : blueprintJson["plot"]) {
            int chNum = item.value("chapter", 0);
            std::string chTitle = item.value("title", "");
            std::string chDesc = chapterDescriptions[chNum];

            nlohmann::json chObj;
            chObj["chapter"] = chNum;
            chObj["title"] = chTitle;
            chObj["description"] = chDesc;
            finalPlot.push_back(chObj);
        }

        nlohmann::json finalBookJson;
        finalBookJson["title"] = blueprintJson["title"];
        finalBookJson["world"] = blueprintJson["world"];
        finalBookJson["startPrompt"] = blueprintJson["startPrompt"];
        finalBookJson["plot"] = finalPlot;

        std::ofstream outFile("book.json");
        if (!outFile.is_open()) {
            outError = "Could not open book.json for writing.";
            return false;
        }
        outFile << finalBookJson.dump(4);
        outFile.close();

        std::ifstream parentSettings("../settings.json");
        if (parentSettings.is_open()) {
            parentSettings.close();
            std::ofstream parentOutFile("../book.json");
            if (parentOutFile.is_open()) {
                parentOutFile << finalBookJson.dump(4);
                parentOutFile.close();
                std::cout << "[AI Book Gen] Successfully saved synchronized book.json copy to parent directory." << std::endl;
            }
        }

        std::cout << "[AI Book Gen] Successfully generated and saved book.json (Route B)!" << std::endl;
        return true;
    }
}


