#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <map>
#include <atomic>
#include <filesystem>
#include <unordered_map>
#include "book_converter.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_net.h>

#include "modelapi.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

enum AppState {
    APP_STATE_ASK_CONTINUE,
    APP_STATE_ENTER_TXT_PATH,
    APP_STATE_SETUP,
    APP_STATE_AI_GENERATING,
    APP_STATE_GAMEPLAY,
    APP_STATE_SELECT_AI,
    APP_STATE_SELECT_BOOK
};

// Default Window Dimensions
int WINDOW_WIDTH = 800;
int WINDOW_HEIGHT = 600;

// Unique RAII resource aliases
using UniqueWindow    = std::unique_ptr<SDL_Window,   decltype(&SDL_DestroyWindow)>;
using UniqueRenderer  = std::unique_ptr<SDL_Renderer, decltype(&SDL_DestroyRenderer)>;
using UniqueFont      = std::unique_ptr<TTF_Font,     decltype(&TTF_CloseFont)>;
using UniqueChunk     = std::unique_ptr<Mix_Chunk,    decltype(&Mix_FreeChunk)>;

// Chat Message structure
struct ChatMessage {
    std::string sender;             // "User" or "AI"
    std::string text;               // Raw full response/question
    std::vector<std::string> lines; // Pre-wrapped lines for rendering
};

// Dynamic response options proposed by the AI
struct ActiveChoice {
    std::string text;
    SDL_Rect rect; // Dynamically calculated per render frame for hit tests
};

// Global Application State Class
struct {
    UniqueWindow window{nullptr, SDL_DestroyWindow};
    UniqueRenderer renderer{nullptr, SDL_DestroyRenderer};
    
    // Core Model and AI Client
    GameState modelState;
    std::unique_ptr<AskAiExternal> aiClient;
    
    // UI Layout Mappings
    std::vector<ChatMessage> uiMessages;
    std::vector<ActiveChoice> uiActiveChoices;
    
    std::string inputText = "";
    bool aiThinking = false;
    bool ignoreTags = false;
    std::string transitionPrefix = "Перейти к Главе ";
    std::string gameLanguage = "Russian";
    
    // Startup State Variables
    AppState appState = APP_STATE_ASK_CONTINUE;
    int generationProgress = 0;
    std::string generationStatus = "";
    std::map<std::string, std::string> localizedUi;
    bool uiLocalized = false;
    std::string txtPath = "";
    std::string fileLoadError = "";
    SDL_Rect customBtnRect1 = {0, 0, 0, 0}; // continue Yes button
    SDL_Rect customBtnRect2 = {0, 0, 0, 0}; // continue No / select file button
    SDL_Rect homeBtnRect = {0, 0, 0, 0};    // Return to main menu button
    
    // Conversational Setup properties
    int setupStep = 0; // 0: Length, 1: Genre, 2: Fidelity, 3: Custom wishes
    std::string chosenLengthText = "";
    std::string chosenGenreText = "";
    std::string chosenFidelityText = "";
    std::string chosenCustomWishesText = "";
    
    // Thread safety synchronization
    std::recursive_mutex mutex;
    bool responseReady = false;
    std::string pendingResponse = "";
    uint64_t currentQueryId = 0;
    
    // Fonts & Audio Assets
    UniqueFont fontTitle{nullptr, TTF_CloseFont};
    UniqueFont fontMessage{nullptr, TTF_CloseFont};
    UniqueFont fontUI{nullptr, TTF_CloseFont};
    UniqueFont fontSmallUI{nullptr, TTF_CloseFont};
    UniqueChunk soundEffect{nullptr, Mix_FreeChunk};
    bool mixOk = false;
    
    // Scroll properties
    int scrollOffset = 0;
    int maxScrollOffset = 0;
    bool scrollToBottom = false;
    
    // Flashing cursor properties
    Uint32 cursorLastBlink = 0;
    bool cursorVisible = true;
    
    bool running = true;
    bool fileDialogActive = false;
    SDL_Rect deathBtnRect = {0, 0, 0, 0};
    SDL_Rect victoryBtnRect = {0, 0, 0, 0};
    SDL_Rect victoryBtnRect2 = {0, 0, 0, 0};
    SDL_Rect clearBtnRect = {0, 0, 0, 0};
    SDL_Rect confirmBtnRect = {0, 0, 0, 0};
    SDL_Rect pasteBtnRect = {0, 0, 0, 0};
    std::vector<std::string> setupDynamicGenres;
    int maxRetries = 3;
    int bookRetries = 3;
    int retryDelayMs = 1000;
    int connectTimeout = 5;
    int requestTimeout = 15;
    std::vector<std::string> savedChoices;
    bool editingLanguage = false;
    std::string tempLanguageInput = "";
    bool editingApiKey = false;
    std::string selectedAiFilename = "";
    SDL_Rect langBtnRect = {0, 0, 0, 0};
    SDL_Rect fontIncBtnRect = {0, 0, 0, 0};
    SDL_Rect fontDecBtnRect = {0, 0, 0, 0};
    int fontSizeOffset = 0;
    
    struct AIModelInfo {
        std::string filename;
        std::string modelName;
    };
    std::vector<AIModelInfo> availableAiModels;
    std::string aiModelName = "";
    AppState previousAppState = APP_STATE_ASK_CONTINUE;
    SDL_Rect aiBtnRect = {0, 0, 0, 0};
    int aiSelectScrollOffset = 0;
    int aiSelectMaxScroll = 0;
    
    struct BookInfo {
        std::string filename;
        std::string path;
    };
    std::vector<BookInfo> availableBooks;
    int bookSelectScrollOffset = 0;
    int bookSelectMaxScroll = 0;
    bool editingBookPath = false;
    std::string bookPathInput = "";
    SDL_Rect bookPathInputRect = {0, 0, 0, 0};
    SDL_Rect bookConfirmBtnRect = {0, 0, 0, 0};
} state;

// Forward Declarations
std::vector<std::string> WrapText(TTF_Font* font, const std::string& text, int maxWidth);
std::vector<std::string> WrapTextDynamic(TTF_Font* font, const std::string& text, const std::vector<int>& lineWidths, int defaultWidth);
std::string Trim(const std::string& str);
void SubmitQuery(const std::string& queryText, bool isRetry = false, bool showInChat = true);
void AddArchitectBubble(const std::string& text);
void TriggerSetupStep(int step);
void SubmitSetupChoice(const std::string& choiceText);
void SubmitInputText();
void RestartAdventure();
void InitAdventureSetup(const std::string& filePath);
void StartBookGeneration(const std::string& filePath);

void SyncModelToUi() {
    // 1. Convert modelState.messages to uiMessages
    state.uiMessages.clear();
    for (const auto& msgData : state.modelState.messages) {
        ChatMessage msg;
        msg.sender = msgData.sender;
        msg.text = msgData.text;
        
        // Strip options and other XML tags for clean UI display
        if (msg.sender == "AI") {
            std::string tempText = msg.text;
            ExtractAndStripOptions(tempText);
            
            // Clean dead / next chapter tags
            size_t deadPos = tempText.find("<player_dead/>");
            if (deadPos != std::string::npos) tempText.erase(deadPos, 14);
            size_t deadPosSq = tempText.find("[player_dead]");
            if (deadPosSq != std::string::npos) tempText.erase(deadPosSq, 13);
            
            size_t nextPos = tempText.find("<next_chapter>");
            if (nextPos != std::string::npos) {
                size_t nextEndPos = tempText.find("</next_chapter>", nextPos);
                if (nextEndPos != std::string::npos && nextEndPos > nextPos) {
                    tempText.erase(nextPos, (nextEndPos + 15) - nextPos);
                } else {
                    tempText.erase(nextPos);
                }
            }
            size_t nextPosSq = tempText.find("[next_chapter]");
            if (nextPosSq != std::string::npos) {
                size_t nextEndPosSq = tempText.find("[/next_chapter]", nextPosSq);
                if (nextEndPosSq != std::string::npos && nextEndPosSq > nextPosSq) {
                    tempText.erase(nextPosSq, (nextEndPosSq + 15) - nextPosSq);
                } else {
                    tempText.erase(nextPosSq);
                }
            }
            
            msg.text = tempText;
            
            // Trim trailing and leading spaces, newlines, and leftover markdown symbols
            while (!msg.text.empty() && (msg.text.back() == '\n' || msg.text.back() == '\r' || msg.text.back() == ' ' || msg.text.back() == '`')) {
                msg.text.pop_back();
            }
            while (!msg.text.empty() && (msg.text.front() == '\n' || msg.text.front() == '\r' || msg.text.front() == ' ')) {
                msg.text.erase(msg.text.begin());
            }
        }
        
        if (state.fontMessage) {
            msg.lines = WrapText(state.fontMessage.get(), msg.text, WINDOW_WIDTH - 64);
        } else {
            msg.lines = { msg.text };
        }
        state.uiMessages.push_back(msg);
    }
    
    // 2. Convert modelState.activeChoices to uiActiveChoices
    state.uiActiveChoices.clear();
    int n = state.modelState.activeChoices.size();
    if (n > 6) n = 6;
    
    int cardH = 45;
    int verticalSpacing = 8;
    int optionsAreaH = n * cardH + (n + 1) * verticalSpacing;
    int footerH = 60;
    int cardW = WINDOW_WIDTH - 40;
    int startX = 20;
    
    for (int i = 0; i < n; i++) {
        ActiveChoice choice;
        choice.text = state.modelState.activeChoices[i];
        choice.rect.x = startX;
        choice.rect.y = WINDOW_HEIGHT - (optionsAreaH + footerH) + verticalSpacing + i * (cardH + verticalSpacing);
        choice.rect.w = cardW;
        choice.rect.h = cardH;
        state.uiActiveChoices.push_back(choice);
    }
}

void UpdateSystemPrompt() {
    UpdateSystemPrompt(state.modelState, state.aiClient.get());
}

void SaveGame() {
    state.mutex.lock();
    SaveGame(state.modelState);
    state.mutex.unlock();
}

bool LoadGame() {
    state.mutex.lock();
    bool loaded = LoadGame(state.modelState);
    if (loaded) {
        SyncModelToUi();
        state.scrollToBottom = true;
    }
    state.mutex.unlock();
    return loaded;
}

bool LoadBookConfig(const std::string& filename = "book.json") {
    state.modelState.chapters.clear();
    std::string bookTitle = "BOOK_TO_GAME";
    std::string bookWorld = "";
    std::string bookPlot = "";
    std::string bookStartPrompt = "";
    std::ifstream bookFile(filename);
    if (!bookFile.is_open()) {
        bookFile.open("../" + filename);
    }
    if (bookFile.is_open()) {
        try {
            nlohmann::json bj;
            bookFile >> bj;
            if (bj.contains("title") && bj["title"].is_string()) {
                bookTitle = bj["title"].get<std::string>();
            }
            if (bj.contains("world") && bj["world"].is_string()) {
                bookWorld = bj["world"].get<std::string>();
            }
            if (bj.contains("plot")) {
                if (bj["plot"].is_string()) {
                    bookPlot = bj["plot"].get<std::string>();
                } else if (bj["plot"].is_array()) {
                    std::stringstream ss;
                    ss << "Сюжет игры по главам:\n";
                    for (const auto& chJson : bj["plot"]) {
                        if (chJson.is_object()) {
                            std::string chTitle = "";
                            std::string chDesc = "";
                            std::string chNum = "";
                            int chIntNum = 1;
                            
                            if (chJson.contains("chapter")) {
                                if (chJson["chapter"].is_number()) {
                                    chIntNum = chJson["chapter"].get<int>();
                                    chNum = std::to_string(chIntNum);
                                } else if (chJson["chapter"].is_string()) {
                                    chNum = chJson["chapter"].get<std::string>();
                                    try {
                                        chIntNum = std::stoi(chNum);
                                    } catch (...) {
                                        chIntNum = 1;
                                    }
                                }
                            }
                            if (chJson.contains("title") && chJson["title"].is_string()) {
                                chTitle = chJson["title"].get<std::string>();
                            }
                            if (chJson.contains("description") && chJson["description"].is_string()) {
                                chDesc = chJson["description"].get<std::string>();
                            }
                            
                            ChapterData chData;
                            chData.number = chIntNum;
                            chData.title = chTitle;
                            chData.description = chDesc;
                            state.modelState.chapters.push_back(chData);
                            
                            if (!chNum.empty()) {
                                ss << "Глава " << chNum;
                            } else {
                                ss << "Глава";
                            }
                            if (!chTitle.empty()) {
                                ss << ": " << chTitle;
                            }
                            ss << "\nОписание: " << chDesc << "\n\n";
                        } else if (chJson.is_string()) {
                            ss << "- " << chJson.get<std::string>() << "\n";
                        }
                    }
                    bookPlot = ss.str();
                }
            }
            if (bj.contains("startPrompt") && bj["startPrompt"].is_string()) {
                bookStartPrompt = bj["startPrompt"].get<std::string>();
            }
            
            state.modelState.bookTitle = bookTitle;
            state.modelState.bookWorld = bookWorld;
            state.modelState.bookStartPrompt = bookStartPrompt;
            
            // Apply title to desktop window
            if (state.window) {
                SDL_SetWindowTitle(state.window.get(), bookTitle.c_str());
            }
            
            // Also let's construct and set system prompt
            std::string combinedPrompt = state.modelState.systemPrompt;
            if (!bookWorld.empty()) {
                combinedPrompt += "\n\nИгровой мир:\n" + bookWorld;
            }
            if (!bookPlot.empty()) {
                combinedPrompt += "\n\nСюжет и цель игры:\n" + bookPlot;
            }
            if (!combinedPrompt.empty()) {
                state.aiClient->setSystemPrompt(combinedPrompt);
            }
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "[Config] Parse error: " << e.what() << std::endl;
            return false;
        }
    } else {
        std::cerr << "[Config] Failed to open " << filename << std::endl;
        return false;
    }
}

// Native HarfBuzz Layout Controllers
void ConfigureFontLayout(TTF_Font* font, const std::string& text) {
    if (!font) return;
    
    std::vector<uint32_t> codePoints = DecodeUTF8(text);
    bool hasArabic = false;
    bool hasHebrew = false;
    
    for (uint32_t cp : codePoints) {
        if (cp >= 0x0600 && cp <= 0x06FF) {
            hasArabic = true;
            break;
        } else if (cp >= 0x0590 && cp <= 0x05FF) {
            hasHebrew = true;
            break;
        }
    }
    
    if (hasArabic) {
        TTF_SetFontDirection(font, TTF_DIRECTION_RTL);
        TTF_SetFontScriptName(font, "Arab");
    } else if (hasHebrew) {
        TTF_SetFontDirection(font, TTF_DIRECTION_RTL);
        TTF_SetFontScriptName(font, "Hebr");
    } else {
        TTF_SetFontDirection(font, TTF_DIRECTION_LTR);
        TTF_SetFontScriptName(font, "Latn");
    }
}

void ResetFontLayout(TTF_Font* font) {
    if (!font) return;
    TTF_SetFontDirection(font, TTF_DIRECTION_LTR);
    TTF_SetFontScriptName(font, "Latn");
}

// Layout-Aware Rendering and Text Wrapping
void RenderText(SDL_Renderer* renderer, TTF_Font* font, const std::string& text, int x, int y, SDL_Color color, bool center = false) {
    if (!font || text.empty()) return;
    
    ConfigureFontLayout(font, text);
    SDL_Surface* surface = TTF_RenderUTF8_Blended(font, text.c_str(), color);
    ResetFontLayout(font);
    
    if (!surface) return;
    
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        int w = 0, h = 0;
        SDL_QueryTexture(texture, nullptr, nullptr, &w, &h);
        
        SDL_Rect destRect;
        destRect.x = center ? x - w / 2 : x;
        destRect.y = center ? y - h / 2 : y;
        destRect.w = w;
        destRect.h = h;
        
        SDL_RenderCopy(renderer, texture, nullptr, &destRect);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

std::vector<std::string> WrapText(TTF_Font* font, const std::string& text, int maxWidth) {
    std::vector<std::string> lines;
    if (!font || text.empty()) return lines;
    
    ConfigureFontLayout(font, text);
    
    // Split by standard words
    std::stringstream ss(text);
    std::string word;
    std::vector<std::string> tokens;
    while (ss >> word) {
        tokens.push_back(word);
    }
    
    std::string currentLine = "";
    for (const auto& token : tokens) {
        std::string testLine = currentLine.empty() ? token : currentLine + " " + token;
        int w = 0, h = 0;
        TTF_SizeUTF8(font, testLine.c_str(), &w, &h);
        if (w <= maxWidth) {
            currentLine = testLine;
        } else {
            if (!currentLine.empty()) {
                lines.push_back(currentLine);
            }
            currentLine = token;
        }
    }
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    
    ResetFontLayout(font);
    return lines;
}

std::vector<std::string> WrapTextDynamic(TTF_Font* font, const std::string& text, const std::vector<int>& lineWidths, int defaultWidth) {
    std::vector<std::string> lines;
    if (!font || text.empty()) return lines;
    
    ConfigureFontLayout(font, text);
    
    // Split by standard words
    std::stringstream ss(text);
    std::string word;
    std::vector<std::string> tokens;
    while (ss >> word) {
        tokens.push_back(word);
    }
    
    std::string currentLine = "";
    for (const auto& token : tokens) {
        int currentLineIdx = lines.size();
        int maxWidth = (currentLineIdx < (int)lineWidths.size()) ? lineWidths[currentLineIdx] : defaultWidth;
        
        std::string testLine = currentLine.empty() ? token : currentLine + " " + token;
        int w = 0, h = 0;
        TTF_SizeUTF8(font, testLine.c_str(), &w, &h);
        if (w <= maxWidth) {
            currentLine = testLine;
        } else {
            if (!currentLine.empty()) {
                lines.push_back(currentLine);
            }
            currentLine = token;
        }
    }
    if (!currentLine.empty()) {
        lines.push_back(currentLine);
    }
    
    ResetFontLayout(font);
    return lines;
}


// Procedural Rounded Rectangle Renderer
void DrawFilledCircle(SDL_Renderer* renderer, int cx, int cy, int radius, SDL_Color color) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (int w = 0; w < radius * 2; w++) {
        for (int h = 0; h < radius * 2; h++) {
            int dx = radius - w;
            int dy = radius - h;
            if ((dx*dx + dy*dy) <= (radius * radius)) {
                SDL_RenderDrawPoint(renderer, cx + dx, cy + dy);
            }
        }
    }
}

void DrawRoundedRect(SDL_Renderer* renderer, const SDL_Rect& rect, int r, SDL_Color color) {
    int w = rect.w;
    int h = rect.h;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 0) r = 0;
    
    if (r == 0) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_RenderFillRect(renderer, &rect);
        return;
    }
    
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    
    // Corners
    DrawFilledCircle(renderer, rect.x + r, rect.y + r, r, color);
    DrawFilledCircle(renderer, rect.x + w - r, rect.y + r, r, color);
    DrawFilledCircle(renderer, rect.x + r, rect.y + h - r, r, color);
    DrawFilledCircle(renderer, rect.x + w - r, rect.y + h - r, r, color);
    
    // Middle components
    SDL_Rect bodyX = { rect.x + r, rect.y, w - 2 * r, h };
    SDL_Rect bodyY = { rect.x, rect.y + r, w, h - 2 * r };
    SDL_RenderFillRect(renderer, &bodyX);
    SDL_RenderFillRect(renderer, &bodyY);
}



inline bool IsRussianLanguage(const std::string& lang) {
    std::string lower = lang;
    for (auto& c : lower) c = tolower((unsigned char)c);
    // Trim leading/trailing spaces
    while (!lower.empty() && isspace((unsigned char)lower.front())) lower.erase(lower.begin());
    while (!lower.empty() && isspace((unsigned char)lower.back())) lower.pop_back();
    return lower == "russian" || lower == "русский" || lower == "ru" || lower == "rus";
}

inline bool IsEnglishLanguage(const std::string& lang) {
    std::string lower = lang;
    for (auto& c : lower) c = tolower((unsigned char)c);
    while (!lower.empty() && isspace((unsigned char)lower.front())) lower.erase(lower.begin());
    while (!lower.empty() && isspace((unsigned char)lower.back())) lower.pop_back();
    return lower == "english" || lower == "en" || lower == "eng";
}

inline bool IsUkrainianLanguage(const std::string& lang) {
    std::string lower = lang;
    for (auto& c : lower) c = tolower((unsigned char)c);
    while (!lower.empty() && isspace((unsigned char)lower.front())) lower.erase(lower.begin());
    while (!lower.empty() && isspace((unsigned char)lower.back())) lower.pop_back();
    return lower == "ukrainian" || lower == "українська" || lower == "ukr" || lower == "ua" || lower == "мова" || lower == "українська мова";
}

std::string GetStartPrompt(const std::string& lang) {
    if (IsRussianLanguage(lang)) {
        return "Привет! Расскажи интересную историю на русском языке и предложи варианты выбора.";
    } else if (IsUkrainianLanguage(lang)) {
        return "Привіт! Розкажи цікаву історію українською мовою та запропонуй варіанти вибору.";
    } else {
        return "Hello! Tell an interesting story in the " + lang + " language and offer choices.";
    }
}

std::string GetSummaryPrompt(const std::string& lang, const std::string& dialogueText) {
    if (IsRussianLanguage(lang)) {
        return "Сделай краткое саммари (2-3 предложения) на русском языке для пройденной главы на основе следующего диалога. Опиши только ключевые события, достижения игрока, инвентарь и важные сюжетные выборы. Не пиши ничего лишнего.\n\nДиалог:\n" + dialogueText;
    } else if (IsUkrainianLanguage(lang)) {
        return "Зроби коротке саммарі (2-3 речення) українською мовою для пройденого розділу на основі наступного діалогу. Опиши тільки ключові події, досягнення гравця, інвентар та важливі сюжетні вибори. Не пиши нічого зайвого.\n\nДіалог:\n" + dialogueText;
    } else {
        return "Make a brief summary (2-3 sentences) in the " + lang + " language for the completed chapter based on the following dialogue. Describe only key events, player achievements, inventory, and important story choices. Do not write anything extra.\n\nDialogue:\n" + dialogueText;
    }
}

std::string GetNextStartMsg(const std::string& lang, int nextChapter) {
    if (IsRussianLanguage(lang)) {
        return "Начни главу " + std::to_string(nextChapter) + ". Опиши начало новой главы, атмосферу вокруг меня и предложи первые варианты действий.";
    } else if (IsUkrainianLanguage(lang)) {
        return "Почни розділ " + std::to_string(nextChapter) + ". Опиши початок нового розділу, атмосферу навколо мене та запропонуй перші варіанти дій.";
    } else {
        return "Start Chapter " + std::to_string(nextChapter) + " in the " + lang + " language. Describe the beginning of the new chapter, the atmosphere around me, and suggest the first choices of action.";
    }
}

std::string GetEpiloguePrompt(const std::string& lang, const std::string& allSummaries) {
    if (IsRussianLanguage(lang)) {
        return "Пожалуйста, напиши красивый, трогательный, краткий и торжественный эпилог на русском языке для нашего приключения, основываясь на пройденном пути. Расскажи, как завершилось наше путешествие и к какому финалу мы пришли. Не выводи никаких тегов вариантов выбора или XML.\n\nКраткое содержание нашего пути по главам:\n" + allSummaries;
    } else if (IsUkrainianLanguage(lang)) {
        return "Будь ласка, напиши красивий, зворушливий, короткий і урочистий епілог українською мовою для нашої пригоди, грунтуючись на пройденому шляху. Розкажи, як завершилася наша подорож і до якого фіналу ми прийшли. Не виводь жодних тегів варіантів вибору або XML.\n\nКороткий зміст нашого шляху по розділах:\n" + allSummaries;
    } else {
        return "Please write a beautiful, moving, brief, and celebratory epilogue in the " + lang + " language for our adventure, based on the journey we have completed. Tell us how our journey concluded and what finale we reached. Do not output any choice tags or XML.\n\nSummary of our journey by chapters:\n" + allSummaries;
    }
}


std::string GetNormalizedLanguageLocal(const std::string& input) {
    std::string lower = input;
    for (auto& c : lower) c = tolower((unsigned char)c);
    while (!lower.empty() && isspace((unsigned char)lower.front())) lower.erase(lower.begin());
    while (!lower.empty() && isspace((unsigned char)lower.back())) lower.pop_back();
    
    if (lower.empty()) return "";
    
    // English
    if (lower == "en" || lower == "eng" || lower.find("english") != std::string::npos || 
        lower.find("англий") != std::string::npos || lower.find("англій") != std::string::npos) {
        return "English";
    }
    // Russian
    if (lower == "ru" || lower == "rus" || lower.find("russian") != std::string::npos || 
        lower.find("рус") != std::string::npos || lower.find("рос") != std::string::npos) {
        return "Russian";
    }
    // Ukrainian
    if (lower == "ua" || lower == "ukr" || lower.find("ukrainian") != std::string::npos || 
        lower.find("украин") != std::string::npos || lower.find("україн") != std::string::npos) {
        return "Ukrainian";
    }
    // Spanish
    if (lower == "es" || lower == "esp" || lower.find("spanish") != std::string::npos || 
        lower.find("испан") != std::string::npos || lower.find("іспан") != std::string::npos) {
        return "Spanish";
    }
    // French
    if (lower == "fr" || lower == "fra" || lower.find("french") != std::string::npos || 
        lower.find("франц") != std::string::npos) {
        return "French";
    }
    // German
    if (lower == "de" || lower == "ger" || lower.find("german") != std::string::npos || 
        lower.find("немец") != std::string::npos || lower.find("німец") != std::string::npos || lower == "deutsch") {
        return "German";
    }
    // Italian
    if (lower == "it" || lower == "ita" || lower.find("italian") != std::string::npos || 
        lower.find("италь") != std::string::npos || lower.find("італь") != std::string::npos) {
        return "Italian";
    }
    // Polish
    if (lower == "pl" || lower == "pol" || lower.find("polish") != std::string::npos || 
        lower.find("польс") != std::string::npos) {
        return "Polish";
    }
    // Japanese
    if (lower == "ja" || lower == "jp" || lower == "jpn" || lower.find("japanese") != std::string::npos || 
        lower.find("япон") != std::string::npos) {
        return "Japanese";
    }
    // Chinese
    if (lower == "zh" || lower == "cn" || lower == "chi" || lower.find("chinese") != std::string::npos || 
        lower.find("китай") != std::string::npos) {
        return "Chinese";
    }
    // Portuguese
    if (lower == "pt" || lower == "por" || lower.find("portuguese") != std::string::npos || 
        lower.find("португ") != std::string::npos) {
        return "Portuguese";
    }
    // Kazakh
    if (lower == "kk" || lower == "kaz" || lower.find("kazakh") != std::string::npos || 
        lower.find("казах") != std::string::npos) {
        return "Kazakh";
    }
    // Hebrew
    if (lower == "he" || lower == "heb" || lower.find("hebrew") != std::string::npos || 
        lower.find("иврит") != std::string::npos || lower.find("іврит") != std::string::npos) {
        return "Hebrew";
    }
    // Arabic
    if (lower == "ar" || lower == "ara" || lower.find("arabic") != std::string::npos || 
        lower.find("араб") != std::string::npos) {
        return "Arabic";
    }
    // Turkish
    if (lower == "tr" || lower == "tur" || lower.find("turkish") != std::string::npos || 
        lower.find("турец") != std::string::npos || lower.find("турець") != std::string::npos) {
        return "Turkish";
    }
    
    return "";
}

