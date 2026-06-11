# LNK1104: Native Unit Test `#pragma comment(lib, ...)` Path Mismatch

## Problem

Building a Microsoft Native Unit Test Framework (CppUnitTest.h) test project failed with:

```
LINK : fatal error LNK1104: cannot open file 'x64\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib'
```

Despite `/LIBPATH` correctly pointing to `...\UnitTest\lib\x64\` and the library physically existing at that location.

## Root Cause

The header `CppUnitTestCommon.h` (installed with VS 2022) contains architecture-specific `#pragma comment(lib, ...)` directives (lines 63-73):

```cpp
#if ((defined _M_AMD64) || (defined _AMD64_))
#pragma comment(lib, "x64\\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")
#elif ((defined _M_IX86) || (defined _X86_))
#pragma comment(lib, "x86\\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")
#elif ((defined _M_ARM) || (defined _ARM_))
#pragma comment(lib, "arm\\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")
#elif ((defined _M_ARM64) || (defined _ARM64_))
#pragma comment(lib, "arm64\\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib")
#endif
```

The pragma passes a **relative path** (`x64\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib`) to the linker. The linker resolves this relative path by appending it to **each `/LIBPATH` entry**. So when `/LIBPATH` is set to the platform subdirectory (`...\UnitTest\lib\x64`), the linker searches for:

```
...\UnitTest\lib\x64 \ x64\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib
                       ^^ double x64 — NOT FOUND
```

The pragma **assumes** `/LIBPATH` points to the **parent** directory (`...\UnitTest\lib\`), not the platform subdirectory.

## Solution

Set `/LIBPATH` to **both** the parent directory (for the pragma) and the platform subdirectory (for explicit references in `AdditionalDependencies`):

```xml
<AdditionalLibraryDirectories>
  $(VCInstallDir)Auxiliary\VS\UnitTest\lib;
  $(VCInstallDir)Auxiliary\VS\UnitTest\lib\$(PlatformTarget);
  %(AdditionalLibraryDirectories)
</AdditionalLibraryDirectories>
```

This resolves:
- Pragma `"x64\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib"` → `...\UnitTest\lib\x64\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib`
- Explicit `Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib` → `...\UnitTest\lib\x64\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib`

No hardcoded paths. Works for all architectures (x64, Win32 → x86, ARM, ARM64).

## Lesson

`#pragma comment(lib, ...)` with a relative path + directory prefix is fragile — it assumes a specific `/LIBPATH` layout. Always verify what path the pragma actually generates and match the `/LIBPATH` to the **parent** of that path, not the leaf.

## Relevant Files
- `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\VS\UnitTest\include\CppUnitTestCommon.h` (lines 63-73)
- `Logger_Adapter_Tests\Logger_Adapter_Tests.vcxproj` (4x `<AdditionalLibraryDirectories>` in `<Link>` sections)
- `C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\VS\UnitTest\lib\x64\Microsoft.VisualStudio.TestTools.CppUnitTestFramework.lib`
