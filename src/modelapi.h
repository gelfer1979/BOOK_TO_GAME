#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

// Abstract base class for asking AI questions
class AskAi {
public:
    virtual std::string ask(const std::string& question) = 0;   
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

        // Apply sensible hardcoded fallbacks if baseUrl is empty
        if (baseUrl_.empty()) {
            baseUrl_ = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent";
            modelName_ = "gemini-2.5-flash";
            apiKey_ = "AIzaSyDwKzr-oMvhGOYdfJPk9p7m4kbwGvW9yTM";
            format_ = "gemini";
            std::cout << "[Config Fallback] Using hardcoded Gemini flash endpoint." << std::endl;
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

    void setSystemPrompt(const std::string& prompt) {
        systemPrompt_ = prompt;
    }

    std::string ask(const std::string& question) override {
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
                        aiResponse = "Error: Invalid Gemini response format.";
                    }
                } else {
                    // Parse standard OpenAI response format
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

    void setRetrySettings(int maxRetries, int retryDelayMs) {
        if (maxRetries >= 0) maxRetries_ = maxRetries;
        if (retryDelayMs > 0) retryDelayMs_ = retryDelayMs;
    }

private:
    std::string baseUrl_;
    std::string modelName_;
    std::string apiKey_;
    std::string apiKeyEnvVar_;
    std::string format_ = "openai";
    std::string systemPrompt_;
    int maxRetries_ = 3;
    int retryDelayMs_ = 1000;

    // Static callback function to write received data into a std::string
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
};

