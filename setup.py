import os
import sys
import urllib.request
import zipfile
import shutil
import math
import struct
import wave

# URLs for SDL2 VC Prebuilt Binaries
VC_URLS = {
    "SDL2": "https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-VC.zip",
    "SDL2_image": "https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.2/SDL2_image-devel-2.8.2-VC.zip",
    "SDL2_mixer": "https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.0/SDL2_mixer-devel-2.8.0-VC.zip",
    "SDL2_ttf": "https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.22.0/SDL2_ttf-devel-2.22.0-VC.zip",
    "SDL2_net": "https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-devel-2.2.0-VC.zip"
}

# URLs for SDL2 MinGW Prebuilt Binaries
MINGW_URLS = {
    "SDL2": "https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-devel-2.30.9-mingw.zip",
    "SDL2_image": "https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.2/SDL2_image-devel-2.8.2-mingw.zip",
    "SDL2_mixer": "https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.0/SDL2_mixer-devel-2.8.0-mingw.zip",
    "SDL2_ttf": "https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.22.0/SDL2_ttf-devel-2.22.0-mingw.zip",
    "SDL2_net": "https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-devel-2.2.0-mingw.zip"
}

# URLs for SDL2 Source Codes (for Android/iOS)
SRC_URLS = {
    "SDL2": "https://github.com/libsdl-org/SDL/releases/download/release-2.30.9/SDL2-2.30.9.zip",
    "SDL2_image": "https://github.com/libsdl-org/SDL_image/releases/download/release-2.8.2/SDL2_image-2.8.2.zip",
    "SDL2_mixer": "https://github.com/libsdl-org/SDL_mixer/releases/download/release-2.8.0/SDL2_mixer-2.8.0.zip",
    "SDL2_ttf": "https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.22.0/SDL2_ttf-2.22.0.zip",
    "SDL2_net": "https://github.com/libsdl-org/SDL_net/releases/download/release-2.2.0/SDL2_net-2.2.0.zip"
}

FONT_URL = "https://github.com/googlefonts/roboto-2/raw/main/src/hinted/Roboto-Regular.ttf"

def create_dirs():
    print("Creating project directories...")
    dirs = [
        "external/vc/include/SDL2",
        "external/vc/lib/x64",
        "external/vc/lib/x86",
        "external/vc/bin/x64",
        "external/vc/bin/x86",
        "external/mingw/include/SDL2",
        "external/mingw/lib/x64",
        "external/mingw/lib/x86",
        "external/mingw/bin/x64",
        "external/mingw/bin/x86",
        "external/src",
        "assets",
        "platforms/android/app/src/main/java/org/libsdl/app",
        "platforms/android/app/src/main/res/values",
        "platforms/emscripten"
    ]
    for d in dirs:
        os.makedirs(d, exist_ok=True)

def download_file(url, filepath):
    print(f"Downloading {url}...")
    try:
        # Standard urllib request with User-Agent to avoid blockage
        req = urllib.request.Request(
            url, 
            headers={'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)'}
        )
        with urllib.request.urlopen(req) as response, open(filepath, 'wb') as out_file:
            shutil.copyfileobj(response, out_file)
        print("Download successful.")
    except Exception as e:
        print(f"FAILED to download {url}: {e}")

def generate_wav(filepath):
    print(f"Generating synthetic chiptune sound at {filepath}...")
    sample_rate = 44100
    duration = 0.5  # seconds
    num_samples = int(sample_rate * duration)
    
    with wave.open(filepath, 'wb') as wav_file:
        wav_file.setnchannels(1)  # Mono
        wav_file.setsampwidth(2)  # 16-bit
        wav_file.setframerate(sample_rate)
        
        for i in range(num_samples):
            t = i / sample_rate
            # Play a retro chiptune rising coin synth effect
            if t < 0.15:
                freq = 523.25  # C5
            else:
                freq = 659.25  # E5
                
            # Volume envelope (fade out)
            volume = 0.5 * (1.0 - (t / duration))
            # Square wave for a nice classic 8-bit chiptune sound!
            sine_val = math.sin(2.0 * math.pi * freq * t)
            wave_val = 1.0 if sine_val >= 0 else -1.0
            
            value = int(32767.0 * volume * wave_val)
            data = struct.pack('<h', value)
            wav_file.writeframesraw(data)
    print("Sound generation successful.")

