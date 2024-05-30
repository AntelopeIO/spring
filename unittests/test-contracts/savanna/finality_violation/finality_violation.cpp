#include "finality_violation.hpp"

std::pair<std::string, std::string> finality_violation::addviolation(const finalizer_policy_input& finalizer_policy, const proof& proof1, const proof& proof2 ){

    //verify finality proofs are present
    check(proof1.finality_proof.has_value(), "must provide finality proof for signature verification");
    check(proof2.finality_proof.has_value(), "must provide finality proof for signature verification");

    //verify the proof of inclusion is valid 
    check(proof1.target_block_proof_of_inclusion.root() == proof1.finality_proof->qc_block.finality_mroot, "proof of inclusion is invalid");
    check(proof2.target_block_proof_of_inclusion.root() == proof2.finality_proof->qc_block.finality_mroot, "proof of inclusion is invalid");

    //check that signatures are valid for both proofs
    _check_qc(proof1.finality_proof->qc, proof1.finality_proof->qc_block.finality_digest(), finalizer_policy);
    _check_qc(proof2.finality_proof->qc, proof2.finality_proof->qc_block.finality_digest(), finalizer_policy);

    //check proofs and finalizer policy are of the same generation
    check(  finalizer_policy.generation == proof1.finality_proof->qc_block.finalizer_policy_generation 
            && finalizer_policy.generation == proof2.finality_proof->qc_block.finalizer_policy_generation, "finalizer policy generation must be the same for all proof elements");

    //check that both proofs target the same block num
    check ( proof1.target_block_proof_of_inclusion.target.dynamic_data.block_num
            == proof2.target_block_proof_of_inclusion.target.dynamic_data.block_num , "both proofs must target the same block");

    //check that proofs have different finality digests for the same target block (evidence of double signing)
    check ( proof1.target_block_proof_of_inclusion.target.finality_data.finality_digest()
            != proof2.target_block_proof_of_inclusion.target.finality_data.finality_digest() , "proof target blocks must have a different finality digest");

    //calculate intersection / symmetric difference
    savanna::bitset proof1_bitset(finalizer_policy.finalizers.size(), proof1.finality_proof->qc.finalizers);
    savanna::bitset proof2_bitset(finalizer_policy.finalizers.size(), proof2.finality_proof->qc.finalizers);
    
    auto result = bitset::compare(proof1_bitset, proof2_bitset);

    //return guilty bitset vs innocent bitset
    return {result.first.to_string(), result.second.to_string()};

}
