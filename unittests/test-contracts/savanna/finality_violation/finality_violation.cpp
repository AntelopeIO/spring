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

void check_rule_2_qcs( const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    check(proof_1.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
    check(proof_2.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
        
    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    _check_qc(proof_1.active_policy_qc, digest_1, finalizer_policy);
    _check_qc(proof_2.active_policy_qc, digest_2, finalizer_policy);
}

//Proof_1 is presumably from a fake, hidden chain, while proof_2 is presumably from the real, discoverable chain
//The block referenced by the proof of inclusion must be proven as a final ancestor of proof_2, while also be proven to conflict with proof_1
ACTION finality_violation::rule2a( const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    block_timestamp last_claim_timestamp_1 = proof_1.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;
    block_timestamp last_claim_timestamp_2 = proof_2.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    bool lessThanRule = last_claim_timestamp_1 < timestamp_2 && timestamp_2 < timestamp_1;

    check(lessThanRule, "proofs must demonstrate a conflicting time range");

    check_rule_2_qcs(finalizer_policy, proof_1, proof_2);

}

ACTION finality_violation::rule2b(  const finalizer_policy_input finalizer_policy, 
                                    const finality_proof proof_1, 
                                    const finality_proof proof_2, 
                                    const block_proof_of_inclusion proof_of_inclusion){

    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    block_timestamp last_claim_timestamp_1 = proof_1.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;
    block_timestamp last_claim_timestamp_2 = proof_2.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    bool greaterThanRule = last_claim_timestamp_2 < timestamp_1 && timestamp_1 < timestamp_2;
    
    check(greaterThanRule, "proofs must demonstrate a conflicting time range");

    check_rule_2_qcs(finalizer_policy, proof_1, proof_2);

    check(proof_of_inclusion.root() == proof_2.qc_block.finality_mroot, "invalid proof of inclusion");

}

ACTION finality_violation::rule3(){

}