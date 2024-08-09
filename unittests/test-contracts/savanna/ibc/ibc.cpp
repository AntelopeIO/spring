#include "ibc.hpp"

void ibc::_maybe_set_finalizer_policy(const finalizer_policy_input& policy, const uint32_t from_block_num){
    policies_table _policies_table(get_self(), get_self().value);
    auto itr = _policies_table.rbegin();
    //if the new policy is more recent than the most recent we are aware of, we record the new one
    if (itr==_policies_table.rend() || itr->generation < policy.generation){

        //if a previous policy was in force, it is now superseded by the newer one for any future proof verification
        if (itr!=_policies_table.rend()){
            auto fwd_itr = itr.base();
            fwd_itr--;
            _policies_table.modify( fwd_itr, same_payer, [&]( auto& sfp ) {
                sfp.last_block_num = from_block_num;
           });
        }
        ibc::storedpolicy spolicy;
        spolicy.generation = policy.generation;
        spolicy.threshold = policy.threshold;
        spolicy.finalizers = policy.finalizers;

        //policy is in force until a newer policy is proven
        spolicy.last_block_num = std::numeric_limits<uint32_t>::max();
        //set cache expiry
        spolicy.cache_expiry = add_time(current_time_point(), POLICY_CACHE_EXPIRY);
        _policies_table.emplace( get_self(), [&]( auto& p ) {
            p = spolicy;
        });
    }
}

//adds the newly proven root if necessary
void ibc::_maybe_add_proven_root(const uint32_t block_num, const checksum256& finality_mroot){
    proofs_table _proofs_table(get_self(), get_self().value);
    //auto block_num_index = _proofs_table.get_index<"blocknum"_n>();
    auto merkle_index = _proofs_table.get_index<"merkleroot"_n>();
    auto last_itr = _proofs_table.rbegin();

    //if first proven root or newer than the last proven root, we store it 
    if (last_itr == _proofs_table.rend() || last_itr->block_num<(uint64_t)block_num){
        auto itr = merkle_index.find(finality_mroot);
        if (itr == merkle_index.end()){
            _proofs_table.emplace( get_self(), [&]( auto& p ) {
                //p.id = _proofs_table.available_primary_key();
                p.block_num = block_num;
                p.finality_mroot = finality_mroot;
                p.cache_expiry = add_time(current_time_point(), PROOF_CACHE_EXPIRY); //set cache expiry
            });
        }
    }
    //otherwise, the proven root is not advancing finality so we don't need to store it
}

template<typename Table>
void ibc::_maybe_remove_from_cache(){

    time_point now = current_time_point();

    Table table(get_self(), get_self().value);

    auto idx = table.template get_index<"expiry"_n>(); //expiry order index

    auto last_itr = idx.rbegin(); //last entry

    if (last_itr == idx.rend() ) return; //no entries, nothing to do

    if (now.sec_since_epoch() < last_itr->cache_expiry.sec_since_epoch()) return; //cache has not yet expired, nothing to do
    else {

        //cache must be cleaned up
        auto itr = idx.begin();
        while (itr!=idx.end()){
            if (itr->primary_key() == last_itr->primary_key()) return; //last entry, we always keep that one
            itr = idx.erase(itr);
        }
    }

}

finalizer_policy_input ibc::_get_stored_finalizer_policy(const uint64_t finalizer_policy_generation){

    policies_table _policies_table(get_self(), get_self().value);
    check(_policies_table.begin() != _policies_table.end(), "must set a finalizer policy before checking proofs");

    //fetch the finalizer policy where generation num is equal prepare.input.finalizer_policy_generation, and that it is still in force
    auto itr = _policies_table.find(finalizer_policy_generation);
    check(itr!=_policies_table.end(), "finalizer policy not found");
    return *itr;

}

