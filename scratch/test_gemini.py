import json
import urllib.request
import urllib.error
import os

def main():
    # Load settings
    with open('settings.json', 'r', encoding='utf-8') as f:
        settings = json.load(f)
    
    # Load book
    with open('book.json', 'r', encoding='utf-8') as f:
        book = json.load(f)
        
    # Load gemini
    with open('gemini.json', 'r', encoding='utf-8') as f:
        gemini = json.load(f)
        
    system_prompt = settings['systemPrompt']
    book_world = book['world']
    chapters = book['plot']
    
    # Build combined prompt for chapter 2
    combined_prompt = system_prompt
    if book_world:
        combined_prompt += "\n\nИгровой мир:\n" + book_world
        
    combined_prompt += "\n\nТекущее состояние игры:\n"
    combined_prompt += "Текущая глава: 2\n"
    
    chapter_summaries = [
        "На оружейном складе ВДНХ ты успешно провел подготовку к выходу, получив от дяди Саши сотню патронов калибра 5.45 и пропуск на дрезину. Проверив исправность автомата и состояние противогаза, ты полностью готов покинуть станцию и отправиться навстречу опасностям туннелей."
    ]
    
    combined_prompt += "\nКраткая история предыдущих глав:\n"
    for i, summary in enumerate(chapter_summaries):
        combined_prompt += f"- Глава {i + 1}: {summary}\n"
        
    active_chapter_desc = ""
    active_chapter_title = ""
    for ch in chapters:
        if ch['chapter'] == 2:
            active_chapter_desc = ch['description']
            active_chapter_title = ch['title']
            break
            
    if active_chapter_desc:
        combined_prompt += f"\nЦели и описание текущей главы (Глава 2: {active_chapter_title}):\n{active_chapter_desc}\n"
        
    combined_prompt += "\n\nПРАВИЛА ИГРЫ ДЛЯ ИИ:\n"
    combined_prompt += "1. Когда игрок успешно достигает всех целей текущей главы и готов перейти к следующей, вы обязательно должны добавить тег <next_chapter>НОМЕР_СЛЕДУЮЩЕЙ_ГЛАВЫ</next_chapter> в самый конец вашего ответа. Не переводите игрока на следующую главу до достижения целей текущей.\n"
    combined_prompt += "2. Вы имеете право и должны убить персонажа игрока, если он совершает фатальные ошибки, лезет на рожон без экипировки (например, в радиоактивную зону без противогаза) или проигрывает бой с мутантами.\n"
    combined_prompt += "3. Если персонаж погибает, красочно опишите его смерть и обязательно добавьте тег <player_dead/> в самый конец вашего ответа."
    
    print("--- SYSTEM PROMPT ---")
    print(combined_prompt)
    print("---------------------\n")
    
    # Query Gemini
    api_key = gemini['apiKey']
    url = gemini['baseUrl']
    
    headers = {
        "Content-Type": "application/json; charset=utf-8",
        "x-goog-api-key": api_key
    }
    
    payload = {
        "contents": [
            {
                "parts": [
                    {"text": "Начни главу 2."}
                ]
            }
        ],
        "systemInstruction": {
            "parts": [
                {"text": combined_prompt}
            ]
        }
    }
    
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode('utf-8'),
        headers=headers,
        method='POST'
    )
    
    print("Sending request to Gemini...")
    try:
        with urllib.request.urlopen(req) as response:
            res_body = response.read().decode('utf-8')
            res_json = json.loads(res_body)
            print("\n--- RESPONSE ---")
            text = res_json['candidates'][0]['content']['parts'][0]['text']
            print(text)
    except urllib.error.HTTPError as e:
        print(f"HTTP Error: {e.code} - {e.reason}")
        print(e.read().decode('utf-8'))
    except Exception as e:
        print("Error:", e)

if __name__ == '__main__':
    main()
