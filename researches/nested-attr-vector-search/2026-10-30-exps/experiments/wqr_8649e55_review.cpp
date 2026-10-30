// Investigation-only probe of immutable commit 8649e55. Not a production fix.
// Include directory must point at the hash-verified, unmodified review snapshot.
#include "wqr_index.h"
#include <iostream>
#include <string>

static void one_slot(wqr::Block& b, int64_t cat, int64_t num) {
    b.slot_to_id[0] = 0;
    b.id_to_slot[0] = 0;
    b.live_count = 1;
    b.setAlive(0, true);
    b.setCategory(0, cat);
    b.setNumeric(0, num);
    b.setTags(0, 1);
    b.addOwnerSlot(0, 0, 0);
}
static bool mask_value(wqr::Block& b, wqr::EulerTour& e,
                       const std::vector<wqr::NodeId>& parent,
                       const wqr::ExactMaskComputer::Query& q) {
    const auto result = wqr::ExactMaskComputer{b,e,parent}.compute(q,b.capacity);
    return (result.mask[0] & 1) != 0;
}
static void build_two(wqr::WQRIndex& idx, const std::vector<wqr::Id>& ids,
                      size_t capacity) {
    idx.init(2, capacity);
    wqr::VectorStore v;
    v.init(2);
    v.data.assign(capacity*2, 0);
    for (size_t i=0; i<ids.size(); ++i) {
        v.data[2*ids[i]] = float(i+1);
        idx.owner[ids[i]] = 0;
    }
    idx.build(ids, {0}, v);
}
int main(int argc, char** argv) {
    const std::string mode = argc>1 ? argv[1] : "safe";
    if (mode == "reserve") {
        wqr::WQRIndex idx;
        build_two(idx, {0,1}, 3);
        // ID 2 has a canonical vector but was not initially indexed.
        idx.apply_I(2,0,0,0,0);
        std::cout << "reserve_insert_survived\n";
        return 0;
    }
    {
        wqr::EulerTour e;
        e.build({0,0,0});
        std::cout << "sibling_containment actual=" << e.contains(1,2)
                  << " expected=0\n";
    }
    {
        std::vector<wqr::NodeId> parent{0};
        wqr::EulerTour e;
        e.build(parent);
        wqr::Block b(2);
        one_slot(b, 3, 5);
        wqr::ExactMaskComputer::Query q;
        q.anchor = 0;
        q.category = 7;
        std::cout << "category_reject actual=" << mask_value(b,e,parent,q)
                  << " expected=0\n";
        q.category = -1;
        q.tagmode = 2;
        q.tags = 0;
        std::cout << "empty_overlap_reject actual=" << mask_value(b,e,parent,q)
                  << " expected=0\n";
        q.tagmode = 0;
        q.combine_or = true;
        q.lo = 0; q.hi = 10;
        std::cout << "numeric_five_in_zero_ten actual=" << mask_value(b,e,parent,q)
                  << " expected=1\n";
        b.setNumeric(0, -2);
        q.lo = -1; q.hi = 0;
        std::cout << "literal_minus_one_bound actual=" << mask_value(b,e,parent,q)
                  << " expected=0\n";
    }
    {
        wqr::WQRIndex idx;
        build_two(idx,{20,21},22);
        wqr::ExactMaskComputer::Query q;
        const float query[2]={1,0};
        auto r=idx.query(query,2,1,64,1024,q);
        std::cout << "external_id_result_count actual=" << r.results.size()
                  << " expected=2 selected_blocks=" << r.blocks_selected << "\n";
    }
    {
        wqr::WQRIndex idx;
        build_two(idx,{0,1},2);
        wqr::ExactMaskComputer::Query q;
        const float query[2]={1,0};
        size_t before=idx.proxy_space->calls;
        auto a=idx.query(query,2,1,64,1024,q);
        size_t after=idx.proxy_space->calls;
        auto b=idx.query(query,2,1,64,1024,q);
        std::cout << "portal_counter first_report=" << a.portal_distances
                  << " first_actual_delta=" << after-before
                  << " second_report=" << b.portal_distances
                  << " second_actual_delta=" << idx.proxy_space->calls-after << "\n";
        std::cout << "local_counter first_report=" << a.local_distances
                  << " returned_objects=" << a.results.size() << "\n";
        const auto loc=idx.location[1];
        auto original_zone=idx.blocks[loc.block].zone_of[loc.slot];
        auto original_live=idx.blocks[loc.block].live_count;
        idx.apply_D(1);
        std::cout << "delete_live_count actual=" << idx.blocks[loc.block].live_count
                  << " expected=" << original_live-1 << "\n";
        idx.apply_I(1,0,0,0,0);
        std::cout << "restore_zone actual=" << unsigned(idx.blocks[loc.block].zone_of[loc.slot])
                  << " expected=" << unsigned(original_zone) << "\n";
    }
}
