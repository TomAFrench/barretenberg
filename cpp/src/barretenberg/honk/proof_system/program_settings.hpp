#pragma once

#include <cstdint>

#include "../../transcript/transcript_wrappers.hpp"
#include "../../plonk/proof_system/types/prover_settings.hpp"
#include "barretenberg/proof_system/flavor/flavor.hpp"

namespace honk {

// TODO(#221)(Luke/Cody): Shouldn't subclass plonk settings here. Also, define standard_settings for Honk prover.
class standard_verifier_settings : public plonk::standard_settings {
  public:
    typedef barretenberg::fr fr;
    typedef transcript::StandardTranscript Transcript;
    static constexpr size_t num_challenge_bytes = 16;
    static constexpr transcript::HashType hash_type = transcript::HashType::PedersenBlake3s;
    static constexpr size_t program_width = 3;
    static constexpr size_t num_polys = bonk::StandardArithmetization::NUM_POLYNOMIALS;
};

} // namespace honk
