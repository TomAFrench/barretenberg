#pragma once
#include "barretenberg/stdlib/primitives/byte_array/byte_array.hpp"

namespace plonk {
class StandardComposer;
class TurboComposer;
class UltraComposer;
} // namespace plonk

namespace plonk {
namespace stdlib {

template <typename Composer> byte_array<Composer> blake2s(const byte_array<Composer>& input);

extern template byte_array<plonk::StandardComposer> blake2s(const byte_array<plonk::StandardComposer>& input);
extern template byte_array<plonk::TurboComposer> blake2s(const byte_array<plonk::TurboComposer>& input);
extern template byte_array<plonk::UltraComposer> blake2s(const byte_array<plonk::UltraComposer>& input);

} // namespace stdlib
} // namespace plonk
