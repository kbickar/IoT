from sklearn import tree
from PIL import Image
from sklearn.tree.export import export_text
from sklearn.tree import _tree
from sklearn.model_selection import train_test_split
import numpy as np
NUM_DIGITS = 9
DIGIT_HEIGHT = 16
DIGIT_WIDTH = 8
DIGIT_SPACE = 2

def digit_blocks(digit_buff):
    digit = [ ]
    
    for y,row in enumerate(digit_buff):
        row_mid = (max(row) - min(row))/2 + min(row) 
        for x, px in enumerate(row):
            if px > row_mid: digit.append(0)
            else: digit.append(1)
    return digit

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
            if y >= origin_y and y < origin_y+16:
                yd = y - origin_y
                for d in range(NUM_DIGITS):
                    for w in range(DIGIT_WIDTH):
                        x = origin_x + d*(DIGIT_WIDTH + DIGIT_SPACE) + w;
                        r,g,b = im.getpixel((x, y))
                        digits[d][yd][w] = g
        if origin_y > -1 and  y > origin_y + 16 : break

    return digits

def tree_to_code(tree):
    tree_ = tree.tree_
    feature_name = [
        feature_names[i] if i != _tree.TREE_UNDEFINED else "undefined!"
        for i in tree_.feature
    ]

    def recurse(node, depth):
        indent = "    " * depth
        if tree_.feature[node] != _tree.TREE_UNDEFINED:
            name = "feature_"+str(node)
            name = feature_name[node]
            print ("{}if {}:".format(indent, name))
            recurse(tree_.children_right[node], depth + 1)
            print ("{}else:".format(indent))
            recurse(tree_.children_left[node], depth + 1)
        else:
            vals = tree_.value[node]
            cls = np.argmax(vals)
            print ("{}return {}".format(indent, cls))

    recurse(0, 1)    

image_vals = {}
with open("data.txt") as f:
    for line in f:
        if ":" not in line: continue
        fn, val = line.split(":")
        image_vals[fn.strip()] = val.strip()
        
                   
feature_names = []
for y in range(DIGIT_HEIGHT):
    for x in range(DIGIT_WIDTH):
        feature_names.append("digit["+str(y)+"]["+str(x)+"]")

X = []
Y = []
for fn, val in image_vals.items():
    try:
        for digit, d in zip(process_image(fn), val[::-1]):
            X.append(digit_blocks(digit))
            Y.append(d)
    except:
        continue
X_train, X_test, y_train, y_test = train_test_split(X, Y, test_size=0.1)
clf = tree.DecisionTreeClassifier(min_samples_leaf=2)
clf = clf.fit(X_train, y_train)
      
tree_to_code(clf)
#print(export_text(clf))
print(clf.score(X_test, y_test))


