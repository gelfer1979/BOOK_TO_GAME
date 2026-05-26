Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Building SDL2 WebAssembly Project via Emscripten..." -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan

# Check if emcmake is available in PATH
$emcmakeCmd = Get-Command emcmake -ErrorAction SilentlyContinue

if ($null -eq $emcmakeCmd) {
    Write-Host "emcmake not found in PATH. Scanning common directories for Emscripten SDK..." -ForegroundColor Yellow

    # Common folders where EMSDK is typically installed
    $emsdkPaths = @(
        $env:EMSDK,
        "C:\emsdk",
        "D:\emsdk",
        "E:\emsdk",
        "$env:USERPROFILE\emsdk",
        "..\emsdk",
        "..\..\emsdk",
        "C:\src\emsdk",
        "D:\src\emsdk"
    )

    $activated = $false
    foreach ($path in $emsdkPaths) {
        if (-not [string]::IsNullOrEmpty($path) -and (Test-Path "$path\emsdk_env.ps1")) {
            Write-Host "Found Emscripten SDK at '$path'. Activating environment..." -ForegroundColor Green
            # Dot-source the environment setup script to load variables in the current session
            . "$path\emsdk_env.ps1"
            
            # Recheck if emcmake is now available
            $emcmakeCmd = Get-Command emcmake -ErrorAction SilentlyContinue
            if ($null -ne $emcmakeCmd) {
                $activated = $true
                break
            }
        }
    }

    if (-not $activated) {
        Write-Host "=====================================================" -ForegroundColor Red
        Write-Host "ERROR: Emscripten SDK (emsdk) not found or not active!" -ForegroundColor Red
        Write-Host "=====================================================" -ForegroundColor Red
        Write-Host "To compile the WebAssembly version, please install and activate EMSDK:" -ForegroundColor Yellow
        Write-Host "1. Clone the EMSDK repository:" -ForegroundColor Gray
        Write-Host "   git clone https://github.com/emscripten-core/emsdk.git" -ForegroundColor White
        Write-Host "2. Navigate to the emsdk directory and install/activate:" -ForegroundColor Gray
        Write-Host "   cd emsdk" -ForegroundColor White
        Write-Host "   .\emsdk install 3.1.45" -ForegroundColor White
        Write-Host "   .\emsdk activate 3.1.45" -ForegroundColor White
        Write-Host "3. Load environment variables into your current PowerShell session:" -ForegroundColor Gray
        Write-Host "   .\emsdk_env.ps1" -ForegroundColor White
        Write-Host "4. Rerun this build script!" -ForegroundColor Gray
        Write-Host "=====================================================" -ForegroundColor Red
        Exit 1
    }
}

# Run CMake configuration and build
Write-Host "1. Configuring CMake..." -ForegroundColor Green
if (Test-Path build_web/CMakeCache.txt) {
    if (!(Select-String -Path build_web/CMakeCache.txt -SimpleMatch -Pattern "CMAKE_HOME_DIRECTORY:INTERNAL=$((Get-Location).Path.Replace('\', '/'))")) {
        Write-Host "Source path changed! Cleaning build_web directory..." -ForegroundColor Yellow
        Remove-Item -Recurse -Force build_web
    }
}
emcmake cmake -B build_web -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed!" -ForegroundColor Red
    Exit $LASTEXITCODE
}

Write-Host "2. Building project..." -ForegroundColor Green
cmake --build build_web --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build execution failed!" -ForegroundColor Red
    Exit $LASTEXITCODE
}

Write-Host "`n=====================================================" -ForegroundColor Green
Write-Host "SUCCESS: WebAssembly build completed successfully!" -ForegroundColor Green
Write-Host "Build output folder: platforms/emscripten/build_web/" -ForegroundColor Green
Write-Host "=====================================================" -ForegroundColor Green
Write-Host "IMPORTANT: WebAssembly files cannot be launched directly via double-click on HTML due to CORS security policies." -ForegroundColor Yellow
Write-Host "You must launch a local web server to test the build." -ForegroundColor Yellow
Write-Host "To start a quick server, execute:" -ForegroundColor Gray
Write-Host "  python -m http.server 8000 --directory build_web" -ForegroundColor Cyan
Write-Host "Then navigate in your browser to: http://localhost:8000/BOOK_TO_GAME.html" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Green

