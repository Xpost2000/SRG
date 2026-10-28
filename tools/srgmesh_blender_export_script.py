#
# tiny little blender export script template,
# this is unfortunately not a proper blender plugin
#
# select stuff, and then export.
#

import bpy;
import bmesh;
import os;
import sys
from bin_helpers import *; # need to check if blender import works.
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
