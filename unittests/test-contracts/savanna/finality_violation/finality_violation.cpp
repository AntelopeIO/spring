#include "finality_violation.hpp"

ACTION finality_violation::rule1(const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    check(proof_1.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
    check(proof_2.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
        
    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    
    check(timestamp_1 == timestamp_2, "proofs must be over blocks that have the same timestamp");

    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    check(digest_1 != digest_2, "finality digests must be different");

    _check_qc(proof_1.active_policy_qc, digest_1, finalizer_policy);
    _check_qc(proof_2.active_policy_qc, digest_2, finalizer_policy);

}

ACTION finality_violation::rule2(const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    check(proof_1.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
    check(proof_2.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
        
    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    block_timestamp last_claim_timestamp_1 = proof_1.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;
    block_timestamp last_claim_timestamp_2 = proof_2.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    bool lessThanRule = last_claim_timestamp_2 < timestamp_1 && timestamp_1 < timestamp_2;
    bool greaterThanRule = last_claim_timestamp_1 < timestamp_2 && timestamp_2 < timestamp_1;

    check(lessThanRule || greaterThanRule, "proofs must demonstrate a conflicting time range");

    _check_qc(proof_1.active_policy_qc, digest_1, finalizer_policy);
    _check_qc(proof_2.active_policy_qc, digest_2, finalizer_policy);

}

ACTION finality_violation::rule3(){

}