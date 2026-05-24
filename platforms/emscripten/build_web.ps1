Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "Building SDL2 WebAssembly Project via Emscripten..." -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Cyan

# Check if Emscripten SDK is active in environment
if ($null -eq $env:EMSDK) {
    Write-Host "WARNING: EMSDK environment variable not detected!" -ForegroundColor Yellow
    Write-Host "Make sure the Emscripten SDK is installed and activated (run 'emsdk_env.ps1' or 'emsdk activate')." -ForegroundColor Yellow
    Write-Host "Attempting build anyway..." -ForegroundColor Gray
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
Write-Host "Then navigate in your browser to: http://localhost:8000/sdl2_template.html" -ForegroundColor Cyan
Write-Host "=====================================================" -ForegroundColor Green
