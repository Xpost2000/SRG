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
