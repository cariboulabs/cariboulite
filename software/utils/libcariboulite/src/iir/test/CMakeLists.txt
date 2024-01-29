cmake_minimum_required(VERSION 3.1.0)

set(CMAKE_CXX_STANDARD 11)

if (MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wconversion -Wextra -pedantic)
endif()

enable_testing()
include_directories(
  ..
  ../iir
  )

add_executable (test_butterworth butterworth.cpp)
target_link_libraries(test_butterworth iir_static)
add_test(TestButterworth test_butterworth)

add_executable (test_chebyshev1 chebyshev1.cpp)
target_link_libraries(test_chebyshev1 iir_static)
add_test(TestChebyshev1 test_chebyshev1)

add_executable (test_chebyshev2 chebyshev2.cpp)
target_link_libraries(test_chebyshev2 iir_static)
add_test(TestChebyshev2 test_chebyshev2)

add_executable (test_rbj rbj.cpp)
target_link_libraries(test_rbj iir_static)
add_test(TestRBJ test_rbj)

add_executable (test_custom custom.cpp)
target_link_libraries(test_custom iir_static)
add_test(TestCUSTOM test_custom)

add_executable (test_badparam badparam.cpp)
target_link_libraries(test_badparam iir_static)
add_test(TestBadParam test_badparam)

add_executable (test_state state.cpp)
target_link_libraries(test_state iir_static)
add_test(TestState test_state)
