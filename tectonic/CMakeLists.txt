# Tools for the Tectonic terrain module (tectonic.h): an example renderer and the checker used by validate.sh.
foreach(tool tectonic_map tectonic_check)
    add_executable(${tool} ${tool}.c)
    target_link_libraries(${tool} PRIVATE cubiomes_static m)
endforeach()
