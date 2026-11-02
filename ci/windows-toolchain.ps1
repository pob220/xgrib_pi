# Select the same VS 2022 instance for dependency compilation and CMake.
# A runner can also contain VS 2026; vcpkg otherwise prefers that newer compiler.
function Get-XgribVisualStudio2022 {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $instances = @(& $vswhere -latest -products Microsoft.VisualStudio.Product.BuildTools `
        -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -format json | ConvertFrom-Json)
    if ($LASTEXITCODE -ne 0) { throw 'Visual Studio discovery failed' }
    if ($instances.Count -eq 0) {
        $instances = @(& $vswhere -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json)
    }
    if ($LASTEXITCODE -ne 0 -or $instances.Count -ne 1) {
        throw 'Visual Studio 2022 with the C++ toolchain is required'
    }
    return $instances[0]
}
