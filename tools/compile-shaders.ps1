$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$output = Join-Path $root "include\render\vulkan\UiShaders.h"
$temp = Join-Path ([System.IO.Path]::GetTempPath()) "kestrel-shaders"
New-Item -ItemType Directory -Force $temp | Out-Null

$lines = @("#pragma once", "", "#include <cstdint>", "", "namespace kestrel::shaders {", "")
$stages = @(
    @{ File = "ui.vert"; Name = "UiVertex" },
    @{ File = "ui.frag"; Name = "UiFragment" },
    @{ File = "world.vert"; Name = "WorldVertex" },
    @{ File = "world.frag"; Name = "WorldFragment" },
    @{ File = "world.frag"; Name = "BlendFragment"; Define = "BLEND" },
    @{ File = "model.vert"; Name = "ModelVertex" },
    @{ File = "actor.vert"; Name = "ActorVertex" },
    @{ File = "sky.vert"; Name = "SkyVertex" },
    @{ File = "sky.frag"; Name = "SkyFragment" },
    @{ File = "model.vert"; Name = "OverlayVertex"; Define = "OVERLAY" },
    @{ File = "world.frag"; Name = "OverlayFragment"; Define = "OVERLAY" }
)
foreach ($stage in $stages) {
    $spirv = Join-Path $temp ($stage.Name + ".spv")
    $defines = @()
    if ($stage.Define) {
        $defines = @("-D$($stage.Define)")
    }
    & glslc -O @defines (Join-Path $root "shaders\$($stage.File)") -o $spirv
    if ($LASTEXITCODE -ne 0) {
        exit 1
    }
    $bytes = [System.IO.File]::ReadAllBytes($spirv)
    $words = for ($i = 0; $i -lt $bytes.Length; $i += 4) { "0x{0:x8}u" -f [System.BitConverter]::ToUInt32($bytes, $i) }
    $lines += "inline constexpr uint32_t $($stage.Name)[] = {"
    for ($i = 0; $i -lt $words.Count; $i += 8) {
        $lines += "    " + (($words[$i..([Math]::Min($i + 7, $words.Count - 1))]) -join ", ") + ","
    }
    $lines += "};"
    $lines += ""
}
$lines += "}"
New-Item -ItemType Directory -Force (Split-Path $output) | Out-Null
[System.IO.File]::WriteAllLines($output, $lines)
