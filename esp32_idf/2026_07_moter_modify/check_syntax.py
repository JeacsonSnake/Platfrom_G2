#!/usr/bin/env python3
import re

with open('main/pid.c', 'r', encoding='utf-8') as f:
    content = f.read()

for brace in ['()', '{}']:
    opens = content.count(brace[0])
    closes = content.count(brace[1])
    print(f"{brace}: open={opens}, close={closes}, ok={opens==closes}")

lines = content.split('\n')
for i, line in enumerate(lines, 1):
    code = re.sub(r'//.*', '', line)
    dq = 0
    j = 0
    while j < len(code):
        if code[j] == '\\' and j+1 < len(code):
            j += 2
            continue
        if code[j] == '"':
            dq += 1
        j += 1
    if dq % 2 != 0:
        print(f"Line {i} may have unbalanced double quotes: {line[:80]}")

print("Basic syntax check done.")
