#include "finality_violation.hpp"

savanna::bitset merge_bitsets(const savanna::bitset& bitset_1, const savanna::bitset& bitset_2){

    //check that bitsets are of the same size
    check(bitset_1.size()==bitset_2.size(), "cannot merge bitsets of different sizes");

    //create a new bitset of the same size as the input bitsets
    savanna::bitset result_bitset(bitset_1.size());

    //merge the bitsets by setting the bits that are set in either of the input bitsets into the result bitset
    for (size_t i = 0 ; i < bitset_1.size(); i++){
        if (bitset_1.test(i) || bitset_2.test(i)) result_bitset.set(i);
    }
    return result_bitset;
}

savanna::bitset create_bitset(const size_t finalizers_count, const std::optional<std::vector<uint8_t>>& strong_votes, const std::optional<std::vector<uint8_t>>& weak_votes){

    //check that at least one set of votes is present
    check(strong_votes.has_value() || weak_votes.has_value(), "must have at least one set of votes to create a bitset");

        savanna::bitset result_bitset(finalizers_count);

    //if both strong and weak votes are present, merge them
    if (strong_votes.has_value() && weak_votes.has_value()){

        savanna::bitset strong_bitset(finalizers_count, strong_votes.value());
        savanna::bitset weak_bitset(finalizers_count, weak_votes.value());

        return merge_bitsets(strong_bitset, weak_bitset);

    }
    //if only strong votes are present, use them
    else if (strong_votes.has_value()) return savanna::bitset(finalizers_count, strong_votes.value());
    //if only weak votes are present, use them
    else return savanna::bitset(finalizers_count, weak_votes.value());

}


std::pair<savanna::bitset, savanna::bitset> check_bitsets(const finalizer_policy_input& finalizer_policy, const finality_proof& high_proof, const finality_proof& low_proof, const bool high_proof_strong_votes_only = false, const bool low_proof_strong_votes_only = false){

    std::optional<std::vector<uint8_t>> hsv = high_proof.active_policy_qc.strong_votes;
    std::optional<std::vector<uint8_t>> lsv = low_proof.active_policy_qc.strong_votes;

    std::optional<std::vector<uint8_t>> hwv = high_proof.active_policy_qc.weak_votes;
    std::optional<std::vector<uint8_t>> lwv = low_proof.active_policy_qc.weak_votes;

    //if bitset verification applies only to strong votes, remove weak votes
    if (high_proof_strong_votes_only) hwv = std::nullopt;
    if (low_proof_strong_votes_only) lwv = std::nullopt;

    //create bitsets
    savanna::bitset high_proof_bitset = create_bitset(finalizer_policy.finalizers.size(), hsv, hwv);
    savanna::bitset low_proof_bitset = create_bitset(finalizer_policy.finalizers.size(), lsv, lwv);

    //compare bitsets
    auto result = bitset::compare(high_proof_bitset, low_proof_bitset);

    return result;

}

