param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$OpenCvRoot = $env:OpenCV_DIR,
    [string]$OnnxRuntimeRoot = $env:ONNXRUNTIME_ROOT,
    # Optional. Root of an Exiv2 install (e.g. a vcpkg installed\x64-windows tree,
    # or an Exiv2 CMake package dir). Enables metadata preservation. When omitted,
    # FaceVeil still builds, but the "Preserve original metadata" option is inert.
    [string]$Exiv2Root = $env:EXIV2_ROOT,
    [string]$Generator = "Ninja",
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$RootDir = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildDir = Join-Path $RootDir "build-windows"
$DistDir = Join-Path $RootDir "dist/windows/FaceVeil"
$ExePath = Join-Path $BuildDir "FaceVeil.exe"

# ── Tool sanity ────────────────────────────────────────────────────
foreach ($tool in @("cmake")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Required tool not found on PATH: $tool"
    }
}

# ── Qt root resolution ─────────────────────────────────────────────
# Accept a wide variety of Qt install layouts (e.g. C:\Qt\6.7.0\msvc2022_64
# or a CMake package dir like .../lib/cmake/Qt6). The canonical marker is
# bin\qmake.exe under the kit root.
function Resolve-QtRootPath {
    param([string]$Path)

    if (-not $Path) {
        foreach ($candidate in @($env:QT_ROOT, $env:QTDIR, $env:Qt6_DIR)) {
            if ($candidate) { $Path = $candidate; break }
        }
    }
    if (-not $Path) { return $null }
    if (-not (Test-Path $Path)) { return $null }

    $resolved = (Resolve-Path $Path).Path

    # If the path points into lib\cmake\Qt6, climb up to the kit root.
    for ($i = 0; $i -lt 4; $i++) {
        if (Test-Path (Join-Path $resolved "bin/qmake.exe")) {
            return $resolved
        }
        $parent = Split-Path $resolved -Parent
        if (-not $parent -or $parent -eq $resolved) { break }
        $resolved = $parent
    }

    # Final check so callers get a clear error rather than a mystery miss.
    if (Test-Path (Join-Path $resolved "bin/qmake.exe")) { return $resolved }
    return $null
}

$QtRoot = Resolve-QtRootPath $QtRoot
if (-not $QtRoot) {
    throw "QtRoot was not provided or does not look like a Qt kit (no bin\qmake.exe). Pass -QtRoot C:\Qt\6.x.x\msvc2022_64 or set QT_ROOT."
}
if (-not $OpenCvRoot -or -not (Test-Path $OpenCvRoot)) {
    throw "OpenCvRoot was not provided or does not exist. Pass -OpenCvRoot C:\opencv\build or set OpenCV_DIR."
}
if (-not $OnnxRuntimeRoot -or -not (Test-Path $OnnxRuntimeRoot)) {
    throw "OnnxRuntimeRoot was not provided or does not exist. Pass -OnnxRuntimeRoot C:\onnxruntime-win-x64 or set ONNXRUNTIME_ROOT."
}

$OpenCvRoot = (Resolve-Path $OpenCvRoot).Path
$OnnxRuntimeRoot = (Resolve-Path $OnnxRuntimeRoot).Path

$PrefixPaths = @($QtRoot, $OpenCvRoot)
if ($Exiv2Root -and (Test-Path $Exiv2Root)) {
    $Exiv2Root = (Resolve-Path $Exiv2Root).Path
    $PrefixPaths += $Exiv2Root
} else {
    if ($Exiv2Root) {
        Write-Warning "Exiv2Root '$Exiv2Root' does not exist; building without metadata preservation."
    } else {
        Write-Host "Exiv2Root not set; building without metadata preservation (the option will be inert)."
    }
    $Exiv2Root = $null
}

Write-Host "Qt root:          $QtRoot"
Write-Host "OpenCV root:      $OpenCvRoot"
Write-Host "ONNX Runtime:     $OnnxRuntimeRoot"
Write-Host "Exiv2 root:       $(if ($Exiv2Root) { $Exiv2Root } else { '(not provided)' })"

