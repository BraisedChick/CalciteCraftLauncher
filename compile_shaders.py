#!/usr/bin/env python3
"""
Compile GLSL shaders to SPIR-V for Android Vulkan
"""
import subprocess
import os
import sys
from pathlib import Path

def find_glslc():
    """Find glslc compiler in Android NDK"""
    android_home = os.environ.get('ANDROID_HOME') or os.environ.get('ANDROID_SDK_ROOT')
    if not android_home:
        print("Warning: ANDROID_HOME not set, trying system glslc")
        return 'glslc'
    
    # Try to find NDK's glslc
    ndk_path = Path(android_home) / 'ndk'
    if ndk_path.exists():
        for version_dir in ndk_path.iterdir():
            glslc_path = version_dir / 'shader-tools' / 'windows-x86_64' / 'glslc.exe'
            if glslc_path.exists():
                return str(glslc_path)
    
    return 'glslc'

def compile_shader(glslc, input_file, output_file, stage):
    """Compile a single shader"""
    cmd = [
        glslc,
        '-fshader-stage=' + stage,
        '-o',
        output_file,
        input_file
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        print(f"✓ Compiled: {os.path.basename(input_file)} -> {os.path.basename(output_file)}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ Error compiling {input_file}:")
        print(e.stderr)
        return False

def main():
    project_root = Path(__file__).parent
    shader_src_dir = project_root / 'src' / 'main' / 'cpp' / 'shaders'
    shader_assets_dir = project_root / 'src' / 'main' / 'assets' / 'shaders'
    
    # Create output directory
    shader_assets_dir.mkdir(parents=True, exist_ok=True)
    
    # Find glslc compiler
    glslc = find_glslc()
    print(f"Using compiler: {glslc}\n")
    
    # Define shaders to compile
    shaders = [
        ('shader.vert', 'vert', 'shader_vert.spv'),
        ('shader.frag', 'frag', 'shader_frag.spv'),
    ]
    
    success_count = 0
    total_count = len(shaders)
    
    for src_file, stage, output_file in shaders:
        input_path = shader_src_dir / src_file
        output_path = shader_assets_dir / output_file
        
        if not input_path.exists():
            print(f"⚠ Warning: {input_path} not found, skipping")
            continue
        
        if compile_shader(glslc, str(input_path), str(output_path), stage):
            success_count += 1
    
    print(f"\n{'='*50}")
    print(f"Compiled {success_count}/{total_count} shaders successfully")
    
    if success_count == total_count:
        print("✓ All shaders compiled!")
        return 0
    else:
        print("✗ Some shaders failed to compile")
        return 1

if __name__ == '__main__':
    sys.exit(main())
