# Web assets
#
# Build products from `tools/embed_web.py` land in `generated/` and are ignored
# by git. If a developer runs `idf.py build` directly on a clean clone, a stub
# copy of the empty asset table is placed at the same path so the component
# always compiles the same source file. A later `make assets` overwrites that
# file and the next build picks it up without a full reconfigure.

set(web_assets_source "${CMAKE_CURRENT_LIST_DIR}/generated/web_assets.cpp")

if(NOT EXISTS "${web_assets_source}")
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/generated")
    configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/src/web_assets_stub.cpp"
        "${web_assets_source}"
        COPYONLY)
    message(WARNING
        "webassets: generated assets missing, using the empty stub. "
        "Run 'python3 tools/embed_web.py' (or 'make assets') before building.")
endif()

idf_component_register(
    SRCS "generated/web_assets.cpp"
    INCLUDE_DIRS "include"
)
