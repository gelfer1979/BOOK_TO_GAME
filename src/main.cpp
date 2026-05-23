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

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_net.h>

#include "modelapi.h"

// Default Window Dimensions
const int WINDOW_WIDTH = 800;
const int WINDOW_HEIGHT = 600;

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

struct ChapterData {
    int number = 1;
    std::string title = "";
    std::string description = "";
};

// Global Application State Class
struct {
    UniqueWindow window{nullptr, SDL_DestroyWindow};
    UniqueRenderer renderer{nullptr, SDL_DestroyRenderer};
    
    // Core AI and Configuration properties
    std::unique_ptr<AskAiExternal> aiClient;
    std::vector<ChatMessage> messages;
    std::string inputText = "";
    std::string lastQuery = "";
    bool aiThinking = false;
    std::vector<ActiveChoice> activeChoices;
    
    // Thread safety synchronization
    std::mutex mutex;
    bool responseReady = false;
    std::string pendingResponse = "";
    
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

    // Progression & check-pointing state
    int currentChapter = 1;
    std::vector<std::string> chapterSummaries;
    std::vector<ChapterData> chapters;
    bool gameOver = false;
    SDL_Rect deathBtnRect = {0, 0, 0, 0};

    // Config backups for dynamic prompt regeneration
    std::string systemPrompt = "";
    std::string bookWorld = "";
    std::string bookTitle = "BOOK_TO_GAME";
    std::string bookStartPrompt = "";
} state;

// Forward Declarations
std::vector<std::string> WrapText(TTF_Font* font, const std::string& text, int maxWidth);
std::string Trim(const std::string& str);
void SubmitQuery(const std::string& queryText, bool isRetry = false, bool showInChat = true);

void UpdateSystemPrompt() {
    std::string combinedPrompt = state.systemPrompt;
    
    if (!state.bookWorld.empty()) {
        combinedPrompt += "\n\nИгровой мир:\n" + state.bookWorld;
    }
    
    combinedPrompt += "\n\nТекущее состояние игры:\n";
    combinedPrompt += "Текущая глава: " + std::to_string(state.currentChapter) + "\n";
    
    if (!state.chapterSummaries.empty()) {
        combinedPrompt += "\nКраткая история предыдущих глав:\n";
        for (size_t i = 0; i < state.chapterSummaries.size(); i++) {
            combinedPrompt += "- Глава " + std::to_string(i + 1) + ": " + state.chapterSummaries[i] + "\n";
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
        combinedPrompt += "\nЦели и описание текущей главы (Глава " + std::to_string(state.currentChapter) + ": " + activeChapterTitle + "):\n" + activeChapterDesc + "\n";
    }
    
    combinedPrompt += "\n\nПРАВИЛА ИГРЫ ДЛЯ ИИ:\n";
    combinedPrompt += "1. В КОНЦЕ КАЖДОГО ОТВЕТА (даже при начале или перезапуске главы) вы ОБЯЗАТЕЛЬНО должны предложить от 2 до 4 вариантов действий, обернутых строго в XML-теги в самом конце ответа: <options><option>Вариант 1</option><option>Вариант 2</option></options>. Не выводите варианты обычным текстом, только внутри этих тегов!\n";
    combinedPrompt += "2. Когда игрок успешно достигает всех целей текущей главы и готов перейти к следующей, вы обязательно должны добавить тег <next_chapter>НОМЕР_СЛЕДУЮЩЕЙ_ГЛАВЫ</next_chapter> ПОСЛЕ тегов выбора </options>.\n";
    combinedPrompt += "3. Вы имеете право и должны убить персонажа игрока, если он совершает фатальные ошибки, лезет на рожон без экипировки или проигрывает бой. Если персонаж погибает, красочно опишите его смерть и обязательно добавьте тег <player_dead/> ПОСЛЕ тегов выбора </options>.";
    
    if (state.aiClient) {
        state.aiClient->setSystemPrompt(combinedPrompt);
    }
}

void SaveGame() {
    state.mutex.lock();
    nlohmann::json j;
    j["currentChapter"] = state.currentChapter;
    j["chapterSummaries"] = state.chapterSummaries;
    j["gameOver"] = state.gameOver;
    
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
        choices.push_back(opt.text);
    }
    j["activeChoices"] = choices;
    state.mutex.unlock();
    
    std::ofstream file("save.json");
    if (file.is_open()) {
        file << j.dump(4);
        file.close();
        std::cout << "[SaveGame] Game saved successfully to save.json" << std::endl;
    } else {
        std::cerr << "[SaveGame] Failed to open save.json for writing" << std::endl;
    }
}