# ── Configure + build ──────────────────────────────────────────────
# Note: every -D argument is wrapped in double quotes so PowerShell
# interpolates $vars. Bare `-DKEY=$var` is parsed as a parameter-style
# token and passed through literally — which previously poisoned the
# CMake cache with strings like "$BuildType" and broke the ninja build.
$PrefixPathArg = ($PrefixPaths -join ";")
cmake -S $RootDir -B $BuildDir `
    -G $Generator `
    "-DCMAKE_BUILD_TYPE=$BuildType" `
    "-DCMAKE_PREFIX_PATH=$PrefixPathArg" `
    "-DONNXRUNTIME_ROOT=$OnnxRuntimeRoot" `
    "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF"
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }

cmake --build $BuildDir --config $BuildType
if ($LASTEXITCODE -ne 0) { throw "CMake build failed (exit $LASTEXITCODE)" }

# Ninja emits directly to $BuildDir; multi-config generators use $BuildDir\$BuildType.
if (-not (Test-Path $ExePath)) {
    $ExePath = Join-Path $BuildDir "$BuildType/FaceVeil.exe"
}
if (-not (Test-Path $ExePath)) {
    throw "FaceVeil.exe was not found after build (looked in $BuildDir and $BuildDir\$BuildType)."
}

# ── Assemble dist tree ─────────────────────────────────────────────
if (Test-Path $DistDir) {
    Remove-Item $DistDir -Recurse -Force
}
New-Item -ItemType Directory -Path $DistDir | Out-Null

Copy-Item $ExePath $DistDir

$windeployqt = Join-Path $QtRoot "bin/windeployqt.exe"
if (-not (Test-Path $windeployqt)) {
    throw "windeployqt.exe was not found under $QtRoot\bin"
}
& $windeployqt --release --compiler-runtime (Join-Path $DistDir "FaceVeil.exe")
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed (exit $LASTEXITCODE)" }

# ── ONNX Runtime DLL ───────────────────────────────────────────────
$onnxCandidates = @(
    (Join-Path $OnnxRuntimeRoot "lib/onnxruntime.dll"),
    (Join-Path $OnnxRuntimeRoot "bin/onnxruntime.dll"),
    (Join-Path $OnnxRuntimeRoot "runtimes/win-x64/native/onnxruntime.dll")
)
$onnxDll = $onnxCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $onnxDll) {
    throw "onnxruntime.dll was not found. Checked:`n  $([string]::Join("`n  ", $onnxCandidates))"
}
Copy-Item $onnxDll $DistDir -Force
Write-Host "Bundled: $onnxDll"

# ── OpenCV DLLs ────────────────────────────────────────────────────
# Probe the usual layouts (kit root, parent, x64\...\bin, bin). Prefer
# opencv_world*.dll (monolithic) when present, otherwise fall back to the
# individual module DLLs. Exclude debug builds (*d.dll).
$searchRoots = @(
    $OpenCvRoot,
    (Join-Path $OpenCvRoot ".."),
    (Join-Path $OpenCvRoot "x64"),
    (Join-Path $OpenCvRoot "bin")
) | Where-Object { Test-Path $_ } | ForEach-Object { (Resolve-Path $_).Path } | Select-Object -Unique

