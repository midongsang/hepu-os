#!/usr/bin/env python3
"""将字体文件转换为C数组头文件"""
import sys
import os

def font_to_c_array(input_file, output_file, array_name):
    with open(input_file, 'rb') as f:
        data = f.read()
    
    size = len(data)
    
    with open(output_file, 'w') as f:
        f.write(f'/* Auto-generated from {os.path.basename(input_file)} */\n')
        f.write(f'/* Size: {size} bytes */\n')
        f.write(f'#ifndef {array_name.upper()}_H\n')
        f.write(f'#define {array_name.upper()}_H\n\n')
        f.write(f'#include <stdint.h>\n\n')
        f.write(f'static const uint32_t {array_name}_size = {size};\n\n')
        f.write(f'static const uint8_t {array_name}_data[] = {{\n')
        
        for i in range(0, size, 16):
            chunk = data[i:i+16]
            hex_vals = ', '.join(f'0x{b:02X}' for b in chunk)
            if i + 16 < size:
                f.write(f'    {hex_vals},\n')
            else:
                f.write(f'    {hex_vals}\n')
        
        f.write(f'}};\n\n')
        f.write(f'#endif /* {array_name.upper()}_H */\n')
    
    print(f'Generated {output_file}: {size} bytes')

if __name__ == '__main__':
    if len(sys.argv) != 4:
        print(f'Usage: {sys.argv[0]} <input.ttf> <output.h> <array_name>')
        sys.exit(1)
    
    font_to_c_array(sys.argv[1], sys.argv[2], sys.argv[3])
