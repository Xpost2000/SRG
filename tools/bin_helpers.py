import sys
import struct;

#
# Binary helpers for python...
#
def write_s8(f, v):  
    f.write(struct.pack('<b', v));
def write_u8(f, v):  
    f.write(struct.pack('<B', v));
def write_s16(f, v): 
    f.write(struct.pack('<h', v));
def write_u16(f, v): 
    f.write(struct.pack('<H', v));
def write_s32(f, v): 
    f.write(struct.pack('<i', v));
def write_u32(f, v): 
    f.write(struct.pack('<I', v));
def write_s64(f, v): 
    f.write(struct.pack('<q', v));
def write_u64(f, v): 
    f.write(struct.pack('<Q', v));
def write_f32(f, v): 
    f.write(struct.pack('<f', v));
def write_f64(f, v): 
    f.write(struct.pack('<d', v));
def read_s8(f):
    return struct.unpack('<b', f.read(1))[0]
def read_u8(f):
    return struct.unpack('<B', f.read(1))[0]
def read_s16(f):
    return struct.unpack('<h', f.read(2))[0]
def read_u16(f):
    return struct.unpack('<H', f.read(2))[0]
def read_s32(f):
    return struct.unpack('<i', f.read(4))[0]
def read_u32(f):
    return struct.unpack('<I', f.read(4))[0]
def read_s64(f):
    return struct.unpack('<q', f.read(8))[0]
def read_u64(f):
    return struct.unpack('<Q', f.read(8))[0]
def read_f32(f):
    return struct.unpack('<f', f.read(4))[0]
def read_f64(f):
    return struct.unpack('<d', f.read(8))[0]

#
# Helpers for writing the PSYQ types
#
def write_VECTOR(f, x, y, z):
    x = int(x);
    y = int(y);
    z = int(z);
    
    write_s32(f, x);
    write_s32(f, y);
    write_s32(f, z);
    
def write_SVECTOR(f, x, y, z):
    x = int(x);
    y = int(y);
    z = int(z);
    
    write_s16(f, x);
    write_s16(f, y);
    write_s16(f, z);
    write_s16(f, 0);

def read_VECTOR(f):
    return (
        read_s32(f, x),
        read_s32(f, y),
        read_s32(f, z) 
    )
    
def read_SVECTOR(f):
    return (
        read_s16(f, x),
        read_s16(f, y),
        read_s16(f, z),
        read_s16(f, 0)
    )

# to read a full SRG font
# read_SRGFONT_info(f); read_TIM_info(f);
def read_SRGFONT_info(f):
    glyph_width  = read_s8(f);
    glyph_height = read_s8(f);
    columns      = read_s8(f);
    rows         = read_s8(f);
    glyphmap     = f.read(256);
    return (
        glyph_width ,
        glyph_height,
        columns     ,
        rows        ,
        glyphmap
    );
    
def read_TIM_info(f):
    # tag
    read_u8(f);
    # version
    read_u8(f);
    # pad 0
    read_u8(f);
    # pad 1
    read_u8(f);

    flags = read_u32(f);

    clut_length = clut_x = clut_y = clut_width = clut_height = 0
    clut_contents = b""
    # contains clut (just in-case though all the images we use have a clut)
    if (flags & 0x8) > 0:
        clut_contents = f.read(clut_width * clut_height * 2);
        clut_length = read_u32(f);
        clut_x = read_u16(f);
        clut_y = read_u16(f);
        clut_width = read_u16(f);
        clut_height = read_u16(f);

    image_length = read_u32(f);
    image_x = read_u16(f);
    image_y = read_u16(f);
    image_width = read_u16(f);
    image_height = read_u16(f);
    image_contents = f.read(image_width * image_height * 2);

    return (
        clut_length,
        clut_x,
        clut_y,
        clut_width,
        clut_height,
        clut_contents,
        image_length,
        image_x,
        image_y,
        image_width,
        image_height,
        image_contents
    )
    