void SaveLanguageToSettings(const std::string& newLanguage) {
    nlohmann::json j;
    std::string settingsPath = "settings.json";
    std::ifstream inFile(settingsPath);
    if (!inFile.is_open()) {
        settingsPath = "../settings.json";
        inFile.open(settingsPath);
    }
    if (inFile.is_open()) {
        try {
            inFile >> j;
        } catch (...) {
            std::cerr << "[Config] Error parsing settings.json before save." << std::endl;
        }
        inFile.close();
    }
    
    j["Language"] = newLanguage;
    
    std::ofstream outFile(settingsPath);
    if (outFile.is_open()) {
        outFile << j.dump(4);
        outFile.close();
        std::cout << "[Config] Saved new language '" << newLanguage << "' to " << settingsPath << std::endl;
    }
}

void SaveApiKeyToModelJson(const std::string& filename, const std::string& apiKey) {
    if (filename.empty()) return;
    nlohmann::json j;
    std::string modelPath = filename;
    std::ifstream inFile(modelPath);
    if (!inFile.is_open()) {
        modelPath = "../" + filename;
        inFile.open(modelPath);
    }
    if (inFile.is_open()) {
        try {
            inFile >> j;
        } catch (...) {
            std::cerr << "[Config] Error parsing model file " << filename << " before save." << std::endl;
        }
        inFile.close();
    }
    
    j["apiKey"] = apiKey;
    
    std::ofstream outFile(modelPath);
    if (outFile.is_open()) {
        outFile << j.dump(4);
        outFile.close();
        std::cout << "[Config] Saved API Key to model configuration '" << filename << "'" << std::endl;
    } else {
        std::cerr << "[Config] Failed to open model configuration '" << filename << "' for writing." << std::endl;
    }
}

// Saves the dynamic font size offset (modifier) to settings.json so it persists across sessions
void SaveFontSizeOffsetToSettings(int offset) {
    nlohmann::json j;
    std::string settingsPath = "settings.json";
    std::ifstream inFile(settingsPath);
    if (!inFile.is_open()) {
        settingsPath = "../settings.json";
        inFile.open(settingsPath);
    }
    if (inFile.is_open()) {
        try {
            inFile >> j;
        } catch (...) {
            std::cerr << "[Config] Error parsing settings.json before font size save." << std::endl;
        }
        inFile.close();
    }
    
    j["fontSizeOffset"] = offset;
    
    std::ofstream outFile(settingsPath);
    if (outFile.is_open()) {
        outFile << j.dump(4);
        outFile.close();
        std::cout << "[Config] Saved font size offset '" << offset << "' to " << settingsPath << std::endl;
    }
}

