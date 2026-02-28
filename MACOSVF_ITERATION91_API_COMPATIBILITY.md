# macOSVF Driver - Iteration 91: libvirt API Compatibility Fixes

## Overview

This iteration fixes compilation errors in the macOSVF driver tests caused by upstream libvirt API changes. The tests were broken due to changes in internal libvirt structures and APIs.

## Date
2026-02-10

## Problem Statement

The macOSVF driver test files failed to compile due to:
1. API changes in libvirt's internal structures
2. Macro signature changes (VIR_APPEND_ELEMENT)
3. Removal/replacement of accessor functions

## Root Causes

### 1. NUMA API Changes
**Before**: Direct access to `def->numa->nCells`
**After**: Use accessor function `virDomainNumaGetNodeCount(def->numa)`

### 2. vCPU API Changes
**Before**: `def->nvcpus` field
**After**: `def->maxvcpus` field (changed from `size_t` to `unsigned int`)

### 3. Boot Menu API Changes
**Before**: Pointer to struct with `enable` and `timeout` fields
```c
def->os.bootmenu->enable
def->os.bootmenu->timeout
```
**After**: Direct fields with type change
```c
def->os.bootmenu  // virTristateBool instead of pointer
def->os.bm_timeout
def->os.bm_timeout_set
```

### 4. VIR_APPEND_ELEMENT Macro Changes
**Before**: Returned `int`, took 2 arguments
```c
if (VIR_APPEND_ELEMENT(def->disks, disk) < 0) {
    // error handling
}
```
**After**: Returns `void`, requires 3 arguments
```c
VIR_APPEND_ELEMENT(def->disks, def->ndisks, disk);
```

## Files Modified

### Test Files Fixed (4 files)

#### 1. tests/macosvflifecycletest.c
**Changes**:
- Replaced `def->numa->nCells` with `virDomainNumaGetNodeCount(def->numa)`
- Updated format specifier for size_t

**Lines changed**: ~10 lines

#### 2. tests/macosvfdomainopstest.c
**Changes**:
- Fixed `nvcpus` → `maxvcpus` references
- Fixed boot menu API usage
- Fixed NUMA accessor usage
- Added `G_GNUC_UNUSED` attributes for unused parameters
- Fixed format specifiers
- Moved variable declarations to top of functions
- Removed manual `virDomainDefFree` calls when using `g_autoptr`

**Lines changed**: ~30 lines

#### 3. tests/macosvfblockiotunetest.c
**Changes**:
- Updated `VIR_APPEND_ELEMENT` calls to use 3-argument form
- Removed return value checks (macro now returns void)
- Fixed dangling code after sed operations

**Lines changed**: ~20 lines

#### 4. tests/macosvfinterfaceparamstest.c
**Changes**:
- Updated `VIR_APPEND_ELEMENT` calls to use 3-argument form
- Fixed variable declaration ordering (C89 compatibility)
- Removed manual cleanup when using `g_autoptr`

**Lines changed**: ~25 lines

## Compilation Status

### ✅ Success
- Core driver code compiles: `src/macosvf/libvirt_driver_macosvf_impl.a`
- XML test compiles: `tests/macosvfxml2xmltest`
- Multiple unit test files now compile:
  - `tests/macosvflifecycletest`
  - `tests/macosvfdomainopstest`
  - `tests/macosvfblockiotunetest`
  - `tests/macosvfinterfaceparamstest`

### ⚠️ Remaining Issues
- `tests/macosvfstatstest.c` - Needs significant API updates (not addressed in this iteration)
- 109 XML test failures (need investigation in future iterations)

## API Changes Summary

| Old API | New API | Notes |
|---------|---------|-------|
| `def->numa->nCells` | `virDomainNumaGetNodeCount(def->numa)` | NUMA is now opaque |
| `def->nvcpus` | `def->maxvcpus` | Field renamed |
| `def->os.bootmenu->enable` | `def->os.bootmenu == VIR_TRISTATE_BOOL_YES` | Type changed from ptr to enum |
| `def->os.bootmenu->timeout` | `def->os.bm_timeout` | Field moved |
| `VIR_APPEND_ELEMENT(ptr, elem)` | `VIR_APPEND_ELEMENT(ptr, count, elem)` | Now requires count |
| `VIR_APPEND_ELEMENT(...)` returns `int` | `VIR_APPEND_ELEMENT(...)` returns `void` | No return value |

## Technical Details

### NUMA Accessor Pattern
The `virDomainNuma` structure is now opaque. All access must go through:
- `virDomainNumaGetNodeCount()` - Get number of NUMA cells
- `virDomainNumaGetNodeMemorySize()` - Get memory size for a cell
- Other `virDomainNumaGet*()` functions

### Boot Menu Type Change
The boot menu configuration changed from:
```c
typedef struct {
    virTristateBool enable;
    unsigned int timeout;
} virDomainBootMenu;
virDomainBootMenu *bootmenu;
```

To:
```c
virTristateBool bootmenu;
unsigned int bm_timeout;
bool bm_timeout_set;
```

### VIR_APPEND_ELEMENT Macro
The macro signature changed from:
```c
#define VIR_APPEND_ELEMENT(ptr, newelem)
```

To:
```c
#define VIR_APPEND_ELEMENT(ptr, count, newelem)
```

And now returns `void` instead of `int`.

## Testing

### Build Tests
```bash
# Core driver
ninja -C build src/macosvf/libvirt_driver_macosvf_impl.a
# Status: ✅ PASS

# XML test
ninja -C build tests/macosvfxml2xmltest
# Status: ✅ PASS

# Lifecycle test
ninja -C build tests/macosvflifecycletest
# Status: ✅ PASS

# Domain ops test
ninja -C build tests/macosvfdomainopstest
# Status: ✅ PASS

# Block I/O tune test
ninja -C build tests/macosvfblockiotunetest
# Status: ✅ PASS

# Interface params test
ninja -C build tests/macosvfinterfaceparamstest
# Status: ✅ PASS
```

### XML Test Results
```bash
./build/tests/macosvfxml2xmltest
# Result: 539/648 tests passed
# Failures: 109 tests (needs investigation)
```

## Benefits

1. **Compilation Success**: All core test files now compile with latest libvirt
2. **API Compatibility**: Code follows current libvirt best practices
3. **Future-Proof**: Using accessor functions instead of direct struct access
4. **Clean Code**: Proper use of g_autoptr and modern GLib patterns

## Lessons Learned

1. **Opaque Structures**: libvirt is moving toward opaque structures with accessor functions
2. **Macro Evolution**: Utility macros like VIR_APPEND_ELEMENT can change signatures
3. **Type Safety**: New virTristateBool enum provides better type safety than pointers
4. **g_autoptr Usage**: Must not manually free objects managed by g_autoptr

## Future Work

1. **Fix macosvfstatstest.c**: Requires significant API updates
2. **Investigate XML Test Failures**: 109 failing tests need analysis
3. **Update Documentation**: Ensure all docs reflect current API usage
4. **Add Deprecation Warnings**: Document APIs that may change

## Related Iterations

- **Iteration 90**: Enhanced device validation
- **Iteration 89**: Memory statistics and graphics validation
- **Iteration 88**: Filesystem support

## Summary

This iteration successfully fixed compilation errors in 4 test files by updating them to use the current libvirt APIs. The core driver code compiles successfully, and most test infrastructure is now working.

**Status**: ✅ Compilation fixes complete
**Next**: Investigate XML test failures

---

**Files Modified**: 4 test files
**Lines Changed**: ~85 lines total
**Compilation**: All core components now compile
**Test Status**: Ready for failure investigation
