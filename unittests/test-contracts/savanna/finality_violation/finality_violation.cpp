#include "finality_violation.hpp"

void check_qcs( const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    check(proof_1.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
    check(proof_2.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
        
    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    _check_qc(proof_1.active_policy_qc, digest_1, finalizer_policy);
    _check_qc(proof_2.active_policy_qc, digest_2, finalizer_policy);

}

ACTION finality_violation::rule1(const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    check_qcs(finalizer_policy, proof_1, proof_2);

    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    
    check(timestamp_1 == timestamp_2, "proofs must be over blocks that have the same timestamp");

    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    check(digest_1 != digest_2, "finality digests must be different");

}


//Proof_1 is presumably from a fake, hidden chain, while proof_2 is presumably from the real, discoverable chain
//The block referenced by the proof of inclusion must be proven as a final ancestor of proof_2, while also be proven to conflict with proof_1
/*ACTION finality_violation::rule2a( const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    block_timestamp last_claim_timestamp_1 = proof_1.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;
    block_timestamp last_claim_timestamp_2 = proof_2.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    bool lessThanRule = last_claim_timestamp_1 < timestamp_2 && timestamp_2 < timestamp_1;

    check(lessThanRule, "proofs must demonstrate a conflicting time range");

    check_rule_2_qcs(finalizer_policy, proof_1, proof_2);

}*/

ACTION finality_violation::rule2(   const finalizer_policy_input finalizer_policy, 
                                    const finality_proof high_proof,
                                    const finality_proof low_proof,
                                    const std::vector<checksum256> reversible_blocks_digests){

    print("check QC\n");
    check_qcs(finalizer_policy, high_proof, low_proof);

    block_timestamp high_proof_timestamp = high_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp low_proof_timestamp = low_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp high_proof_last_claim_timestamp = high_proof.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    print("check range conflict\n");
    bool time_range_conflict = high_proof_last_claim_timestamp < low_proof_timestamp && low_proof_timestamp < high_proof_timestamp;
    
    check(time_range_conflict, "proofs must demonstrate a conflicting time range");

    print("check merkle root\n");
    check(get_merkle_root(reversible_blocks_digests) == high_proof.qc_block.level_3_commitments->reversible_blocks_mroot, "reversible_blocks_digests merkle root does not match reversible_blocks_mroot");

/*    check(std::holds_alternative<extended_block_data>(proof_of_inclusion.target), "must provide extended block data object");

    extended_block_data target = std::get<extended_block_data>(proof_of_inclusion.target);

    block_timestamp target_timestamp = target.timestamp;

    check(high_proof_timestamp == target_timestamp, "timestamp of high_proof block of the target block of the proof of inclusion must be the same");

    auto finality_digest_claimed = block_finality_data_internal(high_proof.qc_block).finality_digest();
    auto finality_digest_actual = block_finality_data_internal(target.finality_data).finality_digest() ;

    check(finality_digest_claimed != finality_digest_actual, "finality digests must be different for a finality violation proof to be valid");*/

}

ACTION finality_violation::rule3(){


}

ACTION finality_violation::testmroot(const checksum256 root, const std::vector<checksum256> reversible_blocks_digests){
    checksum256 c_root = get_merkle_root(reversible_blocks_digests);
    check(c_root == root, "invalid root");
}