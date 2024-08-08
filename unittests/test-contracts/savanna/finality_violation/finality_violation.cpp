#include "finality_violation.hpp"

//Verify QCs presented as proof
void check_qcs( const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    //Verify we have our level 3 commitments for both proofs
    check(proof_1.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
    check(proof_2.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
        
    //Compute finality digests for both proofs 
    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    //Verify QC signatures over the finality digests
    _check_qc(proof_1.active_policy_qc, digest_1, finalizer_policy);
    _check_qc(proof_2.active_policy_qc, digest_2, finalizer_policy);

}

//Rule #1 : Do not vote on different blocks with the same timestamp
ACTION finality_violation::rule1(const finalizer_policy_input finalizer_policy, const finality_proof proof_1, const finality_proof proof_2){

    //Verify QCs
    check_qcs(finalizer_policy, proof_1, proof_2);

    //Compare timestamps
    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    
    check(timestamp_1 == timestamp_2, "proofs must be over blocks that have the same timestamp");

    //Verify that finality digests are different
    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    check(digest_1 != digest_2, "finality digests must be different");

    //Proof of rule #1 finality violation

}

//Rule #2 : Do not vote on a block that conflicts with the time interval of a strong vote
ACTION finality_violation::rule2(   const finalizer_policy_input finalizer_policy, 
                                    const finality_proof high_proof,
                                    const finality_proof low_proof,
                                    const std::vector<checksum256> reversible_blocks_digests){

    //Verify QCs
    check_qcs(finalizer_policy, high_proof, low_proof);

    //Compare timestamps
    block_timestamp high_proof_timestamp = high_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp low_proof_timestamp = low_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp high_proof_last_claim_timestamp = high_proof.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    //A time range conflict has occured if the low proof timestamp is contained within the high proof time range
    bool time_range_conflict = high_proof_last_claim_timestamp < low_proof_timestamp && low_proof_timestamp < high_proof_timestamp;
    
    check(time_range_conflict, "proofs must demonstrate a conflicting time range");

    //Compute the merkle root of the reversible digests, and verify that it matches the commitment of the high proof
    check(get_merkle_root(reversible_blocks_digests) == high_proof.qc_block.level_3_commitments->reversible_blocks_mroot, "reversible_blocks_digests merkle root does not match reversible_blocks_mroot");

    //Compute the finality digest of the low proof
    checksum256 computed_digest = block_finality_data_internal(low_proof.qc_block).finality_digest();

    //Verify that the computed digest for the low proof doesn't appear in the list of reversible block digests committed to by the high proof
    auto f_itr = std::find(reversible_blocks_digests.begin(), reversible_blocks_digests.end(), computed_digest);

    check(f_itr==reversible_blocks_digests.end(), "finality digest of low block exists in reversible_blocks_digests vector");

    //Proof of rule #2 finality violation

}

//Rule #3 : Do not vote on a block that conflicts with another block on which you are locked
ACTION finality_violation::rule3(   const finalizer_policy_input finalizer_policy, 
                                    const finality_proof high_proof,
                                    const finality_proof low_proof,
                                    const std::vector<checksum256> reversible_blocks_digests){

    //Verify QCs
    check_qcs(finalizer_policy, high_proof, low_proof);

    //Compare timestamps
    block_timestamp low_proof_timestamp = low_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp high_proof_timestamp = high_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp low_proof_last_claim_timestamp = low_proof.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;
    block_timestamp high_proof_last_claim_timestamp = high_proof.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    //If the high proof timestamp is higher than the low proof timestamp, but the high proof last QC claim timestamp is lower than the low proof last QC claim, the lock was violated
    bool lock_violation = high_proof_timestamp > low_proof_timestamp && high_proof_last_claim_timestamp < low_proof_timestamp;
    
    check(lock_violation, "proofs must demonstrate a lock violation");

    //Compute the merkle root of the reversible digests, and verify that it matches the commitment of the high proof
    check(get_merkle_root(reversible_blocks_digests) == high_proof.qc_block.level_3_commitments->reversible_blocks_mroot, "reversible_blocks_digests merkle root does not match reversible_blocks_mroot");

    //Compute the finality digest of the low proof
    checksum256 computed_digest = block_finality_data_internal(low_proof.qc_block).finality_digest();

    //Verify that the computed digest for the low proof doesn't appear in the list of reversible block digests committed to by the high proof
    auto f_itr = std::find(reversible_blocks_digests.begin(), reversible_blocks_digests.end(), computed_digest);

    check(f_itr==reversible_blocks_digests.end(), "finality digest of low block exists in reversible_blocks_digests vector");

    //Proof of rule #3 finality violation

}

//For testing purposes, to verify that smart contract merkle tree implementation matches Spring merkle tree implementation 
ACTION finality_violation::testmroot(const checksum256 root, const std::vector<checksum256> reversible_blocks_digests){
    checksum256 c_root = get_merkle_root(reversible_blocks_digests);
    check(c_root == root, "invalid root");
}