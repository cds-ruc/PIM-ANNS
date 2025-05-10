cmake_minimum_required(VERSION 3.24.0 FATAL_ERROR)

project(faiss_upmem
  VERSION 0.0.1
  DESCRIPTION "upmem support for faiss"
  HOMEPAGE_URL "")




set(HOST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/host)
set(DPU_DIR ${CMAKE_CURRENT_SOURCE_DIR}/dpu)
set(COMMON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/common)

set(SDK_INSTALL_DIR ${CMAKE_SOURCE_DIR}/third-party/upmem-2024.2.0-Linux-x86_64)
set(SDK_SRC_DIR ${CMAKE_SOURCE_DIR}/third-party/upmem-2024.2.0-Linux-x86_64/src)



set(FIFO_LINK_OPTION1
    -Wl,--whole-archive ${SDK_INSTALL_DIR}/lib/libdpu.so -Wl,--no-whole-archive
)
set(FIFO_LINK_OPTION2
    -Wl,--whole-archive ${SDK_INSTALL_DIR}/lib/libdpuhw.so -Wl,--no-whole-archive
)



set(HOST_SOURCES
    ${HOST_DIR}/host_fifo.cpp
    ${HOST_DIR}/util.cpp
)

set(DPU_SOURCES
    ${DPU_DIR}/dpu_kernel.c
)

set(BATCH_DPU_SOURCES
    ${DPU_DIR}/batch_dpu_kernel.c
)

set(DPU_UTIL_SOURCES
    ${DPU_DIR}/cyclecounter.h
    
)

set(COMMON_SOURCES
    ${COMMON_DIR}/common.h
)

add_compile_options(-fPIC)

add_compile_options(-fopenmp)
add_link_options(-fopenmp)


set(DPU_FLAGS -DNR_TASKLETS=16)

set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -ggdb3 -O3")



add_custom_target(batch_dpu_adc_code
    COMMAND dpu-upmem-dpurte-clang -Ofast ${DPU_FLAGS} -o ${CMAKE_SOURCE_DIR}/build/batch_dpu_adc_code  ${BATCH_DPU_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES  ${COMMON_SOURCES} ${DPU_UTIL_SOURCES}
)

add_custom_target(dpu_adc_code
    COMMAND dpu-upmem-dpurte-clang -Ofast ${DPU_FLAGS} -o ${CMAKE_SOURCE_DIR}/build/dpu_adc_code  ${DPU_SOURCES}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    SOURCES  ${COMMON_SOURCES} ${DPU_UTIL_SOURCES}
)


add_definitions(-DPROJECT_SOURCE_DIR=\"${CMAKE_SOURCE_DIR}\")

add_subdirectory(third-party/faiss_upmem)

add_subdirectory(third-party/pybind11)


add_library(dpu_lib STATIC ${HOST_SOURCES})


target_link_libraries(dpu_lib PUBLIC ${FIFO_LINK_OPTION1} ${FIFO_LINK_OPTION2})

target_link_libraries(dpu_lib PUBLIC ${CMAKE_SOURCE_DIR}/build/third-party/faiss_upmem/faiss/libfaiss.a -lblas -llapack)

target_link_libraries(dpu_lib PUBLIC boost_coroutine profiler)

target_include_directories(dpu_lib PUBLIC ${HOST_DIR})
target_include_directories(dpu_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(dpu_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third-party/faiss_upmem/ )
target_include_directories(dpu_lib PUBLIC ${SDK_INSTALL_DIR}/include/dpu)
target_include_directories(dpu_lib PUBLIC ${SDK_INSTALL_DIR}/)
target_include_directories(dpu_lib PUBLIC ${SDK_SRC_DIR}/)



target_sources(dpu_lib PUBLIC ${COMMON_SOURCES})

add_dependencies(dpu_lib dpu_adc_code)
add_dependencies(dpu_lib batch_dpu_adc_code)


add_executable(main EXCLUDE_FROM_ALL main.cpp)
target_link_libraries(main PUBLIC dpu_lib)




add_executable(train_sift1B EXCLUDE_FROM_ALL train_sift1B.cpp)
target_link_libraries(train_sift1B PUBLIC dpu_lib)

add_executable(test EXCLUDE_FROM_ALL test.cpp)
target_link_libraries(test PUBLIC dpu_lib)


add_executable(test_zipf EXCLUDE_FROM_ALL test_zipf.cpp)
target_link_libraries(test_zipf PUBLIC dpu_lib)

add_executable(generate_query EXCLUDE_FROM_ALL generate_query.cpp)
target_link_libraries(generate_query PUBLIC dpu_lib)

add_executable(generate_sift1m_from1b EXCLUDE_FROM_ALL generate_sift1m_from1b.cpp)
target_link_libraries(generate_sift1m_from1b PUBLIC dpu_lib)

add_executable(generate_space1m_from1b EXCLUDE_FROM_ALL generate_space1m_from1b.cpp)
target_link_libraries(generate_space1m_from1b PUBLIC dpu_lib)


# pybind11_add_module(cmake_example pybind_main.cpp)

# target_link_libraries(cmake_example PUBLIC dpu_lib)
