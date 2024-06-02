#include "finality_violation.hpp"

template<typename T>
struct InternalForType;

template<> struct InternalForType<simple_block_data> { using type = simple_block_data_internal;  };
template<> struct InternalForType<extended_block_data> { using type = extended_block_data_internal;  };

template <typename T1, typename T2>
void _check_block_data(const T1& target_1, const T2& target_2 ){
    check(target_1.dynamic_data.block_num == target_2.dynamic_data.block_num , "both proofs must target the same block");
    using InternalType1 = typename InternalForType<T1>::type;
    using InternalType2 = typename InternalForType<T2>::type;
    
    check(InternalType1(target_1).finality_leaf() != InternalType2(target_2).finality_leaf()  , "proof target blocks must have a different finality digest") ;

}

void check_block_data(const block_data_type& target_1, const block_data_type& target_2 ) {
    auto visitor = [&](auto&& target_1, auto&& target_2) { _check_block_data(target_1, target_2); };
    std::visit(visitor, target_1, target_2 );
}

std::pair<std::string, std::string> finality_violation::addviolation(const finalizer_policy_input& finalizer_policy, const proof& proof1, const proof& proof2 ){

    //verify finality proofs are present
    check(proof1.finality_proof.has_value(), "must provide finality proof for signature verification");
    check(proof2.finality_proof.has_value(), "must provide finality proof for signature verification");

    //verify the proof of inclusion is valid 
    check(proof1.target_block_proof_of_inclusion.root() == proof1.finality_proof->qc_block.finality_mroot, "proof of inclusion is invalid");
    check(proof2.target_block_proof_of_inclusion.root() == proof2.finality_proof->qc_block.finality_mroot, "proof of inclusion is invalid");

    //check that signatures are valid for both proofs
    _check_qc(proof1.finality_proof->qc, block_finality_data_internal(proof1.finality_proof->qc_block).finality_digest(), finalizer_policy);
    _check_qc(proof2.finality_proof->qc, block_finality_data_internal(proof2.finality_proof->qc_block).finality_digest(), finalizer_policy);

    //check proofs and finalizer policy are of the same generation
    check(  finalizer_policy.generation == proof1.finality_proof->qc_block.finalizer_policy_generation 
            && finalizer_policy.generation == proof2.finality_proof->qc_block.finalizer_policy_generation, "finalizer policy generation must be the same for all proof elements");

    check_block_data(proof1.target_block_proof_of_inclusion.target, proof2.target_block_proof_of_inclusion.target);

    //calculate intersection / symmetric difference
    savanna::bitset proof1_bitset(finalizer_policy.finalizers.size(), proof1.finality_proof->qc.finalizers);
    savanna::bitset proof2_bitset(finalizer_policy.finalizers.size(), proof2.finality_proof->qc.finalizers);
    
    auto result = bitset::compare(proof1_bitset, proof2_bitset);

    //return guilty bitset vs innocent bitset
    return {result.first.to_string(), result.second.to_string()};

}
