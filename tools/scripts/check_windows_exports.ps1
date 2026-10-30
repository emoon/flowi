param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Dll,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$ModuleDefinition
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Dll -PathType Leaf)) {
    throw "check_windows_exports: DLL not found: $Dll"
}
if (-not (Test-Path -LiteralPath $ModuleDefinition -PathType Leaf)) {
    throw "check_windows_exports: module definition not found: $ModuleDefinition"
}

$readObj = Get-Command llvm-readobj -ErrorAction SilentlyContinue
if ($readObj) {
    $readObjPath = $readObj.Source
} else {
    $bundledReadObj = Join-Path $env:ProgramFiles "LLVM\bin\llvm-readobj.exe"
    if (Test-Path -LiteralPath $bundledReadObj -PathType Leaf) {
        $readObjPath = $bundledReadObj
    } else {
        throw "check_windows_exports: llvm-readobj was not found"
    }
}

$expected = Get-Content -LiteralPath $ModuleDefinition |
    Where-Object { $_ -match '^\s+[A-Za-z_][A-Za-z0-9_]*(?:\s|$)' } |
    ForEach-Object { ($_ -replace '^\s+([^\s]+).*$','$1') } |
    Sort-Object -Unique

$actual = & $readObjPath --coff-exports $Dll |
    Where-Object { $_ -match '^\s*Name:\s+(\S+)\s*$' } |
    ForEach-Object { ($_ -replace '^\s*Name:\s+(\S+)\s*$','$1') } |
    Sort-Object -Unique

if ($LASTEXITCODE -ne 0) {
    throw "check_windows_exports: llvm-readobj failed for $Dll"
}

$difference = Compare-Object -ReferenceObject $expected -DifferenceObject $actual
if ($difference) {
    Write-Error "check_windows_exports: flowi.dll does not match the generated export list"
    $difference | Format-Table -AutoSize | Out-String | Write-Host
    exit 1
}

Write-Host "check_windows_exports: OK - $($expected.Count) exported symbols match the curated list."
