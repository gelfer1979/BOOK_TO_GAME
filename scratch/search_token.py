import os
import re

path = r"C:\Users\user\.gemini\antigravity-ide\brain\35ce69b1-e61d-4b31-a8c9-16aaa020cab1\.system_generated\steps\651\content.md"
with open(path, "r", encoding="utf-8") as f:
    c = f.read()

# Let's locate "web"===this.env again and search for fetch or ajax calls in its vicinity (say, 5000 characters after it)
idx = c.find('"web"===this.env')
if idx != -1:
    section = c[idx:idx+10000]
    print("--- Section around 'web' env initialization ---")
    # find all fetch or XMLHttpRequest or APIOrigin references
    for m in re.finditer(r'fetch|APIOrigin|xhr|authToken|localStorage', section):
        start = max(0, m.start() - 50)
        end = min(len(section), m.end() + 150)
        print(f"[{m.group()}]: ... {section[start:end]} ...")
        print("-" * 30)