$worldDlls = foreach ($root in $searchRoots) {
    Get-ChildItem -Path $root -Recurse -Filter "opencv_world*.dll" -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -notmatch 'd\.dll$' }
}
$worldDlls = @($worldDlls) | Select-Object -Unique -First 1
if ($worldDlls) {
    Copy-Item $worldDlls.FullName $DistDir -Force
    Write-Host "Bundled: $($worldDlls.FullName)"
} else {
    $moduleDlls = foreach ($root in $searchRoots) {
        Get-ChildItem -Path $root -Recurse -Filter "opencv_*.dll" -File -ErrorAction SilentlyContinue |
            Where-Object { $_.Name -notmatch 'd\.dll$' }
    }
    $moduleDlls = @($moduleDlls) | Select-Object -Unique
    if (-not $moduleDlls) {
        throw "OpenCV DLLs were not found under $($searchRoots -join ', '). Set -OpenCvRoot to the OpenCV build root (e.g. C:\opencv\build)."
    }
    foreach ($dll in $moduleDlls) {
        Copy-Item $dll.FullName $DistDir -Force
    }
    Write-Host "Bundled $(@($moduleDlls).Count) OpenCV module DLL(s)."
}

# ── Exiv2 + its dependency DLLs ────────────────────────────────────
# Only when an Exiv2 root was provided (metadata preservation enabled).
# vcpkg lays out exiv2.dll and its dependency DLLs (expat, brotli*, zlib1,
# libintl/intl, inih) together under installed\x64-windows\bin, so we copy
# exiv2.dll plus the known dependency DLLs found beside it.
if ($Exiv2Root) {
    $exiv2SearchRoots = @(
        $Exiv2Root,
        (Join-Path $Exiv2Root "bin"),
        (Join-Path $Exiv2Root "installed/x64-windows/bin")
    ) | Where-Object { Test-Path $_ } | ForEach-Object { (Resolve-Path $_).Path } | Select-Object -Unique

    $exiv2Dll = foreach ($root in $exiv2SearchRoots) {
        Get-ChildItem -Path $root -Recurse -Filter "exiv2.dll" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    }
    $exiv2Dll = @($exiv2Dll) | Select-Object -First 1

    if (-not $exiv2Dll) {
        Write-Warning "Exiv2Root was provided but exiv2.dll was not found under $($exiv2SearchRoots -join ', '); metadata preservation may be unavailable at runtime."
    } else {
        $exiv2BinDir = Split-Path $exiv2Dll.FullName -Parent
        Copy-Item $exiv2Dll.FullName $DistDir -Force
        Write-Host "Bundled: $($exiv2Dll.FullName)"

        # Copy dependency DLLs that sit next to exiv2.dll. Match on name so we
        # don't sweep in unrelated files from a shared bin directory.
        $depPatterns = @(
            "libexiv2*.dll", "expat*.dll", "libexpat*.dll",
            "brotli*.dll", "libbrotli*.dll",
            "zlib*.dll", "zlib1.dll",
            "intl*.dll", "libintl*.dll", "libcharset*.dll", "iconv*.dll", "libiconv*.dll",
            "inih*.dll", "libinih*.dll", "INIReader*.dll", "libINIReader*.dll"
        )
        $copiedDeps = 0
        foreach ($pattern in $depPatterns) {
            Get-ChildItem -Path $exiv2BinDir -Filter $pattern -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -notmatch 'd\.dll$' } |
                ForEach-Object {
                    if (-not (Test-Path (Join-Path $DistDir $_.Name))) {
                        Copy-Item $_.FullName $DistDir -Force
                        $copiedDeps++
                    }
                }
        }
        Write-Host "Bundled $copiedDeps Exiv2 dependency DLL(s)."
    }
}

# ── Third-party notices + license ──────────────────────────────────
Copy-Item (Join-Path $RootDir "THIRD_PARTY_NOTICES.txt") $DistDir -Force
Copy-Item (Join-Path $RootDir "LICENSE") (Join-Path $DistDir "LICENSE.txt") -Force

# Guard: SCRFD models are downloaded at runtime and must never be bundled.
if (Get-ChildItem -Path $DistDir -Recurse -Filter *.onnx -ErrorAction SilentlyContinue) {
    throw "ONNX model files found in the package; models must not be bundled."
}

Write-Host ""
Write-Host "✅ Packaged app: $DistDir"
Write-Host "   Run with:     $DistDir\FaceVeil.exe"