def process_vc_zips():
    print("Processing Visual C++ (MSVC) Binaries...")
    temp_dir = "external/temp_vc"
    os.makedirs(temp_dir, exist_ok=True)
    
    for name, url in VC_URLS.items():
        zip_path = os.path.join(temp_dir, f"{name}.zip")
        download_file(url, zip_path)
        
        if not os.path.exists(zip_path):
            continue
            
        print(f"Extracting {name} VC zip...")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(temp_dir)
            
        # Find extracted folder
        extracted_folder = None
        for item in os.listdir(temp_dir):
            if item.startswith(name) and os.path.isdir(os.path.join(temp_dir, item)):
                extracted_folder = os.path.join(temp_dir, item)
                break
                
        if not extracted_folder:
            print(f"Could not find extracted folder for {name}")
            continue
            
        print(f"Organizing {name} VC files...")
        
        # Copy headers
        inc_src = os.path.join(extracted_folder, "include")
        if os.path.exists(inc_src):
            for h in os.listdir(inc_src):
                src_h = os.path.join(inc_src, h)
                # Some packages have header files directly inside include, others in include/SDL2
                if os.path.isdir(src_h) and h == "SDL2":
                    for sub_h in os.listdir(src_h):
                        shutil.copy2(os.path.join(src_h, sub_h), "external/vc/include/SDL2/")
                else:
                    shutil.copy2(src_h, "external/vc/include/SDL2/")
                    
        # Copy libs and DLLs (x64)
        lib_x64_src = os.path.join(extracted_folder, "lib/x64")
        if os.path.exists(lib_x64_src):
            for f in os.listdir(lib_x64_src):
                src_f = os.path.join(lib_x64_src, f)
                if f.endswith(".lib"):
                    shutil.copy2(src_f, "external/vc/lib/x64/")
                elif f.endswith(".dll"):
                    shutil.copy2(src_f, "external/vc/bin/x64/")
                    
        # Copy libs and DLLs (x86)
        lib_x86_src = os.path.join(extracted_folder, "lib/x86")
        if os.path.exists(lib_x86_src):
            for f in os.listdir(lib_x86_src):
                src_f = os.path.join(lib_x86_src, f)
                if f.endswith(".lib"):
                    shutil.copy2(src_f, "external/vc/lib/x86/")
                elif f.endswith(".dll"):
                    shutil.copy2(src_f, "external/vc/bin/x86/")
                    
    # Clean up temp folder
    shutil.rmtree(temp_dir)
    print("MSVC Binaries Setup Complete.")

def process_mingw_zips():
    print("Processing MinGW Binaries...")
    temp_dir = "external/temp_mingw"
    os.makedirs(temp_dir, exist_ok=True)
    
    for name, url in MINGW_URLS.items():
        zip_path = os.path.join(temp_dir, f"{name}.zip")
        download_file(url, zip_path)
        
        if not os.path.exists(zip_path):
            continue
            
        print(f"Extracting {name} MinGW zip...")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(temp_dir)
            
        # Find extracted folder
        extracted_folder = None
        for item in os.listdir(temp_dir):
            if item.startswith(name) and os.path.isdir(os.path.join(temp_dir, item)):
                extracted_folder = os.path.join(temp_dir, item)
                break
                
        if not extracted_folder:
            print(f"Could not find extracted folder for {name}")
            continue
            
        print(f"Organizing {name} MinGW files...")
        
        # MinGW has x86_64-w64-mingw32 (x64) and i686-w64-mingw32 (x86) architectures
        x64_path = os.path.join(extracted_folder, "x86_64-w64-mingw32")
        x86_path = os.path.join(extracted_folder, "i686-w64-mingw32")
        
        # Copy x64 files
        if os.path.exists(x64_path):
            # Headers
            inc_src = os.path.join(x64_path, "include/SDL2")
            if os.path.exists(inc_src):
                for h in os.listdir(inc_src):
                    shutil.copy2(os.path.join(inc_src, h), "external/mingw/include/SDL2/")
            # Libs
            lib_src = os.path.join(x64_path, "lib")
            if os.path.exists(lib_src):
                for f in os.listdir(lib_src):
                    if f.endswith(".a") or f.endswith(".la"):
                        shutil.copy2(os.path.join(lib_src, f), "external/mingw/lib/x64/")
            # DLLs
            bin_src = os.path.join(x64_path, "bin")
            if os.path.exists(bin_src):
                for f in os.listdir(bin_src):
                    if f.endswith(".dll"):
                        shutil.copy2(os.path.join(bin_src, f), "external/mingw/bin/x64/")
                        
        # Copy x86 files
        if os.path.exists(x86_path):
            # Libs
            lib_src = os.path.join(x86_path, "lib")
            if os.path.exists(lib_src):
                for f in os.listdir(lib_src):
                    if f.endswith(".a") or f.endswith(".la"):
                        shutil.copy2(os.path.join(lib_src, f), "external/mingw/lib/x86/")
            # DLLs
            bin_src = os.path.join(x86_path, "bin")
            if os.path.exists(bin_src):
                for f in os.listdir(bin_src):
                    if f.endswith(".dll"):
                        shutil.copy2(os.path.join(bin_src, f), "external/mingw/bin/x86/")
                        
    # Clean up temp folder
    shutil.rmtree(temp_dir)
    print("MinGW Binaries Setup Complete.")

