if(NOT "eos-vm-oc" IN_LIST EOSIO_WASM_RUNTIMES)
   return()
endif()

set(LLVM_TARGETS_TO_BUILD "host")
set(LLVM_INCLUDE_BENCHMARKS Off)
set(LLVM_INCLUDE_EXAMPLES Off)
set(LLVM_INCLUDE_TESTS Off)
set(LLVM_INCLUDE_TOOLS Off)
set(LLVM_INCLUDE_UTILS Off)
set(LLVM_INCLUDE_RUNTIMES Off)
set(LLVM_ENABLE_TERMINFO Off)
set(LLVM_INCLUDE_GO_TESTS Off)
set(LLVM_ENABLE_PIC Off)
set(LLVM_ENABLE_RTTI On)
set(LLVM_ENABLE_ZSTD Off)
set(LLVM_ENABLE_CURL Off)
set(LLVM_ENABLE_HTTPLIB Off)
set(LLVM_ENABLE_LIBEDIT Off)
set(LLVM_ENABLE_ICU Off)
set(LLVM_ENABLE_ICONV Off)
set(LLVM_ENABLE_LIBXML2 Off)
add_subdirectory(llvm/llvm EXCLUDE_FROM_ALL)

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