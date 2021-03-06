if (CGAL_target_use_LASLIB_included)
  return()
endif()
set(CGAL_target_use_LASLIB_included TRUE)

function(CGAL_target_use_LASLIB target)
  target_include_directories(${target} PUBLIC ${LASLIB_INCLUDE_DIR})
  target_include_directories(${target} PUBLIC ${LASZIP_INCLUDE_DIR})
  target_compile_options( ${target} PUBLIC -DCGAL_LINKED_WITH_LASLIB)
  target_link_libraries(${target} PUBLIC ${LASLIB_LIBRARIES})
endfunction()