void ScanAvailableAiModels() {
    state.mutex.lock();
    state.availableAiModels.clear();
    state.mutex.unlock();
    try {
        std::vector<std::string> searchPaths = {".", ".."};
        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        std::string filename = entry.path().filename().string();
                        // Check if starts with "ai_" and ends with ".json"
                        if (filename.rfind("ai_", 0) == 0 && filename.length() > 5 && filename.substr(filename.length() - 5) == ".json") {
                            std::ifstream f(entry.path());
                            if (f.is_open()) {
                                try {
                                    nlohmann::json j;
                                    f >> j;
                                    if (j.contains("modelName") && j["modelName"].is_string()) {
                                        std::string modelName = j["modelName"].get<std::string>();
                                        state.mutex.lock();
                                        bool duplicate = false;
                                        for (const auto& existing : state.availableAiModels) {
                                            if (existing.filename == filename) {
                                                duplicate = true;
                                                break;
                                            }
                                        }
                                        if (!duplicate) {
                                            state.availableAiModels.push_back({filename, modelName});
                                        }
                                        state.mutex.unlock();
                                    }
                                } catch (...) {}
                                f.close();
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {}
}

// Scans local folders for potential adventure book text/JSON files
void ScanAvailableBooks() {
    state.mutex.lock();
    state.availableBooks.clear();
    state.mutex.unlock();
    
    try {
        std::vector<std::string> searchPaths = {".", "assets", "..", "../assets"};
        for (const auto& path : searchPaths) {
            if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
                for (const auto& entry : std::filesystem::directory_iterator(path)) {
                    if (entry.is_regular_file()) {
                        std::string filename = entry.path().filename().string();
                        std::string ext = entry.path().extension().string();
                        
                        // Convert to lowercase for checking extension
                        std::string lowerExt = ext;
                        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
                        
                        if (BookConverter::IsSupportedBookFormat(lowerExt)) {
                            // Skip system configuration, save games, and makefiles
                            if (filename == "save.json" || filename == "settings.json" || 
                                filename == "options.json" || filename == "book_error.txt" ||
                                filename == "CMakeLists.txt" ||
                                filename == "book.txt") {
                                continue;
                            }
                            // Skip AI model files
                            if (filename.rfind("ai_", 0) == 0 && lowerExt == ".json") {
                                continue;
                            }
                            
                            state.mutex.lock();
                            bool duplicate = false;
                            for (const auto& existing : state.availableBooks) {
                                if (existing.filename == filename) {
                                    duplicate = true;
                                    break;
                                }
                            }
                            if (!duplicate) {
                                state.availableBooks.push_back({filename, entry.path().string()});
                            }
                            state.mutex.unlock();
                        }
                    }
                }
            }
        }
    } catch (...) {}
}

void TriggerUiLocalization(); // Forward declaration for ChangeGameLanguage

void ReloadSettingsAndReinit(const std::string& newAiModelFile) {
    state.mutex.lock();
    state.aiModelName = newAiModelFile;
    state.mutex.unlock();
    
    // 1. Read existing settings.json
    std::string settingsPath = "settings.json";
    std::ifstream file(settingsPath);
    if (!file.is_open()) {
        settingsPath = "../settings.json";
        file.open(settingsPath);
    }
    
    nlohmann::json j;
    if (file.is_open()) {
        try {
            file >> j;
        } catch (...) {}
        file.close();
    }
    
    // 2. Update AIModel and save settings.json
    j["AIModel"] = newAiModelFile;
    std::ofstream outFile(settingsPath);
    if (outFile.is_open()) {
        outFile << j.dump(4);
        outFile.close();
        std::cout << "[Config] Saved new AIModel '" << newAiModelFile << "' to " << settingsPath << std::endl;
    }
    
    // 3. Reload settings parameters
    int maxRetries = 3;
    int bookRetries = 3;
    int retryDelayMs = 1000;
    int connectTimeout = 5;
    int requestTimeout = 15;
    
    if (j.contains("maxRetries") && j["maxRetries"].is_number()) {
        maxRetries = j["maxRetries"].get<int>();
    }
    if (j.contains("bookRetries") && j["bookRetries"].is_number()) {
        bookRetries = j["bookRetries"].get<int>();
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
    
    // 4. Re-instantiate AskAiExternal and configure it
    state.mutex.lock();
    state.aiClient = std::make_unique<AskAiExternal>(newAiModelFile);
    state.aiClient->setRetrySettings(maxRetries, retryDelayMs);
    state.aiClient->setTimeoutSettings(connectTimeout, requestTimeout);
    state.maxRetries = maxRetries;
    state.bookRetries = bookRetries;
    state.retryDelayMs = retryDelayMs;
    state.connectTimeout = connectTimeout;
    state.requestTimeout = requestTimeout;
    state.mutex.unlock();
    
    // 5. Trigger localization for the new model
    TriggerUiLocalization();
}

void ChangeGameLanguage(const std::string& newLanguage) {
    std::string trimmed = Trim(newLanguage);
    if (trimmed.empty()) return;
    
    // Try local matching first
    std::string normalized = GetNormalizedLanguageLocal(trimmed);
    
    if (!normalized.empty()) {
        state.mutex.lock();
        state.uiLocalized = false; // Synchronously set to false immediately!
        state.gameLanguage = normalized;
        state.modelState.gameLanguage = normalized;
        state.mutex.unlock();
        
        SaveLanguageToSettings(normalized);
        UpdateSystemPrompt();
        TriggerUiLocalization();
    } else {
        // Exotics / typos: Query the AI in background
        state.mutex.lock();
        state.uiLocalized = false; // Show pulsing loader screen
        state.mutex.unlock();
        
        std::thread([trimmed]() {
            state.aiClient->setSystemPrompt(state.modelState.promptAiLanguageNormalizer);
            std::string prompt = "Normalize the following user input to a standard single-word English language name (e.g. 'Spanish', 'French', 'Kazakh', 'Hebrew'). Correct any typos. Respond with ONLY the standard language name. If the input matches no valid language, respond with 'Unknown'. Input: '" + trimmed + "'";
            std::string aiResponse = state.aiClient->ask(prompt);
            aiResponse = Trim(aiResponse);
            
            if (!aiResponse.empty() && (aiResponse.front() == '"' || aiResponse.front() == '\'')) {
                aiResponse.erase(aiResponse.begin());
            }
            if (!aiResponse.empty() && (aiResponse.back() == '"' || aiResponse.back() == '\'')) {
                aiResponse.pop_back();
            }
            aiResponse = Trim(aiResponse);
            
            if (aiResponse.empty() || aiResponse.find("Error") != std::string::npos || aiResponse == "Unknown" || aiResponse == "unknown") {
                // Safety rollback to avoid breaking the game
                state.mutex.lock();
                state.uiLocalized = true; // hide loading screen
                GameState tempState = state.modelState;
                state.mutex.unlock();
                UpdateSystemPrompt(tempState, state.aiClient.get());
                std::cout << "[Config] AI returned Unknown/Error. Retaining previous active language: " << state.gameLanguage << std::endl;
                return;
            }
            
            state.mutex.lock();
            state.gameLanguage = aiResponse;
            state.modelState.gameLanguage = aiResponse;
            state.mutex.unlock();
            
            SaveLanguageToSettings(aiResponse);
            UpdateSystemPrompt();
            TriggerUiLocalization();
        }).detach();
    }
}

void TriggerUiLocalization() {
    state.mutex.lock();
    std::string activeLang = state.gameLanguage;
    if (activeLang == "English") {
        state.transitionPrefix = "Go to Chapter ";
        state.localizedUi.clear();
        state.uiLocalized = true;
        GameState tempState = state.modelState;
        state.mutex.unlock();
        
        UpdateSystemPrompt(tempState, state.aiClient.get());
        std::cout << "[Localization] English language fast-path: UI localization configured instantly." << std::endl;
        return;
    }
    state.uiLocalized = false; // Show pulsing loader screen for other languages
    state.mutex.unlock();
    
    // Check if options.json exists and contains translations for the active language
    bool loadedFromCache = false;
    std::string cacheFilename = "options.json";
    std::ifstream cacheFile(cacheFilename);
    if (!cacheFile.is_open()) {
        cacheFile.open("../" + cacheFilename);
    }
    
    if (cacheFile.is_open()) {
        try {
            nlohmann::json parsed;
            cacheFile >> parsed;
            cacheFile.close();
            
            if (parsed.contains("language") && parsed["language"].get<std::string>() == activeLang &&
                parsed.contains("transitionPrefix") && parsed["transitionPrefix"].is_string() &&
                parsed.contains("phrases") && parsed["phrases"].is_object()) {
                
                // Validate that the cache contains crucial recently added translation keys.
                // If the cache is stale (missing these keys), discard it to force re-translation.
                bool hasCrucialKeys = parsed["phrases"].contains("setup_step3_title") &&
                                      parsed["phrases"].contains("setup_step4_title") &&
                                      parsed["phrases"].contains("setup_wishes_done") &&
                                      parsed["phrases"].contains("btn_go_to_epilogue") &&
                                      parsed["phrases"].contains("btn_home") &&
                                      parsed["phrases"].contains("pacing_critical_title") &&
                                      parsed["phrases"].contains("status_retry") &&
                                      parsed["phrases"].contains("btn_force_next_chapter") &&
                                      parsed["phrases"].contains("header_chapter") &&
                                      parsed["phrases"].contains("header_of") &&
                                      parsed["phrases"].contains("header_epilogue") &&
                                      parsed["phrases"].contains("status_api_error");
                
                if (hasCrucialKeys) {
                    state.mutex.lock();
                    state.transitionPrefix = parsed["transitionPrefix"].get<std::string>();
                    if (parsed.contains("pacing_forced_conclusion_prompt") && parsed["pacing_forced_conclusion_prompt"].is_string()) {
                        state.modelState.pacingForcedConclusionPrompt = parsed["pacing_forced_conclusion_prompt"].get<std::string>();
                    }
                    if (parsed.contains("prompt_game_world_header") && parsed["prompt_game_world_header"].is_string())
                        state.modelState.promptGameWorldHeader = parsed["prompt_game_world_header"].get<std::string>();
                    if (parsed.contains("prompt_game_state_header") && parsed["prompt_game_state_header"].is_string())
                        state.modelState.promptGameStateHeader = parsed["prompt_game_state_header"].get<std::string>();
                    if (parsed.contains("prompt_current_chapter_label") && parsed["prompt_current_chapter_label"].is_string())
                        state.modelState.promptCurrentChapterLabel = parsed["prompt_current_chapter_label"].get<std::string>();
                    if (parsed.contains("prompt_previous_chapters_header") && parsed["prompt_previous_chapters_header"].is_string())
                        state.modelState.promptPreviousChaptersHeader = parsed["prompt_previous_chapters_header"].get<std::string>();
                    if (parsed.contains("prompt_chapter_summary_item") && parsed["prompt_chapter_summary_item"].is_string())
                        state.modelState.promptChapterSummaryItem = parsed["prompt_chapter_summary_item"].get<std::string>();
                    if (parsed.contains("prompt_chapter_details_header") && parsed["prompt_chapter_details_header"].is_string())
                        state.modelState.promptChapterDetailsHeader = parsed["prompt_chapter_details_header"].get<std::string>();
                    if (parsed.contains("prompt_ai_rules_header") && parsed["prompt_ai_rules_header"].is_string())
                        state.modelState.promptAiRulesHeader = parsed["prompt_ai_rules_header"].get<std::string>();
                    if (parsed.contains("prompt_ai_rule_options_format") && parsed["prompt_ai_rule_options_format"].is_string())
                        state.modelState.promptAiRuleOptionsFormat = parsed["prompt_ai_rule_options_format"].get<std::string>();
                    if (parsed.contains("prompt_ai_rule_chapter_transition") && parsed["prompt_ai_rule_chapter_transition"].is_string())
                        state.modelState.promptAiRuleChapterTransition = parsed["prompt_ai_rule_chapter_transition"].get<std::string>();
                    if (parsed.contains("prompt_ai_rule_language_enforcement") && parsed["prompt_ai_rule_language_enforcement"].is_string())
                        state.modelState.promptAiRuleLanguageEnforcement = parsed["prompt_ai_rule_language_enforcement"].get<std::string>();
                    if (parsed.contains("prompt_ai_final_chapter_warning") && parsed["prompt_ai_final_chapter_warning"].is_string())
                        state.modelState.promptAiFinalChapterWarning = parsed["prompt_ai_final_chapter_warning"].get<std::string>();
                    if (parsed.contains("prompt_ai_epilogue_writer") && parsed["prompt_ai_epilogue_writer"].is_string())
                        state.modelState.promptAiEpilogueWriter = parsed["prompt_ai_epilogue_writer"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_generator") && parsed["prompt_ai_book_generator"].is_string())
                        state.modelState.promptAiBookGenerator = parsed["prompt_ai_book_generator"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_gen_length") && parsed["prompt_ai_book_gen_length"].is_string())
                        state.modelState.promptAiBookGenLength = parsed["prompt_ai_book_gen_length"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_gen_length_default") && parsed["prompt_ai_book_gen_length_default"].is_string())
                        state.modelState.promptAiBookGenLengthDefault = parsed["prompt_ai_book_gen_length_default"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_gen_genre") && parsed["prompt_ai_book_gen_genre"].is_string())
                        state.modelState.promptAiBookGenGenre = parsed["prompt_ai_book_gen_genre"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_gen_fidelity") && parsed["prompt_ai_book_gen_fidelity"].is_string())
                        state.modelState.promptAiBookGenFidelity = parsed["prompt_ai_book_gen_fidelity"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_gen_custom") && parsed["prompt_ai_book_gen_custom"].is_string())
                        state.modelState.promptAiBookGenCustom = parsed["prompt_ai_book_gen_custom"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_gen_rules") && parsed["prompt_ai_book_gen_rules"].is_string())
                        state.modelState.promptAiBookGenRules = parsed["prompt_ai_book_gen_rules"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_block_hydration") && parsed["prompt_ai_book_block_hydration"].is_string())
                        state.modelState.promptAiBookBlockHydration = parsed["prompt_ai_book_block_hydration"].get<std::string>();
                    if (parsed.contains("prompt_ai_summary_compressor") && parsed["prompt_ai_summary_compressor"].is_string())
                        state.modelState.promptAiSummaryCompressor = parsed["prompt_ai_summary_compressor"].get<std::string>();
                    if (parsed.contains("ui_chapters_range_label") && parsed["ui_chapters_range_label"].is_string())
                        state.modelState.uiChaptersRangeLabel = parsed["ui_chapters_range_label"].get<std::string>();
                    if (parsed.contains("prompt_ai_translator") && parsed["prompt_ai_translator"].is_string())
                        state.modelState.promptAiTranslator = parsed["prompt_ai_translator"].get<std::string>();
                    if (parsed.contains("prompt_ai_localizer") && parsed["prompt_ai_localizer"].is_string())
                        state.modelState.promptAiLocalizer = parsed["prompt_ai_localizer"].get<std::string>();
                    if (parsed.contains("prompt_ai_summarizer") && parsed["prompt_ai_summarizer"].is_string())
                        state.modelState.promptAiSummarizer = parsed["prompt_ai_summarizer"].get<std::string>();
                    if (parsed.contains("prompt_ai_language_normalizer") && parsed["prompt_ai_language_normalizer"].is_string())
                        state.modelState.promptAiLanguageNormalizer = parsed["prompt_ai_language_normalizer"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_blueprint_gen") && parsed["prompt_ai_book_blueprint_gen"].is_string())
                        state.modelState.promptAiBookBlueprintGen = parsed["prompt_ai_book_blueprint_gen"].get<std::string>();
                    if (parsed.contains("prompt_ai_book_block_hydration") && parsed["prompt_ai_book_block_hydration"].is_string())
                        state.modelState.promptAiBookBlockHydration = parsed["prompt_ai_book_block_hydration"].get<std::string>();
                    state.localizedUi.clear();
                    for (auto& el : parsed["phrases"].items()) {
                        if (el.value().is_string()) {
                            std::string k = el.key();
                            std::string v = el.value().get<std::string>();
                            
                            // Prevent empty rectangle rendering by removing globe emoji
                            size_t pos;
                            while ((pos = v.find("🌐")) != std::string::npos) {
                                v.erase(pos, 4); // 🌐 is 4 bytes in UTF-8
                            }
                            
                            if (k == "lang_btn_prefix") {
                                continue;
                            }
                            
                            state.localizedUi[k] = v;
                        }
                    }
                    state.uiLocalized = true;
                    GameState tempState = state.modelState;
                    state.mutex.unlock();
                    
                    UpdateSystemPrompt(tempState, state.aiClient.get());
                    std::cout << "[Localization] UI localization loaded successfully from options.json cache for '" << activeLang << "'." << std::endl;
                    loadedFromCache = true;
                } else {
                    std::cout << "[Localization] Cache is missing crucial dynamic translation keys (e.g. setup_step3_title), discarding cache..." << std::endl;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[Localization] Failed to parse options.json cache: " << e.what() << std::endl;
        }
    }

    if (loadedFromCache) {
        return;
    }
    
    std::thread([activeLang]() {
        // AI query 1: Translate prefix
        state.aiClient->setSystemPrompt(state.modelState.promptAiTranslator);
        std::string prefixPrompt = "Translate the phrase 'Перейти к Главе ' into the active language of the game: '" + activeLang + "'. Return ONLY the translated phrase (e.g. 'Go to Chapter ' or 'Passer au Chapitre '). Keep the trailing space if appropriate.";
        std::string prefixResp = "";
        
        int maxAttempts = state.maxRetries;
        int delayMs = state.retryDelayMs;
        int attempt = 0;
        bool success = false;
        
        while (attempt < maxAttempts) {
            prefixResp = state.aiClient->ask(prefixPrompt);
            prefixResp = Trim(prefixResp);
            if (!prefixResp.empty() && prefixResp.find("Error") == std::string::npos) {
                success = true;
                break;
            }
            attempt++;
            if (attempt < maxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }
        
        if (!success || prefixResp.empty()) {
            prefixResp = "Go to Chapter ";
        } else {
            if (!prefixResp.empty() && (prefixResp.front() == '"' || prefixResp.front() == '\'')) {
                prefixResp.erase(prefixResp.begin());
            }
            if (!prefixResp.empty() && (prefixResp.back() == '"' || prefixResp.back() == '\'')) {
                prefixResp.pop_back();
            }
            prefixResp = Trim(prefixResp);
            if (prefixResp.empty()) {
                prefixResp = "Go to Chapter ";
            }
        }
        
        // AI query 2: Translate UI elements
        state.aiClient->setSystemPrompt(state.modelState.promptAiLocalizer);
        std::string jsonPrompt = "You are a professional localizer. Translate the following C++ game UI key-value text pairs into the language: '" + activeLang + "'.\n"
                                 "You MUST return a valid JSON object map containing exactly the same keys with their translated values. Do NOT add any extra fields, markdown blocks, formatting tags, or conversational text. Return ONLY the JSON object string.\n\n"
                                 "JSON data to translate:\n"
                                 "{\n"
                                 "  \"continue_title\": \"CONTINUE CURRENT BOOK?\",\n"
                                 "  \"book_detected\": \"Saved book detected:\",\n"
                                 "  \"btn_yes\": \"1. Yes, continue the adventure\",\n"
                                 "  \"btn_no\": \"2. No, start a new book\",\n"
                                 "  \"load_title\": \"LOAD NEW BOOK\",\n"
                                 "  \"load_prompt\": \"Click below to select a book file, or drag-and-drop it:\",\n"
                                 "  \"btn_select_file\": \"Choose Book File (.txt, .epub, .docx, .mobi, .fb2)\",\n"
                                 "  \"generating_title\": \"AI IS WEAVING THE GAME...\",\n"
                                 "  \"generating_desc\": \"Please wait. The AI is reading the text, creating chapters, and setting up the game world...\",\n"
                                 "  \"err_file_not_found\": \"Error: File not found or could not be opened!\",\n"
                                 "  \"err_ai_gen\": \"AI generation failed. Please try again.\",\n"
                                 "  \"status_init\": \"Initializing file load...\",\n"
                                 "  \"status_ai\": \"AI is generating the quest storyline...\",\n"
                                 "  \"status_chapters\": \"Compiling world chapters and lore...\",\n"
                                 "  \"status_validation\": \"Performing final structure checks...\",\n"
                                 "  \"status_done\": \"Book completed! Loading game...\",\n"
                                 "  \"setup_welcome\": \"Greetings, traveler! I am the AI-Architect of your new adventure. Let's customize the world you will embark upon.\",\n"
                                 "  \"setup_step1_title\": \"Step 1 of 4: Game Length\",\n"
                                 "  \"setup_step1_desc\": \"What scale of adventure do you wish to experience? Choose a preset option below or type your own (e.g., \\\"I want 15 chapters\\\" or \\\"a short blitz of 2 chapters\\\").\",\n"
                                 "  \"setup_len_preset1\": \"Blitz (3-4 chapters)\",\n"
                                 "  \"setup_len_preset2\": \"Standard (5-7 chapters)\",\n"
                                 "  \"setup_len_preset3\": \"Large adventure (8-10 chapters)\",\n"
                                 "  \"setup_len_preset4\": \"Epic (12-14 chapters)\",\n"
                                 "  \"setup_len_preset5\": \"Saga (18-20 chapters)\",\n"
                                 "  \"setup_step2_title\": \"Step 2 of 4: Genre and Atmosphere\",\n"
                                 "  \"setup_step2_desc\": \"What tone, setting, and atmosphere shall we establish? Select a preset genre or describe your own wishes (e.g., \\\"cyberpunk with elements of slavic folklore\\\").\",\n"
                                 "  \"setup_genre_preset1\": \"Fantasy\",\n"
                                 "  \"setup_genre_preset2\": \"Detective\",\n"
                                 "  \"setup_genre_preset3\": \"Survival\",\n"
                                 "  \"setup_genre_preset4\": \"Sci-Fi\",\n"
                                 "  \"setup_genre_preset5\": \"Classic\",\n"
                                 "  \"setup_step3_title\": \"Step 3 of 4: Story Fidelity\",\n"
                                 "  \"setup_step3_desc\": \"How strictly should we follow the book's canon plot? Select a preset option or write your own condition (e.g., \\\"I want to play as the main antagonist\\\" or \\\"save my friend in Chapter 1\\\").\",\n"
                                 "  \"setup_fid_preset1\": \"Canon (close to the text)\",\n"
                                 "  \"setup_fid_preset2\": \"Alternate plot (free story)\",\n"
                                 "  \"setup_step4_title\": \"Step 4 of 4: Custom Wishes\",\n"
                                 "  \"setup_step4_desc\": \"Are there any special rules, items, companions, or ideas you'd like to add? Write them in the text input box. If you have no wishes, just click the button below.\",\n"
                                 "  \"setup_wishes_done\": \"Done (Start generation)\",\n"
                                 "  \"setup_back_to_step1\": \"Back to Step 1\",\n"
                                 "  \"setup_input_placeholder\": \"Type your choice and press Enter...\",\n"
                                 "  \"game_thinking_placeholder\": \"AI is weaving the story...\",\n"
                                 "  \"game_input_placeholder\": \"Type your action and press Enter...\",\n"
                                 "  \"lang_input_placeholder\": \"Type language...\",\n"
                                 "  \"death_title\": \"HERO IS DEAD\",\n"
                                 "  \"btn_restart_chapter\": \"Restart chapter\",\n"
                                 "  \"prompt_restart_chapter\": \"Restart the current chapter from the beginning. Describe the surroundings and offer choice options.\",\n"
                                 "  \"victory_title\": \"CONGRATULATIONS! YOU HAVE COMPLETED THE BOOK!\",\n"
                                 "  \"btn_restart_adventure\": \"Restart Adventure\",\n"
                                 "  \"btn_go_to_epilogue\": \"Proceed to Epilogue\",\n"
                                 "  \"btn_choose_another_book\": \"Choose Another Book\",\n"
                                 "  \"btn_home\": \"Main Menu\",\n"
                                 "  \"btn_force_next_chapter\": \"Complete chapter and proceed\",\n"
                                 "  \"header_chapter\": \"Chapter\",\n"
                                 "  \"header_of\": \"of\",\n"
                                 "  \"header_epilogue\": \"Epilogue\",\n"
                                 "  \"pacing_critical_title\": \"=== CRITICAL PACING INSTRUCTIONS FOR THIS TURN ===\",\n"
                                 "  \"pacing_turn_status\": \"Current Player Turn Count in this Chapter: {turns}\\nTarget Chapter Duration: {min} to {max} player turns.\",\n"
                                 "  \"pacing_rule_early\": \"- The player has taken only {turns} choices in this chapter. This chapter MUST last at least {min} choices.\\n- Do NOT resolve the main objectives of this chapter yet. You MUST introduce complications, side events, dialogue, or obstacles to prolong the scene.\\n- You MUST NOT transition or output the '<next_chapter>' tag under any circumstances on this turn.\",\n"
                                 "  \"pacing_rule_mid\": \"- The player has taken {turns} choices. You may now begin to guide the plot towards resolving the main objectives of this chapter.\\n- If the player's choices successfully resolve the objectives, you can conclude the chapter on this turn or the next.\",\n"
                                 "  \"pacing_rule_limit\": \"- The player has taken {turns} choices, reaching the chapter turn limit of {max}.\\n- You MUST resolve the main objectives of this chapter on this turn, narrate the transition to the next chapter, and append the '<next_chapter>' tag.\",\n"
                                 "  \"status_retry\": \"JSON parsing error. Retrying (attempt {attempt} of {max})...\",\n"
                                 "  \"status_api_error\": \"Error: Failed to receive response from AI. Please check your connection.\",\n"
                                 "  \"apikey_guide\": \"[ Use the bottom input line for API Key, Enter to confirm ]\",\n"
                                 "  \"apikey_placeholder\": \"Type API Key...\",\n"
                                 "  \"apikey_select_title\": \"SELECT AI MODEL\",\n"
                                 "  \"apikey_select_prompt\": \"Select AI configuration for the game:\",\n"
                                 "  \"apikey_back\": \"Back\"\n"
                                 "}";
        
        std::string jsonResp = "";
        attempt = 0;
        success = false;
        
        while (attempt < maxAttempts) {
            jsonResp = state.aiClient->ask(jsonPrompt);
            if (!jsonResp.empty() && jsonResp.find("Error") == std::string::npos) {
                success = true;
                break;
            }
            attempt++;
            if (attempt < maxAttempts) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }
        
        std::map<std::string, std::string> translatedUi;
        if (success && !jsonResp.empty()) {
            size_t startBrace = jsonResp.find('{');
            size_t endBrace = jsonResp.rfind('}');
            if (startBrace != std::string::npos && endBrace != std::string::npos && endBrace > startBrace) {
                jsonResp = jsonResp.substr(startBrace, endBrace - startBrace + 1);
            }
            
            try {
                nlohmann::json parsed = nlohmann::json::parse(jsonResp);
                for (auto& el : parsed.items()) {
                    if (el.value().is_string()) {
                        std::string k = el.key();
                        std::string v = el.value().get<std::string>();
                        
                        // Prevent empty rectangle rendering by removing globe emoji
                        size_t pos;
                        while ((pos = v.find("🌐")) != std::string::npos) {
                            v.erase(pos, 4); // 🌐 is 4 bytes in UTF-8
                        }
                        
                        if (k == "lang_btn_prefix") {
                            continue;
                        }
                        
                        translatedUi[k] = v;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[TriggerUiLocalization] Failed to parse translation JSON: " << e.what() << std::endl;
                std::cerr << "[TriggerUiLocalization] Raw AI response was: " << jsonResp << std::endl;
            }
        }
        state.mutex.lock();
        state.transitionPrefix = prefixResp;
        if (!translatedUi.empty()) {
            state.localizedUi = translatedUi;
        }
        state.uiLocalized = true;
        GameState tempState = state.modelState;
        
        // Save the newly translated phrases to options.json cache
        nlohmann::json cacheJson;
        cacheJson["language"] = activeLang;
        cacheJson["transitionPrefix"] = prefixResp;
        cacheJson["pacing_forced_conclusion_prompt"] = state.modelState.pacingForcedConclusionPrompt;
        cacheJson["prompt_game_world_header"] = state.modelState.promptGameWorldHeader;
        cacheJson["prompt_game_state_header"] = state.modelState.promptGameStateHeader;
        cacheJson["prompt_current_chapter_label"] = state.modelState.promptCurrentChapterLabel;
        cacheJson["prompt_previous_chapters_header"] = state.modelState.promptPreviousChaptersHeader;
        cacheJson["prompt_chapter_summary_item"] = state.modelState.promptChapterSummaryItem;
        cacheJson["prompt_chapter_details_header"] = state.modelState.promptChapterDetailsHeader;
        cacheJson["prompt_ai_rules_header"] = state.modelState.promptAiRulesHeader;
        cacheJson["prompt_ai_rule_options_format"] = state.modelState.promptAiRuleOptionsFormat;
        cacheJson["prompt_ai_rule_chapter_transition"] = state.modelState.promptAiRuleChapterTransition;
        cacheJson["prompt_ai_rule_language_enforcement"] = state.modelState.promptAiRuleLanguageEnforcement;
        cacheJson["prompt_ai_final_chapter_warning"] = state.modelState.promptAiFinalChapterWarning;
        cacheJson["prompt_ai_epilogue_writer"] = state.modelState.promptAiEpilogueWriter;
        cacheJson["prompt_ai_book_generator"] = state.modelState.promptAiBookGenerator;
        cacheJson["prompt_ai_book_gen_length"] = state.modelState.promptAiBookGenLength;
        cacheJson["prompt_ai_book_gen_length_default"] = state.modelState.promptAiBookGenLengthDefault;
        cacheJson["prompt_ai_book_gen_genre"] = state.modelState.promptAiBookGenGenre;
        cacheJson["prompt_ai_book_gen_fidelity"] = state.modelState.promptAiBookGenFidelity;
        cacheJson["prompt_ai_book_gen_custom"] = state.modelState.promptAiBookGenCustom;
        cacheJson["prompt_ai_book_gen_rules"] = state.modelState.promptAiBookGenRules;
        cacheJson["prompt_ai_translator"] = state.modelState.promptAiTranslator;
        cacheJson["prompt_ai_localizer"] = state.modelState.promptAiLocalizer;
        cacheJson["prompt_ai_summarizer"] = state.modelState.promptAiSummarizer;
        cacheJson["prompt_ai_language_normalizer"] = state.modelState.promptAiLanguageNormalizer;
        cacheJson["prompt_ai_book_blueprint_gen"] = state.modelState.promptAiBookBlueprintGen;
        cacheJson["prompt_ai_book_block_hydration"] = state.modelState.promptAiBookBlockHydration;
        cacheJson["prompt_ai_summary_compressor"] = state.modelState.promptAiSummaryCompressor;
        cacheJson["ui_chapters_range_label"] = state.modelState.uiChaptersRangeLabel;
        nlohmann::json phrasesObj = nlohmann::json::object();
        for (const auto& pair : state.localizedUi) {
            if (pair.first == "lang_btn_prefix") continue;
            phrasesObj[pair.first] = pair.second;
        }
        cacheJson["phrases"] = phrasesObj;
        state.mutex.unlock();
        
        // Write options.json to disk
        std::ofstream cacheOut("options.json");
        if (cacheOut.is_open()) {
            cacheOut << cacheJson.dump(4);
            cacheOut.close();
            std::cout << "[Localization] Saved new translations to options.json cache." << std::endl;
        }
        
        // Keep parent copy synchronized
        std::ifstream parentSettings("../settings.json");
        if (parentSettings.is_open()) {
            parentSettings.close();
            std::ofstream parentCacheOut("../options.json");
            if (parentCacheOut.is_open()) {
                parentCacheOut << cacheJson.dump(4);
                parentCacheOut.close();
                std::cout << "[Localization] Saved synchronized options.json copy to parent directory." << std::endl;
            }
        }
        
        UpdateSystemPrompt(tempState, state.aiClient.get());
        std::cout << "[Localization] Dynamic translation thread completed successfully for '" << activeLang << "'." << std::endl;
    }).detach();
}

inline std::string GetUiText(const std::string& key) {
    state.mutex.lock();
    auto it = state.localizedUi.find(key);
    if (it != state.localizedUi.end() && !it->second.empty()) {
        std::string val = it->second;
        state.mutex.unlock();
        return val;
    }
    state.mutex.unlock();
    
    if (key == "victory_title") return "CONGRATULATIONS! YOU HAVE COMPLETED THE BOOK!";
    if (key == "btn_restart_adventure") return "Restart Adventure";
    if (key == "btn_home") return "Main Menu";
    if (key == "btn_go_to_epilogue") return "Proceed to Epilogue";
    if (key == "btn_choose_another_book") return "Choose another book";
    if (key == "btn_force_next_chapter") return "Complete chapter and proceed";
    if (key == "header_chapter") return "Chapter";
    if (key == "header_of") return "of";
    if (key == "header_epilogue") return "Epilogue";
    
    if (key == "pacing_critical_title") return "=== CRITICAL PACING INSTRUCTIONS FOR THIS TURN ===";
    if (key == "pacing_turn_status") return "Current Player Turn Count in this Chapter: {turns}\nTarget Chapter Duration: {min} to {max} player turns.";
    if (key == "pacing_rule_early") return "- The player has taken only {turns} choices in this chapter. This chapter MUST last at least {min} choices.\n- Do NOT resolve the main objectives of this chapter yet. You MUST introduce complications, side events, dialogue, or obstacles to prolong the scene.\n- You MUST NOT transition or output the '<next_chapter>' tag under any circumstances on this turn.";
    if (key == "pacing_rule_mid") return "- The player has taken {turns} choices. You may now begin to guide the plot towards resolving the main objectives of this chapter.\n- If the player's choices successfully resolve the objectives, you can conclude the chapter on this turn or the next.";
    if (key == "pacing_rule_limit") return "- The player has taken {turns} choices, reaching the chapter turn limit of {max}.\n- You MUST resolve the main objectives of this chapter on this turn, narrate the transition to the next chapter, and append the '<next_chapter>' tag.";
    
    if (key == "lang_btn_prefix") return "Language: ";
    
    if (key == "continue_title") return "CONTINUE CURRENT BOOK?";
    if (key == "book_detected") return "Saved book detected:";
    if (key == "btn_yes") return "1. Yes, continue the adventure";
    if (key == "btn_no") return "2. No, start a new book";
    if (key == "load_title") return "LOAD NEW BOOK";
    if (key == "load_prompt") return "Click below to select a book file, or drag-and-drop it:";
    if (key == "btn_select_file") return "Choose Book File (.txt, .epub, .docx, .mobi, .fb2)";
    if (key == "generating_title") return "AI IS WEAVING THE GAME...";
    if (key == "generating_desc") return "Please wait. The AI is reading the text, creating chapters, and setting up the game world...";
    if (key == "err_file_not_found") return "Error: File not found or could not be opened!";
    if (key == "err_ai_gen") return "AI generation failed. Please try again.";
    
    if (key == "status_init") return "Initializing file load...";
    if (key == "status_api_error") return "Error: Failed to receive response from AI. Please check your connection.";
    if (key == "status_retry") return "JSON parsing error. Retrying (attempt {attempt} of {max})...";
    if (key == "status_ai") return "AI is generating the quest storyline...";
    if (key == "status_chapters") return "Compiling world chapters and lore...";
    if (key == "status_validation") return "Performing final structure checks...";
    if (key == "status_done") return "Book completed! Loading game...";
    
    if (key == "setup_welcome") return "Greetings, traveler! I am the AI-Architect of your new adventure. Let's customize the world you will embark upon.";
    if (key == "setup_step1_title") return "Step 1 of 4: Game Length";
    if (key == "setup_step1_desc") return "What scale of adventure do you wish to experience? Choose a preset option below or type your own (e.g., \"I want 15 chapters\" or \"a short blitz of 2 chapters\").";
    if (key == "setup_len_preset1") return "Blitz (3-4 chapters)";
    if (key == "setup_len_preset2") return "Standard (5-7 chapters)";
    if (key == "setup_len_preset3") return "Large adventure (8-10 chapters)";
    if (key == "setup_len_preset4") return "Epic (12-14 chapters)";
    if (key == "setup_len_preset5") return "Saga (18-20 chapters)";
    
    if (key == "setup_step2_title") return "Step 2 of 4: Genre and Atmosphere";
    if (key == "setup_step2_desc") return "What tone, setting, and atmosphere shall we establish? Select a preset genre or describe your own wishes (e.g., \"cyberpunk with elements of slavic folklore\").";
    if (key == "setup_genre_preset1") return "Fantasy";
    if (key == "setup_genre_preset2") return "Detective";
    if (key == "setup_genre_preset3") return "Survival";
    if (key == "setup_genre_preset4") return "Sci-Fi";
    if (key == "setup_genre_preset5") return "Classic";
    
    if (key == "setup_step3_title") return "Step 3 of 4: Story Fidelity";
    if (key == "setup_step3_desc") return "How strictly should we follow the book's canon plot? Select a preset option or write your own condition (e.g., \"I want to play as the main antagonist\" or \"save my friend in Chapter 1\").";
    if (key == "setup_fid_preset1") return "Canon (close to the text)";
    if (key == "setup_fid_preset2") return "Alternate plot (free story)";
    
    if (key == "setup_step4_title") return "Step 4 of 4: Custom Wishes";
    if (key == "setup_step4_desc") return "Are there any special rules, items, companions, or ideas you'd like to add? Write them in the text input box. If you have no wishes, just click the button below.";
    if (key == "setup_wishes_done") return "Done (Start generation)";
    if (key == "setup_input_placeholder") return "Type your choice and press Enter...";
    if (key == "game_thinking_placeholder") return "AI is weaving the story...";
    if (key == "game_input_placeholder") return "Type your action and press Enter...";
    if (key == "lang_input_placeholder") return "Type language...";
    
    if (key == "death_title") return "HERO IS DEAD";
    if (key == "btn_restart_chapter") return "Restart chapter";
    if (key == "prompt_restart_chapter") return "Restart the current chapter from the beginning. Describe the surroundings and offer choice options.";
    if (key == "setup_back_to_step1") return "Back to Step 1";
    
    if (key == "apikey_guide") return "[ Use the bottom input line for API Key, Enter to confirm ]";
    if (key == "apikey_placeholder") return "Type API Key...";
    if (key == "apikey_select_title") return "SELECT AI MODEL";
    if (key == "apikey_select_prompt") return "Select AI configuration for the game:";
    if (key == "apikey_back") return "Back";
    
    return "";
}

std::string OpenFileDialogImpl() {
#if defined(__ANDROID__) || defined(__IPHONEOS__) || defined(ANDROID) || defined(IOS)
    // On mobile platforms, trigger the internal book scanner and selection screen
    ScanAvailableBooks();
    state.mutex.lock();
    state.appState = APP_STATE_SELECT_BOOK;
    state.bookSelectScrollOffset = 0;
    state.mutex.unlock();
    return "";
#else
#ifdef _WIN32
    OPENFILENAMEW ofn;       // common dialog box structure
    wchar_t szFile[512] = {0};  // buffer for file name

    // Initialize OPENFILENAMEW
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

    if (GetOpenFileNameW(&ofn) == TRUE) {
        return BookConverter::WideToUTF8(szFile);
    }
    return "";
#elif defined(__APPLE__)
    char path[1024] = {0};
    FILE *f = popen(BookConverter::GetMacOSScript(), "r");
    if (f) {
        if (fgets(path, sizeof(path), f)) {
            std::string p(path);
            if (!p.empty() && p.back() == '\n') p.pop_back();
            pclose(f);
            return p;
        }
        pclose(f);
    }
    return "";
#elif defined(__linux__)
    char path[1024] = {0};
    FILE *f = popen((std::string("zenity --file-selection --file-filter='") + BookConverter::GetZenityFilter() + "' 2>/dev/null").c_str(), "r");
    if (f) {
        if (fgets(path, sizeof(path), f)) {
            std::string p(path);
            if (!p.empty() && p.back() == '\n') p.pop_back();
            pclose(f);
            return p;
        }
        pclose(f);
    }
    return "";
#else
    return "";
#endif
#endif
}

std::string OpenFileDialog() {
    state.fileDialogActive = true;
    std::string path = OpenFileDialogImpl();
    state.fileDialogActive = false;
    SDL_PumpEvents();
    SDL_FlushEvent(SDL_DROPFILE);
    return path;
}

// Score-based Runtime System Font Loader
std::string GetSystemFontPath() {
    std::vector<std::string> searchPaths;
    const char* winDir = std::getenv("SystemRoot");
    if (!winDir) winDir = std::getenv("WINDIR");
    if (winDir) {
        std::string base(winDir);
        searchPaths.push_back(base + "\\Fonts\\segoeui.ttf");
        searchPaths.push_back(base + "\\Fonts\\arial.ttf");
        searchPaths.push_back(base + "\\Fonts\\calibri.ttf");
        searchPaths.push_back(base + "\\Fonts\\tahoma.ttf");
    }
    searchPaths.push_back("assets/font.ttf"); // local high-fidelity OFL Rubik font
    
    std::string bestFont = "assets/font.ttf";
    int bestScore = 0;
    
    for (const auto& path : searchPaths) {
        TTF_Font* font = TTF_OpenFont(path.c_str(), 16);
        if (font) {
            int score = 0;
            // Latin coverage
            if (TTF_GlyphIsProvided(font, 'a')) score += 1;
            // Cyrillic coverage
            if (TTF_GlyphIsProvided(font, 0x0430)) score += 10;
            // Hebrew coverage
            if (TTF_GlyphIsProvided(font, 0x05E9)) score += 100;
            // Arabic coverage
            if (TTF_GlyphIsProvided(font, 0x0627)) score += 1000;
            
            if (score > bestScore) {
                bestScore = score;
                bestFont = path;
            }
            TTF_CloseFont(font);
        }
    }
    std::cout << "[FontLoader] Loaded Font: " << bestFont << " (Coverage Score: " << bestScore << ")" << std::endl;
    return bestFont;
}

void ApplyFontScale() {
    std::string fontPath = GetSystemFontPath();
    int baseTitle = 24;
    int baseMessage = 18;
    int baseUI = 16;
    int baseSmallUI = 13;
    
    int titleSize = std::max(12, baseTitle + state.fontSizeOffset);
    int messageSize = std::max(10, baseMessage + state.fontSizeOffset);
    int uiSize = std::max(9, baseUI + state.fontSizeOffset);
    int smallUiSize = std::max(8, baseSmallUI + state.fontSizeOffset);
    
    state.fontTitle.reset(TTF_OpenFont(fontPath.c_str(), titleSize));
    state.fontMessage.reset(TTF_OpenFont(fontPath.c_str(), messageSize));
    state.fontUI.reset(TTF_OpenFont(fontPath.c_str(), uiSize));
    state.fontSmallUI.reset(TTF_OpenFont(fontPath.c_str(), smallUiSize));
    
    SyncModelToUi();
}

void AddArchitectBubble(const std::string& text) {
    ChatMessageData msg;
    msg.sender = "AI";
    msg.text = text;
    state.modelState.messages.push_back(msg);
}

void TriggerSetupStep(int step) {
    state.setupStep = step;
    state.modelState.activeChoices.clear();
    
    if (step == 0) {
        AddArchitectBubble(GetUiText("setup_welcome") + "\n\n"
                           "**" + GetUiText("setup_step1_title") + "**\n" +
                           GetUiText("setup_step1_desc"));
        state.modelState.activeChoices = {
            GetUiText("setup_len_preset1"),
            GetUiText("setup_len_preset2"),
            GetUiText("setup_len_preset3"),
            GetUiText("setup_len_preset4"),
            GetUiText("setup_len_preset5")
        };
    } else if (step == 1) {
        AddArchitectBubble("**" + GetUiText("setup_step2_title") + "**\n" +
                           GetUiText("setup_step2_desc"));
        
        state.mutex.lock();
        auto dynamicGenres = state.setupDynamicGenres;
        state.mutex.unlock();
        
        if (!dynamicGenres.empty()) {
            state.modelState.activeChoices = dynamicGenres;
        } else {
            state.modelState.activeChoices = {
                GetUiText("setup_genre_preset1"),
                GetUiText("setup_genre_preset2"),
                GetUiText("setup_genre_preset3"),
                GetUiText("setup_genre_preset4"),
                GetUiText("setup_genre_preset5")
            };
        }
        state.modelState.activeChoices.push_back(GetUiText("setup_back_to_step1"));
    } else if (step == 2) {
        AddArchitectBubble("**" + GetUiText("setup_step3_title") + "**\n" +
                           GetUiText("setup_step3_desc"));
        state.modelState.activeChoices = {
            GetUiText("setup_fid_preset1"),
            GetUiText("setup_fid_preset2"),
            GetUiText("setup_back_to_step1")
        };
    } else if (step == 3) {
        AddArchitectBubble("**" + GetUiText("setup_step4_title") + "**\n" +
                           GetUiText("setup_step4_desc"));
        state.modelState.activeChoices = {
            GetUiText("setup_wishes_done"),
            GetUiText("setup_back_to_step1")
        };
    }
    
    SyncModelToUi();
    state.scrollToBottom = true;
}

void SubmitSetupChoice(const std::string& choiceText) {
    if (state.soundEffect && state.mixOk) {
        Mix_PlayChannel(-1, state.soundEffect.get(), 0);
    }

    ChatMessageData userMsg;
    userMsg.sender = "User";
    userMsg.text = choiceText;
    state.modelState.messages.push_back(userMsg);

    if (choiceText == GetUiText("setup_back_to_step1")) {
        TriggerSetupStep(0);
        return;
    }

    if (state.setupStep == 0) {
        state.chosenLengthText = choiceText;
        TriggerSetupStep(1);
    } else if (state.setupStep == 1) {
        state.chosenGenreText = choiceText;
        TriggerSetupStep(2);
    } else if (state.setupStep == 2) {
        state.chosenFidelityText = choiceText;
        TriggerSetupStep(3);
    } else if (state.setupStep == 3) {
        if (choiceText == GetUiText("setup_wishes_done")) {
            state.chosenCustomWishesText = "";
        } else {
            state.chosenCustomWishesText = choiceText;
        }
        StartBookGeneration(state.txtPath);
    }
}

void RestartAdventure() {
    state.mutex.lock();
    state.modelState.gameWon = false;
    state.modelState.gameOver = false;
    state.modelState.currentChapter = 1;
    state.modelState.chapterSummaries.clear();
    state.modelState.messages.clear();
    state.modelState.activeChoices.clear();
    state.uiMessages.clear();
    state.uiActiveChoices.clear();
    state.inputText = "";
    
    std::string startQ = state.modelState.bookStartPrompt.empty() ? GetStartPrompt(state.gameLanguage) : state.modelState.bookStartPrompt;
    
    // Pre-parse the choices instantly on startup and set them up as active choices
    std::vector<std::string> options = ExtractAndStripOptions(startQ);
    if (options.empty()) {
        options = { IsRussianLanguage(state.gameLanguage) ? "Продолжить историю" : "Continue story" };
    }
    
    // Reconstruct perfect starting response as our very first chat message history
    std::string perfectStart = ReconstructPerfectAiResponse(startQ, options);
    
    ChatMessageData aiMsg;
    aiMsg.sender = "AI";
    aiMsg.text = perfectStart;
    state.modelState.messages.push_back(aiMsg);
    state.modelState.activeChoices = options;
    
    UpdateSystemPrompt(state.modelState, state.aiClient.get());
    SyncModelToUi();
    state.mutex.unlock();
    
    SaveGame();
}

void SubmitInputText() {
    if (state.appState == APP_STATE_SETUP) {
        if (!state.inputText.empty()) {
            std::string inputVal = state.inputText;
            state.inputText = "";
            SubmitSetupChoice(inputVal);
        }
    } else if (state.appState == APP_STATE_GAMEPLAY) {
        if (state.modelState.gameWon) {
            RestartAdventure();
        } else if (state.modelState.gameOver) {
            state.modelState.gameOver = false;
            state.modelState.messages.clear();
            state.modelState.pendingNextChapter = -1;
            SaveGame();
            UpdateSystemPrompt();
            SubmitQuery(GetUiText("prompt_restart_chapter"), false, false);
        } else if (!state.inputText.empty() && !state.aiThinking) {
            SubmitQuery(state.inputText);
            state.inputText = "";
        }
    }
}

void InitAdventureSetup(const std::string& filePath) {
    // Wait for the startup translation thread to finish/fail (up to 3 seconds)
    int waitLimitMs = 3000;
    while (waitLimitMs > 0) {
        state.mutex.lock();
        bool localized = state.uiLocalized;
        state.mutex.unlock();
        if (localized) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waitLimitMs -= 50;
    }

    // Always start a new book from the beginning (reset save.json and ../save.json)
    std::remove("save.json");
    std::remove("../save.json");

    // Auto-convert epub/fb2/docx/mobi to plain text before setup
    std::string actualFilePath = filePath;
    {
        std::string ext = BookConverter::GetExt(filePath);
        if (ext != ".txt" && ext != ".json") {
            std::cout << "[BookConverter] Converting " << ext << " file: " << filePath << std::endl;
            std::string tmpPath = BookConverter::ConvertBookToTempTxt(filePath);
            if (!tmpPath.empty()) {
                std::cout << "[BookConverter] Conversion successful -> " << tmpPath << std::endl;
                actualFilePath = tmpPath;
            } else {
                std::cerr << "[BookConverter] Conversion failed for: " << filePath << std::endl;
            }
        }
    }

    state.mutex.lock();
    state.txtPath = actualFilePath;

    state.appState = APP_STATE_SETUP;
    state.setupStep = 0;
    state.chosenLengthText = "";
    state.chosenGenreText = "";
    state.chosenFidelityText = "";
    state.chosenCustomWishesText = "";
    
    state.modelState.gameWon = false;
    state.modelState.gameOver = false;
    state.modelState.currentChapter = 1;
    state.modelState.chapterSummaries.clear();
    state.modelState.messages.clear();
    state.modelState.activeChoices.clear();
    state.uiMessages.clear();
    state.uiActiveChoices.clear();
    state.mutex.unlock();
    
    // Spawn background thread to generate 5 tailored genres close to the original book
    state.setupDynamicGenres.clear();
    std::thread([actualFilePath]() {
        // Read first 1000 characters from raw book to extract theme/setting
        std::ifstream file(actualFilePath);
        if (!file.is_open()) return;
        std::string sample;
        char buf[1000];
        file.read(buf, sizeof(buf));
        sample.append(buf, file.gcount());
        file.close();
        
        sample = Trim(sample);
        if (sample.empty()) return;
        
        std::string lang = "Russian";
        state.mutex.lock();
        lang = state.gameLanguage;
        AskAi* client = state.aiClient.get();
        state.mutex.unlock();
        
        if (!client) return;
        
        std::string prompt = "You are a literary assistant. Read the following book excerpt and determine the 5 most relevant subgenres or thematic settings for a text-based game based on this book.\n\n"
                             "--- BEGIN BOOK EXCERPT ---\n"
                             + sample + "\n"
                             "--- END BOOK EXCERPT ---\n\n"
                             "CRITICAL INSTRUCTIONS:\n"
                             "1. DO NOT continue the book's story. DO NOT summarize or describe the book.\n"
                             "2. Return ONLY a list of exactly 5 short genre labels (each 1-3 words long).\n"
                             "3. Output exactly 5 lines. No numbers, no bullet points (- or *), no intro, no comments, no emojis, no extra text.\n"
                             "4. The list of genres MUST be written strictly in the language: '" + lang + "'.\n\n"
                             "Example expected format:\n"
                             "Genre1\n"
                             "Genre2\n"
                             "Genre3\n"
                             "Genre4\n"
                             "Genre5";
        
        std::string genresText = client->ask(prompt);
        
        std::vector<std::string> parsedGenres;
        std::stringstream ss(genresText);
        std::string line;
        while (std::getline(ss, line)) {
            line = Trim(line);
            if (line.empty()) continue;
            
            // Trim standard bullet/list prefixes
            if (line.size() > 2 && line[0] == '-' && line[1] == ' ') line = line.substr(2);
            else if (line.size() > 3 && std::isdigit(line[0]) && line[1] == '.' && line[2] == ' ') line = line.substr(3);
            else if (line.size() > 2 && std::isdigit(line[0]) && line[1] == '.') line = line.substr(2);
            
            line = CleanTextForFont(Trim(line));
            if (!line.empty()) {
                // Defensive guard: discard if it's a long sentence instead of a short genre label
                int spaceCount = std::count(line.begin(), line.end(), ' ');
                if (line.length() <= 35 && spaceCount <= 3) {
                    parsedGenres.push_back(line);
                }
            }
        }
        
        if (parsedGenres.size() >= 3) {
            if (parsedGenres.size() > 5) parsedGenres.resize(5);
            state.mutex.lock();
            state.setupDynamicGenres = parsedGenres;
            // If the player is currently viewing Step 2 (genres shelf), refresh in real time
            if (state.setupStep == 1) {
                state.modelState.activeChoices = parsedGenres;
                state.modelState.activeChoices.push_back(GetUiText("setup_back_to_step1"));
                SyncModelToUi();
            }
            state.mutex.unlock();
        }
    }).detach();

    TriggerSetupStep(0);
}

void StartBookGeneration(const std::string& filePath) {
    if (filePath.empty()) return;
    
    state.appState = APP_STATE_AI_GENERATING;
    state.generationProgress = 0;
    state.generationStatus = GetUiText("status_init");
    state.fileLoadError = "";
    
    std::string lengthWishes = state.chosenLengthText;
    std::string genreWishes = state.chosenGenreText;
    std::string fidelityWishes = state.chosenFidelityText;
    std::string customWishes = state.chosenCustomWishesText;
    
    std::thread([filePath, lengthWishes, genreWishes, fidelityWishes, customWishes]() {
        int maxBookAttempts = state.bookRetries;
        if (maxBookAttempts < 1) maxBookAttempts = 1;
        
        bool overallSuccess = false;
        std::string finalErrStr = "";
        
        for (int attempt = 1; attempt <= maxBookAttempts; ++attempt) {
            std::atomic<bool> generationDone(false);
            std::atomic<bool> generationSuccess(false);
            std::string errStr = "";
            
            // If it's a retry attempt, we reset the progress bar and wait for retryDelayMs
            if (attempt > 1) {
                std::string rawRetryPhrase = GetUiText("status_retry");
                std::string statusRetry = FormatRetryString(rawRetryPhrase, attempt, maxBookAttempts);
                
                state.mutex.lock();
                state.generationProgress = 5;
                state.generationStatus = statusRetry;
                state.mutex.unlock();
                
                std::this_thread::sleep_for(std::chrono::milliseconds(state.retryDelayMs));
                
                state.mutex.lock();
                state.generationProgress = 15;
                state.generationStatus = GetUiText("status_ai");
                state.mutex.unlock();
            } else {
                std::string statusInit = GetUiText("status_init");
                state.mutex.lock();
                state.generationProgress = 5;
                state.generationStatus = statusInit;
                state.mutex.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                
                std::string statusAi = GetUiText("status_ai");
                state.mutex.lock();
                state.generationProgress = 15;
                state.generationStatus = statusAi;
                state.mutex.unlock();
            }
            
            auto progressCallback = [&attempt, maxBookAttempts](int progress, const std::string& status) {
                std::string currentStatus = status;
                if (attempt > 1) {
                    currentStatus = "[" + std::to_string(attempt) + "/" + std::to_string(maxBookAttempts) + "] " + currentStatus;
                }
                state.mutex.lock();
                state.generationProgress = progress;
                state.generationStatus = currentStatus;
                state.mutex.unlock();
            };

            std::thread aiThread([&generationDone, &generationSuccess, &errStr, filePath, lengthWishes, genreWishes, fidelityWishes, customWishes, progressCallback]() {
                std::string outError = "";
                bool ok = CreateBookFromTxt(state.modelState, state.aiClient.get(), filePath, state.gameLanguage, lengthWishes, genreWishes, fidelityWishes, customWishes, outError, progressCallback);
                errStr = outError;
                generationSuccess = ok;
                generationDone = true;
            });
            
            int msElapsed = 0;
            std::string statusChapters = GetUiText("status_chapters");
            std::string statusValidation = GetUiText("status_validation");
            std::string statusAi = GetUiText("status_ai");
            int totalChapters = ParseChapterCount(lengthWishes);
            while (!generationDone) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (totalChapters <= 10) {
                    msElapsed += 100;
                    
                    double targetVal = 15.0 + 70.0 * (1.0 - std::exp(-msElapsed / 8000.0));
                    int progress = (int)targetVal;
                    if (progress > 85) progress = 85;
                    
                    std::string currentStatus = statusAi;
                    if (progress > 70) {
                        currentStatus = statusValidation;
                    } else if (progress > 45) {
                        currentStatus = statusChapters;
                    }
                    
                    // If we are on attempt > 1, prefix the status with the attempt info
                    if (attempt > 1) {
                        currentStatus = "[" + std::to_string(attempt) + "/" + std::to_string(maxBookAttempts) + "] " + currentStatus;
                    }
                    
                    state.mutex.lock();
                    state.generationProgress = progress;
                    state.generationStatus = currentStatus;
                    state.mutex.unlock();
                }
            }
            
            aiThread.join();
            
            if (generationSuccess) {
                overallSuccess = true;
                break;
            } else {
                finalErrStr = errStr;
                std::cout << "[Book Gen Retry] Attempt " << attempt << " failed. Error: " << errStr << std::endl;
            }
        }
        
        if (overallSuccess) {
            std::string statusVal = GetUiText("status_validation");
            state.mutex.lock();
            state.generationProgress = 90;
            state.generationStatus = statusVal;
            state.mutex.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            
            std::remove("save.json");
            std::remove("../save.json");
            bool loadConfigOk = LoadBookConfig("book.json");
            
            std::string statusDone = GetUiText("status_done");
            state.mutex.lock();
            state.generationProgress = 100;
            state.generationStatus = statusDone;
            state.mutex.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            
            state.mutex.lock();
            state.modelState.currentChapter = 1;
            state.modelState.chapterSummaries.clear();
            state.modelState.gameOver = false;
            state.modelState.pendingNextChapter = -1;
            state.modelState.messages.clear();
            state.modelState.activeChoices.clear();
            state.uiMessages.clear();
            state.uiActiveChoices.clear();
            state.appState = APP_STATE_GAMEPLAY;
            state.mutex.unlock();
            
            std::string startQ = state.modelState.bookStartPrompt.empty() ? GetStartPrompt(state.gameLanguage) : state.modelState.bookStartPrompt;
            state.mutex.lock();
            std::vector<std::string> options = ExtractAndStripOptions(startQ);
            if (options.empty()) {
                options = { IsRussianLanguage(state.gameLanguage) ? "Продолжить историю" : "Continue story" };
            }
            std::string perfectStart = ReconstructPerfectAiResponse(startQ, options);
            
            ChatMessageData aiMsg;
            aiMsg.sender = "AI";
            aiMsg.text = perfectStart;
            state.modelState.messages.push_back(aiMsg);
            state.modelState.activeChoices = options;
            
            UpdateSystemPrompt(state.modelState, state.aiClient.get());
            SyncModelToUi();
            state.mutex.unlock();
            
            SaveGame();
        } else {
            std::string defaultErr = GetUiText("err_ai_gen");
            state.mutex.lock();
            state.fileLoadError = finalErrStr.empty() ? defaultErr : finalErrStr;
            state.appState = APP_STATE_ENTER_TXT_PATH;
            state.mutex.unlock();
        }
    }).detach();
}

// Detached Asynchronous Thread Safe API Queries
void SubmitQuery(const std::string& queryText, bool isRetry, bool showInChat) {
    if (queryText.empty() || state.aiThinking) return;
    
    // Play synthetic chiptune interaction sound
    if (state.soundEffect && state.mixOk) {
        Mix_PlayChannel(-1, state.soundEffect.get(), 0);
    }
    
    // Secure a direct local value copy to protect background captures
    std::string queryCopy = queryText;
    
    state.mutex.lock();
    if (!showInChat) {
        state.modelState.pendingNextChapter = -1;
    }
    state.ignoreTags = !showInChat;
    if (state.modelState.pendingNextChapter != -1) {
        // Strict verification: only transition if input copy exactly matches one of activeChoices
        bool choiceMatched = false;
        for (const auto& opt : state.modelState.activeChoices) {
            if (queryCopy == opt) {
                choiceMatched = true;
                break;
            }
        }
        if (!choiceMatched) {
            state.mutex.unlock();
            return;
        }

        int nextChapter = state.modelState.pendingNextChapter;
        
        int maxChapterNum = 0;
        for (const auto& ch : state.modelState.chapters) {
            if (ch.number > maxChapterNum) {
                maxChapterNum = ch.number;
            }
        }
        if (maxChapterNum == 0) maxChapterNum = 4;
        bool isVictory = (nextChapter > maxChapterNum);

        state.modelState.lastQuery = queryCopy;
        state.aiThinking = true;
        state.scrollToBottom = true;
        state.modelState.activeChoices.clear();
        state.uiActiveChoices.clear();
        
        // Build summary dialogue from only the first (maxTurnsForce - 2) user turns.
        // This prevents a looping chapter's repeated tail from polluting the summary.
        std::string dialogueText = "";
        {
            int summaryTurnLimit = std::max(1, state.modelState.maxTurnsForce - 2);
            int userTurnsSeen = 0;
            for (const auto& msg : state.modelState.messages) {
                if (userTurnsSeen >= summaryTurnLimit) break;
                dialogueText += "[" + msg.sender + "]: " + msg.text + "\n\n";
                if (msg.sender == "User") {
                    userTurnsSeen++;
                }
            }
        }
        std::string langCopy = state.gameLanguage;
        state.currentQueryId++;
        uint64_t queryId = state.currentQueryId;
        state.mutex.unlock();
        
        // Launch background transition thread
        std::thread([nextChapter, dialogueText, langCopy, isVictory, queryId]() {
            std::cout << "[Transition Thread] Starting summary request..." << std::endl;
            state.mutex.lock();
            if (queryId != state.currentQueryId) {
                state.mutex.unlock();
                return;
            }
            state.mutex.unlock();
            state.aiClient->setSystemPrompt(state.modelState.promptAiSummarizer);
            std::string summaryPrompt = GetSummaryPrompt(langCopy, dialogueText);
            
            std::string summary;
            int attemptSum = 0;
            int maxAttempts = state.maxRetries;
            int delayMs = state.retryDelayMs;
            while (attemptSum < maxAttempts) {
                summary = state.aiClient->ask(summaryPrompt);
                if (summary.empty() || summary.find("Error") != std::string::npos) {
                    attemptSum++;
                    std::cout << "[Transition Thread Summary] Attempt " << attemptSum << " failed: " << summary << std::endl;
                    if (attemptSum < maxAttempts) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    }
                } else {
                    break;
                }
            }
            std::cout << "[Transition Thread] Raw summary received:\n" << summary << std::endl;
            
            // Clean any XML tags from the summary
            size_t startPos = summary.find("<");
            while (startPos != std::string::npos) {
                size_t endPos = summary.find(">", startPos);
                if (endPos != std::string::npos) {
                    summary.erase(startPos, endPos - startPos + 1);
                } else {
                    break;
                }
                startPos = summary.find("<");
            }
            summary = Trim(summary);
            
            if (summary.empty() || summary.find("Error") != std::string::npos) {
                summary = IsRussianLanguage(langCopy) ? "Глава завершена." : (IsUkrainianLanguage(langCopy) ? "Розділ завершено." : "Chapter completed.");
            }
            
            // Reconstruct the system prompt for the next chapter narrative query
            state.mutex.lock();
            GameState tempState = state.modelState;
            state.mutex.unlock();
            
            std::string labeledSummary = std::to_string(nextChapter - 1) + "::" + summary;
            tempState.chapterSummaries.push_back(labeledSummary);
            
            std::string nextStartMsg;
            if (isVictory) {
                std::string allSummaries = "";
                for (size_t i = 0; i < tempState.chapterSummaries.size(); i++) {
                    std::string raw = tempState.chapterSummaries[i];
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
                    
                    if (label.find('-') != std::string::npos) {
                        allSummaries += (IsRussianLanguage(langCopy) ? "Главы " : (IsUkrainianLanguage(langCopy) ? "Розділи " : "Chapters ")) 
                                        + label + ": " + text + "\n\n";
                    } else {
                        allSummaries += (IsRussianLanguage(langCopy) ? "Глава " : (IsUkrainianLanguage(langCopy) ? "Розділ " : "Chapter ")) 
                                        + label + ": " + text + "\n\n";
                    }
                }
                nextStartMsg = GetEpiloguePrompt(langCopy, allSummaries);
                std::cout << "[Transition Thread] Submitting epilogue query..." << std::endl;
                std::string epiloguePrompt = state.modelState.promptAiEpilogueWriter;
                size_t langPos = epiloguePrompt.find("{language}");
                if (langPos != std::string::npos) {
                    epiloguePrompt.replace(langPos, 10, langCopy);
                }
                state.aiClient->setSystemPrompt(epiloguePrompt);
            } else {
                tempState.currentChapter = nextChapter;
                UpdateSystemPrompt(tempState, state.aiClient.get());
                nextStartMsg = GetNextStartMsg(langCopy, nextChapter);
                std::cout << "[Transition Thread] Submitting starting query for Chapter " << nextChapter << std::endl;
            }
            
            std::string response;
            int attemptStart = 0;
            while (attemptStart < maxAttempts) {
                response = state.aiClient->ask(nextStartMsg);
                if (response.empty() || response.find("Error") != std::string::npos) {
                    attemptStart++;
                    std::cout << "[Transition Thread Start] Attempt " << attemptStart << " failed: " << response << std::endl;
                    if (attemptStart < maxAttempts) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                    }
                } else {
                    break;
                }
            }
            
            state.mutex.lock();
            if (queryId != state.currentQueryId) {
                state.mutex.unlock();
                return;
            }
            // Verify if the starting query returned an error
            bool hasError = (response.find("Error") != std::string::npos);
            
            if (hasError) {
                // If it failed, do not commit the transition! Restore the transition button so the player can retry.
                state.modelState.activeChoices = { state.transitionPrefix + std::to_string(nextChapter) };
                state.aiThinking = false;
                SyncModelToUi();
                state.mutex.unlock();
                std::cerr << "[Transition Thread] Next chapter starting request failed: " << response << std::endl;
                return;
            }
            
            // If successful, commit all state changes!
            state.modelState.chapterSummaries.push_back(labeledSummary);
            
            // Summary Compression Trigger Block
            {
                // Count how many uncompressed items we currently have
                // Uncompressed items are those where the label (before ::) does not contain '-'
                std::vector<size_t> uncompressedIndices;
                for (size_t i = 0; i < state.modelState.chapterSummaries.size(); i++) {
                    std::string raw = state.modelState.chapterSummaries[i];
                    size_t sep = raw.find("::");
                    std::string label = (sep != std::string::npos) ? raw.substr(0, sep) : "";
                    if (label.empty() || label.find('-') == std::string::npos) {
                        uncompressedIndices.push_back(i);
                    }
                }
                
                // If we have exactly 10 uncompressed items, compress them!
                if (uncompressedIndices.size() >= 10) {
                    std::cout << "[Transition Thread] Triggering compression for " << uncompressedIndices.size() << " chapter summaries..." << std::endl;
                    
                    // Formulate list of summaries to combine
                    std::string summariesList = "";
                    for (size_t idx : uncompressedIndices) {
                        std::string raw = state.modelState.chapterSummaries[idx];
                        size_t sep = raw.find("::");
                        std::string label = (sep != std::string::npos) ? raw.substr(0, sep) : std::to_string(idx + 1);
                        std::string text = (sep != std::string::npos) ? raw.substr(sep + 2) : raw;
                        
                        summariesList += "- Chapter " + label + ": " + text + "\n";
                    }
                    
                    // Unlock mutex while we call AI to keep UI completely responsive!
                    std::string compressorPromptTemplate = state.modelState.promptAiSummaryCompressor;
                    std::string langCopyForCompress = langCopy;
                    
                    state.mutex.unlock();
                    
                    // Configure system prompt and ask
                    state.aiClient->setSystemPrompt(compressorPromptTemplate);
                    
                    // User prompt
                    std::string userPrompt = "Language: " + langCopyForCompress + "\n\nChapter summaries to combine:\n" + summariesList + "\n\nCombined concise paragraph:";
                    
                    std::string compressedSummary = "";
                    int attemptComp = 0;
                    while (attemptComp < maxAttempts) {
                        compressedSummary = state.aiClient->ask(userPrompt);
                        if (compressedSummary.empty() || compressedSummary.find("Error") != std::string::npos) {
                            attemptComp++;
                            std::cout << "[Transition Thread Compression] Attempt " << attemptComp << " failed: " << compressedSummary << std::endl;
                            if (attemptComp < maxAttempts) {
                                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                            }
                        } else {
                            break;
                        }
                    }
                    
                    // Clean XML tags from compressed summary
                    size_t startPos = compressedSummary.find("<");
                    while (startPos != std::string::npos) {
                        size_t endPos = compressedSummary.find(">", startPos);
                        if (endPos != std::string::npos) {
                            compressedSummary.erase(startPos, endPos - startPos + 1);
                        } else {
                            break;
                        }
                        startPos = compressedSummary.find("<");
                    }
                    compressedSummary = Trim(compressedSummary);
                    
                    state.mutex.lock();
                    
                    // Verify if we were cancelled during the async AI call!
                    if (queryId == state.currentQueryId && !compressedSummary.empty() && compressedSummary.find("Error") == std::string::npos) {
                        // Re-fetch uncompressed indices to be absolutely safe
                        uncompressedIndices.clear();
                        for (size_t i = 0; i < state.modelState.chapterSummaries.size(); i++) {
                            std::string raw = state.modelState.chapterSummaries[i];
                            size_t sep = raw.find("::");
                            std::string label = (sep != std::string::npos) ? raw.substr(0, sep) : "";
                            if (label.empty() || label.find('-') == std::string::npos) {
                                uncompressedIndices.push_back(i);
                            }
                        }
                        
                        if (uncompressedIndices.size() >= 10) {
                            // Find the first and last chapter numbers of the range being compressed
                            std::string firstLabel, lastLabel;
                            
                            // Get first label
                            {
                                std::string rawFirst = state.modelState.chapterSummaries[uncompressedIndices.front()];
                                size_t sepFirst = rawFirst.find("::");
                                firstLabel = (sepFirst != std::string::npos) ? rawFirst.substr(0, sepFirst) : std::to_string(uncompressedIndices.front() + 1);
                            }
                            // Get last label
                            {
                                std::string rawLast = state.modelState.chapterSummaries[uncompressedIndices.back()];
                                size_t sepLast = rawLast.find("::");
                                lastLabel = (sepLast != std::string::npos) ? rawLast.substr(0, sepLast) : std::to_string(uncompressedIndices.back() + 1);
                            }
                            
                            std::string rangeString = firstLabel + "-" + lastLabel;
                            std::string mergedEntry = rangeString + "::" + compressedSummary;
                            
                            // Build a new list of summaries replacing the 10 detailed entries with the merged entry
                            std::vector<std::string> newSummaries;
                            size_t firstIdx = uncompressedIndices.front();
                            size_t lastIdx = uncompressedIndices.back();
                            
                            for (size_t i = 0; i < state.modelState.chapterSummaries.size(); i++) {
                                if (i < firstIdx) {
                                    newSummaries.push_back(state.modelState.chapterSummaries[i]);
                                } else if (i == firstIdx) {
                                    newSummaries.push_back(mergedEntry);
                                } else if (i > lastIdx) {
                                    newSummaries.push_back(state.modelState.chapterSummaries[i]);
                                }
                            }
                            
                            state.modelState.chapterSummaries = newSummaries;
                            std::cout << "[Transition Thread] Successfully compressed chapters " << rangeString << " into: " << compressedSummary << std::endl;
                        }
                    }
                }
            }
            
            if (isVictory) {
                state.modelState.gameWon = true;
                state.modelState.pendingNextChapter = -1;
                state.modelState.activeChoices.clear();
            } else {
                state.modelState.currentChapter = nextChapter;
                state.modelState.pendingNextChapter = -1;
            }
            
            state.modelState.messages.clear();
            state.uiMessages.clear();
            
            if (isVictory) {
                // Epilogue does not have any options or tags, but just in case, clean any XML tags.
                size_t startPos = response.find("<");
                while (startPos != std::string::npos) {
                    size_t endPos = response.find(">", startPos);
                    if (endPos != std::string::npos) {
                        response.erase(startPos, endPos - startPos + 1);
                    } else {
                        break;
                    }
                    startPos = response.find("<");
                }
                size_t startPosSq = response.find("[");
                while (startPosSq != std::string::npos) {
                    size_t endPosSq = response.find("]", startPosSq);
                    if (endPosSq != std::string::npos) {
                        response.erase(startPosSq, endPosSq - startPosSq + 1);
                    } else {
                        break;
                    }
                    startPosSq = response.find("[");
                }
                response = Trim(response);
            } else {
                // Process the AI response in a robust loop (ignore player_dead and next_chapter since this is the start of a chapter)
                size_t deadPos = response.find("<player_dead/>");
                while (deadPos != std::string::npos) {
                    response.erase(deadPos, 14);
                    deadPos = response.find("<player_dead/>");
                }
                size_t deadPosSq = response.find("[player_dead]");
                while (deadPosSq != std::string::npos) {
                    response.erase(deadPosSq, 13);
                    deadPosSq = response.find("[player_dead]");
                }
                
                size_t nextPos = response.find("<next_chapter>");
                while (nextPos != std::string::npos) {
                    size_t nextEndPos = response.find("</next_chapter>", nextPos);
                    if (nextEndPos != std::string::npos && nextEndPos > nextPos) {
                        response.erase(nextPos, (nextEndPos + 15) - nextPos);
                    } else {
                        response.erase(nextPos);
                        break;
                    }
                    nextPos = response.find("<next_chapter>");
                }
                
                size_t nextPosSq = response.find("[next_chapter]");
                while (nextPosSq != std::string::npos) {
                    size_t nextEndPosSq = response.find("[/next_chapter]", nextPosSq);
                    if (nextEndPosSq != std::string::npos && nextEndPosSq > nextPosSq) {
                        response.erase(nextPosSq, (nextEndPosSq + 15) - nextPosSq);
                    } else {
                        response.erase(nextPosSq);
                        break;
                    }
                    nextPosSq = response.find("[next_chapter]");
                }
            }
            
            std::string rawResponse = response;
            std::vector<std::string> options;
            if (!isVictory) {
                options = ExtractAndStripOptions(response);
                if (!options.empty()) {
                    rawResponse = ReconstructPerfectAiResponse(response, options);
                }
                if (options.empty()) {
                    options.push_back("Продолжить историю");
                }
            }
            
            ChatMessageData aiMsg;
            aiMsg.sender = "AI";
            aiMsg.text = rawResponse;
            state.modelState.messages.push_back(aiMsg);
            state.scrollToBottom = true;
            
            state.modelState.pendingNextChapter = -1;
            if (!isVictory) {
                state.modelState.activeChoices = options;
            }
            
            state.aiThinking = false;
            SyncModelToUi();
            state.mutex.unlock();
            
            // Update system prompt on committed state (only for non-victory)
            if (!isVictory) {
                UpdateSystemPrompt();
            }
            SaveGame();
        }).detach();
        return;
    }
    state.currentQueryId++;
    uint64_t queryId = state.currentQueryId;
    state.modelState.lastQuery = queryCopy;
    if (showInChat && !isRetry) {
        // Add User query to Dialogue bubbles list
        ChatMessageData userMsg;
        userMsg.sender = "User";
        userMsg.text = queryCopy;
        state.modelState.messages.push_back(userMsg);
        
        SyncModelToUi();
    }
    std::vector<ChatMessageData> historyCopy = state.modelState.messages;
    std::string langCopy = state.gameLanguage;
    if (!isRetry) {
        state.savedChoices = state.modelState.activeChoices;
    }
    state.aiThinking = true;
    state.scrollToBottom = true;
    state.modelState.activeChoices.clear(); // Instantly collapse options shelf during thinking
    state.uiActiveChoices.clear();
    state.mutex.unlock();
    
    
    // Fire detaching query thread
    std::thread([queryCopy, historyCopy, langCopy, queryId]() {
        std::string response;
        if (historyCopy.empty()) {
            response = state.aiClient->ask(queryCopy);
        } else {
            response = state.aiClient->askChat(historyCopy, langCopy);
        }
        
        state.mutex.lock();
        if (queryId != state.currentQueryId) {
            state.mutex.unlock();
            return;
        }
        if (!response.empty() && !ContainsErrorCaseInsensitive(response)) {
            state.pendingResponse = response;
            state.responseReady = true;
        } else {
            // All client retries failed or returned an API error!
            // Push the cURL error message to pendingResponse so it is caught in ConsumeApiResponse.
            state.pendingResponse = response.empty() ? "Error: Failed to receive response from AI." : response;
            state.responseReady = true;
        }
        state.mutex.unlock();
    }).detach();
}

// Background thread queue consumer
void ConsumeApiResponse() {
    state.mutex.lock();
    if (state.responseReady) {
        std::string fullResponse = state.pendingResponse;
        state.responseReady = false;
        state.aiThinking = false;
        
        if (fullResponse.empty() || ContainsErrorCaseInsensitive(fullResponse)) {
            // Render the error message bubble in the chat
            ChatMessageData aiMsg;
            aiMsg.sender = "AI";
            aiMsg.text = GetUiText("status_api_error") + "\n\n(" + fullResponse + ")";
            state.modelState.messages.push_back(aiMsg);
            state.scrollToBottom = true;
            
            // Offer "Повторить запрос" retry card
            state.modelState.pendingNextChapter = -1;
            state.modelState.activeChoices = { "Повторить запрос" };
            SyncModelToUi();
            state.mutex.unlock();
            return;
        }
        
        std::cout << "\n[API Response Received]\nRaw Response:\n" << fullResponse << "\n[End of Raw Response]\n" << std::endl;
        
        // Check for tags and strip them in a robust loop
        bool isDead = false;
        size_t deadPos = fullResponse.find("<player_dead/>");
        while (deadPos != std::string::npos) {
            if (!state.ignoreTags) {
                isDead = true;
                std::cout << "[ConsumeApiResponse] Found <player_dead/> tag!" << std::endl;
            }
            fullResponse.erase(deadPos, 14);
            deadPos = fullResponse.find("<player_dead/>");
        }
        size_t deadPosSq = fullResponse.find("[player_dead]");
        while (deadPosSq != std::string::npos) {
            if (!state.ignoreTags) {
                isDead = true;
                std::cout << "[ConsumeApiResponse] Found [player_dead] tag!" << std::endl;
            }
            fullResponse.erase(deadPosSq, 13);
            deadPosSq = fullResponse.find("[player_dead]");
        }
        
        int nextChapter = -1;
        size_t nextPos = fullResponse.find("<next_chapter>");
        while (nextPos != std::string::npos) {
            size_t nextEndPos = fullResponse.find("</next_chapter>", nextPos);
            if (nextEndPos != std::string::npos && nextEndPos > nextPos) {
                if (!state.ignoreTags && nextChapter == -1) {
                    std::string chNumStr = fullResponse.substr(nextPos + 14, nextEndPos - (nextPos + 14));
                    try {
                        nextChapter = std::stoi(chNumStr);
                        std::cout << "[ConsumeApiResponse] Found <next_chapter> tag. Target Chapter: " << nextChapter << std::endl;
                    } catch (...) {}
                }
                fullResponse.erase(nextPos, (nextEndPos + 15) - nextPos);
            } else {
                fullResponse.erase(nextPos);
                break;
            }
            nextPos = fullResponse.find("<next_chapter>");
        }
        
        size_t nextPosSq = fullResponse.find("[next_chapter]");
        while (nextPosSq != std::string::npos) {
            size_t nextEndPosSq = fullResponse.find("[/next_chapter]", nextPosSq);
            if (nextEndPosSq != std::string::npos && nextEndPosSq > nextPosSq) {
                if (!state.ignoreTags && nextChapter == -1) {
                    std::string chNumStr = fullResponse.substr(nextPosSq + 14, nextEndPosSq - (nextPosSq + 14));
                    try {
                        nextChapter = std::stoi(chNumStr);
                        std::cout << "[ConsumeApiResponse] Found [next_chapter] tag. Target Chapter: " << nextChapter << std::endl;
                    } catch (...) {}
                }
                fullResponse.erase(nextPosSq, (nextEndPosSq + 15) - nextPosSq);
            } else {
                fullResponse.erase(nextPosSq);
                break;
            }
            nextPosSq = fullResponse.find("[next_chapter]");
        }
        
        std::string rawResponse = fullResponse;
        
        // Strip XML choice tags and parse dynamic action cards
        std::vector<std::string> options = ExtractAndStripOptions(fullResponse);
        
        // Reconstruct the perfect response in '|' separator format for chat history
        std::string perfectResponse = ReconstructPerfectAiResponse(fullResponse, options);
        
        std::cout << "[ConsumeApiResponse] Parsed options count: " << options.size() << std::endl;
        for (size_t i = 0; i < options.size(); i++) {
            std::cout << "  Option " << (i + 1) << ": \"" << options[i] << "\"" << std::endl;
        }
        
        // Offer a retry or fallback option in case of empty options
        if (options.empty() && !isDead && nextChapter == -1) {
            if (ContainsErrorCaseInsensitive(fullResponse)) {
                options.push_back("Повторить запрос");
            } else {
                options.push_back("Продолжить историю");
            }
        }
        
        int userTurnCount = 0;
        for (const auto& msg : state.modelState.messages) {
            if (msg.sender == "User") {
                userTurnCount++;
            }
        }
        if (nextChapter == -1 && !isDead) {
            if (userTurnCount >= state.modelState.maxTurnsForce + 5) {
                // Forced transition after 3 turns: clear choices and offer ONLY the transition button
                options = { GetUiText("btn_force_next_chapter") };
            } else if (userTurnCount >= state.modelState.maxTurnsForce + 2) {
                // Regular turn limit reached: offer the optional transition button alongside other choices
                options.push_back(GetUiText("btn_force_next_chapter"));
            }
        }
        
        ChatMessageData aiMsg;
        aiMsg.sender = "AI";
        aiMsg.text = perfectResponse;
        state.modelState.messages.push_back(aiMsg);
        state.scrollToBottom = true;
        
        if (isDead) {
            state.modelState.gameOver = true;
            state.modelState.activeChoices.clear();
            SyncModelToUi();
            state.mutex.unlock();
            
            // Auto-save game state on death
            SaveGame();
            return;
        }
        
        // Populate options if parsed cleanly or handle next chapter transition
        if (nextChapter != -1) {
            state.modelState.pendingNextChapter = nextChapter;
            int maxChapterNum = 0;
            for (const auto& ch : state.modelState.chapters) {
                if (ch.number > maxChapterNum) {
                    maxChapterNum = ch.number;
                }
            }
            if (maxChapterNum == 0) maxChapterNum = 4;
            if (nextChapter > maxChapterNum) {
                state.modelState.activeChoices = { GetUiText("btn_go_to_epilogue") };
            } else {
                state.modelState.activeChoices = { state.transitionPrefix + std::to_string(nextChapter) };
            }
        } else {
            state.modelState.pendingNextChapter = -1;
            state.modelState.activeChoices = options;
        }
        
        SyncModelToUi();
        state.mutex.unlock();
        
        // Save regular state
        SaveGame();
        return;
    }
    state.mutex.unlock();
}

// Main Frame Loop Execution
void MainIteration() {
    // Ensure SDL text input state matches gameplay/editing requirements
    bool wantTextInput = (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP || state.editingLanguage || state.editingBookPath || state.editingApiKey);
    if (wantTextInput && !SDL_IsTextInputActive()) {
        SDL_StartTextInput();
    } else if (!wantTextInput && SDL_IsTextInputActive()) {
        SDL_StopTextInput();
    }

    // 1. Consume any incoming background API responses
    ConsumeApiResponse();
    
    // Blinking text cursor timer
    Uint32 currentTicks = SDL_GetTicks();
    if (currentTicks - state.cursorLastBlink >= 500) {
        state.cursorVisible = !state.cursorVisible;
        state.cursorLastBlink = currentTicks;
    }
    
    // Dynamic Layout Sizing Properties
    int optionsAreaH = 0;
    int footerH = 0;
    if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP || state.editingLanguage || state.editingApiKey) {
        if (!state.uiActiveChoices.empty()) {
            int n = state.uiActiveChoices.size();
            int cardH = 45;
            int verticalSpacing = 8;
            optionsAreaH = n * cardH + (n + 1) * verticalSpacing;
        }
        footerH = 60;
    }
    int headerH = 0;
    if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP || state.editingLanguage) {
        headerH = 42;
    }
    int viewportH = WINDOW_HEIGHT - (optionsAreaH + footerH + headerH);
    
    // 2. Process keyboard & mouse inputs
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_FINGERDOWN) {
            SDL_Event fakeMouseEvent = {};
            fakeMouseEvent.type = SDL_MOUSEBUTTONDOWN;
            fakeMouseEvent.button.button = SDL_BUTTON_LEFT;
            fakeMouseEvent.button.state = SDL_PRESSED;
            fakeMouseEvent.button.x = (int)(event.tfinger.x * WINDOW_WIDTH);
            fakeMouseEvent.button.y = (int)(event.tfinger.y * WINDOW_HEIGHT);
            SDL_PushEvent(&fakeMouseEvent);
            continue;
        } else if (event.type == SDL_FINGERUP) {
            SDL_Event fakeMouseEvent = {};
            fakeMouseEvent.type = SDL_MOUSEBUTTONUP;
            fakeMouseEvent.button.button = SDL_BUTTON_LEFT;
            fakeMouseEvent.button.state = SDL_RELEASED;
            fakeMouseEvent.button.x = (int)(event.tfinger.x * WINDOW_WIDTH);
            fakeMouseEvent.button.y = (int)(event.tfinger.y * WINDOW_HEIGHT);
            SDL_PushEvent(&fakeMouseEvent);
            continue;
        }
        
        if (event.type == SDL_QUIT) {
            state.running = false;
        } else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                WINDOW_WIDTH = event.window.data1;
                WINDOW_HEIGHT = event.window.data2;
                SDL_RenderSetLogicalSize(state.renderer.get(), WINDOW_WIDTH, WINDOW_HEIGHT);
                int hH = 0;
                if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP || state.editingLanguage) {
                    hH = 42;
                }
                viewportH = WINDOW_HEIGHT - (optionsAreaH + footerH + hH);
                state.mutex.lock();
                SyncModelToUi();
                state.mutex.unlock();
            }
        } else if (event.type == SDL_DROPFILE) {
            char* droppedFile = event.drop.file;
            if (droppedFile != nullptr) {
                std::string path(droppedFile);
                SDL_free(droppedFile);
                if (state.appState == APP_STATE_ENTER_TXT_PATH && !state.fileDialogActive) {
                    InitAdventureSetup(path);
                }
            }
        } else if (event.type == SDL_KEYDOWN) {
            state.mutex.lock();
            bool isLoc = !state.uiLocalized;
            state.mutex.unlock();
            if (isLoc && (state.appState == APP_STATE_ASK_CONTINUE || state.appState == APP_STATE_ENTER_TXT_PATH)) {
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    state.running = false;
                }
                continue; // Ignore inputs during UI localization on startup
            }

            if (state.editingLanguage) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_RETURN) {
                    ChangeGameLanguage(state.inputText);
                    state.editingLanguage = false;
                    state.inputText = "";
                    SDL_StopTextInput();
                } else if (sym == SDLK_ESCAPE) {
                    state.editingLanguage = false;
                    state.inputText = "";
                    SDL_StopTextInput();
                } else if (sym == SDLK_BACKSPACE) {
                    PopUTF8Character(state.inputText);
                }
                // Bypass other hotkeys while in input mode
                continue;
            }

            if (state.editingApiKey) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_RETURN) {
                    SaveApiKeyToModelJson(state.selectedAiFilename, state.inputText);
                    ReloadSettingsAndReinit(state.selectedAiFilename);
                    state.appState = state.previousAppState;
                    state.editingApiKey = false;
                    state.inputText = "";
                    SDL_StopTextInput();
                } else if (sym == SDLK_ESCAPE) {
                    state.editingApiKey = false;
                    state.inputText = "";
                    SDL_StopTextInput();
                } else if (sym == SDLK_BACKSPACE) {
                    PopUTF8Character(state.inputText);
                }
                // Bypass other hotkeys while in input mode
                continue;
            }
            
            if (state.editingBookPath) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_RETURN) {
                    if (!state.bookPathInput.empty()) {
                        InitAdventureSetup(state.bookPathInput);
                    }
                    state.editingBookPath = false;
                    SDL_StopTextInput();
                } else if (sym == SDLK_ESCAPE) {
                    state.editingBookPath = false;
                    SDL_StopTextInput();
                } else if (sym == SDLK_BACKSPACE) {
                    PopUTF8Character(state.bookPathInput);
                    state.inputText = state.bookPathInput;
                }
                // Bypass other hotkeys while in input mode
                continue;
            }
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                state.running = false;
            } else if (state.appState == APP_STATE_ASK_CONTINUE) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_1 || sym == SDLK_y || sym == SDLK_RETURN) {
                    state.appState = APP_STATE_GAMEPLAY;
                    if (state.uiMessages.empty()) {
                        std::string startQ = state.modelState.bookStartPrompt.empty() ? GetStartPrompt(state.gameLanguage) : state.modelState.bookStartPrompt;
                        state.mutex.lock();
                        std::vector<std::string> options = ExtractAndStripOptions(startQ);
                        if (options.empty()) {
                            options = { IsRussianLanguage(state.gameLanguage) ? "Продолжить историю" : "Continue story" };
                        }
                        std::string perfectStart = ReconstructPerfectAiResponse(startQ, options);
                        
                        ChatMessageData aiMsg;
                        aiMsg.sender = "AI";
                        aiMsg.text = perfectStart;
                        state.modelState.messages.push_back(aiMsg);
                        state.modelState.activeChoices = options;
                        
                        UpdateSystemPrompt(state.modelState, state.aiClient.get());
                        SyncModelToUi();
                        state.mutex.unlock();
                        
                        SaveGame();
                    } else if (!state.modelState.messages.empty() && state.modelState.messages.back().sender == "User") {
                        std::cout << "[Resume] Last message is from User. Auto-resuming narrative request..." << std::endl;
                        SubmitQuery(state.modelState.messages.back().text, true, false);
                    }
                } else if (sym == SDLK_2 || sym == SDLK_n) {
                    std::remove("save.json");
                    std::remove("../save.json");
                    state.mutex.lock();
                    state.modelState.currentChapter = 1;
                    state.modelState.chapterSummaries.clear();
                    state.modelState.gameOver = false;
                    state.modelState.pendingNextChapter = -1;
                    state.modelState.messages.clear();
                    state.modelState.activeChoices.clear();
                    state.uiMessages.clear();
                    state.uiActiveChoices.clear();
                    state.mutex.unlock();
                    state.appState = APP_STATE_ENTER_TXT_PATH;
                }
            } else if (state.appState == APP_STATE_ENTER_TXT_PATH) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_RETURN || sym == SDLK_SPACE) {
                    std::string path = OpenFileDialog();
                    if (!path.empty()) {
                        InitAdventureSetup(path);
                    }
                }
            } else if (state.appState == APP_STATE_SELECT_BOOK) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_ESCAPE) {
                    state.appState = APP_STATE_ENTER_TXT_PATH;
                } else if (sym >= SDLK_1 && sym <= SDLK_9) {
                    int index = sym - SDLK_1;
                    if (index >= 0 && index < (int)state.availableBooks.size()) {
                        std::string path = state.availableBooks[index].path;
                        InitAdventureSetup(path);
                    }
                }
            } else if (state.appState == APP_STATE_SELECT_AI) {
                SDL_Keycode sym = event.key.keysym.sym;
                if (sym == SDLK_ESCAPE) {
                    state.appState = state.previousAppState;
                } else if (sym >= SDLK_1 && sym <= SDLK_9) {
                    int index = sym - SDLK_1;
                    if (index >= 0 && index < (int)state.availableAiModels.size()) {
                        state.selectedAiFilename = state.availableAiModels[index].filename;
                        std::string existingKey = "";
                        std::ifstream f(state.selectedAiFilename);
                        if (!f.is_open()) {
                            f.open("../" + state.selectedAiFilename);
                        }
                        if (f.is_open()) {
                            try {
                                nlohmann::json j;
                                f >> j;
                                if (j.contains("apiKey") && j["apiKey"].is_string()) {
                                    existingKey = j["apiKey"].get<std::string>();
                                }
                            } catch (...) {}
                            f.close();
                        }
                        state.editingApiKey = true;
                        state.inputText = existingKey;
                        SDL_StartTextInput();
                    }
                }
            } else if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP) {
                if (event.key.keysym.sym == SDLK_RETURN) {
                    SubmitInputText();
                } else if (event.key.keysym.sym == SDLK_SPACE) {
                    if (state.appState == APP_STATE_GAMEPLAY && state.modelState.gameWon) {
                        RestartAdventure();
                    } else if (state.appState == APP_STATE_GAMEPLAY && state.modelState.gameOver) {
                        state.modelState.gameOver = false;
                        state.modelState.messages.clear();
                        state.modelState.pendingNextChapter = -1;
                        SaveGame();
                        UpdateSystemPrompt();
                        SubmitQuery(GetUiText("prompt_restart_chapter"), false, false);
                    }
                } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                    if (!state.modelState.gameOver && !state.modelState.gameWon) PopUTF8Character(state.inputText);
                } else {
                    if (!state.modelState.gameOver && !state.modelState.gameWon) {
                        // Handle keyboard dynamic choice bindings (1-9)
                        SDL_Keycode sym = event.key.keysym.sym;
                        if (sym >= SDLK_1 && sym <= SDLK_9 && state.inputText.empty()) {
                            int index = sym - SDLK_1;
                            state.mutex.lock();
                            if (index >= 0 && index < (int)state.uiActiveChoices.size() && !state.aiThinking) {
                                std::string choiceText = state.uiActiveChoices[index].text;
                                if (state.appState == APP_STATE_SETUP) {
                                    state.mutex.unlock();
                                    SubmitSetupChoice(choiceText);
                                } else {
                                    if (choiceText == "Повторить запрос") {
                                        if (!state.modelState.messages.empty() && state.modelState.messages.back().sender == "AI") {
                                            state.modelState.messages.pop_back(); // Remove the error bubble from history
                                        }
                                        state.mutex.unlock();
                                        SubmitQuery(state.modelState.lastQuery, true);
                                    } else if (choiceText == GetUiText("btn_force_next_chapter")) {
                                        state.modelState.pendingNextChapter = state.modelState.currentChapter + 1;
                                        state.modelState.activeChoices = { state.transitionPrefix + std::to_string(state.modelState.pendingNextChapter) };
                                        state.mutex.unlock();
                                        SubmitQuery(state.transitionPrefix + std::to_string(state.modelState.currentChapter + 1));
                                    } else {
                                        state.mutex.unlock();
                                        SubmitQuery(choiceText);
                                    }
                                }
                            } else {
                                state.mutex.unlock();
                            }
                        }
                    }
                }
            }
        } else if (event.type == SDL_TEXTINPUT) {
            if (state.editingLanguage || state.editingApiKey) {
                state.inputText += event.text.text;
            } else if (state.editingBookPath) {
                state.bookPathInput += event.text.text;
                state.inputText = state.bookPathInput;
            } else if ((state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP) && !state.modelState.gameOver && !state.modelState.gameWon) {
                // Append keyboard string inputs safely
                state.inputText += event.text.text;
            }
        } else if (event.type == SDL_MOUSEWHEEL) {
            if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP) {
                // Smooth vertical chat scrollbox
                state.scrollOffset -= event.wheel.y * 30;
                if (state.scrollOffset < 0) state.scrollOffset = 0;
                if (state.scrollOffset > state.maxScrollOffset) state.scrollOffset = state.maxScrollOffset;
            } else if (state.appState == APP_STATE_SELECT_AI) {
                // Smooth vertical select AI scrollbox
                state.aiSelectScrollOffset -= event.wheel.y * 25;
                if (state.aiSelectScrollOffset < 0) state.aiSelectScrollOffset = 0;
                if (state.aiSelectScrollOffset > state.aiSelectMaxScroll) state.aiSelectScrollOffset = state.aiSelectMaxScroll;
            } else if (state.appState == APP_STATE_SELECT_BOOK) {
                // Smooth vertical select book scrollbox
                state.bookSelectScrollOffset -= event.wheel.y * 25;
                if (state.bookSelectScrollOffset < 0) state.bookSelectScrollOffset = 0;
                if (state.bookSelectScrollOffset > state.bookSelectMaxScroll) state.bookSelectScrollOffset = state.bookSelectMaxScroll;
            }
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            state.mutex.lock();
            bool isLoc = !state.uiLocalized;
            state.mutex.unlock();
            if (isLoc && (state.appState == APP_STATE_ASK_CONTINUE || state.appState == APP_STATE_ENTER_TXT_PATH)) {
                continue; // Ignore clicks during UI localization on startup
            }

            int mx = event.button.x;
            int my = event.button.y;
            
            if (state.editingApiKey) {
                if (mx >= state.clearBtnRect.x && mx <= (state.clearBtnRect.x + state.clearBtnRect.w) &&
                    my >= state.clearBtnRect.y && my <= (state.clearBtnRect.y + state.clearBtnRect.h)) {
                    state.inputText = "";
                } else if (mx >= state.pasteBtnRect.x && mx <= (state.pasteBtnRect.x + state.pasteBtnRect.w) &&
                           my >= state.pasteBtnRect.y && my <= (state.pasteBtnRect.y + state.pasteBtnRect.h)) {
                    if (SDL_HasClipboardText()) {
                        char* clipText = SDL_GetClipboardText();
                        if (clipText) {
                            state.inputText = clipText;
                            SDL_free(clipText);
                        }
                    }
                } else if (mx >= state.confirmBtnRect.x && mx <= (state.confirmBtnRect.x + state.confirmBtnRect.w) &&
                           my >= state.confirmBtnRect.y && my <= (state.confirmBtnRect.y + state.confirmBtnRect.h)) {
                    SaveApiKeyToModelJson(state.selectedAiFilename, state.inputText);
                    ReloadSettingsAndReinit(state.selectedAiFilename);
                    state.appState = state.previousAppState;
                    state.editingApiKey = false;
                    state.inputText = "";
                    SDL_StopTextInput();
                } else if (my >= WINDOW_HEIGHT - 60) {
                    // Clicked inside the bottom bar, retain editing state
                } else {
                    state.editingApiKey = false;
                    state.inputText = "";
                    SDL_StopTextInput();
                }
                continue; // Skip screen specific actions
            }
            
            if (state.appState == APP_STATE_ASK_CONTINUE || state.appState == APP_STATE_ENTER_TXT_PATH) {
                if (mx >= state.langBtnRect.x && mx <= (state.langBtnRect.x + state.langBtnRect.w) &&
                    my >= state.langBtnRect.y && my <= (state.langBtnRect.y + state.langBtnRect.h)) {
                    state.editingLanguage = true;
                    state.inputText = state.gameLanguage;
                    SDL_StartTextInput();
                    continue; // Skip screen specific actions
                } else if (mx >= state.aiBtnRect.x && mx <= (state.aiBtnRect.x + state.aiBtnRect.w) &&
                           my >= state.aiBtnRect.y && my <= (state.aiBtnRect.y + state.aiBtnRect.h)) {
                    ScanAvailableAiModels();
                    state.previousAppState = state.appState;
                    state.appState = APP_STATE_SELECT_AI;
                    continue;
                } else if (state.editingLanguage) {
                    if (mx >= state.clearBtnRect.x && mx <= (state.clearBtnRect.x + state.clearBtnRect.w) &&
                        my >= state.clearBtnRect.y && my <= (state.clearBtnRect.y + state.clearBtnRect.h)) {
                        state.inputText = "";
                    } else if (mx >= state.confirmBtnRect.x && mx <= (state.confirmBtnRect.x + state.confirmBtnRect.w) &&
                               my >= state.confirmBtnRect.y && my <= (state.confirmBtnRect.y + state.confirmBtnRect.h)) {
                        ChangeGameLanguage(state.inputText);
                        state.editingLanguage = false;
                        state.inputText = "";
                        SDL_StopTextInput();
                    } else if (my >= WINDOW_HEIGHT - 60) {
                        // Clicked inside the bottom bar, retain editing state
                    } else {
                        state.editingLanguage = false;
                        state.inputText = "";
                        SDL_StopTextInput();
                    }
                    continue; // Skip other actions while editing/just finished
                }
            } else if (state.appState == APP_STATE_SELECT_AI) {
                int cardW = 600;
                int cardH = 440;
                int cardX = (WINDOW_WIDTH - cardW) / 2;
                int cardY = (WINDOW_HEIGHT - cardH) / 2;
                int startBtnY = cardY + 130;
                int btnSpacing = 55;
                
                bool modelClicked = false;
                if (my >= cardY + 130 && my <= cardY + 340) {
                    for (size_t i = 0; i < state.availableAiModels.size(); i++) {
                        SDL_Rect btnRect = { cardX + 50, startBtnY + (int)i * btnSpacing - state.aiSelectScrollOffset, 500, 45 };
                        if (mx >= btnRect.x && mx <= (btnRect.x + btnRect.w) &&
                            my >= btnRect.y && my <= (btnRect.y + btnRect.h)) {
                            state.selectedAiFilename = state.availableAiModels[i].filename;
                            std::string existingKey = "";
                            std::ifstream f(state.selectedAiFilename);
                            if (!f.is_open()) {
                                f.open("../" + state.selectedAiFilename);
                            }
                            if (f.is_open()) {
                                try {
                                    nlohmann::json j;
                                    f >> j;
                                    if (j.contains("apiKey") && j["apiKey"].is_string()) {
                                        existingKey = j["apiKey"].get<std::string>();
                                    }
                                } catch (...) {}
                                f.close();
                            }
                            state.editingApiKey = true;
                            state.inputText = existingKey;
                            SDL_StartTextInput();
                            modelClicked = true;
                            break;
                        }
                    }
                }
                
                if (!modelClicked) {
                    SDL_Rect backBtnRect = { cardX + 50, cardY + 365, 500, 45 };
                    if (mx >= backBtnRect.x && mx <= (backBtnRect.x + backBtnRect.w) &&
                        my >= backBtnRect.y && my <= (backBtnRect.y + backBtnRect.h)) {
                        state.appState = state.previousAppState;
                    }
                }
                continue;
            }
            
            if (state.appState == APP_STATE_ASK_CONTINUE) {
                if (mx >= state.customBtnRect1.x && mx <= (state.customBtnRect1.x + state.customBtnRect1.w) &&
                    my >= state.customBtnRect1.y && my <= (state.customBtnRect1.y + state.customBtnRect1.h)) {
                    state.appState = APP_STATE_GAMEPLAY;
                    if (state.uiMessages.empty()) {
                        std::string startQ = state.modelState.bookStartPrompt.empty() ? GetStartPrompt(state.gameLanguage) : state.modelState.bookStartPrompt;
                        state.mutex.lock();
                        std::vector<std::string> options = ExtractAndStripOptions(startQ);
                        if (options.empty()) {
                            options = { IsRussianLanguage(state.gameLanguage) ? "Продолжить историю" : "Continue story" };
                        }
                        std::string perfectStart = ReconstructPerfectAiResponse(startQ, options);
                        
                        ChatMessageData aiMsg;
                        aiMsg.sender = "AI";
                        aiMsg.text = perfectStart;
                        state.modelState.messages.push_back(aiMsg);
                        state.modelState.activeChoices = options;
                        
                        UpdateSystemPrompt(state.modelState, state.aiClient.get());
                        SyncModelToUi();
                        state.mutex.unlock();
                        
                        SaveGame();
                    } else if (!state.modelState.messages.empty() && state.modelState.messages.back().sender == "User") {
                        std::cout << "[Resume] Last message is from User. Auto-resuming narrative request..." << std::endl;
                        SubmitQuery(state.modelState.messages.back().text, true, false);
                    }
                } else if (mx >= state.customBtnRect2.x && mx <= (state.customBtnRect2.x + state.customBtnRect2.w) &&
                           my >= state.customBtnRect2.y && my <= (state.customBtnRect2.y + state.customBtnRect2.h)) {
                    std::remove("save.json");
                    std::remove("../save.json");
                    state.mutex.lock();
                    state.modelState.currentChapter = 1;
                    state.modelState.chapterSummaries.clear();
                    state.modelState.gameOver = false;
                    state.modelState.pendingNextChapter = -1;
                    state.modelState.messages.clear();
                    state.modelState.activeChoices.clear();
                    state.uiMessages.clear();
                    state.uiActiveChoices.clear();
                    state.mutex.unlock();
                    state.appState = APP_STATE_ENTER_TXT_PATH;
                }
            } else if (state.appState == APP_STATE_ENTER_TXT_PATH) {
                // Click Select File
                if (mx >= state.customBtnRect2.x && mx <= (state.customBtnRect2.x + state.customBtnRect2.w) &&
                    my >= state.customBtnRect2.y && my <= (state.customBtnRect2.y + state.customBtnRect2.h)) {
                    std::string path = OpenFileDialog();
                    if (!path.empty()) {
                        InitAdventureSetup(path);
                    }
                }
            } else if (state.appState == APP_STATE_SELECT_BOOK) {
                int cardX = WINDOW_WIDTH / 2 - 300;
                int cardY = WINDOW_HEIGHT / 2 - 220;
                
                // Back button click
                SDL_Rect backBtnRect = { cardX + 50, cardY + 365, 500, 45 };
                if (mx >= backBtnRect.x && mx <= (backBtnRect.x + backBtnRect.w) &&
                    my >= backBtnRect.y && my <= (backBtnRect.y + backBtnRect.h)) {
                    state.appState = APP_STATE_ENTER_TXT_PATH;
                }
                
                // Scrollable book buttons click
                int startBtnY = cardY + 130;
                int btnSpacing = 55;
                for (size_t i = 0; i < state.availableBooks.size(); i++) {
                    SDL_Rect btnRect = { cardX + 50, startBtnY + (int)i * btnSpacing - state.bookSelectScrollOffset, 500, 45 };
                    if (my >= cardY + 130 && my <= cardY + 340 &&
                        mx >= btnRect.x && mx <= (btnRect.x + btnRect.w) &&
                        my >= btnRect.y && my <= (btnRect.y + btnRect.h)) {
                        std::string path = state.availableBooks[i].path;
                        InitAdventureSetup(path);
                        break;
                    }
                }
            } else if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP) {
                // Check if they clicked the Font Increase 'A' button at top-left
                if (mx >= state.fontIncBtnRect.x && mx <= (state.fontIncBtnRect.x + state.fontIncBtnRect.w) &&
                    my >= state.fontIncBtnRect.y && my <= (state.fontIncBtnRect.y + state.fontIncBtnRect.h)) {
                    state.mutex.lock();
                    if (state.fontSizeOffset < 8) { // Maximum font increase limit (+8px)
                        state.fontSizeOffset += 2;
                        ApplyFontScale();
                        SaveFontSizeOffsetToSettings(state.fontSizeOffset);
                    }
                    state.mutex.unlock();
                    continue; // Skip further event processing
                }
                
                // Check if they clicked the Font Decrease 'a' button at top-left
                if (mx >= state.fontDecBtnRect.x && mx <= (state.fontDecBtnRect.x + state.fontDecBtnRect.w) &&
                    my >= state.fontDecBtnRect.y && my <= (state.fontDecBtnRect.y + state.fontDecBtnRect.h)) {
                    state.mutex.lock();
                    if (state.fontSizeOffset > -4) { // Minimum font decrease limit (-4px)
                        state.fontSizeOffset -= 2;
                        ApplyFontScale();
                        SaveFontSizeOffsetToSettings(state.fontSizeOffset);
                    }
                    state.mutex.unlock();
                    continue; // Skip further event processing
                }

                // Check if they clicked the Home / Main Menu button at top-right
                if (mx >= state.homeBtnRect.x && mx <= (state.homeBtnRect.x + state.homeBtnRect.w) &&
                    my >= state.homeBtnRect.y && my <= (state.homeBtnRect.y + state.homeBtnRect.h)) {
                    state.mutex.lock();
                    state.modelState.gameWon = false;
                    state.modelState.gameOver = false;
                    
                    // Increment query ID to invalidate any background AI threads
                    state.currentQueryId++;
                    
                    bool cancelledChoice = false;
                    if (state.aiThinking) {
                        state.aiThinking = false;
                        state.responseReady = false;
                        state.pendingResponse = "";
                        
                        // Revert the last user choice in the dialogue if it's the last message
                        if (!state.modelState.messages.empty() && state.modelState.messages.back().sender == "User") {
                            state.modelState.messages.pop_back();
                        }
                        
                        // Restore previous choices so the user can select again
                        if (state.modelState.pendingNextChapter != -1) {
                            state.modelState.activeChoices = { state.transitionPrefix + std::to_string(state.modelState.pendingNextChapter) };
                            state.modelState.pendingNextChapter = -1;
                        } else if (!state.savedChoices.empty()) {
                            state.modelState.activeChoices = state.savedChoices;
                            state.savedChoices.clear();
                        }
                        
                        SyncModelToUi();
                        cancelledChoice = true;
                    }
                    state.mutex.unlock();
                    
                    if (cancelledChoice) {
                        SaveGame();
                    }
                    
                    if (IsBookValid("book.json")) {
                        state.appState = APP_STATE_ASK_CONTINUE;
                    } else {
                        state.appState = APP_STATE_ENTER_TXT_PATH;
                    }
                    continue; // Skip further event processing
                }

                if (state.modelState.gameWon) {
                    if (my >= 42 && mx >= state.victoryBtnRect.x && mx <= (state.victoryBtnRect.x + state.victoryBtnRect.w) &&
                        my >= state.victoryBtnRect.y && my <= (state.victoryBtnRect.y + state.victoryBtnRect.h)) {
                        RestartAdventure();
                    } else if (my >= 42 && mx >= state.victoryBtnRect2.x && mx <= (state.victoryBtnRect2.x + state.victoryBtnRect2.w) &&
                               my >= state.victoryBtnRect2.y && my <= (state.victoryBtnRect2.y + state.victoryBtnRect2.h)) {
                        std::remove("save.json");
                        std::remove("../save.json");
                        state.mutex.lock();
                        state.modelState.gameWon = false;
                        state.modelState.gameOver = false;
                        state.modelState.currentChapter = 1;
                        state.modelState.chapterSummaries.clear();
                        state.modelState.messages.clear();
                        state.modelState.activeChoices.clear();
                        state.uiMessages.clear();
                        state.uiActiveChoices.clear();
                        state.mutex.unlock();
                        state.appState = APP_STATE_ENTER_TXT_PATH;
                    }
                } else if (state.modelState.gameOver) {
                    if (my >= 42 && mx >= state.deathBtnRect.x && mx <= (state.deathBtnRect.x + state.deathBtnRect.w) &&
                        my >= state.deathBtnRect.y && my <= (state.deathBtnRect.y + state.deathBtnRect.h)) {
                        state.modelState.gameOver = false;
                        state.modelState.messages.clear();
                        state.modelState.pendingNextChapter = -1;
                        SaveGame();
                        UpdateSystemPrompt();
                        SubmitQuery(GetUiText("prompt_restart_chapter"), false, false);
                    }
                } else {
                    // Check if they clicked the clear button or confirm button
                    if (mx >= state.clearBtnRect.x && mx <= (state.clearBtnRect.x + state.clearBtnRect.w) &&
                        my >= state.clearBtnRect.y && my <= (state.clearBtnRect.y + state.clearBtnRect.h)) {
                        state.inputText = "";
                    } else if (mx >= state.confirmBtnRect.x && mx <= (state.confirmBtnRect.x + state.confirmBtnRect.w) &&
                               my >= state.confirmBtnRect.y && my <= (state.confirmBtnRect.y + state.confirmBtnRect.h)) {
                        SubmitInputText();
                    } else {
                        // Check dynamic card hitboxes
                        state.mutex.lock();
                        bool cardClicked = false;
                        std::string clickedText = "";
                        for (const auto& opt : state.uiActiveChoices) {
                            if (mx >= opt.rect.x && mx <= (opt.rect.x + opt.rect.w) &&
                                my >= opt.rect.y && my <= (opt.rect.y + opt.rect.h)) {
                                clickedText = opt.text;
                                cardClicked = true;
                                break;
                            }
                        }
                        state.mutex.unlock();
                        
                        if (cardClicked && !state.aiThinking) {
                            if (state.appState == APP_STATE_SETUP) {
                                SubmitSetupChoice(clickedText);
                            } else {
                                if (clickedText == "Повторить запрос") {
                                    state.mutex.lock();
                                    if (!state.modelState.messages.empty() && state.modelState.messages.back().sender == "AI") {
                                        state.modelState.messages.pop_back(); // Remove the error bubble from history
                                    }
                                    state.mutex.unlock();
                                    SubmitQuery(state.modelState.lastQuery, true);
                                } else if (clickedText == GetUiText("btn_force_next_chapter")) {
                                    state.mutex.lock();
                                    state.modelState.pendingNextChapter = state.modelState.currentChapter + 1;
                                    state.modelState.activeChoices = { state.transitionPrefix + std::to_string(state.modelState.pendingNextChapter) };
                                    state.mutex.unlock();
                                    SubmitQuery(state.transitionPrefix + std::to_string(state.modelState.currentChapter + 1));
                                } else {
                                    SubmitQuery(clickedText);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    
    // 3. Clear canvas with dark slate blue HSL background
    SDL_SetRenderDrawColor(state.renderer.get(), 18, 18, 26, 255);
    SDL_RenderClear(state.renderer.get());
    
    // Render Neon blue layout guidelines
    SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 12);
    for (int x = 0; x < WINDOW_WIDTH; x += 40) {
        SDL_RenderDrawLine(state.renderer.get(), x, 42, x, 42 + viewportH);
    }
    for (int y = 42; y < 42 + viewportH; y += 40) {
        SDL_RenderDrawLine(state.renderer.get(), 0, y, WINDOW_WIDTH, y);
    }
    
    if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP) {
        // 4. Render Scrollable dialogue bubbles
        SDL_Rect clipRect = { 0, 42, WINDOW_WIDTH, viewportH };
        SDL_RenderSetClipRect(state.renderer.get(), &clipRect);
        
        int bubbleY = 42 + 20 - state.scrollOffset;
        int lineH = TTF_FontHeight(state.fontMessage.get());
        
        state.mutex.lock();
        for (const auto& msg : state.uiMessages) {
            int actualBubbleW = WINDOW_WIDTH - 40;
            int actualBubbleH = msg.lines.size() * (lineH + 4) + 16;
            
            SDL_Rect bubbleRect;
            SDL_Color bubbleColor;
            SDL_Color textColor;
            
            bubbleRect.x = 20;
            bubbleRect.y = bubbleY;
            bubbleRect.w = actualBubbleW;
            bubbleRect.h = actualBubbleH;
            
            if (msg.sender == "User") {
                bubbleColor = { 35, 78, 142, 255 }; // Slate Blue bubble
                textColor = { 255, 255, 255, 255 };
            } else {
                bubbleColor = { 38, 38, 52, 255 }; // Deep Purple bubble
                textColor = { 220, 220, 240, 255 };
            }
            
            // Draw elegant rounded dialog card
            DrawRoundedRect(state.renderer.get(), bubbleRect, 10, bubbleColor);
            
            // Render pre-wrapped message lines inside card
            int textY = bubbleRect.y + 8;
            for (const auto& line : msg.lines) {
                int textX = bubbleRect.x + 12;
                RenderText(state.renderer.get(), state.fontMessage.get(), line, textX, textY, textColor);
                textY += lineH + 4;
            }
            
            bubbleY += actualBubbleH + 15; // Vertical flow spacing
        }
        
        // Draw Victory block inside the scrollable chat feed if game is won
        state.victoryBtnRect = { 0, 0, 0, 0 };
        if (state.modelState.gameWon) {
            int titleH = TTF_FontHeight(state.fontTitle.get());
            int textY = bubbleY + 10;
            SDL_Color goldColor = { 255, 215, 0, 255 }; // Gold for victory!
            
            RenderText(state.renderer.get(), state.fontTitle.get(), GetUiText("victory_title"), WINDOW_WIDTH / 2, textY + titleH, goldColor, true);
            
            textY += titleH + 15;
            
            SDL_Rect btnRect1 = { WINDOW_WIDTH / 2 - 180, textY, 360, 48 };
            SDL_Rect btnRect2 = { WINDOW_WIDTH / 2 - 180, textY + 48 + 12, 360, 48 };
            state.victoryBtnRect = btnRect1;
            state.victoryBtnRect2 = btnRect2;
            
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            
            bool hovered1 = false;
            if (my >= 42 && my < 42 + viewportH) {
                hovered1 = (mx >= btnRect1.x && mx <= (btnRect1.x + btnRect1.w) &&
                            my >= btnRect1.y && my <= (btnRect1.y + btnRect1.h));
            }
            bool hovered2 = false;
            if (my >= 42 && my < 42 + viewportH) {
                hovered2 = (mx >= btnRect2.x && mx <= (btnRect2.x + btnRect2.w) &&
                            my >= btnRect2.y && my <= (btnRect2.y + btnRect2.h));
            }
            
            SDL_Color cardColor1 = hovered1 ? SDL_Color{ 40, 150, 80, 255 } : SDL_Color{ 20, 100, 50, 255 };
            SDL_Color txtColor1 = hovered1 ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 220, 255, 220, 255 };
            
            SDL_Color cardColor2 = hovered2 ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 255 };
            SDL_Color txtColor2 = hovered2 ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
            
            DrawRoundedRect(state.renderer.get(), btnRect1, 8, cardColor1);
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("btn_restart_adventure"), btnRect1.x + btnRect1.w / 2, btnRect1.y + btnRect1.h / 2 + 8, txtColor1, true);
            
            DrawRoundedRect(state.renderer.get(), btnRect2, 8, cardColor2);
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("btn_choose_another_book"), btnRect2.x + btnRect2.w / 2, btnRect2.y + btnRect2.h / 2 + 8, txtColor2, true);
            
            bubbleY += titleH + 15 + 108 + 25;
        }
        
        // Draw Game Over block inside the scrollable chat feed if character dies
        state.deathBtnRect = { 0, 0, 0, 0 };
        if (state.modelState.gameOver) {
            int titleH = TTF_FontHeight(state.fontTitle.get());
            int textY = bubbleY + 10;
            SDL_Color redColor = { 255, 60, 60, 255 };
            
            // "ГЕРОЙ ПОГИБ" - Large title in Red, centered
            RenderText(state.renderer.get(), state.fontTitle.get(), GetUiText("death_title"), WINDOW_WIDTH / 2, textY + titleH, redColor, true);
            
            textY += titleH + 15;
            
            // Dynamic retry button below the header
            SDL_Rect btnRect = { WINDOW_WIDTH / 2 - 180, textY, 360, 48 };
            state.deathBtnRect = btnRect;
            
            int mx, my;
            SDL_GetMouseState(&mx, &my);
            bool hovered = false;
            if (my >= 42 && my < 42 + viewportH) {
                hovered = (mx >= btnRect.x && mx <= (btnRect.x + btnRect.w) &&
                           my >= btnRect.y && my <= (btnRect.y + btnRect.h));
            }
            
            SDL_Color cardColor = hovered ? SDL_Color{ 180, 40, 40, 255 } : SDL_Color{ 100, 20, 25, 255 };
            SDL_Color txtColor = hovered ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 255, 150, 150, 255 };
            
            DrawRoundedRect(state.renderer.get(), btnRect, 8, cardColor);
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("btn_restart_chapter"), btnRect.x + btnRect.w / 2, btnRect.y + btnRect.h / 2 + 8, txtColor, true);
            
            bubbleY += titleH + 15 + btnRect.h + 25;
        }
        
        // Update maximum vertical offset scroll bounds
        int totalContentHeight = bubbleY + state.scrollOffset - 62;
        if (totalContentHeight > viewportH) {
            state.maxScrollOffset = totalContentHeight - viewportH;
        } else {
            state.maxScrollOffset = 0;
        }
        if (state.scrollToBottom) {
            state.scrollOffset = state.maxScrollOffset;
            state.scrollToBottom = false;
        }
        state.mutex.unlock();
        
        // Reset ClipRect to allow layout shelf renders
        SDL_RenderSetClipRect(state.renderer.get(), nullptr);

        // 1. Render Top Header bar background
        SDL_Rect headerBg = { 0, 0, WINDOW_WIDTH, 42 };
        SDL_SetRenderDrawColor(state.renderer.get(), 20, 20, 28, 255);
        SDL_RenderFillRect(state.renderer.get(), &headerBg);
        
        // Render subtle bottom border for the header
        SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
        SDL_RenderDrawLine(state.renderer.get(), 0, 41, WINDOW_WIDTH, 41);
        
        // Render centered Book Title in header
        if (!state.modelState.bookTitle.empty()) {
            SDL_Color titleCol = { 200, 200, 220, 255 };
            std::string headerTitle = state.modelState.bookTitle;
            int maxTitleW = WINDOW_WIDTH - 300;
            if (maxTitleW < 200) maxTitleW = 200;
            
            int tw = 0, th = 0;
            TTF_SizeUTF8(state.fontUI.get(), headerTitle.c_str(), &tw, &th);
            if (tw > maxTitleW) {
                while (!headerTitle.empty() && tw > maxTitleW - 20) {
                    PopUTF8Character(headerTitle);
                    std::string testStr = headerTitle + "...";
                    TTF_SizeUTF8(state.fontUI.get(), testStr.c_str(), &tw, &th);
                }
                headerTitle += "...";
            }
            
            RenderText(state.renderer.get(), state.fontUI.get(), headerTitle, 
                       WINDOW_WIDTH / 2, 21, titleCol, true);
        }
        
        // 1.5. Render "Chapter N of Y" or "Epilogue" next to the Home button
        std::string labelText = "";
        int currentChapter = state.modelState.currentChapter;
        int totalChapters = state.modelState.chapters.size();
        if (totalChapters == 0) totalChapters = 4; // safety fallback
        
        if (state.modelState.gameWon) {
            labelText = GetUiText("header_epilogue");
        } else {
            labelText = GetUiText("header_chapter") + " " + std::to_string(currentChapter) + " " + GetUiText("header_of") + " " + std::to_string(totalChapters);
        }
        
        SDL_Color labelCol = { 150, 150, 180, 255 }; // Premium soft slate blue/gray
        int labelTw = 0, labelTh = 0;
        TTF_SizeUTF8(state.fontSmallUI.get(), labelText.c_str(), &labelTw, &labelTh);
        int labelX = (WINDOW_WIDTH - 160) - labelTw / 2;
        RenderText(state.renderer.get(), state.fontSmallUI.get(), labelText, labelX, 21, labelCol, true);

        // 1.8. Render font increase/decrease scaling buttons on top-left of header
        state.fontIncBtnRect = { 10, 6, 30, 30 };
        state.fontDecBtnRect = { 46, 6, 30, 30 };
        
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        bool hoverInc = (mx >= state.fontIncBtnRect.x && mx <= state.fontIncBtnRect.x + state.fontIncBtnRect.w &&
                         my >= state.fontIncBtnRect.y && my <= state.fontIncBtnRect.y + state.fontIncBtnRect.h);
        bool hoverDec = (mx >= state.fontDecBtnRect.x && mx <= state.fontDecBtnRect.x + state.fontDecBtnRect.w &&
                         my >= state.fontDecBtnRect.y && my <= state.fontDecBtnRect.y + state.fontDecBtnRect.h);
        
        SDL_Color incBgCol = hoverInc ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 220 };
        SDL_Color incTxtCol = hoverInc ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
        
        SDL_Color decBgCol = hoverDec ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 220 };
        SDL_Color decTxtCol = hoverDec ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
        
        // Draw Increase button 'A'
        DrawRoundedRect(state.renderer.get(), state.fontIncBtnRect, 6, incBgCol);
        if (hoverInc) {
            SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 255);
        } else {
            SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
        }
        SDL_RenderDrawRect(state.renderer.get(), &state.fontIncBtnRect);
        RenderText(state.renderer.get(), state.fontUI.get(), "A", 
                   state.fontIncBtnRect.x + state.fontIncBtnRect.w / 2, 
                   state.fontIncBtnRect.y + state.fontIncBtnRect.h / 2 + 4, 
                   incTxtCol, true);
                   
        // Draw Decrease button 'a'
        DrawRoundedRect(state.renderer.get(), state.fontDecBtnRect, 6, decBgCol);
        if (hoverDec) {
            SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 255);
        } else {
            SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
        }
        SDL_RenderDrawRect(state.renderer.get(), &state.fontDecBtnRect);
        RenderText(state.renderer.get(), state.fontUI.get(), "a", 
                   state.fontDecBtnRect.x + state.fontDecBtnRect.w / 2, 
                   state.fontDecBtnRect.y + state.fontDecBtnRect.h / 2 + 4, 
                   decTxtCol, true);

        // 2. Render Home / Main Menu button at top-right
        state.homeBtnRect = { WINDOW_WIDTH - 140, 6, 120, 30 };
        SDL_GetMouseState(&mx, &my);
        bool hoverHome = (mx >= state.homeBtnRect.x && mx <= state.homeBtnRect.x + state.homeBtnRect.w &&
                          my >= state.homeBtnRect.y && my <= state.homeBtnRect.y + state.homeBtnRect.h);
        
        SDL_Color homeBgCol = hoverHome ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 220 };
        SDL_Color homeTxtCol = hoverHome ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
        
        DrawRoundedRect(state.renderer.get(), state.homeBtnRect, 6, homeBgCol);
        if (hoverHome) {
           SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 255); // Cyan border on hover
        } else {
            SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
        }
        SDL_RenderDrawRect(state.renderer.get(), &state.homeBtnRect);
        
        RenderText(state.renderer.get(), state.fontSmallUI.get(), GetUiText("btn_home"), 
                   state.homeBtnRect.x + state.homeBtnRect.w / 2, 
                   state.homeBtnRect.y + state.homeBtnRect.h / 2 + 4, 
                   homeTxtCol, true);
        
        // 3. Render dynamic scrolls bar indicator if overflowed
        if (state.maxScrollOffset > 0) {
            int barH = viewportH - 40;
            int scrollBarH = (viewportH * barH) / totalContentHeight;
            if (scrollBarH < 20) scrollBarH = 20;
            int scrollBarY = 62 + (state.scrollOffset * (barH - scrollBarH)) / state.maxScrollOffset;
            
            SDL_Rect barBg = { WINDOW_WIDTH - 8, 62, 4, barH };
            SDL_SetRenderDrawColor(state.renderer.get(), 30, 30, 40, 100);
            SDL_RenderFillRect(state.renderer.get(), &barBg);
            
            SDL_Rect barFg = { WINDOW_WIDTH - 8, scrollBarY, 4, scrollBarH };
            SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 180); // Cyan slider
            SDL_RenderFillRect(state.renderer.get(), &barFg);
        }
        
        // 5. Render dynamic choice cards shelf
        if (optionsAreaH > 0) {
            SDL_Rect shelfBg = { 0, 42 + viewportH, WINDOW_WIDTH, optionsAreaH };
            SDL_SetRenderDrawColor(state.renderer.get(), 24, 24, 34, 255);
            SDL_RenderFillRect(state.renderer.get(), &shelfBg);
            
            SDL_GetMouseState(&mx, &my);
            
            state.mutex.lock();
            for (int i = 0; i < (int)state.uiActiveChoices.size(); i++) {
                const auto& opt = state.uiActiveChoices[i];
                
                // Hover highlight check
                bool hovered = (mx >= opt.rect.x && mx <= (opt.rect.x + opt.rect.w) &&
                                my >= opt.rect.y && my <= (opt.rect.y + opt.rect.h));
                
                SDL_Color cardColor = hovered ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 255 };
                SDL_Color txtColor = hovered ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
                
                DrawRoundedRect(state.renderer.get(), opt.rect, 8, cardColor);
                
                // Wrap and render the option text prefixing it with the index number
                std::string displayText = std::to_string(i + 1) + ". " + opt.text;
                std::vector<std::string> optLines = WrapText(state.fontUI.get(), displayText, opt.rect.w - 16);
                
                TTF_Font* chosenFont = state.fontUI.get();
                int fontH = TTF_FontHeight(chosenFont);
                
                // If the text wrapped to more than 2 lines, fall back to fontSmallUI to prevent overflow!
                if (optLines.size() > 2) {
                    optLines = WrapText(state.fontSmallUI.get(), displayText, opt.rect.w - 16);
                    chosenFont = state.fontSmallUI.get();
                    fontH = TTF_FontHeight(chosenFont);
                }
                
                int lineSpacing = 1;
                int totalH = optLines.size() * (fontH + lineSpacing) - lineSpacing;
                
                // Safety cap: if lines still overflow card height, truncate the lines
                if (optLines.size() > 3) {
                    optLines.resize(3);
                    if (!optLines.empty()) {
                        optLines.back() += "...";
                    }
                    totalH = optLines.size() * (fontH + lineSpacing) - lineSpacing;
                }
                
                int startTextY = opt.rect.y + (opt.rect.h - totalH) / 2;
                
                for (size_t lineIdx = 0; lineIdx < optLines.size(); lineIdx++) {
                    RenderText(state.renderer.get(), chosenFont, optLines[lineIdx],
                               opt.rect.x + opt.rect.w / 2, startTextY + lineIdx * (fontH + lineSpacing) + fontH / 2,
                               txtColor, true);
                }
            }
            state.mutex.unlock();
        }
    } else {
        // Draw elegant centered startup cards (made taller to accommodate language selection and error messages)
        int cardW = 600;
        int cardH = 440;
        if (!state.fileLoadError.empty()) {
            cardH = 490;
        }
        int cardX = (WINDOW_WIDTH - cardW) / 2;
        int cardY = (WINDOW_HEIGHT - cardH) / 2;
        
        SDL_Rect borderRect = { cardX - 2, cardY - 2, cardW + 4, cardH + 4 };
        SDL_Rect cardRect = { cardX, cardY, cardW, cardH };
        
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        
        if (state.appState == APP_STATE_ASK_CONTINUE) {
            DrawRoundedRect(state.renderer.get(), borderRect, 12, SDL_Color{ 0, 192, 255, 180 }); // Cyan border
            DrawRoundedRect(state.renderer.get(), cardRect, 10, SDL_Color{ 20, 20, 30, 250 });   // Deep dark background
            
            RenderText(state.renderer.get(), state.fontTitle.get(), GetUiText("continue_title"), WINDOW_WIDTH / 2, cardY + 40, SDL_Color{ 255, 215, 0, 255 }, true);
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("book_detected"), WINDOW_WIDTH / 2, cardY + 85, SDL_Color{ 200, 200, 220, 255 }, true);
            // Render book title dynamically adapting to card width to prevent text overflow
            std::string displayTitle = state.modelState.bookTitle;
            int tw = 0, th = 0;
            if (TTF_SizeUTF8(state.fontTitle.get(), displayTitle.c_str(), &tw, &th) == 0) {
                if (tw > cardW - 60) { // If it's wider than 540px
                    // Try with the UI font (which is smaller, 16px instead of 24px)
                    int tw2 = 0, th2 = 0;
                    if (TTF_SizeUTF8(state.fontUI.get(), displayTitle.c_str(), &tw2, &th2) == 0 && tw2 <= cardW - 60) {
                        RenderText(state.renderer.get(), state.fontUI.get(), displayTitle, WINDOW_WIDTH / 2, cardY + 115, SDL_Color{ 0, 255, 220, 255 }, true);
                    } else {
                        // Still too long. Truncate it character by character with "..."
                        std::string truncated = displayTitle;
                        while (!truncated.empty()) {
                            PopUTF8Character(truncated);
                            std::string testStr = truncated + "...";
                            int tw3 = 0, th3 = 0;
                            if (TTF_SizeUTF8(state.fontUI.get(), testStr.c_str(), &tw3, &th3) == 0 && tw3 <= cardW - 60) {
                                displayTitle = testStr;
                                break;
                            }
                        }
                        RenderText(state.renderer.get(), state.fontUI.get(), displayTitle, WINDOW_WIDTH / 2, cardY + 115, SDL_Color{ 0, 255, 220, 255 }, true);
                    }
                } else {
                    RenderText(state.renderer.get(), state.fontTitle.get(), displayTitle, WINDOW_WIDTH / 2, cardY + 115, SDL_Color{ 0, 255, 220, 255 }, true);
                }
            } else {
                RenderText(state.renderer.get(), state.fontTitle.get(), displayTitle, WINDOW_WIDTH / 2, cardY + 115, SDL_Color{ 0, 255, 220, 255 }, true);
            }
            
            // Buttons shifted slightly upwards to fit the bottom selector
            state.customBtnRect1 = { cardX + 50, cardY + 170, 500, 45 };
            state.customBtnRect2 = { cardX + 50, cardY + 230, 500, 45 };
            
            bool hoveredYes = (mx >= state.customBtnRect1.x && mx <= (state.customBtnRect1.x + state.customBtnRect1.w) &&
                               my >= state.customBtnRect1.y && my <= (state.customBtnRect1.y + state.customBtnRect1.h));
            bool hoveredNo = (mx >= state.customBtnRect2.x && mx <= (state.customBtnRect2.x + state.customBtnRect2.w) &&
                              my >= state.customBtnRect2.y && my <= (state.customBtnRect2.y + state.customBtnRect2.h));
            
            SDL_Color colorYes = hoveredYes ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 255 };
            SDL_Color colorNo = hoveredNo ? SDL_Color{ 172, 45, 98, 255 } : SDL_Color{ 34, 34, 46, 255 };
            
            SDL_Color textColYes = hoveredYes ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
            SDL_Color textColNo = hoveredNo ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 255, 100, 100, 255 };
            
            DrawRoundedRect(state.renderer.get(), state.customBtnRect1, 8, colorYes);
            DrawRoundedRect(state.renderer.get(), state.customBtnRect2, 8, colorNo);
            
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("btn_yes"), state.customBtnRect1.x + state.customBtnRect1.w / 2, state.customBtnRect1.y + state.customBtnRect1.h / 2 + 8, textColYes, true);
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("btn_no"), state.customBtnRect2.x + state.customBtnRect2.w / 2, state.customBtnRect2.y + state.customBtnRect2.h / 2 + 8, textColNo, true);
            
        } else if (state.appState == APP_STATE_ENTER_TXT_PATH) {
            DrawRoundedRect(state.renderer.get(), borderRect, 12, SDL_Color{ 142, 60, 220, 180 }); // Purple border
            DrawRoundedRect(state.renderer.get(), cardRect, 10, SDL_Color{ 20, 20, 30, 250 });   // Deep dark background
            
            RenderText(state.renderer.get(), state.fontTitle.get(), GetUiText("load_title"), WINDOW_WIDTH / 2, cardY + 40, SDL_Color{ 255, 80, 180, 255 }, true);
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("load_prompt"), WINDOW_WIDTH / 2, cardY + 90, SDL_Color{ 200, 200, 220, 255 }, true);
            
            // Choose file button (slightly shorter height to be sleek)
            state.customBtnRect2 = { cardX + 100, cardY + 150, 400, 55 };
            bool hoveredBtn = (mx >= state.customBtnRect2.x && mx <= (state.customBtnRect2.x + state.customBtnRect2.w) &&
                               my >= state.customBtnRect2.y && my <= (state.customBtnRect2.y + state.customBtnRect2.h));
            
            SDL_Color colorBtn = hoveredBtn ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 255 };
            SDL_Color textColBtn = hoveredBtn ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
            
            DrawRoundedRect(state.renderer.get(), state.customBtnRect2, 8, colorBtn);
            RenderText(state.renderer.get(), state.fontUI.get(), GetUiText("btn_select_file"), state.customBtnRect2.x + state.customBtnRect2.w / 2, state.customBtnRect2.y + state.customBtnRect2.h / 2 + 8, textColBtn, true);
            
            // Drag & drop hint
            std::string dropHint = IsRussianLanguage(state.gameLanguage) ? "[ Перетащите файл книги в окно ]" : "[ Drag & drop book file anywhere ]";
            RenderText(state.renderer.get(), state.fontSmallUI.get(), dropHint, WINDOW_WIDTH / 2, cardY + 235, SDL_Color{ 120, 120, 150, 255 }, true);
            
            // Error handling (wrapped and shifted down to fit beautifully inside the card)
            if (!state.fileLoadError.empty()) {
                std::vector<std::string> errLines = WrapText(state.fontSmallUI.get(), state.fileLoadError, cardW - 60);
                int errY = cardY + 360;
                for (const auto& line : errLines) {
                    RenderText(state.renderer.get(), state.fontSmallUI.get(), line, WINDOW_WIDTH / 2, errY, SDL_Color{ 255, 80, 80, 255 }, true);
                    errY += 18;
                }
            }
        }
        
        // Draw elegant inline language and AI selector bars side-by-side at the bottom of startup cards
        if (state.appState == APP_STATE_ASK_CONTINUE || state.appState == APP_STATE_ENTER_TXT_PATH) {
            state.langBtnRect = { cardX + 50, cardY + 300, 240, 45 };
            state.aiBtnRect = { cardX + 310, cardY + 300, 240, 45 };
            
            bool hoveredLang = (mx >= state.langBtnRect.x && mx <= (state.langBtnRect.x + state.langBtnRect.w) &&
                                my >= state.langBtnRect.y && my <= (state.langBtnRect.y + state.langBtnRect.h));
            bool hoveredAi = (mx >= state.aiBtnRect.x && mx <= (state.aiBtnRect.x + state.aiBtnRect.w) &&
                              my >= state.aiBtnRect.y && my <= (state.aiBtnRect.y + state.aiBtnRect.h));
            
            // Render Language Button
            if (!state.editingLanguage) {
                SDL_Color borderCol = hoveredLang ? SDL_Color{ 0, 255, 220, 255 } : SDL_Color{ 0, 192, 255, 120 };
                SDL_Color bgCol = hoveredLang ? SDL_Color{ 34, 50, 68, 250 } : SDL_Color{ 25, 25, 35, 200 };
                
                SDL_Rect outlineRect = { state.langBtnRect.x - 1, state.langBtnRect.y - 1, state.langBtnRect.w + 2, state.langBtnRect.h + 2 };
                DrawRoundedRect(state.renderer.get(), outlineRect, 8, borderCol);
                DrawRoundedRect(state.renderer.get(), state.langBtnRect, 7, bgCol);
                
                std::string btnText = GetUiText("lang_btn_prefix") + state.gameLanguage;
                RenderText(state.renderer.get(), state.fontUI.get(), btnText, state.langBtnRect.x + state.langBtnRect.w / 2, state.langBtnRect.y + state.langBtnRect.h / 2 + 8, SDL_Color{ 255, 255, 255, 255 }, true);
            } else {
                DrawRoundedRect(state.renderer.get(), state.langBtnRect, 8, SDL_Color{ 0, 255, 220, 255 });
                SDL_Rect innerInput = { state.langBtnRect.x + 2, state.langBtnRect.y + 2, state.langBtnRect.w - 4, state.langBtnRect.h - 4 };
                DrawRoundedRect(state.renderer.get(), innerInput, 7, SDL_Color{ 10, 10, 15, 255 });
                
                std::string btnText = GetUiText("lang_btn_prefix") + state.inputText;
                RenderText(state.renderer.get(), state.fontUI.get(), btnText, state.langBtnRect.x + state.langBtnRect.w / 2, state.langBtnRect.y + state.langBtnRect.h / 2 + 8, SDL_Color{ 0, 255, 220, 255 }, true);
                
                Uint32 pulseTicks = SDL_GetTicks();
                Uint8 alpha = 130 + 120 * (0.5 + 0.5 * sin(pulseTicks * 0.005));
                SDL_Color guideColor = { 150, 150, 180, alpha };
                
                std::string guideText = "[ Use the bottom input line, Esc to cancel ]";
                if (IsRussianLanguage(state.gameLanguage)) {
                    guideText = "[ Используйте строку ввода внизу, Esc для отмены ]";
                } else if (IsUkrainianLanguage(state.gameLanguage)) {
                    guideText = "[ Використовуйте рядок введення внизу, Esc для скасування ]";
                }
                RenderText(state.renderer.get(), state.fontSmallUI.get(), guideText, WINDOW_WIDTH / 2, state.langBtnRect.y + state.langBtnRect.h + 12, guideColor, true);
            }
            
            // Render AI Selector Button
            SDL_Color borderColAi = hoveredAi ? SDL_Color{ 0, 255, 220, 255 } : SDL_Color{ 0, 192, 255, 120 };
            SDL_Color bgColAi = hoveredAi ? SDL_Color{ 34, 50, 68, 250 } : SDL_Color{ 25, 25, 35, 200 };
            
            SDL_Rect outlineRectAi = { state.aiBtnRect.x - 1, state.aiBtnRect.y - 1, state.aiBtnRect.w + 2, state.aiBtnRect.h + 2 };
            DrawRoundedRect(state.renderer.get(), outlineRectAi, 8, borderColAi);
            DrawRoundedRect(state.renderer.get(), state.aiBtnRect, 7, bgColAi);
            
            std::string aiDisplay = state.aiModelName;
            if (aiDisplay.length() > 5 && aiDisplay.substr(aiDisplay.length() - 5) == ".json") {
                aiDisplay = aiDisplay.substr(0, aiDisplay.length() - 5);
            }
            if (aiDisplay.rfind("ai_", 0) == 0) {
                aiDisplay = aiDisplay.substr(3); // skip "ai_"
            }
            
            std::string aiBtnText = "AI: " + aiDisplay;
            RenderText(state.renderer.get(), state.fontUI.get(), aiBtnText, state.aiBtnRect.x + state.aiBtnRect.w / 2, state.aiBtnRect.y + state.aiBtnRect.h / 2 + 8, SDL_Color{ 255, 255, 255, 255 }, true);
        } else if (state.appState == APP_STATE_SELECT_AI) {
            DrawRoundedRect(state.renderer.get(), borderRect, 12, SDL_Color{ 0, 255, 220, 180 }); // Cyan border
            DrawRoundedRect(state.renderer.get(), cardRect, 10, SDL_Color{ 20, 20, 30, 250 });   // Deep dark background
            
            std::string selectTitle = GetUiText("apikey_select_title");
            std::string selectPrompt = GetUiText("apikey_select_prompt");
            
            RenderText(state.renderer.get(), state.fontTitle.get(), selectTitle, WINDOW_WIDTH / 2, cardY + 40, SDL_Color{ 255, 215, 0, 255 }, true);
            RenderText(state.renderer.get(), state.fontUI.get(), selectPrompt, WINDOW_WIDTH / 2, cardY + 85, SDL_Color{ 200, 200, 220, 255 }, true);
            
            int startBtnY = cardY + 130;
            int btnSpacing = 55;
            int listH = 210;
            
            // Calculate max scroll dynamically
            int totalListH = (int)state.availableAiModels.size() * btnSpacing - 10;
            state.aiSelectMaxScroll = totalListH > listH ? totalListH - listH : 0;
            if (state.aiSelectScrollOffset > state.aiSelectMaxScroll) {
                state.aiSelectScrollOffset = state.aiSelectMaxScroll;
            }
            
            // Set clipping rectangle to allow clean scrolling inside card bounds
            SDL_Rect clipRect = { cardX + 20, cardY + 130, cardW - 40, listH };
            SDL_RenderSetClipRect(state.renderer.get(), &clipRect);
            
            for (size_t i = 0; i < state.availableAiModels.size(); i++) {
                SDL_Rect btnRect = { cardX + 50, startBtnY + (int)i * btnSpacing - state.aiSelectScrollOffset, 500, 45 };
                bool hovered = (my >= cardY + 130 && my <= cardY + 340 &&
                                mx >= btnRect.x && mx <= (btnRect.x + btnRect.w) &&
                                my >= btnRect.y && my <= (btnRect.y + btnRect.h));
                                
                SDL_Color btnCol = hovered ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 255 };
                SDL_Color textCol = hovered ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
                
                // Draw elegant outline for active choice or currently editing model
                if (state.editingApiKey && state.availableAiModels[i].filename == state.selectedAiFilename) {
                    SDL_Rect outlineRect = { btnRect.x - 2, btnRect.y - 2, btnRect.w + 4, btnRect.h + 4 };
                    DrawRoundedRect(state.renderer.get(), outlineRect, 8, SDL_Color{ 0, 255, 220, 255 });
                } else if (state.availableAiModels[i].filename == state.aiModelName) {
                    SDL_Rect outlineRect = { btnRect.x - 2, btnRect.y - 2, btnRect.w + 4, btnRect.h + 4 };
                    DrawRoundedRect(state.renderer.get(), outlineRect, 8, SDL_Color{ 255, 215, 0, 255 });
                }
                
                DrawRoundedRect(state.renderer.get(), btnRect, 8, btnCol);
                
                std::string btnText = std::to_string(i + 1) + ". " + state.availableAiModels[i].modelName + " (" + state.availableAiModels[i].filename + ")";
                RenderText(state.renderer.get(), state.fontUI.get(), btnText, btnRect.x + btnRect.w / 2, btnRect.y + btnRect.h / 2 + 8, textCol, true);
            }
            
            // Clear clipping rectangle to allow rendering buttons outside of it
            SDL_RenderSetClipRect(state.renderer.get(), nullptr);
            
            // Render premium scrollbar indicator on the right of the scrollable panel
            if (state.aiSelectMaxScroll > 0) {
                int scrollBarW = 4;
                int scrollBarH = (listH * listH) / totalListH;
                int scrollBarY = cardY + 130 + (state.aiSelectScrollOffset * (listH - scrollBarH)) / state.aiSelectMaxScroll;
                SDL_Rect scrollBar = { cardX + cardW - 15, scrollBarY, scrollBarW, scrollBarH };
                DrawRoundedRect(state.renderer.get(), scrollBar, 2, SDL_Color{ 0, 192, 255, 180 }); // Cyan scrollbar
            }
            
            // Draw elegant red Back button positioned statically at the bottom
            SDL_Rect backBtnRect = { cardX + 50, cardY + 365, 500, 45 };
            bool hoveredBack = (mx >= backBtnRect.x && mx <= (backBtnRect.x + backBtnRect.w) &&
                                my >= backBtnRect.y && my <= (backBtnRect.y + backBtnRect.h));
            SDL_Color backCol = hoveredBack ? SDL_Color{ 172, 45, 98, 255 } : SDL_Color{ 34, 34, 46, 255 };
            SDL_Color textColBack = hoveredBack ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 255, 100, 100, 255 };
            
            DrawRoundedRect(state.renderer.get(), backBtnRect, 8, backCol);
            std::string backText = GetUiText("apikey_back");
            RenderText(state.renderer.get(), state.fontUI.get(), backText, backBtnRect.x + backBtnRect.w / 2, backBtnRect.y + backBtnRect.h / 2 + 8, textColBack, true);
            
            if (state.editingApiKey) {
                Uint32 pulseTicks = SDL_GetTicks();
                Uint8 alpha = 130 + 120 * (0.5 + 0.5 * sin(pulseTicks * 0.005));
                SDL_Color guideColor = { 0, 255, 220, alpha };
                
                std::string guideText = GetUiText("apikey_guide");
                RenderText(state.renderer.get(), state.fontSmallUI.get(), guideText, WINDOW_WIDTH / 2, cardY + cardH + 15, guideColor, true);
            }
        } else if (state.appState == APP_STATE_SELECT_BOOK) {
            DrawRoundedRect(state.renderer.get(), borderRect, 12, SDL_Color{ 142, 60, 220, 180 }); // Purple border
            DrawRoundedRect(state.renderer.get(), cardRect, 10, SDL_Color{ 20, 20, 30, 250 });   // Deep dark background
            
            std::string selectTitle = IsRussianLanguage(state.gameLanguage) ? "ВЫБОР КНИГИ" : "SELECT BOOK FILE";
            std::string selectPrompt = IsRussianLanguage(state.gameLanguage) ? "Выберите книгу (.txt или .json) для начала приключения:" : "Select a book file (.txt or .json) to start your adventure:";
            
            RenderText(state.renderer.get(), state.fontTitle.get(), selectTitle, WINDOW_WIDTH / 2, cardY + 40, SDL_Color{ 255, 80, 180, 255 }, true);
            RenderText(state.renderer.get(), state.fontUI.get(), selectPrompt, WINDOW_WIDTH / 2, cardY + 85, SDL_Color{ 200, 200, 220, 255 }, true);
            
            int startBtnY = cardY + 130;
            int btnSpacing = 55;
            int listH = 210;
            
            // Calculate max scroll dynamically
            int totalListH = (int)state.availableBooks.size() * btnSpacing - 10;
            state.bookSelectMaxScroll = totalListH > listH ? totalListH - listH : 0;
            if (state.bookSelectScrollOffset > state.bookSelectMaxScroll) {
                state.bookSelectScrollOffset = state.bookSelectMaxScroll;
            }
            
            // Set clipping rectangle to allow clean scrolling inside card bounds
            SDL_Rect clipRect = { cardX + 20, cardY + 130, cardW - 40, listH };
            SDL_RenderSetClipRect(state.renderer.get(), &clipRect);
            
            if (state.availableBooks.empty()) {
                RenderText(state.renderer.get(), state.fontUI.get(), "[ No compatible books found in local directory ]", WINDOW_WIDTH / 2, cardY + 180, SDL_Color{ 120, 120, 150, 255 }, true);
            } else {
                for (size_t i = 0; i < state.availableBooks.size(); i++) {
                    SDL_Rect btnRect = { cardX + 50, startBtnY + (int)i * btnSpacing - state.bookSelectScrollOffset, 500, 45 };
                    bool hovered = (my >= cardY + 130 && my <= cardY + 340 &&
                                    mx >= btnRect.x && mx <= (btnRect.x + btnRect.w) &&
                                    my >= btnRect.y && my <= (btnRect.y + btnRect.h));
                                    
                    SDL_Color btnCol = hovered ? SDL_Color{ 45, 98, 172, 255 } : SDL_Color{ 34, 34, 46, 255 };
                    SDL_Color textCol = hovered ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 0, 192, 255, 255 };
                    
                    DrawRoundedRect(state.renderer.get(), btnRect, 8, btnCol);
                    
                    std::string btnText = std::to_string(i + 1) + ". " + state.availableBooks[i].filename;
                    RenderText(state.renderer.get(), state.fontUI.get(), btnText, btnRect.x + btnRect.w / 2, btnRect.y + btnRect.h / 2 + 8, textCol, true);
                }
            }
            
            // Clear clipping rectangle to allow rendering buttons outside of it
            SDL_RenderSetClipRect(state.renderer.get(), nullptr);
            
            // Render premium scrollbar indicator on the right of the scrollable panel
            if (state.bookSelectMaxScroll > 0) {
                int scrollBarW = 4;
                int scrollBarH = (listH * listH) / totalListH;
                int scrollBarY = cardY + 130 + (state.bookSelectScrollOffset * (listH - scrollBarH)) / state.bookSelectMaxScroll;
                SDL_Rect scrollBar = { cardX + cardW - 15, scrollBarY, scrollBarW, scrollBarH };
                DrawRoundedRect(state.renderer.get(), scrollBar, 2, SDL_Color{ 255, 80, 180, 180 }); // Purple scrollbar
            }
            
            // Draw elegant red Back button positioned statically at the bottom
            SDL_Rect backBtnRect = { cardX + 50, cardY + 365, 500, 45 };
            bool hoveredBack = (mx >= backBtnRect.x && mx <= (backBtnRect.x + backBtnRect.w) &&
                                my >= backBtnRect.y && my <= (backBtnRect.y + backBtnRect.h));
                                
            SDL_Color backCol = hoveredBack ? SDL_Color{ 172, 45, 98, 255 } : SDL_Color{ 34, 34, 46, 255 };
            SDL_Color textColBack = hoveredBack ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 255, 100, 100, 255 };
            
            DrawRoundedRect(state.renderer.get(), backBtnRect, 8, backCol);
            std::string backText = IsRussianLanguage(state.gameLanguage) ? "Назад" : "Back";
            RenderText(state.renderer.get(), state.fontUI.get(), backText, backBtnRect.x + backBtnRect.w / 2, backBtnRect.y + backBtnRect.h / 2 + 8, textColBack, true);
        }

        // Draw elegant overlay if UI localization is in progress
        state.mutex.lock();
        bool isLoc = !state.uiLocalized;
        state.mutex.unlock();
        
        if (isLoc) {
            SDL_Rect locOverlay = cardRect;
            DrawRoundedRect(state.renderer.get(), locOverlay, 10, SDL_Color{ 10, 10, 18, 240 }); // Glassmorphic mask
            
            // Draw a fine neon border around the loading mask
            SDL_Rect locBorder = borderRect;
            Uint32 pulseTicks = SDL_GetTicks();
            Uint8 alpha = 120 + 80 * (0.5 + 0.5 * sin(pulseTicks * 0.005));
            SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, alpha);
            SDL_RenderDrawRect(state.renderer.get(), &locBorder);
            
            // Pulsing localized loading text
            std::string locText = "Localizing interface...";
            if (IsRussianLanguage(state.gameLanguage)) {
                locText = "Локализация интерфейса...";
            } else if (IsUkrainianLanguage(state.gameLanguage)) {
                locText = "Локалізація інтерфейсу...";
            }
            
            SDL_Color locCol = { 0, 255, 220, alpha };
            RenderText(state.renderer.get(), state.fontTitle.get(), locText, WINDOW_WIDTH / 2, cardY + cardH / 2 - 10, locCol, true);
            
            std::string locSub = "Please wait a moment";
            if (IsRussianLanguage(state.gameLanguage)) {
                locSub = "Пожалуйста, подождите";
            } else if (IsUkrainianLanguage(state.gameLanguage)) {
                locSub = "Будь ласка, зачекайте";
            }
            RenderText(state.renderer.get(), state.fontUI.get(), locSub, WINDOW_WIDTH / 2, cardY + cardH / 2 + 30, SDL_Color{ 200, 200, 220, alpha }, true);
        }
        
        if (state.appState == APP_STATE_AI_GENERATING) {
            DrawRoundedRect(state.renderer.get(), borderRect, 12, SDL_Color{ 0, 192, 255, 180 }); // Cyan border
            DrawRoundedRect(state.renderer.get(), cardRect, 10, SDL_Color{ 20, 20, 30, 250 });   // Deep dark background
            
            RenderText(state.renderer.get(), state.fontTitle.get(), GetUiText("generating_title"), WINDOW_WIDTH / 2, cardY + 60, SDL_Color{ 0, 255, 220, 255 }, true);
            
            // Wrap and render the long description text nicely
            std::vector<std::string> descLines = WrapText(state.fontUI.get(), GetUiText("generating_desc"), 520);
            int descY = cardY + 110;
            for (const auto& line : descLines) {
                RenderText(state.renderer.get(), state.fontUI.get(), line, WINDOW_WIDTH / 2, descY, SDL_Color{ 180, 180, 200, 255 }, true);
                descY += 22;
            }
            
            // Progress Bar
            SDL_Rect barOutline = { cardX + 48, cardY + 198, 504, 24 };
            SDL_Rect barBg = { cardX + 50, cardY + 200, 500, 20 };
            SDL_Rect barFill = { cardX + 50, cardY + 200, (int)(500 * (state.generationProgress / 100.0)), 20 };
            
            DrawRoundedRect(state.renderer.get(), barOutline, 6, SDL_Color{ 0, 192, 255, 150 });
            DrawRoundedRect(state.renderer.get(), barBg, 4, SDL_Color{ 24, 24, 34, 255 });
            DrawRoundedRect(state.renderer.get(), barFill, 4, SDL_Color{ 0, 255, 150, 255 }); // glowing green-cyan progress
            
            // Percentage
            RenderText(state.renderer.get(), state.fontUI.get(), std::to_string(state.generationProgress) + "%", WINDOW_WIDTH / 2, cardY + 185, SDL_Color{ 255, 255, 255, 255 }, true);
            
            // Status
            RenderText(state.renderer.get(), state.fontUI.get(), state.generationStatus, WINDOW_WIDTH / 2, cardY + 250, SDL_Color{ 255, 215, 0, 255 }, true);
        }
    }

    // 6. Render glassmorphism footer input bar
    if (state.appState == APP_STATE_GAMEPLAY || state.appState == APP_STATE_SETUP || state.editingLanguage || state.editingApiKey) {
        SDL_Rect footerBg = { 0, WINDOW_HEIGHT - footerH, WINDOW_WIDTH, footerH };
        SDL_SetRenderDrawColor(state.renderer.get(), 14, 14, 20, 255);
        SDL_RenderFillRect(state.renderer.get(), &footerBg);
        
        // Define clear button geometry
        state.clearBtnRect = { 20, WINDOW_HEIGHT - footerH + 10, 40, 40 };
        
        // Handle clear button hover styling
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        bool hoverClear = (mx >= state.clearBtnRect.x && mx <= state.clearBtnRect.x + state.clearBtnRect.w &&
                           my >= state.clearBtnRect.y && my <= state.clearBtnRect.y + state.clearBtnRect.h);
        
        SDL_Color clearBgColor = hoverClear ? SDL_Color{ 45, 45, 60, 255 } : SDL_Color{ 26, 26, 36, 255 };
        DrawRoundedRect(state.renderer.get(), state.clearBtnRect, 8, clearBgColor);
        
        if (hoverClear) {
            SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 255); // Cyan border on hover
        } else {
            SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
        }
        SDL_RenderDrawRect(state.renderer.get(), &state.clearBtnRect);
        
        // Draw the clear symbol centered in the button
        SDL_Color clearTxtColor = { 200, 200, 220, 255 };
        RenderText(state.renderer.get(), state.fontUI.get(), "X", state.clearBtnRect.x + state.clearBtnRect.w / 2, state.clearBtnRect.y + state.clearBtnRect.h / 2, clearTxtColor, true);

        // Define confirm button geometry (right edge has same padding as clear button on the left)
        state.confirmBtnRect = { WINDOW_WIDTH - 60, WINDOW_HEIGHT - footerH + 10, 40, 40 };
        
        // Handle confirm button hover styling
        bool hoverConfirm = (mx >= state.confirmBtnRect.x && mx <= state.confirmBtnRect.x + state.confirmBtnRect.w &&
                             my >= state.confirmBtnRect.y && my <= state.confirmBtnRect.y + state.confirmBtnRect.h);
        
        SDL_Color confirmBgColor = hoverConfirm ? SDL_Color{ 45, 45, 60, 255 } : SDL_Color{ 26, 26, 36, 255 };
        DrawRoundedRect(state.renderer.get(), state.confirmBtnRect, 8, confirmBgColor);
        
        if (hoverConfirm) {
            SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 255); // Cyan border on hover
        } else {
            SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
        }
        SDL_RenderDrawRect(state.renderer.get(), &state.confirmBtnRect);
        
        // Draw the confirm symbol ">" centered in the button
        SDL_Color confirmTxtColor = { 0, 192, 255, 255 };
        RenderText(state.renderer.get(), state.fontUI.get(), ">", state.confirmBtnRect.x + state.confirmBtnRect.w / 2, state.confirmBtnRect.y + state.confirmBtnRect.h / 2, confirmTxtColor, true);

        // Input bar is shifted between the clear and confirm buttons
        SDL_Rect inputBar;
        if (state.editingApiKey) {
            // Define Paste button geometry (after input bar and before confirm button)
            state.pasteBtnRect = { WINDOW_WIDTH - 110, WINDOW_HEIGHT - footerH + 10, 40, 40 };
            inputBar = { 70, WINDOW_HEIGHT - footerH + 10, WINDOW_WIDTH - 190, 40 };
        } else {
            state.pasteBtnRect = { 0, 0, 0, 0 };
            inputBar = { 70, WINDOW_HEIGHT - footerH + 10, WINDOW_WIDTH - 140, 40 };
        }

        SDL_Color inputBgColor = { 26, 26, 36, 255 };
        DrawRoundedRect(state.renderer.get(), inputBar, 8, inputBgColor);

        // Draw Paste Button if in API Key entry state
        if (state.editingApiKey) {
            bool hoverPaste = (mx >= state.pasteBtnRect.x && mx <= state.pasteBtnRect.x + state.pasteBtnRect.w &&
                               my >= state.pasteBtnRect.y && my <= state.pasteBtnRect.y + state.pasteBtnRect.h);
            
            SDL_Color pasteBgColor = hoverPaste ? SDL_Color{ 45, 45, 60, 255 } : SDL_Color{ 26, 26, 36, 255 };
            DrawRoundedRect(state.renderer.get(), state.pasteBtnRect, 8, pasteBgColor);
            
            if (hoverPaste) {
                SDL_SetRenderDrawColor(state.renderer.get(), 0, 255, 220, 255); // glowing bright cyan border on hover
            } else {
                SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
            }
            SDL_RenderDrawRect(state.renderer.get(), &state.pasteBtnRect);
            
            // Procedurally draw clipboard paste symbol
            int px = state.pasteBtnRect.x;
            int py = state.pasteBtnRect.y;
            
            // Clipboard board (dark grey)
            SDL_Rect boardRect = { px + 12, py + 10, 16, 20 };
            SDL_Color boardColor = { 100, 100, 120, 255 };
            DrawRoundedRect(state.renderer.get(), boardRect, 3, boardColor);
            
            // Paper sheet (white/light grey)
            SDL_Rect paperRect = { px + 15, py + 14, 10, 13 };
            SDL_Color paperColor = hoverPaste ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 200, 200, 210, 255 };
            DrawRoundedRect(state.renderer.get(), paperRect, 1, paperColor);
            
            // Metal clip at the top (Cyan highlight)
            SDL_Rect clipRect = { px + 16, py + 8, 8, 4 };
            SDL_Color clipColor = hoverPaste ? SDL_Color{ 0, 255, 220, 255 } : SDL_Color{ 0, 192, 255, 255 };
            DrawRoundedRect(state.renderer.get(), clipRect, 1, clipColor);
        }
        
        // Input border highlights cyan/purple during processing
        if (state.aiThinking) {
            SDL_SetRenderDrawColor(state.renderer.get(), 142, 60, 220, 255); // Purple thinking border
        } else {
            SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
        }
        SDL_RenderDrawRect(state.renderer.get(), &inputBar);
        
        // Auto-scroll calculation: find the largest UTF-8 suffix that fits in maxTextW
        std::string visibleText = state.inputText;
        int maxTextW = inputBar.w - 24; // padding left/right
        int textW = 0, textH = 0;
        
        if (!state.inputText.empty()) {
            ConfigureFontLayout(state.fontUI.get(), state.inputText);
            TTF_SizeUTF8(state.fontUI.get(), state.inputText.c_str(), &textW, &textH);
            ResetFontLayout(state.fontUI.get());
            
            if (textW > maxTextW) {
                int last = state.inputText.length() - 1;
                while (last >= 0) {
                    // Walk backward to find the next UTF-8 character start
                    while (last >= 0) {
                        unsigned char c = state.inputText[last];
                        if ((c & 0xC0) != 0x80) { // Not a continuation byte
                            break;
                        }
                        last--;
                    }
                    if (last < 0) break;
                    
                    std::string suffix = state.inputText.substr(last);
                    int sufW = 0, sufH = 0;
                    ConfigureFontLayout(state.fontUI.get(), suffix);
                    TTF_SizeUTF8(state.fontUI.get(), suffix.c_str(), &sufW, &sufH);
                    ResetFontLayout(state.fontUI.get());
                    
                    if (sufW > maxTextW) {
                        break;
                    }
                    visibleText = suffix;
                    textW = sufW;
                    
                    last--;
                }
            }
        }
        
        // Draw input content or standard placeholder
        if (state.inputText.empty()) {
            SDL_Color holderColor = { 100, 100, 120, 255 };
            std::string placeholder = GetUiText("setup_input_placeholder");
            if (state.editingLanguage) {
                placeholder = GetUiText("lang_input_placeholder");
            } else if (state.editingApiKey) {
                placeholder = GetUiText("apikey_placeholder");
            } else if (state.appState == APP_STATE_GAMEPLAY) {
                placeholder = state.aiThinking ? GetUiText("game_thinking_placeholder") : GetUiText("game_input_placeholder");
            }
            RenderText(state.renderer.get(), state.fontUI.get(), placeholder, inputBar.x + 12, inputBar.y + 10, holderColor);
        } else {
            SDL_Color txtColor = { 255, 255, 255, 255 };
            RenderText(state.renderer.get(), state.fontUI.get(), visibleText, inputBar.x + 12, inputBar.y + 10, txtColor);
        }
        
        // Draw pulsing vertical text cursor
        if (state.cursorVisible && !state.aiThinking) {
            int cursorX = inputBar.x + 12 + textW;
            if (cursorX > inputBar.x + inputBar.w - 12) {
                cursorX = inputBar.x + inputBar.w - 12;
            }
            
            SDL_Rect textCursor = { cursorX, inputBar.y + 10, 2, 20 };
            SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 255);
            SDL_RenderFillRect(state.renderer.get(), &textCursor);
        }
    }
    

    
    // 7. Render present buffer
    SDL_RenderPresent(state.renderer.get());
}

