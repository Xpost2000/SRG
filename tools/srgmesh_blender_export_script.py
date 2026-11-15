#
# tiny little blender export script template,
# this is unfortunately not a proper blender plugin
#
# select stuff, and then export.
#

import bpy;
import bmesh;
import os;
import sys;
import subprocess;
from pathlib import Path;

#
# stupid little preamble cause of the way the blender virtual paths work...
#
blend_path = bpy.data.filepath;
blend_path = Path(blend_path).parent;
tools_path = Path(blend_path.parent) / 'tools';
toolchain_path = Path(blend_path.parent) / 'toolchain-win64';
if tools_path not in sys.path:
    sys.path.insert(0, str(tools_path));

from bin_helpers import *;

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

#
# The setup for this to work is that the materials
# need to have a material using "Diffuse BSDF" with color set to the texture.
# annoying, but eh.
#
def get_texture_image_from_material(material_name):
    material = bpy.data.materials[material_name];
    image    = None;
    for node in material.node_tree.nodes:
        if node.type == "BSDF_DIFFUSE":
            color = node.inputs["Color"];
            image = color.links[0].from_node.image;
    return image;

def get_color_count(image):
    image_pixels = image.pixels[:]
    colors = set(zip(
        image_pixels[0::4],
        image_pixels[1::4],
        image_pixels[2::4],
        image_pixels[3::4],
    ));
    return len(colors);

def get_best_bpp(count):
    if count <= 16: return 4;
    if count <= 128: return 8;
    return 16;

def invoke_image2tim(image, outfile, bpp):
    subprocess.run(
        [
            str(toolchain_path / "bin" / "img2tim.exe"),
            "-bpp", str(bpp),
            "-o", str(outfile),
            bpy.path.abspath(image.filepath),
        ],
        check=True,
    )

# following this basically
#struct Render_Mesh {
#  uint16_t vertex_count;
#  SVECTOR* vertices;
#  SVECTOR* normals;
#  SVECTOR* colors;
#};

image = get_texture_image_from_material("tex");
print(Path.cwd());
if image is not None:
    color_count = get_color_count(image);
    image_width, image_height = image.size;
    #
    # FYI the name of the final tex file is based off of
    # the name of the texture and not the source texture file.
    # since the original texture file name can (and maybe should be longer/descriptive)
    # as applicable.
    #
    invoke_image2tim(image, str(blend_path / (image.name+".tim")), get_best_bpp(color_count));

if True:
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
            
        if image is not None:
            for _, _, uv in vertex_data:
                uv.x *= image_width;
                uv.y *= image_height;
                uv.x = round(uv.x);
                uv.y = round(uv.y);
                print("uv", uv);
       #        write_SVECTOR(f, normal.x, normal.y, normal.z);
            
    print("wrote", OUTPUT_FILE_NAME);
    print("vtx count", vertex_count);
bm.free();
