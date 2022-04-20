cmake_minimum_required(VERSION 3.15)
project(smi_modules VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Module info
add_definitions(-D__KERNEL__ -DMODULE)

# Find the kernel release
execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE KERNEL_RELEASE
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

# Find the headers
set(KERNELHEADERS_DIR /usr/src/linux-headers-${KERNEL_RELEASE})

message(STATUS "Kernel release: ${KERNEL_RELEASE}")
message(STATUS "Kernel headers: ${KERNELHEADERS_DIR}")

function(compile_module obj)
  set(TARGET_NAME ${obj})
  add_custom_target(${TARGET_NAME} ALL cp -f ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.h ${CMAKE_CURRENT_BINARY_DIR}/
  		    COMMAND echo "compiling module ${obj}.ko..."
  		    )
  list(LENGTH ARGN argn_len)
  set(i 0)
  set(depend_objlist "")
  while( i LESS ${argn_len})  
	list(GET ARGN ${i} argn_value)  
	set(depend_objlist "${depend_objlist} ${argn_value}.o") 
	math(EXPR i "${i} + 1")  
  endwhile() 
  
  add_custom_command(
			TARGET ${TARGET_NAME}
		     	POST_BUILD
		     	COMMAND echo "obj-m := ${obj}.o" > ${CMAKE_CURRENT_BINARY_DIR}/Makefile
		     	COMMAND echo "${obj}-objs:=${depend_objlist}" >>${CMAKE_CURRENT_BINARY_DIR}/Makefile
		     	COMMAND make -C ${KERNELHEADERS_DIR} M=${CMAKE_CURRENT_BINARY_DIR} modules
			COMMAND ../../../../../utils/generate_bin_blob ${obj}.ko ${obj} ../${obj}_gen.h
		    )
endfunction()

compile_module(bcm2835_smi)
compile_module(smi_stream_dev)
