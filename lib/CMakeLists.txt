file(GLOB subdirs RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "*")

foreach(d ${subdirs})
	if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${d}")
		add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/${d})
	endif()
endforeach()