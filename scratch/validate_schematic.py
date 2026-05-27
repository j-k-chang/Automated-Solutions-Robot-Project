import sys

def parse_sexpr(content):
    tokens = []
    length = len(content)
    idx = 0
    
    line_num = 1
    col_num = 1
    
    while idx < length:
        char = content[idx]
        if char == '\n':
            line_num += 1
            col_num = 1
            idx += 1
            continue
        elif char.isspace():
            col_num += 1
            idx += 1
            continue
            
        if char == '(':
            tokens.append(('OPEN', '(', line_num, col_num, idx))
            col_num += 1
            idx += 1
        elif char == ')':
            tokens.append(('CLOSE', ')', line_num, col_num, idx))
            col_num += 1
            idx += 1
        elif char == '"':
            # String token
            start_idx = idx
            start_l, start_c = line_num, col_num
            idx += 1
            col_num += 1
            escaped = False
            val = []
            
            while idx < length:
                c = content[idx]
                if c == '\n':
                    line_num += 1
                    col_num = 1
                else:
                    col_num += 1
                    
                if escaped:
                    val.append(c)
                    escaped = False
                elif c == '\\':
                    escaped = True
                elif c == '"':
                    break
                else:
                    val.append(c)
                idx += 1
                
            if idx >= length:
                print(f"Unclosed string starting at line {start_l}, col {start_c}")
                return None
            idx += 1 # Consume closing quote
            col_num += 1
            tokens.append(('STRING', "".join(val), start_l, start_c, start_idx))
        else:
            # Atom token
            start_idx = idx
            start_l, start_c = line_num, col_num
            val = []
            while idx < length:
                c = content[idx]
                if c.isspace() or c in '()':
                    break
                val.append(c)
                idx += 1
                col_num += 1
            tokens.append(('ATOM', "".join(val), start_l, start_c, start_idx))
            
    print(f"Successfully tokenized {len(tokens)} tokens!")
    return tokens

def build_tree(tokens):
    stack = [[]]
    
    for tok_type, val, l, c, idx in tokens:
        if tok_type == 'OPEN':
            new_list = []
            stack[-1].append(new_list)
            stack.append(new_list)
        elif tok_type == 'CLOSE':
            if len(stack) <= 1:
                print(f"Extra closing parenthesis at line {l}, col {c}")
                return None
            stack.pop()
        else:
            stack[-1].append((tok_type, val, l, c))
            
    if len(stack) != 1:
        print(f"Unclosed parenthesis block! Nesting level: {len(stack) - 1}")
        return None
        
    return stack[0]

if __name__ == "__main__":
    filepath = "14ch_dispensing_project/14ch_dispensing_system.kicad_sch"
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()
        
    tokens = parse_sexpr(content)
    if tokens:
        tree = build_tree(tokens)
        if tree:
            print("Successfully parsed schematic S-expression tree!")
            # Check top level
            if len(tree) > 0 and isinstance(tree[0], list):
                print(f"Top-level command: {tree[0][0] if len(tree[0]) > 0 else 'EMPTY'}")
