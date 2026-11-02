
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "." REQUIRES driver)

# idf_component_register(SRCS ${app_sources} REQUIRES esp_pm)
