idf_component_register(
    SRCS "src/service.cpp" "src/auth.cpp"
    INCLUDE_DIRS "include"
    REQUIRES domain storage
)
