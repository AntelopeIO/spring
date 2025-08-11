if(NOT "eos-vm-oc" IN_LIST EOSIO_WASM_RUNTIMES)
   return()
endif()

#many items are option()s which can simply be set() below, but others are set(CACHE) so we need to follow suit for these
set(LLVM_TARGETS_TO_BUILD "host" CACHE STRING "Semicolon-separated list of targets to build, or \"all\".")
set(LLVM_ENABLE_LIBXML2 "OFF" CACHE STRING "Use libxml2 if available. Can be ON, OFF, or FORCE_ON")

set(LLVM_INCLUDE_BENCHMARKS Off)
set(LLVM_INCLUDE_EXAMPLES Off)
set(LLVM_INCLUDE_TESTS Off)
set(LLVM_INCLUDE_TOOLS Off)
set(LLVM_INCLUDE_UTILS Off)
set(LLVM_INCLUDE_RUNTIMES Off)
set(LLVM_INCLUDE_DOCS Off)
set(LLVM_INCLUDE_GO_TESTS Off)

set(LLVM_ENABLE_TERMINFO Off)
set(LLVM_ENABLE_BINDINGS Off)
set(LLVM_ENABLE_LIBEDIT Off)
set(LLVM_ENABLE_Z3_SOLVER Off)
set(LLVM_ENABLE_LIBPFM Off)
set(LLVM_ENABLE_OCAMLDOC Off)
set(LLVM_ENABLE_BACKTRACES Off)
set(LLVM_ENABLE_CRASH_OVERRIDES Off)

#this one is a little iffy. It stops adding -fPIC to build commands, but it adds -fno-pie to EXE_LINKER. But the
# submoduled build never builds an executable so the -fno-pie is never used. It's nice to keep hands off compile
# flags as much as possible, so adding this for now
set(LLVM_ENABLE_PIC Off)

#This doesn't actually add -frtti, but rather stops -fno-rtti from being added
set(LLVM_ENABLE_RTTI On)
#similarly, stops -fno-exceptions from being added
set(LLVM_ENABLE_EH On)
#similarly, doesn't disable warnings because our project already has -Wall but stops llvm from adding its own various options
set(LLVM_ENABLE_WARNINGS Off)

#llvm's cmake uses check_c/cxx_compiler_flag() to check the availability of some flags. prepopulate some cache entries to fail these checks
set(SUPPORTS_FVISIBILITY_INLINES_HIDDEN_FLAG Off CACHE BOOL "allow -fvisibility-inlines-hidden")
set(CXX_SUPPORTS_FFUNCTION_SECTIONS Off CACHE BOOL "allow -ffunction-sections (in c++)")
set(C_SUPPORTS_FFUNCTION_SECTIONS Off CACHE BOOL "allow -ffunction-sections (in c)")
set(CXX_SUPPORTS_FDATA_SECTIONS Off CACHE BOOL "allow -fdata-sections (in c++)")
set(C_SUPPORTS_FDATA_SECTIONS Off CACHE BOOL "allow -fdata-sections (in c)")

#llvm cmake rules will enable this by default for Debug BUILD_TYPE; always force to off for now. Expose as option later
set(LLVM_ENABLE_ASSERTIONS Off)

#llvm's cmake is so chatty. useful for dev work, but let's quiet it down normally
function(add_llvm_quietly)
   message(STATUS "Configuring LLVM, this might take a moment...")
   set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
   add_subdirectory(llvm/llvm EXCLUDE_FROM_ALL)
endfunction()

add_llvm_quietly()

add_library(llvm INTERFACE)
target_link_libraries(llvm INTERFACE
   LLVMSupport
   LLVMCore
   LLVMPasses
   LLVMMCJIT
   LLVMOrcJIT
   LLVMExecutionEngine
   LLVMCodeGen
   LLVMX86CodeGen
   LLVMX86Disassembler
   LLVMX86AsmParser
)
target_include_directories(llvm SYSTEM INTERFACE llvm/llvm/include ${CMAKE_CURRENT_BINARY_DIR}/llvm/llvm/include)