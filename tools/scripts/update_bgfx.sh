#/bin/bash

# This script copies the bgfx source code into the external folder.

rm -rf external/bx
mkdir external/bx

# Copy bx 

cp -r $1/bx/include external/bx
cp -r $1/bx/src external/bx
cp -r $1/bx/3rdparty external/bx
cp -r $1/bx/README.md external/bx
cp -r $1/bx/LICENSE external/bx

# Copy bimg

rm -rf external/bimg
mkdir external/bimg

cp -r $1/bimg/include external/bimg
cp -r $1/bimg/src external/bimg
cp -r $1/bimg/3rdparty external/bimg
cp -r $1/bimg/README.md external/bimg
cp -r $1/bimg/LICENSE external/bimg

# Copy bgfx

rm -rf external/bgfx
mkdir external/bgfx
mkdir external/bgfx/3rdparty

cp -r $1/bgfx/include external/bgfx
cp -r $1/bgfx/src external/bgfx

cp -r $1/bgfx/3rdparty/cgltf external/bgfx/3rdparty
cp -r $1/bgfx/3rdparty/directx-headers external/bgfx/3rdparty
cp -r $1/bgfx/3rdparty/fcpp external/bgfx/3rdparty
cp -r $1/bgfx/3rdparty/khronos external/bgfx/3rdparty
cp -r $1/bgfx/3rdparty/renderdoc external/bgfx/3rdparty
cp -r $1/bgfx/3rdparty/sdf external/bgfx/3rdparty
cp -r $1/bgfx/3rdparty/webgpu external/bgfx/3rdparty

cp -r $1/bgfx/README.md external/bgfx
cp -r $1/bgfx/LICENSE external/bgfx
