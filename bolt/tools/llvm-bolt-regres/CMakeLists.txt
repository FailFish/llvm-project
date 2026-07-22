set(LLVM_LINK_COMPONENTS
  ${BOLT_TARGETS_TO_BUILD}
  DebugInfoDWARF
  MC
  Object
  Support
  TargetParser
  )

add_bolt_tool(llvm-bolt-regres
  llvm-bolt-regres.cpp
  RegisterWebExtractor.cpp
  RegReallocEngine.cpp
  RegReallocPasses.cpp
  SpareRegisters.cpp
  DISABLE_LLVM_LINK_LLVM_DYLIB
  )

target_include_directories(llvm-bolt-regres
  PRIVATE
  ${LLVM_MAIN_SRC_DIR}/lib/Target/X86
  ${LLVM_BINARY_DIR}/lib/Target/X86
  )

target_link_libraries(llvm-bolt-regres
  PRIVATE
  LLVMBOLTCore
  LLVMBOLTPasses
  LLVMBOLTRewrite
  LLVMBOLTUtils
  )

add_dependencies(bolt llvm-bolt-regres)
