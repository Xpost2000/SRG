#
# tiny little blender export script template,
# this is unfortunately not a proper blender plugin
#
# select stuff, and then export.
#

import bpy;
import bmesh;
import struct
import os;
import sys
from pathlib import Path;

#
# boilerplate script setup
#
args             = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
text             = bpy.data.texts[bpy.context.space_data.text.name];
SCRIPT_DIR       = Path(os.path.dirname(os.path.realpath(bpy.path.abspath(text.filepath))));
OUTPUT_FILE_NAME = SCRIPT_DIR / (args[0] if args else "out.bin");

#
# need to pick some better scaling to make sure we don't lose vtx precision
#
PSX_SRG_UNIT_SCALE_FACTOR = 150; 
PSX_GTE_UNIT_SCALE_FACTOR = 4096;

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

obj = bpy.context.object.data;

bm = bmesh.new();
bm.from_mesh(obj);
bmesh.ops.triangulate(bm, faces=bm.faces[:]);


bm.verts.ensure_lookup_table();

#
# get whatever active UV layer is
# since Blender supports multiple UV layers (we only need 1/first one.)
#
uv_layer = bm.loops.layers.uv.verify();

def extract_vertex_data_tuple(l):
    v = l.vert;
    position = v.co.copy();
    normal = v.normal.copy();
    uv = l[uv_layer].uv.copy() if uv_layer else None
    return (position, normal, uv);

# following this basically
#struct Render_Mesh {
#  uint16_t vertex_count;
#  SVECTOR* vertices;
#  SVECTOR* normals;
#  SVECTOR* colors;
#};
with open(OUTPUT_FILE_NAME, "wb") as f:
    vertex_data = [extract_vertex_data_tuple(l) for f in bm.faces for l in f.loops]
    vertex_count = len(vertex_data);
    write_u16(f, vertex_count);
    
    for position, _, _ in vertex_data:
        position *= PSX_SRG_UNIT_SCALE_FACTOR;
        print("position", position);
        write_SVECTOR(f, position.x, position.y, position.z);
        
    for _, normal, _ in vertex_data:
        normal *= PSX_GTE_UNIT_SCALE_FACTOR;
        print("norm", normal);
        write_SVECTOR(f, normal.x, normal.y, normal.z);
        
print("wrote", OUTPUT_FILE_NAME);
print("vtx count", vertex_count);
bm.free();