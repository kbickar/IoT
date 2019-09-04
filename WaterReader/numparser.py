from sklearn import tree
from PIL import Image
import os
NUM_DIGITS = 9
DIGIT_HEIGHT = 16
DIGIT_WIDTH = 8
DIGIT_SPACE = 2

def process_digit(digit_buff):
    digit = [ [0 for x in range(DIGIT_WIDTH)] for y in range(DIGIT_HEIGHT) ]

    pxs = []
    for y,row in enumerate(digit_buff):
        for x, px in enumerate(row):
            pxs.append(px)
    row_mid = (max(pxs) - min(pxs))/2 + min(pxs) 
    for y,row in enumerate(digit_buff):
        row_mid = (max(row) - min(row))/2 + min(row) 
        for x, px in enumerate(row):
            if px > row_mid: digit[y][x] = 0
            else: digit[y][x] = 1
    ds = ""
    bin_val = 0
    # top left
    if digit[3][0] + digit[3][1] + digit[4][0] + digit[4][1] > 2 or \
       digit[3][1] + digit[3][2] + digit[4][1] + digit[4][2] > 2 or \
       digit[3][0] + digit[4][0] + digit[5][0] > 2 or \
       digit[3][1] + digit[4][1] + digit[5][1] > 2 :
        bin_val |= 1 # 1
        ds+= "1 "
    # bottom left
    if digit[10][0] + digit[10][1] + digit[11][0] + digit[11][1] > 2 or \
       digit[10][1] + digit[10][2] + digit[11][1] + digit[11][2] > 2 or \
       digit[9][0] + digit[10][0] + digit[11][0] > 2 or \
       digit[9][1] + digit[10][1] + digit[11][1] > 2 :
        bin_val |= 2 # 2
        ds+= "2 "
    # top right
    if digit[3][4] + digit[3][5] + digit[4][4] + digit[4][5] > 2 or \
       digit[3][5] + digit[3][6] + digit[4][5] + digit[4][6] > 2 or \
       digit[4][5] + digit[5][5] + digit[6][5] > 2 or \
       digit[4][6] + digit[5][6] + digit[6][6] > 2 :
        bin_val |= 4 # 3
        ds+= "3 "
    # bottom right
    if digit[10][4] + digit[10][5] + digit[11][4] + digit[11][5] > 2 or \
       digit[10][5] + digit[10][6] + digit[11][5] + digit[11][6] > 2 or \
       digit[9][5] + digit[10][5] + digit[11][5] + digit[12][5] > 2 or \
       digit[9][6] + digit[10][6] + digit[11][6] + digit[12][6] > 2 :
        bin_val |= 8 # 4
        ds+= "4 "
    # top
    if digit[0][2] + digit[0][3] + digit[1][2] + digit[1][3] > 3 or \
       digit[1][2] + digit[1][3] + digit[2][2] + digit[2][3] > 3:
        bin_val |= 16 # 5
        ds+= "5 "
    # middle
    if digit[6][2] + digit[7][2] + digit[8][2] + digit[9][2] > 0 and \
       digit[6][3] + digit[7][3] + digit[8][3] + digit[9][3] > 0 and \
       digit[6][4] + digit[7][4] + digit[8][4] + digit[9][4] > 0 or \
       digit[7][2] + digit[8][2] + digit[7][3] + digit[8][3] > 3:
        bin_val |= 32 # 6
        ds+= "6 "
    # bottom
    if digit[13][3] + digit[13][4] + digit[14][3] + digit[14][4] > 3 or \
       digit[14][3] + digit[14][4] + digit[15][3] + digit[15][4] > 3 :
        bin_val |= 64 # 7
        ds+= "7 "

    if DEBUG:
        print
        for y,row in enumerate(digit):
            for x, px in enumerate(row):
                if digit[y][x]: print "#",
                else: print " ",
            print
        print ds+"=",
    

    vals = { 3 : 1, 118: 2, 115:3, 43:4, 121:5, 125:6, 67:7, 127:8, 107:9, 95:0}
    if bin_val not in vals:
        if DEBUG: print ">",bin_val
        return "X"
    return vals[bin_val]


