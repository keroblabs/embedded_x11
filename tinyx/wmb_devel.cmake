if(NOT TARGET wmb_tinyx_project)
    add_library(wmb_tinyx_project INTERFACE)
    target_compile_definitions(wmb_tinyx_project INTERFACE WMB_DEVEL=1)
endif()
