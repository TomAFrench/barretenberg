#pragma once

namespace honk::sumcheck {

// TODO(#226)(Adrian): Remove zeta, alpha as they are not used by the relations.
template <typename FF> struct RelationParameters {
    FF zeta;
    FF alpha;
    FF beta;
    FF gamma;
    FF public_input_delta;
};
} // namespace honk::sumcheck
