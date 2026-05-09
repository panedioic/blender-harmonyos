# build_files/cmake/platform/platform_ohos.cmake
# Blender 4.5 OHOS (HarmonyOS) 
# 注意：你需要修改这份文件中的绝对路径为你的实际路径

message(STATUS "============================================")
message(STATUS "  Blender for OHOS (${OHOS_ARCH})")
message(STATUS "  SYSROOT: ${CMAKE_SYSROOT}")
message(STATUS "============================================")

# ---- 基础编译参数 ----
# add_definitions(-D__OHOS__ -D_GNU_SOURCE)
# add_definitions(-D__OHOS__)
add_definitions(-D__OHOS__=1)

# ---- 强行屏蔽 Clang 对无用参数的无脑刷屏警告 ----
add_compile_options(-Wno-unused-command-line-argument -Wno-ignored-attributes -Wunused-parameter)
add_link_options(-Wno-unused-command-line-argument -Wno-ignored-attributes -Wunused-parameter)

# OHOS musl 没有 execinfo.h / backtrace，后面 patch 会用到
add_definitions(-DHAVE_NO_BACKTRACE)

# ---- lycium 三方库根 ----
if(NOT DEFINED LYCIUM_USR)
    set(LYCIUM_USR "/home/suwan/tpc_c_cplusplus/lycium/usr"
        CACHE PATH "lycium built libs root")
endif()

set(_OHOS_LIB_BASE "${LYCIUM_USR}")

# ---- 手动 include / link lycium 三方库 ----
macro(ohos_add_lib name)
    set(_inc "${_OHOS_LIB_BASE}/${name}/${OHOS_ARCH}/include")
    set(_lib "${_OHOS_LIB_BASE}/${name}/${OHOS_ARCH}/lib")
    if(EXISTS "${_inc}")
        include_directories(SYSTEM "${_inc}")
        message(STATUS "  [OHOS-LIB] +I ${_inc}")
    endif()
    if(EXISTS "${_lib}")
        link_directories("${_lib}")
        message(STATUS "  [OHOS-LIB] +L ${_lib}")
    endif()
endmacro()

# 按你已移植的库列表挨个加入
foreach(_l zlib libpng libjpeg-turbo libtiff libwebp xz zstd_1_5_6 libdeflate
           jbigkit fmt Imath openexr oneTBB boost openimageio libepoxy brotil freetype2)
    ohos_add_lib(${_l})
endforeach()
# ⚠️ Freetype 特有的恶心要求：它的头文件必须把 freetype2 这一层也 include 进去
include_directories(SYSTEM "${_OHOS_LIB_BASE}/freetype2/${OHOS_ARCH}/include/freetype2")
include_directories(SYSTEM "/home/suwan/blender-git/ohos-shaderc/libshaderc/include")

# ---- Brotli 显式配置（确保 FreeType 能找到）----
set(BROTLI_FOUND TRUE)
set(BROTLI_INCLUDE_DIRS "${_OHOS_LIB_BASE}/brotli/${OHOS_ARCH}/include")
set(BROTLIDEC_LIBRARY "${_OHOS_LIB_BASE}/brotli/${OHOS_ARCH}/lib/libbrotlidec.a")
set(BROTLICOMMON_LIBRARY "${_OHOS_LIB_BASE}/brotli/${OHOS_ARCH}/lib/libbrotlicommon.a")
set(BROTLI_LIBRARIES ${BROTLIDEC_LIBRARY} ${BROTLICOMMON_LIBRARY})
message(STATUS "  [OHOS-BROTLI] INCLUDE: ${BROTLI_INCLUDE_DIRS}")
message(STATUS "  [OHOS-BROTLI] LIBRARIES: ${BROTLI_LIBRARIES}")

# ---- FreeType 显式配置（启用 Brotli 支持）----
set(FREETYPE_FOUND TRUE)
set(FREETYPE_INCLUDE_DIRS 
    "${_OHOS_LIB_BASE}/freetype2/${OHOS_ARCH}/include"
    "${_OHOS_LIB_BASE}/freetype2/${OHOS_ARCH}/include/freetype2")
set(FREETYPE_LIBRARY "${_OHOS_LIB_BASE}/freetype2/${OHOS_ARCH}/lib/libfreetype.a")
set(FREETYPE_LIBRARIES ${FREETYPE_LIBRARY} ${BROTLI_LIBRARIES})
message(STATUS "  [OHOS-FREETYPE] INCLUDE: ${FREETYPE_INCLUDE_DIRS}")
message(STATUS "  [OHOS-FREETYPE] LIBRARY: ${FREETYPE_LIBRARY}")
message(STATUS "  [OHOS-FREETYPE] WITH BROTLI: YES")

