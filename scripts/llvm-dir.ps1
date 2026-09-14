# Dot-sourced by the scripts that run LLVM tools (`. (Join-Path $PSScriptRoot "llvm-dir.ps1")`):
# defines Get-LlvmDir, which resolves the LLVM bin directory (clang-cl, lld-link, clang-format,
# clang-tidy, llvm-cov, llvm-profdata, llvm-readobj) in the same order as cmake/find-llvm.cmake:
# an explicit directory, then $env:PVDKIT_LLVM_DIR (the CI build, lint and coverage jobs set it),
# then the VS 2022 layouts this project is known to build with - the reference machine's Build
# Tools first, so nothing changes there - then the LLVM of any Visual Studio major and edition
# under either Program Files root (`Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin`, the first
# in path order; the same glob .github/actions/toolchain uses, which is how the VS 2026 runner
# image is found), then clang-cl on PATH. The Program Files roots come from the ProgramFiles and
# ProgramFiles(x86) environment variables, so a probe can point them at a fake tree. An explicit
# directory or an environment variable that names a directory without clang-cl.exe is an error,
# never a fallback.
function Get-LlvmDir {
  [CmdletBinding()]
  [OutputType([string])]
  param(
    # A directory named on the command line; an empty value means "resolve".
    [string]$Requested
  )

  if ($Requested) {
    if (-not (Test-Path -LiteralPath (Join-Path $Requested "clang-cl.exe") -PathType Leaf)) {
      throw "LLVM directory $Requested does not contain clang-cl.exe"
    }
    return [IO.Path]::GetFullPath($Requested)
  }
  if ($env:PVDKIT_LLVM_DIR) {
    if (-not (Test-Path -LiteralPath (Join-Path $env:PVDKIT_LLVM_DIR "clang-cl.exe") -PathType Leaf)) {
      throw "PVDKIT_LLVM_DIR=$env:PVDKIT_LLVM_DIR does not contain clang-cl.exe"
    }
    return [IO.Path]::GetFullPath($env:PVDKIT_LLVM_DIR)
  }
  $programFiles86 = [Environment]::GetEnvironmentVariable("ProgramFiles(x86)")
  $programFiles = [Environment]::GetEnvironmentVariable("ProgramFiles")
  $candidates = @(
    "$programFiles86\Microsoft Visual Studio\2022\BuildTools",
    "$programFiles\Microsoft Visual Studio\2022\Enterprise",
    "$programFiles\Microsoft Visual Studio\2022\Professional",
    "$programFiles\Microsoft Visual Studio\2022\Community",
    "$programFiles\Microsoft Visual Studio\2022\BuildTools"
  ) | ForEach-Object { Join-Path $_ "VC\Tools\Llvm\x64\bin" }
  foreach ($candidate in $candidates) {
    if (Test-Path -LiteralPath (Join-Path $candidate "clang-cl.exe") -PathType Leaf) {
      return [IO.Path]::GetFullPath($candidate)
    }
  }
  $anyVisualStudio = @(
    @($programFiles, $programFiles86) |
      Where-Object { $_ } |
      ForEach-Object { Get-ChildItem -Path (Join-Path $_ "Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-cl.exe") -ErrorAction SilentlyContinue } |
      Sort-Object -Property FullName
  )
  if ($anyVisualStudio.Count -gt 0) {
    return $anyVisualStudio[0].DirectoryName
  }
  $onPath = Get-Command "clang-cl.exe" -ErrorAction SilentlyContinue
  if ($onPath) {
    return Split-Path -Parent $onPath.Source
  }
  throw "clang-cl.exe was not found: set PVDKIT_LLVM_DIR to the LLVM bin directory of a Visual Studio installation (VC\Tools\Llvm\x64\bin) or put clang-cl on PATH"
}
