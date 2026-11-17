# Import libraries for Mss32.dll (Miles) and binkw32.dll (Bink), the proprietary DLLs that
# ship with Renegade. Their SDK import libraries aren't redistributable, so they are made
# from Port/importlibs/*.def (export names read from the game's own DLLs).
#
# The DLLs export decorated stdcall names ("_AIL_startup@0") verbatim. MSVC's lib.exe can't
# express that in a .def file; llvm-dlltool can ("AIL_startup@0 == _AIL_startup@0").
# It is run at configure time with execute_process (no shell, so paths with '&' are fine).

find_program(RENEGADE_DLLTOOL llvm-dlltool
	HINTS "F:/RealRTCW VR project/sof2-vr-proxy/llvm-mingw-20261006-msvcrt-i686/bin"
	DOC "llvm-dlltool (from any LLVM / llvm-mingw install)")
if(NOT RENEGADE_DLLTOOL)
	message(FATAL_ERROR "llvm-dlltool not found: install LLVM or set RENEGADE_DLLTOOL to its path.")
endif()

set(RENEGADE_IMPORTLIB_DIR ${CMAKE_BINARY_DIR}/importlibs)
file(MAKE_DIRECTORY ${RENEGADE_IMPORTLIB_DIR})

foreach(name mss32 binkw32)
	set(def ${PORT_DIR}/importlibs/${name}.def)
	set(out ${RENEGADE_IMPORTLIB_DIR}/${name}.lib)
	if(NOT EXISTS ${out} OR ${def} IS_NEWER_THAN ${out})
		execute_process(COMMAND ${RENEGADE_DLLTOOL} -m i386 -d ${def} -l ${out}
			RESULT_VARIABLE result)
		if(NOT result EQUAL 0)
			message(FATAL_ERROR "llvm-dlltool failed for ${name}.def")
		endif()
	endif()
endforeach()

target_link_libraries(Renegade PRIVATE ${RENEGADE_IMPORTLIB_DIR}/mss32.lib ${RENEGADE_IMPORTLIB_DIR}/binkw32.lib)