//Verify QCs presented as proof
std::pair<checksum256, checksum256> check_qcs( const finalizer_policy_input& finalizer_policy, const finality_proof& proof_1, const finality_proof& proof_2){

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

    return {digest_1, digest_2};

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
                                    const reversible_proof_of_inclusion& proof_of_inclusion){

    //Verify QCs
    auto finalizer_digests = check_qcs(finalizer_policy, high_proof, low_proof);

    //Compare timestamps
    block_timestamp high_proof_timestamp = high_proof.qc_block.level_3_commitments.value().timestamp;
    block_timestamp low_proof_timestamp = low_proof.qc_block.level_3_commitments.value().timestamp;
    
    block_timestamp high_proof_parent_timestamp = proof_of_inclusion.target.parent_timestamp;

    //Verify that the proof of inclusion resolves to the reversible blocks mroot of the high proof
    check(proof_of_inclusion.root() == high_proof.qc_block.level_3_commitments.value().reversible_blocks_mroot, "proof of inclusion must resolve to the reversible blocks mroot of the high proof");

    //A time range conflict has occured if the high proof timestamp is greater than or equal to the low proof timestamp and the high proof parent timestamp is less than the low proof timestamp
    bool time_range_conflict = high_proof_parent_timestamp < low_proof_timestamp && high_proof_timestamp >= low_proof_timestamp;
    check(time_range_conflict, "proofs must demonstrate a conflicting time range");

    bool finality_violation = false;

    //If the timestamp for the submitted reversible blocks leaf node is strictly greater than low_proof_timestamp, we know that the low proof block is not an ancestor of the high proof block 
    //and therefore, a rule 2 violation has occurred.
    if(proof_of_inclusion.target.timestamp > low_proof_timestamp) finality_violation = true;
    else if(proof_of_inclusion.target.timestamp == low_proof_timestamp) {
        //If the timestamp for the submitted reversible blocks leaf node is exactly equal to low_proof_timestamp, we need to compare the finality digest of the low proof block to the finality digest of the submitted reversible blocks leaf node,
        //to check that they are not the same. If they are the same, the submitted proof is not correct. But if they are different, then we know that the low proof block is not an ancestor of the high proof block 
        check(finalizer_digests.second != proof_of_inclusion.target.finality_digest, "finality digest of low proof must be different from the finality digest of the submitted reversible blocks leaf node");
        finality_violation = true;
    }
    
    check(finality_violation, "proofs must demonstrate a finality violation");

    //Proof of rule #2 finality violation
    auto result = check_bitsets(finalizer_policy, high_proof, low_proof, false, true);

    return {result.first.to_string(), result.second.to_string()};

}

//Rule #3 : Do not vote on a block that conflicts with another block on which you are locked
std::pair<std::string, std::string> finality_violation::rule3(   const finalizer_policy_input& finalizer_policy, 
                                    const finality_proof& high_proof,
                                    const finality_proof& low_proof,
                                    const reversible_proof_of_inclusion& proof_of_inclusion){

    //Verify QCs
    auto finalizer_digests = check_qcs(finalizer_policy, high_proof, low_proof);

    //Compare timestamps
    block_timestamp target_proof_timestamp = proof_of_inclusion.target.timestamp;
    block_timestamp low_proof_last_claim_timestamp = low_proof.qc_block.level_3_commitments.value().latest_qc_claim_timestamp;

    block_timestamp target_proof_parent_timestamp = proof_of_inclusion.target.parent_timestamp;

    //Verify that the proof of inclusion resolves to the reversible blocks mroot of the high proof
    check(proof_of_inclusion.root() == high_proof.qc_block.level_3_commitments.value().reversible_blocks_mroot, "proof of inclusion must resolve to the reversible blocks mroot of the high proof");

    //A lock violation has occured if the high proof timestamp is greater than or equal to the low proof last claim timestamp and the high proof parent timestamp is less than the low proof last claim timestamp
    bool lock_violation = target_proof_timestamp >= low_proof_last_claim_timestamp && target_proof_parent_timestamp < low_proof_last_claim_timestamp;
    check(lock_violation, "proofs must demonstrate a lock violation");
    
    bool finality_violation = false;

    //If the timestamp for the submitted reversible blocks leaf node is strictly greater than low_proof_last_claim_timestamp, we know that the low proof block is not an ancestor of the high proof block 
    //and therefore, a rule 3 violation has occurred.
    if(target_proof_timestamp > low_proof_last_claim_timestamp) finality_violation = true;
    else if(target_proof_timestamp == low_proof_last_claim_timestamp) {
        //If the timestamp for the submitted reversible blocks leaf node is exactly equal to low_proof_timestamp, we need to compare the finality digest of the low proof block to the finality digest of the submitted reversible blocks leaf node,
        //to check that they are not the same. If they are the same, the submitted proof is not correct. But if they are different, then we know that the low proof block is not an ancestor of the high proof block 
        check(finalizer_digests.second != proof_of_inclusion.target.finality_digest, "finality digest of low proof must be different from the finality digest of the submitted reversible blocks leaf node");
        finality_violation = true;
    }
    
    check(finality_violation, "proofs must demonstrate a finality violation");

    //Proof of rule #3 finality violation
    auto result = check_bitsets(finalizer_policy, high_proof, low_proof, true, false);

    return {result.first.to_string(), result.second.to_string()};

}