bool LoadGame() {
    std::ifstream file("save.json");
    if (!file.is_open()) {
        return false;
    }
    
    try {
        nlohmann::json j;
        file >> j;
        
        state.mutex.lock();
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
        
        state.messages.clear();
        if (j.contains("messages") && j["messages"].is_array()) {
            for (const auto& m : j["messages"]) {
                if (m.contains("sender") && m["sender"].is_string() &&
                    m.contains("text") && m["text"].is_string()) {
                    ChatMessage msg;
                    msg.sender = m["sender"].get<std::string>();
                    msg.text = m["text"].get<std::string>();
                    if (state.fontMessage) {
                        msg.lines = WrapText(state.fontMessage.get(), msg.text, 736);
                    }
                    state.messages.push_back(msg);
                }
            }
        }
        
        state.activeChoices.clear();
        if (j.contains("activeChoices") && j["activeChoices"].is_array()) {
            std::vector<std::string> opts;
            for (const auto& optVal : j["activeChoices"]) {
                if (optVal.is_string()) {
                    opts.push_back(optVal.get<std::string>());
                }
            }
            int n = opts.size();
            if (n > 4) n = 4;
            
            int cardH = 45;
            int verticalSpacing = 8;
            int optionsAreaH = n * cardH + (n + 1) * verticalSpacing;
            int footerH = 60;
            int cardW = 760;
            int startX = 20;
            
            for (int i = 0; i < n; i++) {
                ActiveChoice choice;
                choice.text = opts[i];
                choice.rect.x = startX;
                choice.rect.y = WINDOW_HEIGHT - (optionsAreaH + footerH) + verticalSpacing + i * (cardH + verticalSpacing);
                choice.rect.w = cardW;
                choice.rect.h = cardH;
                state.activeChoices.push_back(choice);
            }
        }
        
        state.scrollToBottom = true;
        state.mutex.unlock();
        
        std::cout << "[LoadGame] Game loaded successfully from save.json. Current Chapter: " << state.currentChapter << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[LoadGame] Failed to parse save.json: " << e.what() << std::endl;
        return false;
    }
}


