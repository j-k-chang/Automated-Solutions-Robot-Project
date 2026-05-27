import sys

def check_parentheses(filepath):
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
    except Exception as e:
        print(f"Error reading file: {e}")
        return

    stack = []
    line_num = 1
    col_num = 1
    
    for idx, char in enumerate(content):
        if char == '\n':
            line_num += 1
            col_num = 1
        else:
            col_num += 1
            
        if char == '(':
            stack.append((line_num, col_num, idx))
        elif char == ')':
            if not stack:
                print(f"Extra closing parenthesis ')' at line {line_num}, col {col_num}")
                # Print context
                start = max(0, idx - 40)
                end = min(len(content), idx + 40)
                print(f"Context: {content[start:idx]} >>> {char} <<< {content[idx+1:end]}")
                return False
            stack.pop()
            
    if stack:
        print(f"Unmatched opening parentheses: {len(stack)}")
        for i in range(min(5, len(stack))):
            l, c, idx = stack[i]
            print(f"  Parenthesis at line {l}, col {c}")
            end = min(len(content), idx + 80)
            print(f"    Context: {content[idx:end]}")
        return False
        
    print("Parentheses are perfectly balanced!")
    return True

if __name__ == "__main__":
    check_parentheses("14ch_dispensing_project/14ch_dispensing_system.kicad_sch")
