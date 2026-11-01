// Fixed didactic fixture: ../data/dag-worked-example.json (stable IDs, no loader shuffle).
// This executable uses the production query implementation, not a replacement API.
#define main dag_scope_ann_cli_main
#include "../../../../codes/directory-aware-vector-attr-search/src/dag_scope_ann.cpp"
#undef main
#include <stdexcept>

namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
std::string bits(const Bitmap& b) { std::string s; for (Id i=0;i<8;++i) s += b.test(i)?'1':'0'; return s; }
template<class T> void array(std::ostream& o,const std::vector<T>& v) { o<<'['; for(size_t i=0;i<v.size();++i){if(i)o<<',';o<<v[i];}o<<']'; }
void pairs(std::ostream& o,const std::vector<std::pair<float,Id>>& v) { o<<'[';for(size_t i=0;i<v.size();++i){if(i)o<<',';o<<"{\"id\":"<<v[i].second<<",\"squared_l2\":"<<v[i].first<<'}';}o<<']'; }
std::vector<Id> ids(const Bitmap& b) { std::vector<Id> v;for(Id i=0;i<8;++i)if(b.test(i))v.push_back(i);return v; }
// Independent oracle deliberately does not call RawL2Space or exact_search.
std::vector<std::pair<float,Id>> oracle(const Bitmap& b,const Vectors& v,size_t k) { std::vector<std::pair<float,Id>> r;for(Id i:ids(b)){float d=0;for(size_t j=0;j<2;++j){float x=v.data[2*i+j]-1;d+=x*x;}r.emplace_back(d,i);}std::sort(r.begin(),r.end());if(r.size()>k)r.resize(k);return r; }
std::string records(hnswlib::HierarchicalNSW<float>& graph, const Vectors& base) {
    std::ostringstream out;
    out << "{\"entry_id\":" << graph.getExternalLabel(graph.enterpoint_node_)
        << ",\"max_level\":" << graph.maxlevel_ << ",\"records\":[";
    for (size_t local = 0; local < graph.cur_element_count; ++local) {
        if (local) out << ',';
        const Id id = graph.getExternalLabel(local);
        const auto* payload = reinterpret_cast<const float*>(graph.getDataByInternalId(local));
        require(payload != base.data.data() + 2 * id, "HNSW must own a vector copy");
        require(payload[0] == base.data[2 * id] && payload[1] == base.data[2 * id + 1],
                "stored vector mismatch");
        require(graph.label_lookup_.at(id) == local, "external/internal mapping mismatch");
        out << "{\"internal_id\":" << local << ",\"id\":" << id
            << ",\"vector\":[" << payload[0] << ',' << payload[1]
            << "],\"owns_vector_copy\":true,\"level\":" << graph.element_levels_[local]
            << ",\"neighbor_internal_ids_by_level\":[";
        for (int level = 0; level <= graph.element_levels_[local]; ++level) {
            if (level) out << ',';
            auto* links = level ? graph.get_linklist(local, level) : graph.get_linklist0(local);
            const auto* neighbors = reinterpret_cast<const hnswlib::tableint*>(links + 1);
            out << '[';
            for (size_t j = 0; j < graph.getListCount(links); ++j) {
                if (j) out << ',';
                out << neighbors[j];
            }
            out << ']';
        }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
} // namespace

int main(int argc,char** argv) try {
 std::string out;for(int i=1;i<argc;++i){require(std::string(argv[i])=="--out"&&i+1<argc,"usage: dag_worked_example --out result.json");out=argv[++i];}require(!out.empty(),"--out is required");
 Graph g;g.parents.resize(7);g.children.resize(7);g.ancestors.resize(7);g.descendants.resize(7);
 const std::vector<std::pair<Id,Id>> edges={{0,1},{0,2},{0,6},{1,3},{1,4},{2,3},{2,5}};
 for(auto e:edges){g.children[e.first].push_back(e.second);g.parents[e.second].push_back(e.first);}for(Id c=0;c<7;++c){g.ancestors[c]=closure(c,g.parents);g.descendants[c]=closure(c,g.children);}
 Workload w;w.objects=8;w.terminal={1,3,4,3,2,5,6,0};w.postings.resize(7);for(Id i=0;i<8;++i)w.postings[w.terminal[i]].push_back(i);
 Vectors base;base.dim=2;base.data={0,0,1,0,2,0,0,2,3,0,0,3,4,4,5,5};float q[]={1,1};RawL2Space space(2);
 // Same shortest-BFS-depth/minimum-stable-ancestor rule as the production CLI.
 std::vector<size_t> depth(7,99);std::vector<Id> queue;for(Id c=0;c<7;++c)if(g.parents[c].empty()){depth[c]=0;queue.push_back(c);}for(size_t i=0;i<queue.size();++i)for(Id c:g.children[queue[i]])if(depth[c]>depth[queue[i]]+1){depth[c]=depth[queue[i]]+1;queue.push_back(c);}
 std::vector<Id> dense(7,99),concept_part(7),part_of(8);size_t np=0;for(Id c=0;c<7;++c){Id key=c;if(depth[c]>=1){key=99;for(Id a:g.ancestors[c])if(depth[a]==1)key=std::min(key,a);}require(key<7,"missing partition key");if(dense[key]==99)dense[key]=np++;concept_part[c]=dense[key];}for(Id i=0;i<8;++i)part_of[i]=concept_part[w.terminal[i]];
 require(np==4&&part_of==std::vector<Id>({1,1,1,1,2,2,3,0}),"fixture partition contract");
 hnswlib::HierarchicalNSW<float> global(&space,8,16,200);std::vector<std::unique_ptr<hnswlib::HierarchicalNSW<float>>> parts;for(Id b=0;b<4;++b)parts.push_back(std::make_unique<hnswlib::HierarchicalNSW<float>>(&space,std::count(part_of.begin(),part_of.end(),b),16,200));for(Id i=0;i<8;++i){global.addPoint(base.data.data()+2*i,i);parts[part_of[i]]->addPoint(base.data.data()+2*i,i);}
 auto snapshot=[&](){std::ostringstream o;o<<"{\"global\":"<<records(global,base)<<",\"partitions\":[";for(size_t b=0;b<4;++b){if(b)o<<',';o<<records(*parts[b],base);}o<<"]}";return o.str();};const std::string before=snapshot();
 std::vector<Bitmap> plan(7);plan[2]=reference_scope({2},g,w);require(bits(plan[2])=="01011100","initial bitmap");
 auto state=[&](){
     std::ostringstream o;
     const auto b=reference_scope({2},g,w),f=reference_scope({6},g,w);
     o<<"{\"P_F\":";array(o,w.postings[6]);
     o<<",\"P_B\":";array(o,w.postings[2]);
     o<<",\"S_F\":";array(o,ids(f));
     o<<",\"S_B\":";array(o,ids(b));
     o<<",\"M_B\":\""<<bits(plan[2])
      <<"\",\"derived_example_cardinalities\":{\"B\":"<<b.count()
      <<",\"F\":"<<f.count()<<",\"R\":"<<reference_scope({0},g,w).count()<<"}}";
     return o.str();
 };const std::string state_before=state();
 auto query=[&](const std::vector<Id>& u,bool cached){auto r=run_query(u,q,g,w,base,space,cached?&plan:nullptr,2,20,2,global,part_of,&parts);auto ref=reference_scope(u,g,w);require(r.scope.words==ref.words,"scope differs from raw-edge oracle");require(r.neighbors==oracle(ref,base,2),"query differs from brute squared-L2 oracle");std::ostringstream o;o<<"{\"concept_ids\":";array(o,u);o<<",\"materialized_B\":"<<(cached?"true":"false")<<",\"scope_ids\":";array(o,ids(r.scope));o<<",\"bitmap\":\""<<bits(r.scope)<<"\",\"eligible\":"<<r.eligible<<",\"path\":\""<<r.path<<"\",\"touched_partitions\":"<<r.touched<<",\"partition_eligible\":[";std::vector<size_t> counts(4);for(Id i:ids(ref))++counts[part_of[i]];for(size_t b=0;b<4;++b){if(b)o<<',';o<<counts[b];}o<<"],\"local_filtered_top2\":[";ScopeFilter filter(ref);for(size_t b=0;b<4;++b){if(b)o<<',';if(counts[b]&&std::string(r.path)=="graph"){auto local=search_graph(*parts[b],q,std::min<size_t>(2,counts[b]),std::min<size_t>(20,counts[b]),filter);Bitmap local_scope;local_scope.words={0};for(Id i:ids(ref))if(part_of[i]==b)local_scope.set(i);require(local==oracle(local_scope,base,2),"local search differs from oracle");pairs(o,local);}else o<<"[]";}o<<"],\"neighbors\":";pairs(o,r.neighbors);o<<'}';return o.str();};
 const auto online=query({2},false),cached=query({2},true),small=query({4},false),united=query({1,2},false);
 require(ids(reference_scope({2},g,w))==std::vector<Id>({1,3,4,5}),"B acceptance");require(ids(reference_scope({4},g,w))==std::vector<Id>({2}),"D acceptance");require(ids(reference_scope({1,2},g,w))==std::vector<Id>({0,1,2,3,4,5}),"union acceptance");
 // Logical-label/postings/cache edit only: never move the graph record or rebuild.
 auto old_anc=g.ancestors[w.terminal[6]];w.postings[6].erase(std::find(w.postings[6].begin(),w.postings[6].end(),6));w.terminal[6]=2;w.postings[2].push_back(6);plan[2].set(6);require(old_anc==std::vector<Id>({0,6})&&g.ancestors[2]==std::vector<Id>({0,2}),"ancestor delta acceptance");require(bits(plan[2])=="01011110"&&part_of[6]==3,"updated bitmap/location");
 const auto after_online=query({2},false),after_cached=query({2},true),state_after=state(),after=snapshot();require(before==after,"graph records/adjacency changed");
 std::ofstream o(out);require(bool(o),"cannot open output");o<<"{\n\"kind\":\"actual_core_didactic_proof_not_performance_data\",\n\"fixture_source\":\"data/dag-worked-example.json\",\n\"fixture_validation\":\"fixed constants transcribed from source; acceptance assertions checked; no loader shuffle\",\n\"bitmap_order\":\"o0 through o7 left-to-right, not numeric binary\",\n\"materialization\":\"B explicitly selected for illustration, not selected by budget heuristic\",\n\"counter_note\":\"illustrative maintained cardinalities only; production run_query computes scope.count at query time\",\n\"partition_of_object\":";array(o,part_of);o<<",\n\"B_online\":"<<online<<",\n\"B_materialized\":"<<cached<<",\n\"D_exact\":"<<small<<",\n\"A_or_B\":"<<united<<",\n\"before\":"<<state_before<<",\n\"after\":"<<state_after<<",\n\"B_after_online\":"<<after_online<<",\n\"B_after_materialized\":"<<after_cached<<",\n\"membership_changes\":{\"ancestor_drop\":[6],\"ancestor_add\":[2],\"ancestor_unchanged\":[0],\"cached_bitmap_bit_changes\":1,\"not_whole_update_time\":true},\n\"graph_snapshots_equal\":true,\n\"graph_before\":"<<before<<",\n\"graph_after\":"<<after<<"\n}\n";require(bool(o),"output write failed");return 0;
} catch(const std::exception& e){std::cerr<<"dag_worked_example: "<<e.what()<<'\n';return 1;}
