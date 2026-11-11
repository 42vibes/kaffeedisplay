# Web assets
#
# `tools/embed_web.py` writes `generated/web_assets_generated.inc` (gitignored).
# The stable `src/web_assets.cpp` includes it when present and defines an empty
# table otherwise, so a clean clone always builds. Regenerating assets later
# only requires recompiling that translation unit; no CMake reconfigure and no
# root-owned stub file.

idf_component_register(
    SRCS "src/web_assets.cpp"
    INCLUDE_DIRS "include"
)