#ifdef main
#undef main
#endif

// Explicit entry point with C linkage for static linking across all platforms
extern "C" int SDL_main(int argc, char* argv[]) {
#ifdef _WIN32
    // Hide terminal/console window on startup
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        ShowWindow(hwnd, SW_HIDE);
    }
#endif
    // Change working directory to executable path to resolve config files and assets on macOS / all platforms
#ifndef __EMSCRIPTEN__
    char* basePath = SDL_GetBasePath();
    if (basePath) {
        try {
            std::filesystem::current_path(basePath);
            std::cout << "[WorkingDirectory] Changed process directory to: " << basePath << std::endl;
        } catch (...) {}
        SDL_free(basePath);
    }
#endif
    // 1. Load settings.json configuration properties
    std::string aiModel = "ai_gemini.json";
    std::string systemPrompt = "";
    int maxRetries = 3;
    int bookRetries = 3;
    int retryDelayMs = 1000;
    int connectTimeout = 5;
    int requestTimeout = 15;
    int maxTurnsForce = 10;
    std::ifstream file("settings.json");
    if (!file.is_open()) {
        file.open("../settings.json");
    }
    if (file.is_open()) {
        try {
            nlohmann::json j;
            file >> j;
            if (j.contains("AIModel") && j["AIModel"].is_string()) {
                aiModel = j["AIModel"].get<std::string>();
            }
            if (j.contains("Language") && j["Language"].is_string()) {
                state.gameLanguage = j["Language"].get<std::string>();
            }
            // Read the dynamic font size offset from settings and apply safe constraints
            if (j.contains("fontSizeOffset") && j["fontSizeOffset"].is_number()) {
                state.fontSizeOffset = j["fontSizeOffset"].get<int>();
                if (state.fontSizeOffset > 8) state.fontSizeOffset = 8;
                if (state.fontSizeOffset < -4) state.fontSizeOffset = -4;
            }
            if (j.contains("systemPrompt") && j["systemPrompt"].is_string()) {
                systemPrompt = j["systemPrompt"].get<std::string>();
            } else if (j.contains("systemPrompt") && j["systemPrompt"].is_array()) {
                // Array of lines — join with \n so editors can read it comfortably
                systemPrompt = "";
                for (const auto& line : j["systemPrompt"]) {
                    if (line.is_string()) {
                        if (!systemPrompt.empty()) systemPrompt += "\n";
                        systemPrompt += line.get<std::string>();
                    }
                }
            }
            if (j.contains("maxRetries") && j["maxRetries"].is_number()) {
                maxRetries = j["maxRetries"].get<int>();
            }
            if (j.contains("bookRetries") && j["bookRetries"].is_number()) {
                bookRetries = j["bookRetries"].get<int>();
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
            if (j.contains("maxTurnsForce") && j["maxTurnsForce"].is_number()) {
                maxTurnsForce = j["maxTurnsForce"].get<int>();
            }
        } catch (const std::exception& e) {
            std::cerr << "[Config] Parse error settings.json: " << e.what() << std::endl;
        }
        file.close();
    } else {
        std::cerr << "[Config] Failed to open settings.json (also tried parent directory)." << std::endl;
    }
    
    // Load book.json configuration properties
    std::string bookTitle = "BOOK_TO_GAME";
    std::string bookWorld = "";
    std::string bookPlot = "";
    std::string bookStartPrompt = "";
    std::ifstream bookFile("book.json");
    if (!bookFile.is_open()) {
        bookFile.open("../book.json");
    }
    if (bookFile.is_open()) {
        try {
            nlohmann::json bj;
            bookFile >> bj;
            if (bj.contains("title") && bj["title"].is_string()) {
                bookTitle = bj["title"].get<std::string>();
            }
            if (bj.contains("world") && bj["world"].is_string()) {
                bookWorld = bj["world"].get<std::string>();
            }
            if (bj.contains("plot")) {
                if (bj["plot"].is_string()) {
                    bookPlot = bj["plot"].get<std::string>();
                } else if (bj["plot"].is_array()) {
                    std::stringstream ss;
                    ss << "Сюжет игры по главам:\n";
                    for (const auto& chJson : bj["plot"]) {
                        if (chJson.is_object()) {
                            std::string chTitle = "";
                            std::string chDesc = "";
                            std::string chNum = "";
                            int chIntNum = 1;
                            
                            if (chJson.contains("chapter")) {
                                if (chJson["chapter"].is_number()) {
                                    chIntNum = chJson["chapter"].get<int>();
                                    chNum = std::to_string(chIntNum);
                                } else if (chJson["chapter"].is_string()) {
                                    chNum = chJson["chapter"].get<std::string>();
                                    try {
                                        chIntNum = std::stoi(chNum);
                                    } catch (...) {
                                        chIntNum = 1;
                                    }
                                }
                            }
                            if (chJson.contains("title") && chJson["title"].is_string()) {
                                chTitle = chJson["title"].get<std::string>();
                            }
                            if (chJson.contains("description") && chJson["description"].is_string()) {
                                chDesc = chJson["description"].get<std::string>();
                            }
                            
                            ChapterData chData;
                            chData.number = chIntNum;
                            chData.title = chTitle;
                            chData.description = chDesc;
                            state.modelState.chapters.push_back(chData);
                            
                            if (!chNum.empty()) {
                                ss << "Глава " << chNum;
                            } else {
                                ss << "Глава";
                            }
                            if (!chTitle.empty()) {
                                ss << ": " << chTitle;
                            }
                            ss << "\nОписание: " << chDesc << "\n\n";
                        } else if (chJson.is_string()) {
                            ss << "- " << chJson.get<std::string>() << "\n";
                        }
                    }
                    bookPlot = ss.str();
                }
            }
            if (bj.contains("startPrompt") && bj["startPrompt"].is_string()) {
                bookStartPrompt = bj["startPrompt"].get<std::string>();
            }
        } catch (const std::exception& e) {
            std::cerr << "[Config] Parse error book.json: " << e.what() << std::endl;
        }
        bookFile.close();
    } else {
        std::cerr << "[Config] Failed to open book.json (also tried parent directory)." << std::endl;
    }
    
    // Copy settings to state.modelState
    state.modelState.systemPrompt = systemPrompt;
    state.modelState.bookWorld = bookWorld;
    state.modelState.gameLanguage = state.gameLanguage;
    state.modelState.bookTitle = bookTitle;
    state.modelState.bookStartPrompt = bookStartPrompt;
    state.modelState.maxTurnsForce = maxTurnsForce;
    
    // Instanciate external AI API client
    state.aiClient = std::make_unique<AskAiExternal>(aiModel);
    state.aiModelName = aiModel;
    state.aiClient->setRetrySettings(maxRetries, retryDelayMs);
    state.aiClient->setTimeoutSettings(connectTimeout, requestTimeout);
    state.maxRetries = maxRetries;
    state.bookRetries = bookRetries;
    state.retryDelayMs = retryDelayMs;
    state.connectTimeout = connectTimeout;
    state.requestTimeout = requestTimeout;
    
    // Launch AI Translation Thread for chapter transition prefix and UI labels
    TriggerUiLocalization();
    
    // Blend settings.json systemPrompt with book.json world and plot lore
    std::string combinedPrompt = systemPrompt;
    if (!bookWorld.empty()) {
        combinedPrompt += "\n\nИгровой мир:\n" + bookWorld;
    }
    if (!bookPlot.empty()) {
        combinedPrompt += "\n\nСюжет и цель игры:\n" + bookPlot;
    }
    if (!combinedPrompt.empty()) {
        state.aiClient->setSystemPrompt(combinedPrompt);
    }
    
    // 2. Initialize SDL2 subsystems
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return -1;
    }
    
    // 3. Create Desktop Window
    state.window.reset(SDL_CreateWindow(
        bookTitle.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    ));
    if (!state.window) {
        std::cerr << "Window Create Fail: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    
    // 4. Create hardware-accelerated renderer
    state.renderer.reset(SDL_CreateRenderer(state.window.get(), -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!state.renderer) {
        std::cerr << "Renderer Create Fail: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return -1;
    }
    SDL_SetRenderDrawBlendMode(state.renderer.get(), SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(state.renderer.get(), WINDOW_WIDTH, WINDOW_HEIGHT);
    
    // 5. Initialize extensions libraries
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    TTF_Init();
    SDLNet_Init();
    
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        std::cerr << "Mix_OpenAudio Warning: " << Mix_GetError() << std::endl;
    } else {
        state.mixOk = true;
        state.soundEffect.reset(Mix_LoadWAV("assets/sound.wav"));
        if (!state.soundEffect) {
            std::cerr << "Sound Load Fail: " << Mix_GetError() << std::endl;
        }
    }
    
    // Enable text input events dynamically
    SDL_StartTextInput();
    
    // 6. Select and open dynamic Windows system or local fonts
    ApplyFontScale();
    
    if (!state.fontTitle || !state.fontMessage || !state.fontUI || !state.fontSmallUI) {
        std::cerr << "Font Init Fail: " << TTF_GetError() << std::endl;
    }
    
    // 6. Startup check for book and save game
    bool bookValid = IsBookValid("book.json");
    bool loaded = false;
    
    if (bookValid) {
        LoadBookConfig("book.json");
        loaded = LoadGame(); // Loads save game if it exists
        state.appState = APP_STATE_ASK_CONTINUE;
    } else {
        state.appState = APP_STATE_ENTER_TXT_PATH;
    }
    
    UpdateSystemPrompt();
    
    // 7. Start main execution thread loop
#ifdef __EMSCRIPTEN__
    emscripten_set_main_loop(MainIteration, 0, 1);
#else
    while (state.running) {
        MainIteration();
        SDL_Delay(1); // CPU throttle limiter
    }
#endif
    
    // 8. Clean up resources and terminate safely
    SDL_StopTextInput();
    state.fontTitle.reset();
    state.fontMessage.reset();
    state.fontUI.reset();
    state.fontSmallUI.reset();
    state.soundEffect.reset();
    state.renderer.reset();
    state.window.reset();
    
    if (state.mixOk) Mix_CloseAudio();
    SDLNet_Quit();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    
    std::cout << "All BOOK_TO_GAME subsystems shut down cleanly!" << std::endl;
    return 0;
}

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if !defined(_WIN32) && !defined(__ANDROID__) && (!defined(__APPLE__) || !TARGET_OS_IPHONE)
int main(int argc, char* argv[]) {
    return SDL_main(argc, argv);
}
#endif