def process_image(imgname):
    im = Image.open(imgname)
    digits = [[ [0 for x in range(DIGIT_WIDTH)] for y in range(DIGIT_HEIGHT) ]
              for d in range(NUM_DIGITS)]

    origin_x = -1
    origin_y = -1
    finds = 0
    # store the dot col
    dot_col = [0 for i in range(8)]
    for y in range(40, 90):
        if y < 48:
            x = 50
            r,g,b = im.getpixel((x, y))
            dot_col[y-40] = g
        if origin_x < 0:
            light = False
            for x in range(30,44):
                r,g,b = im.getpixel((x, y))
                if light and g < 200:
                    finds += 1

                    if finds > 2:
                        origin_x = x
                        origin_y = y
                        
                        origin_x += 3
                        origin_y += 3
                        if DEBUG: print "Origin:", origin_x, origin_y
                    break
                if g > 200: light= True
        else:
            # find dot to fine tune alignment
            if y == origin_y-1:
                dot_row = [0 for i in range(10)]
                for i,x in enumerate(range(origin_x + 25,  origin_x+ 35)):
                    r,g,b = im.getpixel((x, y))
                    dot_row[i] = g
                mid = (max(dot_row) - min(dot_row))/2 + min(dot_row) 
                for g in dot_row[2:]:
                    if g > mid: origin_x+= 1
                    if g < mid: break
                    
                # now fine tune y axis
                mid = (max(dot_col[origin_y -4 - 40:]) - min(dot_col[origin_y -4 - 40:]))/2 + min(dot_col[origin_y -4 - 40:])
                if dot_col[origin_y -3 - 40] < mid:
                    origin_y+=1
                    continue
            # store digits
            if DEBUG: print "Origin:", origin_x, origin_y
            if y >= origin_y and y < origin_y+16:
                yd = y - origin_y
                for d in range(NUM_DIGITS):
                    for w in range(DIGIT_WIDTH):
                        x = origin_x + d*(DIGIT_WIDTH + DIGIT_SPACE) + w;
                        r,g,b = im.getpixel((x, y))
                        digits[d][yd][w] = g
        if origin_y > -1 and  y > origin_y + 16 : break

    val = ""
    for digit in digits:
        x = process_digit(digit)
        val = str(x)+val        
        if DEBUG: print x,
    return val

image_vals = {}
with open("data.txt") as f:
    for line in f:
        if ":" not in line: continue
        fn, val = line.split(":")
        image_vals[fn.strip()] = val.strip()
INVALID = {
    "msg19-08-11-15.30.07.jpg": "00XXXXXXX",
    "msg19-08-18-16.50.10.jpg": "00XXXXXXX",
    "msg19-08-19-03.32.10.jpg": "00XX111XX",
    "msg19-08-19-08.16.10.jpg": "00XXXXXXX",
    "msg19-08-19-10.38.10.jpg": "003XXXXXX",
    "msg19-08-19-10.51.10.jpg": "003XXXXXX",
    "msg19-08-20-02.50.11.jpg": "00XXXXXXX",
    "msg19-08-20-05.01.11.jpg": "00XX111XX",
    "msg19-08-20-08.17.11.jpg": "00XX111XX",
    "msg19-08-20-15.10.11.jpg": "00XX111XX",
    }
DEBUG = 1

if not DEBUG:
    wrong = 0
    for fn, val in image_vals_full.iteritems():
        try:
            guess = process_image(fn)
            guess = "00"+guess[2:]
            if guess != val:
                print fn, guess, val
                wrong += 1
        except:
            continue
    print "Wrong:",wrong
    print 1.0*wrong/len(image_vals_full)

##    issues = []
##    prev = 0
##    for fn in os.listdir("."):
##        if fn in INVALID: continue
##        if fn.startswith("msg19") and fn.endswith(".jpg"):
##            if fn in image_vals:
##                guess = image_vals[fn]
##            else:
##                try:
##                    guess = process_image(fn)
##                except:
##                    pass
##            if guess[0] != "0" or guess[1] != "0":
##                guess = "00" + guess[2:]
##            line = "    \""+fn+"\": \""+guess+"\","
##            if "X" in guess:
##                issues.append(line)
##            else:
##                curr = int(guess)
##                if (curr < int(prev) or curr > int(prev) + 10000) and prev and fn not in image_vals:
##                    issues.append(line+"# "+prev)
##                else:
##                    prev = guess
##                    print line
##    print "====="
##    for line in issues:
##        print line
    
process_image("msg19-08-18-16.33.10.jpg")