def process_sources():
    print("Processing Library Source Codes (for Android/iOS)...")
    temp_dir = "external/temp_src"
    os.path.join("external", "src")
    os.makedirs(temp_dir, exist_ok=True)
    
    sdl2_extracted_path = None
    
    for name, url in SRC_URLS.items():
        zip_path = os.path.join(temp_dir, f"{name}.zip")
        download_file(url, zip_path)
        
        if not os.path.exists(zip_path):
            continue
            
        print(f"Extracting {name} Source zip...")
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            zip_ref.extractall(temp_dir)
            
        # Find extracted folder
        extracted_folder = None
        for item in os.listdir(temp_dir):
            # Check if this item is a folder and matches name (like SDL2-2.30.9 or SDL2_image-2.8.2)
            if item.startswith(name) and os.path.isdir(os.path.join(temp_dir, item)):
                # Make sure it's not the prebuilt folder from earlier steps if we didn't clean correctly
                # (but they were in other temp dirs anyway, so we are safe)
                extracted_folder = os.path.join(temp_dir, item)
                break
                
        if not extracted_folder:
            print(f"Could not find extracted source folder for {name}")
            continue
            
        dest_folder = f"external/src/{name}"
        if os.path.exists(dest_folder):
            shutil.rmtree(dest_folder)
            
        print(f"Moving source {name} to {dest_folder}...")
        shutil.move(extracted_folder, dest_folder)
        
        if name == "SDL2":
            sdl2_extracted_path = dest_folder
            
    # Copy SDLActivity.java into the android project structure
    if sdl2_extracted_path:
        java_src = os.path.join(
            sdl2_extracted_path, 
            "android-project/app/src/main/java/org/libsdl/app/SDLActivity.java"
        )
        java_dest = "platforms/android/app/src/main/java/org/libsdl/app/SDLActivity.java"
        if os.path.exists(java_src):
            print(f"Copying official {java_src} -> {java_dest}...")
            shutil.copy2(java_src, java_dest)
        else:
            print("WARNING: Could not locate SDLActivity.java in the SDL2 source package.")
            
    # Clean up temp folder
    shutil.rmtree(temp_dir)
    print("Source Codes Setup Complete.")

def main():
    create_dirs()
    
    # 1. Generate Wav Asset
    generate_wav("assets/sound.wav")
    
    # 2. Download Font Asset
    download_file(FONT_URL, "assets/font.ttf")
    
    # 3. Process All Zips (Only download if not already setup to save bandwidth/time)
    # We will do a full download to guarantee correctness
    process_vc_zips()
    process_mingw_zips()
    process_sources()
    
    print("\n=======================================================")
    print("SUCCESS: Multiplatform SDL2 Template Setup is Complete!")
    print("You are ready to compile the project for any platform.")
    print("=======================================================")

if __name__ == "__main__":
    main()
