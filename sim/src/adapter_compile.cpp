// Compile-only check that the firmware display adapter stays host-buildable.
#include "kaffee/ui_board_adapter.hpp"

namespace sim {
void board_adapter_compile_anchor() {
    static_assert(sizeof(kaffee::ui::BoardDisplayPort) > 0, "adapter must be complete");
}
}  // namespace sim
