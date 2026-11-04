#!/usr/bin/env python3
"""V0.13.8 P6: 截图定位窗口/启动器并输出点击坐标
用法: hepu_clickauto.py <hmp_sock> <png_prefix> <mode> <arg>
mode: auto_menu<i> / auto_cb<i> / auto_close / auto_launcher<i>
输出: "<x> <y>"
"""
import socket, time, sys
from PIL import Image
import numpy as np

sock, prefix, mode, arg = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

def hmp(cmd):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock); s.settimeout(1)
    try: s.recv(4096)
    except Exception: pass
    s.send((cmd + '\n').encode()); time.sleep(0.15); s.close()

hmp('screendump ' + prefix + '_pos.ppm')
time.sleep(2)
a = np.array(Image.open(prefix + '_pos.ppm').convert('RGB'))
H, W = a.shape[0], a.shape[1]

# 窗口: 找 titlebar 色 (236,229,214) 长行
tb = (np.abs(a[:,:,0].astype(int)-236)<4)&(np.abs(a[:,:,1].astype(int)-229)<4)&(np.abs(a[:,:,2].astype(int)-214)<4)
rows = tb.sum(axis=1)
best_y, best_n = -1, 0
for y in range(H):
    if rows[y] > best_n:
        best_n, best_y = rows[y], y

if best_n >= 80 and best_y >= 0:
    xs = np.where(tb[best_y])[0]
    wx, ww = int(xs.min()), int(xs.max() - xs.min())
    top = best_y
    while top > 0 and rows[top-1] > best_n*0.4:
        top -= 1
    wy = top
    if mode == 'auto_menu':
        i = int(arg)
        print(wx + 76, wy + 36 + i*32 + 14)
    elif mode == 'auto_cb':
        i = int(arg)
        print(wx + 16 + 140 + 7, wy + 36 + i*22 - 1 + 7)
    elif mode == 'auto_close':
        print(wx + ww - 26 + 9, wy + 14)
    else:
        print('0 0')
else:
    # 启动器卡片 (居中 500x350)
    lx = (W-500)//2; ly = (H-350)//2 - 50
    if mode == 'auto_launcher':
        i = int(arg)
        print(lx + 16 + 100, ly + 68 + i*34 + 13)
    else:
        print('0 0')
