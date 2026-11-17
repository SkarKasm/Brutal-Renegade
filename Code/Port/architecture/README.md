# Native architecture foundation (Module 1)

Status: initial headers and a compile-only CMake component. Both MSVC x86 and
AMD64 object builds succeed. The playable game still uses the existing x86
engine; these headers have not yet replaced its runtime allocator, persistence
or dependency implementations. No game, simulator or automated tests were run.

## Files and contracts

- `platform_types.h`: fixed-width scalars, native `uintptr_t`/`intptr_t`/`size_t`
  equivalents, full-address pointer hashing, separate 32-bit legacy reference
  tokens and future 64-bit persistent object IDs. Windows DWORD/LONG remain 32-bit.
- `checked_memory.h`: checked byte multiplication/addition/alignment/ranges,
  optional allocation budgets, independent GPU buffer limits and pointer-sized,
  aligned intrusive pool layouts. There is no hard-coded 2GB/4GB allocation cap.
- `wire_layout.h`: explicit little-endian unsigned/signed integer and binary32
  fields; eight-byte legacy chunk header, two-byte micro-header and four-byte
  legacy reference fields. No compiler-wide packing or raw pointer fields.
- `legacy_pointer_remap.h`: per-save synthetic token assignment and per-load
  token-to-native-address bindings with typed deferred fixups. All references are
  checked before any destination slot is modified. Unknown IDs fail resolution.
  Distinct base/interface addresses remain distinct identities. These maps are
  session-owned and require single-threaded use or external synchronization.
- `dependency_abi.h`: CPU-aware I386/AMD64 dependency compatibility and explicit
  native backend/memory capability contracts. ARM is not misidentified as AMD64.
  No OpenAL/graphics/video backend implementation is claimed here.
- `architecture.h`: public umbrella include.
- `architecture_contracts.cpp`: compile-only width/layout constraints and typed
  fixup instantiations; has no entry point or runtime initialization.

## Compile from the engine root

Use an x64 Native Tools prompt for 64-bit, x86 Native Tools for 32-bit. Keep build
directories separate. No commercial game files or x86 DLLs are required.

```text
cmake -S <Code> -B <build-x64> -G Ninja -DBR_ARCHITECTURE_ONLY=ON -DBR_EXPECT_POINTER_BITS=64
cmake --build <build-x64> --target br_architecture_contracts
```

For x86 change the toolchain, build directory and expected bits to 32. The source
folder `Port/architecture` is also a standalone CMake project. Header consumers
require C++17. The existing complete-game build remains x86-only by default.

`BR_ARCHITECTURE_ONLY` branches before RC, the engine pointer-width guard,
_USE_32BIT_TIME_T, proprietary import libraries and legacy sources. It builds an
OBJECT target, never a game executable. Successful header compilation therefore
does not establish that the full engine or its dependencies run on 64-bit.

## Integration order

1. Adopt pointer/count types in native containers, handle wrappers, allocator
   headers, string/vector pointer arithmetic and hash keys. Do not globally
   redefine int, long, DWORD, uint32, object IDs or packet fields.
2. Replace every pointer-bearing save/micro-chunk write and read as a coordinated
   change. Existing saves retain four-byte identity tokens. New writers must use
   one shared LegacyPointerWriter per save, not one registry per object.
3. Load old IDs through LegacyPointerField32LE; register each actual native base
   or interface address with its corresponding token. Bindings must outlive
   ResolveAll; queued destination slots must remain alive until resolution.
4. Require explicit schema versions before introducing persistent 64-bit IDs.
   PersistentObjectField64LE is a reserved type, not an enabled format migration.
   Existing packet formats have not changed.
5. Replace x86 assembly/intrinsics and diagnostics. Rebuild scripts/BandTest for
   x64, implement native audio and graphics adapters, and replace/isolate the
   x86 video decoder. Changing an import library's machine tag cannot convert DLLs.
6. Convert allocator/cache counts to size_t while preserving overflow guards.
   Change GPU count/index widths only when the selected backend supports them.

## Confirmed audit sites (existing engine, still pending refactor)

- wwsaveload/persistfactory.h117/133/135: pointer read uses sizeof(T*) while
  save truncates to uint32 and writes 4 bytes.
- wwsaveload/pointerremap.h85/86/95 and pointerremap.cpp97/183/204: serialized
  identities, native pointers and writable slots share pointer-shaped storage;
  ordering synthetic values as pointers must be removed.
- wwphys/pscene_saveload.cpp659/684: pointer-width save/load fields.
- wwlib/chunkio.h297/338 and callers: sizeof(var) micro-chunks can grow silently
  when var is a native pointer. Actual chunk header remains 2 x u32, micro-header 2 x u8.
- Combat/persistentgameobjobserver.cpp71/72: saved identity uses an adjusted
  observer base pointer; preserve that address domain in the remap.
- wwlib/mempool.h290/295/297: x64 header/free-list link storage and alignment.
- wwlib/registry.h118, registry.cpp74/92: HKEY narrowed into int.
- wwlib/thread.h102, wwstring.cpp105/200/201, vector.h320,
  ww3d2/surfaceclass.cpp607/681: native handles/address arithmetic narrowed.
- wwlib/hashtemplate.h59 and ww3d2/dx8renderer.h320: pointer hash folding.
- wwlib/mutex.h129, cpudetect.cpp156, mpu.cpp91, lcw.cpp221,
  wwdebug/wwprofile.cpp90, blitblit.h453, rlerle.h460: active x86 assembly.
- wwlib/Except.cpp474/590/1246/1276: x86 register and stack-walk path.
- Port/importlibs.cmake23: hard-coded i386 import-library generation. Installed
  Miles, Bink, scripts and BandTest binaries were identified as PE I386.

The scan identifies priorities; it is not an exhaustive completed pointer port.
