#pragma once

namespace rex::input {
class InputSystem;
struct X_INPUT_STATE;
}  // namespace rex::input

// Reads pad 0 for host UI (ignores the "menu open" input block), under the same
// lock as the guest's XAM input calls. Returns false if no pad is connected.
bool SsxReadPadForUI(rex::input::InputSystem* input, rex::input::X_INPUT_STATE* state);
