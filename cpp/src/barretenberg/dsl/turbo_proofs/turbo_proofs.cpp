
#include "turbo_proofs.hpp"
#include "barretenberg/proof_system/proving_key/serialize.hpp"
#include "barretenberg/dsl/acir_format/acir_format.hpp"
#include "barretenberg/stdlib/types/types.hpp"
#include "barretenberg/srs/reference_string/pippenger_reference_string.hpp"

using namespace plonk::stdlib::types;

namespace turbo_proofs {

uint32_t turbo_get_exact_circuit_size(uint8_t const* constraint_system_buf)
{
    auto constraint_system = from_buffer<acir_format::acir_format>(constraint_system_buf);
    auto crs_factory = std::make_unique<bonk::ReferenceStringFactory>();
    auto composer = create_circuit(constraint_system, std::move(crs_factory));

    auto num_gates = composer.get_num_gates();
    return static_cast<uint32_t>(num_gates);
}

size_t turbo_init_proving_key(uint8_t const* constraint_system_buf, uint8_t const** pk_buf)
{
    auto constraint_system = from_buffer<acir_format::acir_format>(constraint_system_buf);
    // We know that we don't actually need any CRS to create a proving key, so just feed in a nothing.
    // Hacky, but, right now it needs *something*.
    auto crs_factory = std::make_unique<ReferenceStringFactory>();
    auto composer = create_circuit(constraint_system, std::move(crs_factory));
    auto proving_key = composer.compute_proving_key();

    auto buffer = to_buffer(*proving_key);
    auto raw_buf = (uint8_t*)malloc(buffer.size());
    memcpy(raw_buf, (void*)buffer.data(), buffer.size());
    *pk_buf = raw_buf;

    return buffer.size();
}

size_t turbo_init_verification_key(void* pippenger, uint8_t const* g2x, uint8_t const* pk_buf, uint8_t const** vk_buf)
{
    std::shared_ptr<ProverReferenceString> crs;
    bonk::proving_key_data pk_data;
    read(pk_buf, pk_data);
    auto proving_key = std::make_shared<bonk::proving_key>(std::move(pk_data), crs);

    auto crs_factory = std::make_unique<PippengerReferenceStringFactory>(
        reinterpret_cast<scalar_multiplication::Pippenger*>(pippenger), g2x);
    proving_key->reference_string = crs_factory->get_prover_crs(proving_key->circuit_size);

    TurboComposer composer(proving_key, nullptr);
    auto verification_key =
        plonk::stdlib::types::Composer::compute_verification_key_base(proving_key, crs_factory->get_verifier_crs());

    // The composer_type has not yet been set. We need to set the composer_type for when we later read in and
    // construct the verification key so that we have the correct polynomial manifest
    verification_key->composer_type = ComposerType::TURBO;

    auto buffer = to_buffer(*verification_key);
    auto raw_buf = (uint8_t*)malloc(buffer.size());
    memcpy(raw_buf, (void*)buffer.data(), buffer.size());
    *vk_buf = raw_buf;

    return buffer.size();
}

size_t turbo_new_proof(void* pippenger,
                       uint8_t const* g2x,
                       uint8_t const* pk_buf,
                       uint8_t const* constraint_system_buf,
                       uint8_t const* witness_buf,
                       uint8_t** proof_data_buf)
{
    auto constraint_system = from_buffer<acir_format::acir_format>(constraint_system_buf);

    std::shared_ptr<ProverReferenceString> crs;
    bonk::proving_key_data pk_data;
    read(pk_buf, pk_data);
    auto proving_key = std::make_shared<bonk::proving_key>(std::move(pk_data), crs);

    auto witness = from_buffer<std::vector<fr>>(witness_buf);

    auto crs_factory = std::make_unique<PippengerReferenceStringFactory>(
        reinterpret_cast<scalar_multiplication::Pippenger*>(pippenger), g2x);
    proving_key->reference_string = crs_factory->get_prover_crs(proving_key->circuit_size);

    TurboComposer composer(proving_key, nullptr);
    create_circuit_with_witness(composer, constraint_system, witness);

    auto prover = composer.create_prover();
    auto heapProver = new TurboProver(std::move(prover));
    auto& proof_data = heapProver->construct_proof().proof_data;
    *proof_data_buf = proof_data.data();

    return proof_data.size();
}

bool turbo_verify_proof(
    uint8_t const* g2x, uint8_t const* vk_buf, uint8_t const* constraint_system_buf, uint8_t* proof, uint32_t length)
{
    bool verified = false;

#ifndef __wasm__
    try {
#endif
        auto constraint_system = from_buffer<acir_format::acir_format>(constraint_system_buf);

        auto crs = std::make_shared<VerifierMemReferenceString>(g2x);
        bonk::verification_key_data vk_data;
        read(vk_buf, vk_data);
        auto verification_key = std::make_shared<bonk::verification_key>(std::move(vk_data), crs);

        TurboComposer composer(nullptr, verification_key);
        create_circuit(composer, constraint_system);
        plonk::proof pp = { std::vector<uint8_t>(proof, proof + length) };

        auto verifier = composer.create_verifier();

        verified = verifier.verify_proof(pp);
#ifndef __wasm__
    } catch (const std::exception& e) {
        verified = false;
        info(e.what());
    }
#endif
    return verified;
}

} // namespace turbo_proofs
