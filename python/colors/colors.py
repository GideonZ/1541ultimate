from PIL import Image, ImageDraw
from math import sin, cos
from colorsys import hsv_to_rgb

u64_colors = [
    ( 0x00, 0x00, 0x00, 0b000000, 0x0, 0b000, 0.00,  0 ),  ## 0 black                     # Colodore 
    ( 0xEF, 0xEF, 0xEF, 0b000000, 0xE, 0b000, 2.45,  0 ),  ## 1 white -- FIXME BACK TO FF # Colodore 
    ( 0x8D, 0x2F, 0x34, 0b010010, 0x5, 0b110, 0.82, 18.1 ),  ## 2 red                       # Colodore #8D2F34
    ( 0x6A, 0xD4, 0xCD, 0b110010, 0xA, 0b110, 1.77, 50.4 ),  ## 3 cyan                      # Colodore #6AD4CD
    ( 0x98, 0x35, 0xA4, 0b000111, 0x6, 0b110, 1.00, 10.6 ),  ## 4 purple                    # Colodore #9835A4
    ( 0x4C, 0xB4, 0x42, 0b101000, 0x8, 0b110, 1.36, 44.4 ),  ## 5 green                     # Colodore #4CB442
    ( 0x2C, 0x29, 0xB1, 0b111111, 0x4, 0b110, 0.62,  62  ),  ## 6 blue                      # Colodore #2C29B1
    ( 0xEF, 0xEF, 0x5D, 0b011111, 0xC, 0b110, 2.18, 32.1 ),  ## 7 yellow                    # Colodore #F0F45D
    ( 0x98, 0x4E, 0x20, 0b010110, 0x6, 0b110, 1.00, 23.6 ),  ## 8 orange                    # Colodore #984E20
    ( 0x5B, 0x38, 0x00, 0b010110, 0x4, 0b110, 0.62, 27.2 ),  ## 9 brown                     # Colodore #5B3800
    ( 0xD1, 0x67, 0x6D, 0b010010, 0x8, 0b110, 1.36, 18.1 ),  ## A pink                      # Colodore #D1676D
    ( 0x4A, 0x4A, 0x4A, 0b000000, 0x5, 0b000, 0.82,  0 ),  ## B dark grey                 # Colodore #4A4A4A
    ( 0x7B, 0x7B, 0x7B, 0b000000, 0x7, 0b000, 1.28,  0 ),  ## C medium grey               # Colodore #7B7B7B
    ( 0x9F, 0xEF, 0x93, 0b101000, 0xC, 0b110, 2.18, 44.4 ),  ## D light green               # Colodore #9FFF93
    ( 0x6D, 0x6A, 0xEF, 0b111111, 0x7, 0b110, 1.28, 0.1 ),  ## E light blue                # Colodore #6D6AFF
    ( 0xB2, 0xB2, 0xB2, 0b000000, 0xA, 0b000, 1.78,  0 ),  ## F light grey                # Colodore #B2B2B2
    ]

# R = Y + 1.140V    
# G = Y - 0.395U - 0.581V    
# B = Y + 2.032U    
def my_yuv_to_rgb(y, u, v):
    r = int(255 * (y + 1.14 * v))
    g = int(255 * (y - 0.395 * u - 0.581 * v))
    b = int(255 * (y + 2.032 * u))
    r = max(0, min(255, r))
    g = max(0, min(255, g))
    b = max(0, min(255, b))
    return (r, g, b)

def my_hsv_to_rgb(intensity, angle, saturation):

    angle = 3.14159265 * float(angle) / 32.0 
    y = float(intensity)
    u = cos(angle) * saturation
    v = sin(angle) * saturation
    return my_yuv_to_rgb(y, u, v)

def my_hsv_to_yuv(intensity, angle, saturation):
    angle = 3.14159265 * float(angle) / 32.0 
    y = float(intensity)
    u = cos(angle) * saturation
    v = sin(angle) * saturation
    return (y,u,v)

if __name__ == '__main__': 
    im = Image.new(mode = "RGB", size = (480, 480), color=0)    
    draw = ImageDraw.Draw(im)

    for i in range(16):
        (r,g,b,angle,intensity,saturation,intvolt, perfect_angle) = u64_colors[i]
        draw.rectangle((50,30*i,200,27+30*i), (r,g,b), (r,g,b), 0)

        saturation = float(saturation) / 7.0
        intensity = float(intensity) / 15.0

        (r, g, b) = my_hsv_to_rgb(intensity, angle, saturation * 0.27)
        draw.rectangle((250,30*i, 400,27+30*i), (r,g,b), (r,g,b), 0)
        
        intensity = intvolt / 2.5
        (r, g, b) = my_hsv_to_rgb(intensity, perfect_angle, saturation * 0.27)
        draw.rectangle((405,30*i,435,27+30*i), (r,g,b), (r,g,b), 0)
        draw.rectangle((15,30*i,45,27+30*i), (r,g,b), (r,g,b), 0)

        #(r, g, b) = my_hsv_to_rgb(intensity, perfect_angle, saturation * 0.3)
        #if (perfect_angle != angle):
        #    draw.rectangle((205,30*i,235,27+30*i), (r,g,b), (r,g,b), 0)

    # write to stdout
    im.save("colors.png", "PNG")

if __name__ == '__main__':
    im = Image.new(mode = "RGB", size = (480, 480), color=0)    
    draw = ImageDraw.Draw(im)

    mixes = [ (6, 9), (2, 11), (4, 8), (12, 14), (5, 10), (3, 15), (7, 13) ]
    pos = 50
    for (col1, col2) in mixes:
        (r,g,b,angle,intensity,saturation,intvolt, perfect_angle) = u64_colors[col1]
        saturation = float(saturation) / 7.0
        intensity = intvolt / 2.5
        (y1, u1, v1) = my_hsv_to_yuv(intensity, angle, saturation * 0.3)
    
        (r,g,b,angle,intensity,saturation,intvolt, perfect_angle) = u64_colors[col2]
        saturation = float(saturation) / 7.0
        intensity = intvolt / 2.5
        (y2, u2, v2) = my_hsv_to_yuv(intensity, angle, saturation * 0.3)
    
        v_mix = (v1 + v2) / 2
        
        (r1, g1, b1) = my_yuv_to_rgb(y1, u1, v1)
        (r2, g2, b2) = my_yuv_to_rgb(y2, u2, v2)
        (r3, g3, b3) = my_yuv_to_rgb(y1, u1, v_mix)
        (r4, g4, b4) = my_yuv_to_rgb(y2, u2, v_mix)
    
        for i in range(7):
            draw.line((50, pos+6*i+0, 200,pos+6*i+0), (r1, g1, b1), 3, None)
            draw.line((50, pos+6*i+3, 200,pos+6*i+3), (r2, g2, b2), 3, None)
    
            draw.line((250, pos+6*i+0, 400,pos+6*i+0), (r3, g3, b3), 3, None)
            draw.line((250, pos+6*i+3, 400,pos+6*i+3), (r4, g4, b4), 3, None)

        pos += 50
        
    im.save("mixed4.png", "PNG")
