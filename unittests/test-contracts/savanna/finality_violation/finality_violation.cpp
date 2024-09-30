#include "finality_violation.hpp"

savanna::bitset merge_bitsets(const savanna::bitset& bitset_1, const savanna::bitset& bitset_2){
    check(bitset_1.size()==bitset_2.size(), "cannot merge bitsets of different sizes");
    savanna::bitset result_bitset(bitset_1.size());
    for (size_t i = 0 ; i < bitset_1.size(); i++){
        if (bitset_1.test(i) || bitset_2.test(i)) result_bitset.set(i);
    }
    return result_bitset;
}

savanna::bitset create_bitset(const size_t finalizers_count, const std::optional<std::vector<uint8_t>>& strong_votes, const std::optional<std::vector<uint8_t>>& weak_votes){

    check(strong_votes.has_value() || weak_votes.has_value(), "must have at least one set of votes to create a bitset");

    savanna::bitset result_bitset(finalizers_count);

    if (strong_votes.has_value() && weak_votes.has_value()){

        savanna::bitset strong_bitset(finalizers_count, strong_votes.value());
        savanna::bitset weak_bitset(finalizers_count, weak_votes.value());

        return merge_bitsets(strong_bitset, weak_bitset);

    }
    else if (strong_votes.has_value()) return savanna::bitset(finalizers_count, strong_votes.value());

    else return savanna::bitset(finalizers_count, weak_votes.value());

}


std::pair<savanna::bitset, savanna::bitset> check_bitsets(const finalizer_policy_input& finalizer_policy, const finality_proof& proof_1, const finality_proof& proof_2){

    std::optional<std::vector<uint8_t>> sv1 = proof_1.active_policy_qc.strong_votes;
    std::optional<std::vector<uint8_t>> sv2 = proof_2.active_policy_qc.strong_votes;

    std::optional<std::vector<uint8_t>> wv1 = proof_1.active_policy_qc.weak_votes;
    std::optional<std::vector<uint8_t>> wv2 = proof_2.active_policy_qc.weak_votes;

    savanna::bitset proof_1_bitset = create_bitset(finalizer_policy.finalizers.size(), sv1, wv1);
    savanna::bitset proof_2_bitset = create_bitset(finalizer_policy.finalizers.size(), sv2, wv2);

    auto result = bitset::compare(proof_1_bitset, proof_2_bitset);

    return result;

}

//Verify QCs presented as proof
void check_qcs( const finalizer_policy_input& finalizer_policy, const finality_proof& proof_1, const finality_proof& proof_2){

    //Verify we have our level 3 commitments for both proofs
    check(proof_1.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
    check(proof_2.qc_block.level_3_commitments.has_value(), "level 3 commitments structure must be present in both proofs to prove a finality violation");
        
    //Compute finality digests for both proofs 
    checksum256 digest_1 = block_finality_data_internal(proof_1.qc_block).finality_digest();
    checksum256 digest_2 = block_finality_data_internal(proof_2.qc_block).finality_digest();

    check(digest_1 != digest_2, "finality digests must be different");

    //Verify QC signatures over the finality digests
    _check_qc(proof_1.active_policy_qc, digest_1, finalizer_policy, false, false);
    _check_qc(proof_2.active_policy_qc, digest_2, finalizer_policy, false, false);

}

//Rule #1 : Do not vote on different blocks with the same timestamp
std::pair<std::string, std::string> finality_violation::rule1(const finalizer_policy_input& finalizer_policy, const finality_proof& proof_1, const finality_proof& proof_2){

    //Verify QCs
    check_qcs(finalizer_policy, proof_1, proof_2);

    //Compare timestamps
    block_timestamp timestamp_1 = proof_1.qc_block.level_3_commitments.value().timestamp;
    block_timestamp timestamp_2 = proof_2.qc_block.level_3_commitments.value().timestamp;
    
    check(timestamp_1 == timestamp_2, "proofs must be over blocks that have the same timestamp");

    //Proof of rule #1 finality violation

    auto result = check_bitsets(finalizer_policy, proof_1, proof_2);

    return {result.first.to_string(), result.second.to_string()};

}

//Rule #2 : Do not vote on a block that conflicts with the time interval of a strong vote
std::pair<std::string, std::string> finality_violation::rule2(   const finalizer_policy_input& finalizer_policy, 
                                    const finality_proof& high_proof,
                                    const finality_proof& low_proof,
                                    const std::vector<checksum256>& reversible_blocks_digests){

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

    auto result = check_bitsets(finalizer_policy, high_proof, low_proof);

    return {result.first.to_string(), result.second.to_string()};

}

//Rule #3 : Do not vote on a block that conflicts with another block on which you are locked
std::pair<std::string, std::string> finality_violation::rule3(   const finalizer_policy_input& finalizer_policy, 
                                    const finality_proof& high_proof,
                                    const finality_proof& low_proof,
                                    const std::vector<checksum256>& reversible_blocks_digests){

    //Verify QCs
    check_qcs(finalizer_policy, high_proof, low_proof);

    //Compare timestamps
    block_timestamp low_proof_timestamp = low_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp high_proof_timestamp = high_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp low_proof_last_claim_timestamp = low_proof.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;
    block_timestamp high_proof_last_claim_timestamp = high_proof.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    //If the low proof timestamp is less than the high proof timestamp, but the high proof last QC claim timestamp is lower than or equal to the low proof last QC claim, the lock was violated
    bool lock_violation = high_proof_last_claim_timestamp <= low_proof_last_claim_timestamp && low_proof_timestamp < high_proof_timestamp ;
    
    check(lock_violation, "proofs must demonstrate a lock violation");

    //Compute the merkle root of the reversible digests, and verify that it matches the commitment of the high proof
    check(get_merkle_root(reversible_blocks_digests) == high_proof.qc_block.level_3_commitments->reversible_blocks_mroot, "reversible_blocks_digests merkle root does not match reversible_blocks_mroot");

    //Compute the finality digest of the low proof
    checksum256 computed_digest = block_finality_data_internal(low_proof.qc_block).finality_digest();

    //Verify that the computed digest for the low proof doesn't appear in the list of reversible block digests committed to by the high proof
    auto f_itr = std::find(reversible_blocks_digests.begin(), reversible_blocks_digests.end(), computed_digest);

    check(f_itr==reversible_blocks_digests.end(), "finality digest of low block exists in reversible_blocks_digests vector");

    //Proof of rule #3 finality violation

    auto result = check_bitsets(finalizer_policy, high_proof, low_proof);

    return {result.first.to_string(), result.second.to_string()};

}

//For testing purposes, to verify that smart contract merkle tree implementation matches Spring merkle tree implementation 
ACTION finality_violation::testmroot(const checksum256& root, const std::vector<checksum256>& reversible_blocks_digests){
    checksum256 c_root = get_merkle_root(reversible_blocks_digests);
    check(c_root == root, "invalid root");
}