# ---- Python (lycium 静态库) ----
if(WITH_PYTHON)
    set(_PY_VER "3.11")
    set(_PY_ROOT "${_OHOS_LIB_BASE}/cpython_3.11.11/${OHOS_ARCH}")

    set(PYTHON_VERSION        ${_PY_VER})
    set(PYTHON_VERSION_MAJOR  3)
    set(PYTHON_VERSION_MINOR  11)

    set(PYTHON_INCLUDE_DIR    "${_PY_ROOT}/include/python${_PY_VER}")
    set(PYTHON_INCLUDE_DIRS   "${PYTHON_INCLUDE_DIR}")
    set(PYTHON_INCLUDE_CONFIG_DIR "${PYTHON_INCLUDE_DIR}")

    set(PYTHON_LIBRARY        "${_PY_ROOT}/lib/libpython${_PY_VER}.a")
    set(PYTHON_LIBRARIES      "${PYTHON_LIBRARY}")
    set(PYTHON_LIBPATH        "${_PY_ROOT}/lib")
    set(PYTHON_LINKFLAGS      "")
    set(PYTHON_ROOT_DIR       "${_PY_ROOT}")

    set(WITH_PYTHON_INSTALL        OFF CACHE BOOL "" FORCE)
    set(WITH_PYTHON_INSTALL_NUMPY  OFF CACHE BOOL "" FORCE)
    set(WITH_PYTHON_INSTALL_ZSTANDARD OFF CACHE BOOL "" FORCE)
    set(WITH_PYTHON_MODULE         OFF CACHE BOOL "" FORCE)
    set(WITH_PYTHON_SAFETY         OFF CACHE BOOL "" FORCE)
    set(WITH_PYTHON_NUMPY          OFF CACHE BOOL "" FORCE)

    include_directories(SYSTEM "${PYTHON_INCLUDE_DIR}")
    link_directories("${PYTHON_LIBPATH}")

    message(STATUS "  [OHOS-PYTHON] version   : ${_PY_VER}")
    message(STATUS "  [OHOS-PYTHON] include   : ${PYTHON_INCLUDE_DIR}")
    message(STATUS "  [OHOS-PYTHON] library   : ${PYTHON_LIBRARY} (STATIC)")
endif()

# ---- libepoxy----
if(WITH_SYSTEM_EPOXY)
    set(Epoxy_FOUND TRUE)
    # 变量已由命令行 -D 注入，这里只做兜底
    if(NOT Epoxy_LIBRARY)
        set(Epoxy_LIBRARY "${_OHOS_LIB_BASE}/libepoxy/${OHOS_ARCH}/lib/libepoxy.so")
    endif()
    if(NOT Epoxy_INCLUDE_DIR)
        set(Epoxy_INCLUDE_DIR "${_OHOS_LIB_BASE}/libepoxy/${OHOS_ARCH}/include")
    endif()
    set(Epoxy_LIBRARIES ${Epoxy_LIBRARY})
    set(Epoxy_INCLUDE_DIRS ${Epoxy_INCLUDE_DIR})
    message(STATUS "  [OHOS-EPOXY] dynamic: ${Epoxy_LIBRARY}")
endif()

# ---- 禁用在鸿蒙上无意义/缺失的功能 ----
set(WITH_X11            OFF CACHE BOOL "" FORCE)
set(WITH_GHOST_X11      OFF CACHE BOOL "" FORCE)
set(WITH_GHOST_WAYLAND  OFF CACHE BOOL "" FORCE)
set(WITH_XINPUT         OFF CACHE BOOL "" FORCE)
set(WITH_XF86VMODE      OFF CACHE BOOL "" FORCE)
set(WITH_XFIXES         OFF CACHE BOOL "" FORCE)
set(WITH_XRENDER        OFF CACHE BOOL "" FORCE)
set(WITH_GHOST_XDND     OFF CACHE BOOL "" FORCE)
set(WITH_INPUT_IME      OFF CACHE BOOL "" FORCE)
set(WITH_INPUT_NDOF     OFF CACHE BOOL "" FORCE)
set(WITH_DOC_MANPAGE    OFF CACHE BOOL "" FORCE)
set(WITH_INSTALL_PORTABLE OFF CACHE BOOL "" FORCE)

# ---- musl 无 pthread_setname_np 的第二个参数签名问题 ----
add_definitions(-DWITH_OHOS_PTHREAD_STUB)

# ---- 链接参数 ----
#  -Wl,--allow-shlib-undefined  ：EGL/GLES 符号等到鸿蒙运行期才解析
#  -Wl,--unresolved-symbols=ignore-in-shared-libs  ：更宽容
set(CMAKE_SHARED_LINKER_FLAGS
    "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--allow-shlib-undefined")

message(STATUS "  OHOS platform config loaded.")

# ---- 强行全局注入基础库，兜底交叉编译下 find_package 失效的问题 ----
link_libraries(
    z 
    png 
    freetype
    "${_OHOS_LIB_BASE}/brotli/${OHOS_ARCH}/lib/libbrotlidec.a"
    "${_OHOS_LIB_BASE}/brotli/${OHOS_ARCH}/lib/libbrotlicommon.a"
    "${_OHOS_LIB_BASE}/bzip2_1_0_8/${OHOS_ARCH}/lib/libbz2.a"
    zstd
    jpeg
    OpenImageIO
    OpenImageIO_Util
    OpenEXR
    Imath
    Iex
    IlmThread
    lzma
    deflate
    webp
    tiff
    libhilog_ndk.z.so
    "/home/suwan/blender-git/ohos-shaderc/build_ohos_aarch64/libshaderc/libshaderc_combined.a"
    vulkan
    "${_OHOS_LIB_BASE}/cpython_3.11.11/${OHOS_ARCH}/lib/libpython3.11.a"
    dl
    m
    util
    "${_OHOS_LIB_BASE}/gettext/${OHOS_ARCH}/lib/libintl.a"
)
# ---- Musl / OHOS 核心类型兼容性降级映射 ----
add_definitions(
    -D_LARGEFILE64_SOURCE
    -Doff64_t=off_t
    -Dlseek64=lseek
    -Dfseeko64=fseeko
    -Dftello64=ftello
    -Dstat64=stat
    -Dfstat64=fstat
)