// UTF-8 Helpers
std::vector<uint32_t> DecodeUTF8(const std::string& text) {
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

bool IsRTLCodePoint(uint32_t cp) {
    return ((cp >= 0x0590 && cp <= 0x05FF) || // Hebrew range
            (cp >= 0x0600 && cp <= 0x06FF) || // Arabic range
            (cp >= 0x0750 && cp <= 0x077F) || // Arabic Supplement
            (cp >= 0x08A0 && cp <= 0x08FF) || // Arabic Extended-A
            (cp >= 0xFB50 && cp <= 0xFDFF) || // Arabic Presentation Forms-A
            (cp >= 0xFE70 && cp <= 0xFEFF));  // Arabic Presentation Forms-B
}

bool HasRTLCharacter(const std::string& text) {
    std::vector<uint32_t> codePoints = DecodeUTF8(text);
    for (uint32_t cp : codePoints) {
        if (IsRTLCodePoint(cp)) {
            return true;
        }
    }
    return false;
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

// Safe Backspace POP UTF-8 Helper
void PopUTF8Character(std::string& s) {
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

// String utility to trim whitespace and newlines from both ends
std::string Trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first) {
        return "";
    }
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

// XML Options Parser & Dialogue Strip
std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
    return s;
}

std::vector<std::string> ExtractAndStripOptions(std::string& aiResponse) {
    std::vector<std::string> options;
    
    std::string lowerResponse = ToLower(aiResponse);
    size_t startPos = lowerResponse.find("<options>");
    size_t endPos = lowerResponse.find("</options>");
    
    if (startPos != std::string::npos && endPos != std::string::npos && endPos > startPos) {
        std::string optionsBlock = aiResponse.substr(startPos + 9, endPos - (startPos + 9));
        
        size_t optStart = 0;
        std::string lowerBlock = ToLower(optionsBlock);
        while ((optStart = lowerBlock.find("<option>", optStart)) != std::string::npos) {
            size_t optEnd = lowerBlock.find("</option>", optStart);
            if (optEnd != std::string::npos) {
                std::string optText = optionsBlock.substr(optStart + 8, optEnd - (optStart + 8));
                options.push_back(Trim(optText));
                optStart = optEnd + 9;
            } else {
                break;
            }
        }
        
        // Strip out the entire XML options tags block cleanly from narrative
        aiResponse.erase(startPos, (endPos + 10) - startPos);
    }
    
    // Also clean up any leftover markdown code blocks if the AI wrapped options in them
    size_t codeBlockPos = aiResponse.rfind("```");
    if (codeBlockPos != std::string::npos) {
        if (codeBlockPos >= aiResponse.length() - 10) {
            aiResponse.erase(codeBlockPos);
        }
    }
    
    // Strip trailing/leading newlines and spaces
    while (!aiResponse.empty() && (aiResponse.back() == '\n' || aiResponse.back() == '\r' || aiResponse.back() == ' ')) {
        aiResponse.pop_back();
    }
    while (!aiResponse.empty() && (aiResponse.front() == '\n' || aiResponse.front() == '\r' || aiResponse.front() == ' ')) {
        aiResponse.erase(aiResponse.begin());
    }
    
    return options;
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
    state.lastQuery = queryCopy;
    if (showInChat && !isRetry) {
        // Add User query to Dialogue bubbles list
        ChatMessage userMsg;
        userMsg.sender = "User";
        userMsg.text = queryCopy;
        userMsg.lines = WrapText(state.fontMessage.get(), queryCopy, 736);
        state.messages.push_back(userMsg);
    }
    state.aiThinking = true;
    state.scrollToBottom = true;
    state.activeChoices.clear(); // Instantly collapse options shelf during thinking
    state.mutex.unlock();
    
    if (showInChat && !isRetry) {
        SaveGame();
    }
    
    // Fire detaching query thread
    std::thread([queryCopy]() {
        std::string response = state.aiClient->ask(queryCopy);
        
        state.mutex.lock();
        state.pendingResponse = response;
        state.responseReady = true;
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
        
        std::cout << "\n[API Response Received]\nRaw Response:\n" << fullResponse << "\n[End of Raw Response]\n" << std::endl;
        
        // Check for tags and strip them
        bool isDead = false;
        size_t deadPos = fullResponse.find("<player_dead/>");
        if (deadPos != std::string::npos) {
            isDead = true;
            fullResponse.erase(deadPos, 14);
            std::cout << "[ConsumeApiResponse] Found <player_dead/> tag!" << std::endl;
        }
        
        int nextChapter = -1;
        size_t nextPos = fullResponse.find("<next_chapter>");
        size_t nextEndPos = fullResponse.find("</next_chapter>");
        if (nextPos != std::string::npos && nextEndPos != std::string::npos && nextEndPos > nextPos) {
            std::string chNumStr = fullResponse.substr(nextPos + 14, nextEndPos - (nextPos + 14));
            try {
                nextChapter = std::stoi(chNumStr);
                std::cout << "[ConsumeApiResponse] Found <next_chapter> tag. Target Chapter: " << nextChapter << std::endl;
            } catch (...) {}
            fullResponse.erase(nextPos, (nextEndPos + 15) - nextPos);
        }
        
        // Strip XML choice tags and parse dynamic action cards
        std::vector<std::string> options = ExtractAndStripOptions(fullResponse);
        
        std::cout << "[ConsumeApiResponse] Parsed options count: " << options.size() << std::endl;
        for (size_t i = 0; i < options.size(); i++) {
            std::cout << "  Option " << (i + 1) << ": \"" << options[i] << "\"" << std::endl;
        }
        
        // Offer a retry option in case of API errors
        if (options.empty() && (fullResponse.rfind("Error", 0) == 0 || fullResponse.find("Error") != std::string::npos)) {
            options.push_back("🔄 Повторить запрос");
        }
        
        ChatMessage aiMsg;
        aiMsg.sender = "AI";
        aiMsg.text = fullResponse;
        aiMsg.lines = WrapText(state.fontMessage.get(), fullResponse, 736);
        state.messages.push_back(aiMsg);
        state.scrollToBottom = true;
        
        if (isDead) {
            state.gameOver = true;
            state.activeChoices.clear();
            state.mutex.unlock();
            
            // Auto-save game state on death
            SaveGame();
            return;
        }
        
        // Populate options if parsed cleanly
        if (!options.empty() && nextChapter == -1) {
            int n = options.size();
            if (n > 4) n = 4; // Max 4 Dynamic Buttons
            
            int cardH = 45;
            int verticalSpacing = 8;
            int optionsAreaH = n * cardH + (n + 1) * verticalSpacing;
            int footerH = 60;
            
            int cardW = 760; // Same width as text bubbles
            int startX = 20; // Same X coordinate
            
            for (int i = 0; i < n; i++) {
                ActiveChoice choice;
                choice.text = options[i];
                
                choice.rect.x = startX;
                choice.rect.y = WINDOW_HEIGHT - (optionsAreaH + footerH) + verticalSpacing + i * (cardH + verticalSpacing);
                choice.rect.w = cardW;
                choice.rect.h = cardH;
                
                state.activeChoices.push_back(choice);
            }
        }
        
        state.mutex.unlock();
        
        // Save regular state
        SaveGame();
        
        if (nextChapter != -1) {
            state.mutex.lock();
            state.aiThinking = true;
            std::string dialogueText = "";
            for (const auto& msg : state.messages) {
                dialogueText += "[" + msg.sender + "]: " + msg.text + "\n\n";
            }
            state.mutex.unlock();
            
            // Launch background summarization thread
            std::thread([nextChapter, dialogueText]() {
                std::cout << "[Summarization Thread] Starting summary request..." << std::endl;
                std::string summaryPrompt = "Сделай краткое саммари (2-3 предложения) на русском языке для пройденной главы на основе следующего диалога. Опиши только ключевые события, достижения игрока, инвентарь и важные сюжетные выборы. Не пиши ничего лишнего.\n\nДиалог:\n" + dialogueText;
                
                std::string summary = state.aiClient->ask(summaryPrompt);
                std::cout << "[Summarization Thread] Raw summary received:\n" << summary << std::endl;
                
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
                    summary = "Глава завершена.";
                }
                
                state.mutex.lock();
                state.chapterSummaries.push_back(summary);
                state.currentChapter = nextChapter;
                state.messages.clear();
                state.aiThinking = false; // Finished thinking
                state.mutex.unlock();
                
                UpdateSystemPrompt();
                SaveGame();
                
                std::string nextStartMsg = "Начни главу " + std::to_string(nextChapter) + ". Опиши начало новой главы, атмосферу вокруг меня и предложи первые варианты действий.";
                std::cout << "[Summarization Thread] Submitting starting query for Chapter " << nextChapter << std::endl;
                SubmitQuery(nextStartMsg, false, false);
            }).detach();
        }
        return;
    }
    state.mutex.unlock();
}

// Main Frame Loop Execution
void MainIteration() {
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
    if (!state.activeChoices.empty()) {
        int n = state.activeChoices.size();
        int cardH = 45;
        int verticalSpacing = 8;
        optionsAreaH = n * cardH + (n + 1) * verticalSpacing;
    }
    int footerH = 60;
    int viewportH = WINDOW_HEIGHT - (optionsAreaH + footerH);
    
    // 2. Process keyboard & mouse inputs
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            state.running = false;
        } else if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                state.running = false;
            } else if (event.key.keysym.sym == SDLK_RETURN) {
                if (state.gameOver) {
                    state.gameOver = false;
                    state.messages.clear();
                    SaveGame();
                    UpdateSystemPrompt();
                    SubmitQuery("Начни текущую главу заново с самого начала. Опиши обстановку и предложи варианты действий.", false, false);
                } else if (!state.inputText.empty() && !state.aiThinking) {
                    SubmitQuery(state.inputText);
                    state.inputText = "";
                }
            } else if (event.key.keysym.sym == SDLK_SPACE) {
                if (state.gameOver) {
                    state.gameOver = false;
                    state.messages.clear();
                    SaveGame();
                    UpdateSystemPrompt();
                    SubmitQuery("Начни текущую главу заново с самого начала. Опиши обстановку и предложи варианты действий.", false, false);
                }
            } else if (event.key.keysym.sym == SDLK_BACKSPACE) {
                if (!state.gameOver) PopUTF8Character(state.inputText);
            } else {
                if (!state.gameOver) {
                    // Handle keyboard dynamic choice bindings (1-9)
                    SDL_Keycode sym = event.key.keysym.sym;
                    if (sym >= SDLK_1 && sym <= SDLK_9) {
                        int index = sym - SDLK_1;
                        state.mutex.lock();
                        if (index >= 0 && index < (int)state.activeChoices.size() && !state.aiThinking) {
                            std::string choiceText = state.activeChoices[index].text;
                            state.mutex.unlock();
                            if (choiceText == "🔄 Повторить запрос") {
                                SubmitQuery(state.lastQuery, true);
                            } else {
                                SubmitQuery(choiceText);
                            }
                        } else {
                            state.mutex.unlock();
                        }
                    }
                }
            }
        } else if (event.type == SDL_TEXTINPUT) {
            if (!state.gameOver) {
                // Append keyboard string inputs safely
                state.inputText += event.text.text;
            }
        } else if (event.type == SDL_MOUSEWHEEL) {
            // Smooth vertical chat scrollbox
            state.scrollOffset -= event.wheel.y * 30;
            if (state.scrollOffset < 0) state.scrollOffset = 0;
            if (state.scrollOffset > state.maxScrollOffset) state.scrollOffset = state.maxScrollOffset;
        } else if (event.type == SDL_MOUSEBUTTONDOWN) {
            int mx = event.button.x;
            int my = event.button.y;
            
            if (state.gameOver) {
                if (mx >= state.deathBtnRect.x && mx <= (state.deathBtnRect.x + state.deathBtnRect.w) &&
                    my >= state.deathBtnRect.y && my <= (state.deathBtnRect.y + state.deathBtnRect.h)) {
                    state.gameOver = false;
                    state.messages.clear();
                    SaveGame();
                    UpdateSystemPrompt();
                    SubmitQuery("Начни текущую главу заново с самого начала. Опиши обстановку и предложи варианты действий.", false, false);
                }
            } else {
                // Check dynamic card hitboxes
                state.mutex.lock();
                bool cardClicked = false;
                std::string clickedText = "";
                for (const auto& opt : state.activeChoices) {
                    if (mx >= opt.rect.x && mx <= (opt.rect.x + opt.rect.w) &&
                        my >= opt.rect.y && my <= (opt.rect.y + opt.rect.h)) {
                        clickedText = opt.text;
                        cardClicked = true;
                        break;
                    }
                }
                state.mutex.unlock();
                
                if (cardClicked && !state.aiThinking) {
                    if (clickedText == "🔄 Повторить запрос") {
                        SubmitQuery(state.lastQuery, true);
                    } else {
                        SubmitQuery(clickedText);
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
        SDL_RenderDrawLine(state.renderer.get(), x, 0, x, viewportH);
    }
    for (int y = 0; y < viewportH; y += 40) {
        SDL_RenderDrawLine(state.renderer.get(), 0, y, WINDOW_WIDTH, y);
    }
    
    // 4. Render Scrollable dialogue bubbles
    SDL_Rect clipRect = { 0, 0, WINDOW_WIDTH, viewportH };
    SDL_RenderSetClipRect(state.renderer.get(), &clipRect);
    
    int bubbleY = 20 - state.scrollOffset;
    int lineH = TTF_FontHeight(state.fontMessage.get());
    
    state.mutex.lock();
    for (const auto& msg : state.messages) {
        int actualBubbleW = 760;
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
    
    // Draw Game Over block inside the scrollable chat feed if character dies
    state.deathBtnRect = { 0, 0, 0, 0 };
    if (state.gameOver) {
        int titleH = TTF_FontHeight(state.fontTitle.get());
        int textY = bubbleY + 10;
        SDL_Color redColor = { 255, 60, 60, 255 };
        
        // "ГЕРОЙ ПОГИБ" - Large title in Red, centered
        RenderText(state.renderer.get(), state.fontTitle.get(), "ГЕРОЙ ПОГИБ", WINDOW_WIDTH / 2, textY + titleH, redColor, true);
        
        textY += titleH + 15;
        
        // Dynamic retry button below the header
        SDL_Rect btnRect = { WINDOW_WIDTH / 2 - 180, textY, 360, 48 };
        state.deathBtnRect = btnRect;
        
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        bool hovered = false;
        if (my < viewportH) {
            hovered = (mx >= btnRect.x && mx <= (btnRect.x + btnRect.w) &&
                       my >= btnRect.y && my <= (btnRect.y + btnRect.h));
        }
        
        SDL_Color cardColor = hovered ? SDL_Color{ 180, 40, 40, 255 } : SDL_Color{ 100, 20, 25, 255 };
        SDL_Color txtColor = hovered ? SDL_Color{ 255, 255, 255, 255 } : SDL_Color{ 255, 150, 150, 255 };
        
        DrawRoundedRect(state.renderer.get(), btnRect, 8, cardColor);
        RenderText(state.renderer.get(), state.fontUI.get(), "🔄 Начать главу заново", btnRect.x + btnRect.w / 2, btnRect.y + btnRect.h / 2 + 8, txtColor, true);
        
        bubbleY += titleH + 15 + btnRect.h + 25;
    }
    
    // Update maximum vertical offset scroll bounds
    int totalContentHeight = bubbleY + state.scrollOffset - 20;
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
    
    // Render dynamic scrolls bar indicator if overflowed
    if (state.maxScrollOffset > 0) {
        int barH = viewportH - 40;
        int scrollBarH = (viewportH * barH) / totalContentHeight;
        if (scrollBarH < 20) scrollBarH = 20;
        int scrollBarY = 20 + (state.scrollOffset * (barH - scrollBarH)) / state.maxScrollOffset;
        
        SDL_Rect barBg = { WINDOW_WIDTH - 8, 20, 4, barH };
        SDL_SetRenderDrawColor(state.renderer.get(), 30, 30, 40, 100);
        SDL_RenderFillRect(state.renderer.get(), &barBg);
        
        SDL_Rect barFg = { WINDOW_WIDTH - 8, scrollBarY, 4, scrollBarH };
        SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 180); // Cyan slider
        SDL_RenderFillRect(state.renderer.get(), &barFg);
    }
    
    // 5. Render dynamic choice cards shelf
    if (optionsAreaH > 0) {
        SDL_Rect shelfBg = { 0, viewportH, WINDOW_WIDTH, optionsAreaH };
        SDL_SetRenderDrawColor(state.renderer.get(), 24, 24, 34, 255);
        SDL_RenderFillRect(state.renderer.get(), &shelfBg);
        
        int mx, my;
        SDL_GetMouseState(&mx, &my);
        
        state.mutex.lock();
        for (int i = 0; i < (int)state.activeChoices.size(); i++) {
            const auto& opt = state.activeChoices[i];
            
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
    
    // 6. Render glassmorphism footer input bar
    SDL_Rect footerBg = { 0, WINDOW_HEIGHT - footerH, WINDOW_WIDTH, footerH };
    SDL_SetRenderDrawColor(state.renderer.get(), 14, 14, 20, 255);
    SDL_RenderFillRect(state.renderer.get(), &footerBg);
    
    SDL_Rect inputBar = { 20, WINDOW_HEIGHT - footerH + 10, WINDOW_WIDTH - 40, 40 };
    SDL_Color inputBgColor = { 26, 26, 36, 255 };
    DrawRoundedRect(state.renderer.get(), inputBar, 8, inputBgColor);
    
    // Input border highlights cyan during processing
    if (state.aiThinking) {
        SDL_SetRenderDrawColor(state.renderer.get(), 142, 60, 220, 255); // Purple thinking border
    } else {
        SDL_SetRenderDrawColor(state.renderer.get(), 40, 40, 60, 255);
    }
    SDL_RenderDrawRect(state.renderer.get(), &inputBar);
    
    // Draw input content or standard placeholder
    if (state.inputText.empty()) {
        SDL_Color holderColor = { 100, 100, 120, 255 };
        std::string placeholder = state.aiThinking ? "AI is weaving the story..." : "Type your action and press Enter...";
        RenderText(state.renderer.get(), state.fontUI.get(), placeholder, 32, inputBar.y + 10, holderColor);
    } else {
        SDL_Color txtColor = { 255, 255, 255, 255 };
        RenderText(state.renderer.get(), state.fontUI.get(), state.inputText, 32, inputBar.y + 10, txtColor);
    }
    
    // Draw pulsing vertical text cursor
    if (state.cursorVisible && !state.aiThinking) {
        int textW = 0, textH = 0;
        if (!state.inputText.empty()) {
            ConfigureFontLayout(state.fontUI.get(), state.inputText);
            TTF_SizeUTF8(state.fontUI.get(), state.inputText.c_str(), &textW, &textH);
            ResetFontLayout(state.fontUI.get());
        }
        int cursorX = 32 + textW;
        if (cursorX > WINDOW_WIDTH - 32) cursorX = WINDOW_WIDTH - 32;
        
        SDL_Rect textCursor = { cursorX, inputBar.y + 10, 2, 20 };
        SDL_SetRenderDrawColor(state.renderer.get(), 0, 192, 255, 255);
        SDL_RenderFillRect(state.renderer.get(), &textCursor);
    }
    

    
    // 7. Render present buffer
    SDL_RenderPresent(state.renderer.get());
}

int main(int argc, char* argv[]) {
    // 1. Load settings.json configuration properties
    std::string aiModel = "gemini.json";
    std::string systemPrompt = "";
    int maxRetries = 3;
    int retryDelayMs = 1000;
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
            if (j.contains("systemPrompt") && j["systemPrompt"].is_string()) {
                systemPrompt = j["systemPrompt"].get<std::string>();
            }
            if (j.contains("maxRetries") && j["maxRetries"].is_number()) {
                maxRetries = j["maxRetries"].get<int>();
            }
            if (j.contains("retryDelayMs") && j["retryDelayMs"].is_number()) {
                retryDelayMs = j["retryDelayMs"].get<int>();
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
                            state.chapters.push_back(chData);
                            
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
    
    // Copy settings to state
    state.systemPrompt = systemPrompt;
    state.bookWorld = bookWorld;
    state.bookTitle = bookTitle;
    state.bookStartPrompt = bookStartPrompt;
    
    // Instanciate external AI API client
    state.aiClient = std::make_unique<AskAiExternal>(aiModel);
    state.aiClient->setRetrySettings(maxRetries, retryDelayMs);
    
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
    std::string fontPath = GetSystemFontPath();
    state.fontTitle.reset(TTF_OpenFont(fontPath.c_str(), 24));
    state.fontMessage.reset(TTF_OpenFont(fontPath.c_str(), 18));
    state.fontUI.reset(TTF_OpenFont(fontPath.c_str(), 16));
    state.fontSmallUI.reset(TTF_OpenFont(fontPath.c_str(), 13));
    
    if (!state.fontTitle || !state.fontMessage || !state.fontUI || !state.fontSmallUI) {
        std::cerr << "Font Init Fail: " << TTF_GetError() << std::endl;
    }
    
    // Load existing game state if save.json is present
    bool loaded = LoadGame();
    UpdateSystemPrompt();
    
    // Submit introductory query automatically to start narration (hide from visible chat history)
    if (!loaded || state.messages.empty()) {
        std::string startQ = state.bookStartPrompt.empty() ? "Привет! Расскажи интересную историю на русском языке и предложи варианты выбора." : state.bookStartPrompt;
        SubmitQuery(startQ, false, false);
    } else if (state.messages.back().sender == "User") {
        std::cout << "[Startup] Last message is from User. Auto-resuming narrative request..." << std::endl;
        SubmitQuery(state.messages.back().text, true, false);
    }
    
    // 7. Start main execution thread loop
    while (state.running) {
        MainIteration();
        SDL_Delay(1); // CPU throttle limiter
    }
    
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