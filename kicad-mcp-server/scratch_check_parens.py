def check_parens(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
    
    stack = []
    for idx, char in enumerate(content):
        if char == '(':
            stack.append(idx)
        elif char == ')':
            if not stack:
                print(f"Unmatched closing parenthesis at char index {idx}")
                return False
            stack.pop()
            
    if stack:
        print(f"Unmatched opening parenthesis at char indices: {stack[:10]}")
        return False
        
    print("Parenthesis are perfectly balanced!")
    return True

check_parens(r"c:\Users\littl\Documents\PlatformIO\Projects\260330-225943-uno_r4_wifi\14ch_dispensing_project\14ch_dispensing_system.kicad_sch")