void ibc::_check_finality_proof(const finality_proof& finality_proof, const block_proof_of_inclusion& target_block_proof_of_inclusion){

    //attempt to retrieve the stored policy with the correct generation number
    finalizer_policy_input finalizer_policy = _get_stored_finalizer_policy(finality_proof.qc_block.active_finalizer_policy_generation);

    //verify QC. If QC is valid, it means that we have reached finality on the block referenced by the finality_mroot
    _check_qc(finality_proof.active_policy_qc, block_finality_data_internal(finality_proof.qc_block).finality_digest(), finalizer_policy);

    if (finality_proof.qc_block.last_pending_finalizer_policy_generation.has_value()){

        check(std::holds_alternative<extended_block_data>(target_block_proof_of_inclusion.target), "must provide extended data for transition blocks");

        auto target = std::get<extended_block_data>(target_block_proof_of_inclusion.target);

        check(target.finality_data.pending_finalizer_policy.has_value(), "must provide pending finalizer policy for transition blocks");

        _check_qc(finality_proof.pending_policy_qc.value(), block_finality_data_internal(finality_proof.qc_block).finality_digest(), target.finality_data.pending_finalizer_policy.value());

        _maybe_set_finalizer_policy(target.finality_data.pending_finalizer_policy.value(), target.dynamic_data.block_num);

    }

    //check if the target proof of inclusion correctly resolves to the root of the finality proof
    _check_target_block_proof_of_inclusion(target_block_proof_of_inclusion, finality_proof.qc_block.finality_mroot);
    
    //if the finality_mroot we just proven is more recent than the last root we have stored, store it
    uint64_t offset = target_block_proof_of_inclusion.final_block_index - target_block_proof_of_inclusion.target_block_index;

    dynamic_data_v0 d_data = std::visit([&](const auto& bd) { return bd.dynamic_data; }, target_block_proof_of_inclusion.target);

    _maybe_add_proven_root(d_data.block_num + offset, finality_proof.qc_block.finality_mroot);

}

void ibc::_check_target_block_proof_of_inclusion(const block_proof_of_inclusion& proof, const std::optional<checksum256> reference_root){

    //resolve the proof to its merkle root
    checksum256 finality_mroot = proof.root();
    if (reference_root.has_value()){
        check(reference_root.value() == finality_mroot, "proof of inclusion is invalid");
    }
    else {
        proofs_table _proofs_table(get_self(), get_self().value);
        auto merkle_index = _proofs_table.get_index<"merkleroot"_n>();
        auto itr = merkle_index.find(finality_mroot);
        check(itr!= merkle_index.end(), "proof of inclusion is invalid");
    }

}

ACTION ibc::setfpolicy(const finalizer_policy_input& policy, const uint32_t from_block_num){

    //can only be called with account authority
    require_auth(get_self());

    policies_table _policies_table(get_self(), get_self().value);

    //can only be used once for the initilization of the contract
    check(_policies_table.begin() == _policies_table.end(), "can only set finalizer policy manually for initialization");

    _maybe_set_finalizer_policy(policy, from_block_num);

    //clean up if necessary
    _maybe_remove_from_cache<policies_table>();
    _maybe_remove_from_cache<proofs_table>();
    
}

ACTION ibc::checkproof(const proof& proof){

    //if we have a finality proof, we execute the "heavy" code path
    if (proof.finality_proof.has_value()){
        _check_finality_proof(proof.finality_proof.value(), proof.target_block_proof_of_inclusion);
    }
    else {
        //if we only have a proof of inclusion of the target block, we execute the "light" code path
        _check_target_block_proof_of_inclusion(proof.target_block_proof_of_inclusion, std::nullopt);
    }

    //clean up if necessary
    _maybe_remove_from_cache<policies_table>();
    _maybe_remove_from_cache<proofs_table>();

}

ACTION ibc::testbitset(const std::string bitset_string, const std::vector<uint8_t> bitset_vector, const uint32_t finalizers_count){
    savanna::bitset b(finalizers_count, bitset_vector);

    check(b.to_string() == bitset_string, "bitset mismatch");

}