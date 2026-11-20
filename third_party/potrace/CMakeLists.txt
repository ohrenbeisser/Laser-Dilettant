# potrace 1.16 als statische C-Bibliothek (nur der Kern, ohne Programm und Ausgabeformate).
# Lizenz: GPL-2.0-oder-später, siehe COPYING in diesem Ordner.
add_library(potrace_core STATIC
    curve.c
    decompose.c
    potracelib.c
    trace.c
)
target_include_directories(potrace_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(potrace_core PRIVATE HAVE_CONFIG_H)
# Fremdcode: keine eigenen Warnungen anzeigen.
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(potrace_core PRIVATE -w)
elseif(MSVC)
    target_compile_options(potrace_core PRIVATE /w)
endif()
set_target_properties(potrace_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
