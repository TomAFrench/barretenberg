#include "gemini.hpp"

#include "../commitment_key.test.hpp"
#include "barretenberg/polynomials/polynomial.hpp"
#include <cstddef>
#include <gtest/gtest.h>
#include <span>

namespace honk::pcs::gemini {

template <class Params> class GeminiTest : public CommitmentTest<Params> {
    using Gemini = MultilinearReductionScheme<Params>;
    using Fr = typename Params::Fr;
    using Commitment = typename Params::Commitment;
    using Polynomial = typename barretenberg::Polynomial<Fr>;

  public:
    void execute_gemini_and_verify_claims(size_t log_n,
                                          std::vector<Fr> multilinear_evaluation_point,
                                          std::vector<Fr> multilinear_evaluations,
                                          std::vector<std::span<Fr>> multilinear_polynomials,
                                          std::vector<std::span<Fr>> multilinear_polynomials_to_be_shifted,
                                          std::vector<Commitment> multilinear_commitments,
                                          std::vector<Commitment> multilinear_commitments_to_be_shifted)
    {
        using Transcript = transcript::StandardTranscript;
        auto transcript = std::make_shared<Transcript>(StandardHonk::create_manifest(0, log_n));
        transcript->mock_inputs_prior_to_challenge("rho");
        transcript->apply_fiat_shamir("rho");
        const Fr rho = Fr::serialize_from_buffer(transcript->get_challenge("rho").begin());

        std::vector<Fr> rhos = Gemini::powers_of_rho(rho, multilinear_evaluations.size());

        // Compute batched multivariate evaluation
        Fr batched_evaluation = Fr::zero();
        for (size_t i = 0; i < multilinear_evaluations.size(); ++i) {
            batched_evaluation += multilinear_evaluations[i] * rhos[i];
        }

        Polynomial batched_unshifted(1 << log_n);
        Polynomial batched_to_be_shifted(1 << log_n);
        Commitment batched_commitment_unshifted = Commitment::zero();
        Commitment batched_commitment_to_be_shifted = Commitment::zero();
        const size_t num_unshifted = multilinear_polynomials.size();
        const size_t num_shifted = multilinear_polynomials_to_be_shifted.size();
        for (size_t i = 0; i < num_unshifted; ++i) {
            batched_unshifted.add_scaled(multilinear_polynomials[i], rhos[i]);
            batched_commitment_unshifted += multilinear_commitments[i] * rhos[i];
        }
        for (size_t i = 0; i < num_shifted; ++i) {
            size_t rho_idx = num_unshifted + i;
            batched_to_be_shifted.add_scaled(multilinear_polynomials_to_be_shifted[i], rhos[rho_idx]);
            batched_commitment_to_be_shifted += multilinear_commitments_to_be_shifted[i] * rhos[rho_idx];
        }

        // Compute:
        // - (d+1) opening pairs: {r, \hat{a}_0}, {-r^{2^i}, a_i}, i = 0, ..., d-1
        // - (d+1) Fold polynomials Fold_{r}^(0), Fold_{-r}^(0), and Fold^(i), i = 0, ..., d-1
        auto prover_output = Gemini::reduce_prove(this->ck(),
                                                  multilinear_evaluation_point,
                                                  std::move(batched_unshifted),
                                                  std::move(batched_to_be_shifted),
                                                  transcript);

        // Check that the Fold polynomials have been evaluated correctly in the prover
        this->verify_batch_opening_pair(prover_output.opening_pairs, prover_output.witnesses);

        // Construct a Gemini proof object consisting of
        // - d Fold poly evaluations a_0, ..., a_{d-1}
        // - (d-1) Fold polynomial commitments [Fold^(1)], ..., [Fold^(d-1)]
        auto gemini_proof = Gemini::reconstruct_proof_from_transcript(transcript, log_n);

        // Compute:
        // - Single opening pair: {r, \hat{a}_0}
        // - 2 partially evaluated Fold polynomial commitments [Fold_{r}^(0)] and [Fold_{-r}^(0)]
        // Aggregate: d+1 opening pairs and d+1 Fold poly commitments into verifier claim
        auto verifier_claim = Gemini::reduce_verify(multilinear_evaluation_point,
                                                    batched_evaluation,
                                                    batched_commitment_unshifted,
                                                    batched_commitment_to_be_shifted,
                                                    gemini_proof,
                                                    transcript);

        // Check equality of the opening pairs computed by prover and verifier
        for (size_t i = 0; i < (log_n + 1); ++i) {
            ASSERT_EQ(prover_output.opening_pairs[i], verifier_claim[i].opening_pair);
        }

        // Explicitly verify the claims computed by the verfier
        this->verify_batch_opening_claim(verifier_claim, prover_output.witnesses);
    }
};

TYPED_TEST_SUITE(GeminiTest, CommitmentSchemeParams);

TYPED_TEST(GeminiTest, Single)
{
    using Fr = typename TypeParam::Fr;
    using Commitment = typename TypeParam::Commitment;

    const size_t n = 16;
    const size_t log_n = 4;

    auto u = this->random_evaluation_point(log_n);
    auto poly = this->random_polynomial(n);
    auto commitment = this->commit(poly);
    auto eval = poly.evaluate_mle(u);

    // Collect multilinear polynomials evaluations, and commitments for input to prover/verifier
    std::vector<Fr> multilinear_evaluations = { eval };
    std::vector<std::span<Fr>> multilinear_polynomials = { poly };
    std::vector<std::span<Fr>> multilinear_polynomials_to_be_shifted = {};
    std::vector<Commitment> multilinear_commitments = { commitment };
    std::vector<Commitment> multilinear_commitments_to_be_shifted = {};

    this->execute_gemini_and_verify_claims(log_n,
                                           u,
                                           multilinear_evaluations,
                                           multilinear_polynomials,
                                           multilinear_polynomials_to_be_shifted,
                                           multilinear_commitments,
                                           multilinear_commitments_to_be_shifted);
}

TYPED_TEST(GeminiTest, SingleShift)
{
    using Fr = typename TypeParam::Fr;
    using Commitment = typename TypeParam::Commitment;

    const size_t n = 16;
    const size_t log_n = 4;

    auto u = this->random_evaluation_point(log_n);

    // shiftable polynomial must have 0 as last coefficient
    auto poly = this->random_polynomial(n);
    poly[0] = Fr::zero();

    auto commitment = this->commit(poly);
    auto eval_shift = poly.evaluate_mle(u, true);

    // Collect multilinear polynomials evaluations, and commitments for input to prover/verifier
    std::vector<Fr> multilinear_evaluations = { eval_shift };
    std::vector<std::span<Fr>> multilinear_polynomials = {};
    std::vector<std::span<Fr>> multilinear_polynomials_to_be_shifted = { poly };
    std::vector<Commitment> multilinear_commitments = {};
    std::vector<Commitment> multilinear_commitments_to_be_shifted = { commitment };

    this->execute_gemini_and_verify_claims(log_n,
                                           u,
                                           multilinear_evaluations,
                                           multilinear_polynomials,
                                           multilinear_polynomials_to_be_shifted,
                                           multilinear_commitments,
                                           multilinear_commitments_to_be_shifted);
}

TYPED_TEST(GeminiTest, Double)
{
    using Fr = typename TypeParam::Fr;
    using Commitment = typename TypeParam::Commitment;

    const size_t n = 16;
    const size_t log_n = 4;

    auto u = this->random_evaluation_point(log_n);

    auto poly1 = this->random_polynomial(n);
    auto poly2 = this->random_polynomial(n);

    auto commitment1 = this->commit(poly1);
    auto commitment2 = this->commit(poly2);

    auto eval1 = poly1.evaluate_mle(u);
    auto eval2 = poly2.evaluate_mle(u);

    // Collect multilinear polynomials evaluations, and commitments for input to prover/verifier
    std::vector<Fr> multilinear_evaluations = { eval1, eval2 };
    std::vector<std::span<Fr>> multilinear_polynomials = { poly1, poly2 };
    std::vector<std::span<Fr>> multilinear_polynomials_to_be_shifted = {};
    std::vector<Commitment> multilinear_commitments = { commitment1, commitment2 };
    std::vector<Commitment> multilinear_commitments_to_be_shifted = {};

    this->execute_gemini_and_verify_claims(log_n,
                                           u,
                                           multilinear_evaluations,
                                           multilinear_polynomials,
                                           multilinear_polynomials_to_be_shifted,
                                           multilinear_commitments,
                                           multilinear_commitments_to_be_shifted);
}

TYPED_TEST(GeminiTest, DoubleWithShift)
{
    // using Gemini = MultilinearReductionScheme<TypeParam>;
    using Fr = typename TypeParam::Fr;
    using Commitment = typename TypeParam::Commitment;

    const size_t n = 16;
    const size_t log_n = 4;

    auto u = this->random_evaluation_point(log_n);

    auto poly1 = this->random_polynomial(n);
    auto poly2 = this->random_polynomial(n);
    poly2[0] = Fr::zero(); // necessary for polynomial to be 'shiftable'

    auto commitment1 = this->commit(poly1);
    auto commitment2 = this->commit(poly2);

    auto eval1 = poly1.evaluate_mle(u);
    auto eval2 = poly2.evaluate_mle(u);
    auto eval2_shift = poly2.evaluate_mle(u, true);

    // Collect multilinear polynomials evaluations, and commitments for input to prover/verifier
    std::vector<Fr> multilinear_evaluations = { eval1, eval2, eval2_shift };
    std::vector<std::span<Fr>> multilinear_polynomials = { poly1, poly2 };
    std::vector<std::span<Fr>> multilinear_polynomials_to_be_shifted = { poly2 };
    std::vector<Commitment> multilinear_commitments = { commitment1, commitment2 };
    std::vector<Commitment> multilinear_commitments_to_be_shifted = { commitment2 };

    this->execute_gemini_and_verify_claims(log_n,
                                           u,
                                           multilinear_evaluations,
                                           multilinear_polynomials,
                                           multilinear_polynomials_to_be_shifted,
                                           multilinear_commitments,
                                           multilinear_commitments_to_be_shifted);
}

} // namespace honk::pcs::gemini
