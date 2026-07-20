set(LLVM_LINK_COMPONENTS
  ${BOLT_TARGETS_TO_BUILD}
  DebugInfoDWARF
  MC
  Object
  Support
  TargetParser
  )

add_bolt_tool(llvm-bolt-obj-cfg
  llvm-bolt-obj-cfg.cpp
  DISABLE_LLVM_LINK_LLVM_DYLIB
  )

target_link_libraries(llvm-bolt-obj-cfg
  PRIVATE
  LLVMBOLTCore
  LLVMBOLTPasses
  LLVMBOLTRewrite
  LLVMBOLTUtils
  )

add_dependencies(bolt llvm-bolt-obj-cfg)
