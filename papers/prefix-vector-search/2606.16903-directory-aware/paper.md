# 原始论文全文提取

来源：https://arxiv.org/pdf/2606.16903v1

提取日期：2026-10-19。PyMuPDF；公式与表格需回看原始 PDF。

## PDF page 1

Directory-Aware Query and Maintenance
in Vector Databases
Mengzhao Wang∗§, Zheng Gong†, Jingpei Hu‡, Jiajie Fu‡, Maojia Sheng‡, Junwen Chen‡, Yifan Zhu†
∗Hangzhou Dianzi University †Zhejiang University ‡ByteDance
wmzssy@hdu.edu.cn, {gongzheng kurt, xtf z}@zju.edu.cn,
{hujingpei, fujiajie.168, shengmaojia, chenjunwen}@bytedance.com
Abstract—Vector databases typically manage metadata as flat
scalar attributes, which limits their ability to express hierarchical
directory semantics commonly used to organize code repositories,
enterprise documents, and agent memories. As a result, directory-
scoped retrieval and structural updates are often implemented
as application-layer workarounds, making recursive scope res-
olution expensive and directory maintenance difficult to keep
consistent. This paper studies native directory semantics as a
first-class capability for vector databases. We formalize two core
operators: Directory-Semantic Query (DSQ) for hierarchically
scoped retrieval, and Directory-Semantic Maintenance (DSM) for
structural updates. We then evaluate three implementation strate-
gies: query-time path expansion (PE-ONLINE), ingestion-time
path expansion (PE-OFFLINE), and a Trie-based Hierarchical
Index (TRIEHI). Our analysis exposes the fundamental limita-
tions of expansion-based designs: flattening the hierarchy incurs
high recursive-query latency in PE-ONLINE and unscalable
write amplification during structural changes in both expansion
strategies. In contrast, TRIEHI keeps the directory topology as
a native prefix tree, enabling efficient recursive retrieval through
tree traversal and reducing maintenance cost through topological
node manipulation. We benchmark these design points within
ByteDance’s Viking vector search engine and release two large-
scale datasets1, WIKI-Dir and ARXIV-Dir, to support future
research on directory-semantic vector search. Finally, TRIEHI
has been integrated into OpenViking2, an open-source context
database for AI agents, where it supports filesystem-style context
organization and directory-recursive retrieval.
I. INTRODUCTION
Vector
databases
[1]–[6]
have
become
a
core
data-
management substrate for Retrieval-Augmented Generation
(RAG) and AI-agent systems. They allow Large Language
Models (LLMs) to retrieve external context from large cor-
pora, including enterprise knowledge bases [7], public article
archives such as Wikipedia [8] and arXiv [9], and code
repositories [10]. Since generation quality depends heavily on
the relevance and consistency of retrieved context [11]–[14],
the basic vector-database pipeline—embedding data, indexing
vectors, and executing k-nearest neighbor search—now sits
on the critical path of systems such as Claude Code [15],
OpenClaw [16], and Hermes [17].
As these systems move from document collections to oper-
ational knowledge bases, retrieval requires more than nearest-
neighbor similarity. The valid search space is often determined
1https://github.com/KurtPatrickHere/dir-vector-dataset
2https://github.com/volcengine/OpenViking
§Work done while working with ByteDance.
MineContext/opencontext/storage
/backends/sqlite_backend.py
(a) Code Repositories
Home/VectorDatabaseCloud/Quic
kStart/Getting...databases
(b) Corporate Wikis
Bytedance/Documents/Personal
/Education/Notes/...
(c) File Systems
Fig. 1: Knowledge bases with hierarchical directory structures.
by structural scope: a project directory, a versioned documen-
tation branch, a user-memory namespace, or an agent-skill
repository. Current vector databases address part of this need
through hybrid search and metadata filtering [18]–[21]. These
mechanisms combine dense retrieval with sparse keyword
matching [22]–[25] and scalar predicates [26]–[28], such as
category = “finance” or price < 100. They are effective for
flat attributes, but they do not model the hierarchical topology
that defines many real retrieval scopes.
Directory structures are the most common such topol-
ogy. Code repositories, corporate wikis, personal file sys-
tems, and agent context stores are all organized as nested
namespaces, as illustrated in Figure 1. In these settings, a
path is not merely a string-valued attribute. A file such as
/src/core/payment.py inherits meaning from its ances-
tors (e.g., /src/ and /src/core/), and a query scoped to
a directory must usually include or exclude entire subtrees.
Flattening paths into scalar metadata discards this topology.
The result is a recurring mismatch between semantic similarity
and contextual validity: a query for “architecture design” may
retrieve both current documents under /docs/v2.0/ and
obsolete documents under /archive/v1.0/, while relevant
material under /docs/ may be diluted by semantically sim-
ilar but irrelevant files under /logs/. These failures are not
ranking artifacts alone; they arise because the database lacks
native operators for hierarchical scope.
Today, directory semantics are usually implemented outside
the vector database. To search recursively under /src/, an
application first enumerates descendant paths from an external
metadata store and then submits a vector query with a large
disjunctive predicate, such as path IN [‘‘/src/a/’’,
...]. This workaround has two consequences. On the read
path, recursive scope resolution becomes an opaque pre-
processing step, preventing the database from co-optimizing
structural pruning with vector search and often inflating la-
arXiv:2606.16903v1  [cs.DB]  15 Jun 2026

## PDF page 2

tency. On the write path, directory mutations such as MOVE
or MERGE require coordinated updates across an external
directory store and vector metadata. A logical subtree oper-
ation degenerates into many document- or path-level updates,
causing write amplification and exposing consistency hazards.
These issues stem from keeping directory structure outside the
vector database.
This paper argues that directory semantics should be first-
class operations in vector databases. We formalize two prim-
itives. Directory-Semantic Query (DSQ) scopes vector search
by directory topology, supporting recursive subtree confine-
ment, non-recursive directory access, and branch exclusion.
Directory-Semantic Maintenance (DSM) captures structural
mutations, including moving and merging subtrees. Together,
DSQ and DSM provide the basic interface for expressing
directory-semantic operations over vector data.
We study this design space by building a native directory-
semantic module in ByteDance’s Viking vector search engine.
The module is decoupled from the underlying Approximate
Nearest Neighbor (ANN) index, allowing directory semantics
to be evaluated as a pluggable scope-resolution layer. We
implement and compare three strategies: Online Path Expan-
sion (PE-ONLINE), Offline Path Expansion (PE-OFFLINE),
and a native Trie-based Hierarchical Index (TRIEHI). The
expansion-based strategies expose the limits of flattening: for
recursive queries, PE-ONLINE pays high query-time expan-
sion cost, while PE-OFFLINE shifts the cost to ingestion. Both
remain vulnerable to subtree-wide maintenance amplification.
TRIEHI instead stores the hierarchy as a prefix tree, enabling
recursive scope resolution through tree traversal and structural
maintenance through tree-local operations.
TRIEHI has also been integrated into OpenViking2, an
open-source context database for AI agents. In OpenViking,
TRIEHI serves as the directory substrate for two core context-
management functions: organizing memories, resources, and
skills, and supporting directory-recursive retrieval that first
locates promising directories and then searches their relevant
descendants. Section IV-C describes this integration, and Sec-
tion V-F evaluates its effect on agent-memory and knowledge-
base Question Answering (QA) workloads.
Our main contributions can be summarized as follows:
• We formulate directory semantics as a first-class vector-
database abstraction. We define DSQ and DSM to capture
hierarchy-scoped retrieval and topology-preserving main-
tenance, clarifying the requirements that cannot be repre-
sented by flat metadata filters alone.
• We develop a pluggable directory-semantic layer in vector
databases and instantiate three designs: PE-ONLINE, PE-
OFFLINE, and TRIEHI. The first two capture the natural
expansion-based baselines, while TRIEHI preserves the
directory topology in a native prefix tree. We analyze their
costs and trade-offs across query processing, storage, and
structural maintenance.
• We construct and release WIKI-Dir and ARXIV-Dir,
two large-scale benchmarks with hierarchical directory
topologies1. Using these datasets, we evaluate the three de-
signs under recursive/non-recursive DSQ, DSM operations,
and indexing/storage overheads with both graph-based and
partition-based vector indexes.
• We integrate TRIEHI into the open-source OpenViking
context database, where it supports filesystem-style orga-
nization over memories, resources, and skills, as well as
directory-recursive retrieval. We evaluate this integration on
user-memory and knowledge-base QA workloads.
The rest of the paper is organized as follows. Section II
formalizes directory-semantic operations. Sections III and IV
present the expansion-based designs and TRIEHI, respectively.
Section V reports the experimental results. Section VI reviews
related work, and Section VII concludes the paper.
II. BACKGROUND
A. Vector Database Execution Model
Vector databases manage vectorized entries and execute
similarity search through ANN indexes. A query vector is
evaluated against an index such as a proximity graph (PG)
or an inverted file (IVF), and the system returns the top-
k entries by vector similarity. Modern vector databases [5],
[29], [30] also support metadata filtering, a scalar predicate is
resolved into a valid entry set, and the vector executor ranks
only candidates that satisfy the predicate.
This execution model is a natural substrate for directory
semantics. A directory constraint can be viewed as a scope
predicate that determines entries eligible for vector ranking.
The key difference from ordinary scalar filtering is that the
predicate is topological rather than flat: resolving a path may
require traversing ancestors, descendants, or sibling branches.
Our goal is therefore not to replace ANN indexes, but to
provide a directory-semantic scope-resolution layer that can
feed valid candidate sets to existing vector-search executors.
In our implementation, this layer is integrated into Viking, the
vector search engine underlying VikingDB [31], [32], which
provides the PG and IVF indexes used in our experiments.
B. Directory-Structured Knowledge Bases
Hierarchical directories are a common organization model
for human-generated and agent-managed knowledge [33]–
[35]. A path is an ordered sequence of segments, and
each
prefix
denotes
a
directory
scope.
For
example,
/src/core/payment.py is not merely a string identifier:
the prefixes /src/ and /src/core/ encode structural
context that distinguishes the entry from similarly named items
in other branches. Figure 2 shows a simplified enterprise
knowledge base used as a running example. The same ab-
straction applies to code repositories, file systems, and agent
context stores. Each leaf entry has a vector payload, while each
internal directory defines a scope over all entries in its subtree.
A directory-aware vector database must therefore support both
retrieval over such scopes and maintenance of the underlying
topology as the namespace evolves.

## PDF page 3

Directory-structured KB
/
HR/
overview.pdf
doc_1
Policies/
benefits.pdf
doc_2
Finance/
report_finance.pdf
doc_3
Legal/
report_legal.pdf
doc_4
Dept_A/
report_A.pdf
doc_5
OKR/
plan_A.pdf
doc_8
Dept_B/
report_B.pdf
doc_6
OKR/
plan_B.pdf
doc_9
Archive/
HR/
old_policy.pdf
doc_7
... legacy reports
Fig. 2: Example of a directory-structured knowledge base.
C. Directory-Semantic Operations
We define two classes of operations over directory-
structured vector data.
• Directory-Semantic Query (DSQ): read-path operations that
convert directory constraints into valid candidate scopes for
vector search.
• Directory-Semantic Maintenance (DSM): write-path opera-
tions that mutate the directory topology while preserving
the intended namespace semantics for subsequent DSQs.
DSQ Primitives. We focus on two base query primitives. A
recursive query searches all entries under a target directory,
including entries in descendant directories. For example, a
query anchored at /HR/ considers both doc_1 and doc_2; a
query anchored at /HR/Policies/ considers doc_2 while
excluding relevant entries outside that subtree, such as doc_7
under /Archive/HR/. A non-recursive query searches only
entries directly bound to the target directory. For instance, a
non-recursive query over /HR/ includes doc_1 but excludes
doc_2, which resides in a child directory.
These two primitives are sufficient to express common
derived operations. An exclusion query can be represented
by subtracting the recursive scope of a branch. Sibling and
ancestor queries can be reduced to combinations of directory
traversal and recursive/non-recursive scope resolution. Accord-
ingly, our design and evaluation focus on the two base DSQ
primitives as the core execution workload.
DSM Primitives. We use two structural mutations to charac-
terize maintenance cost. A directory move relocates an entire
subtree to a new parent. In Figure 2, moving /Dept_A/
under /Dept_B/ changes the namespace of Dept_A and
all descendants while preserving their internal structure. A
directory merge consolidates a source subtree into a target
subtree and reconciles name conflicts recursively. For example,
Write
doc_2
/HR/Policies/
benefits.pdf
PE-Online
PE-Offline
Entry catalog
doc_2 ->
/HR/Policies/
No expansion
exact
parent only
Parent-path index
add doc_2
to parent key
Aux. dir index
InsertKey
(/HR/Policies/)
Entry catalog
doc_2 ->
/HR/Policies/
Path expander
/, /HR/,
/HR/Policies/
Ancestor-path index
add doc_2
to all ancestor keys
Aux. dir index
InsertKey
(/HR/Policies/)
Fig. 3: Comparison of ingestion workflows. PE-ONLINE (top)
indexes only the exact parent path; PE-OFFLINE (bottom) pre-
computes and indexes all ancestral paths.
merging /Dept_A/ into /Dept_B/ transfers doc_5 into
the target branch and recursively reconciles the overlapping
OKR directories containing doc_8 and doc_9.
MOVE and MERGE capture the main challenge in directory
maintenance: a single logical namespace operation may af-
fect many descendants. Other lifecycle operations, such as
directory creation, deletion, and renaming, can be modeled as
simpler variants of these primitives. We therefore use MOVE
and MERGE as representative DSM workloads.
D. Design Requirements
A directory-semantic layer for vector databases must satisfy
four requirements. First, it should provide scope correctness:
DSQ must resolve the intended recursive or non-recursive di-
rectory scope before vector ranking. Second, it should provide
query efficiency: resolving a directory scope should not require
scanning or expanding a large subtree on every query. Third,
it should provide maintenance efficiency: structural mutations
should avoid rewriting every entry or materialized path in the
affected subtree whenever possible. Fourth, it should preserve
ANN-index independence: directory semantics should act as a
scope-resolution module that can work with different vector
indexes, rather than requiring changes to the ANN algorithm.
III. PATH EXPANSION STRATEGIES
Path expansion is the most direct way to implement direc-
tory semantics on top of a scalar-filtering vector database. The
hierarchy is represented by path strings, and a directory con-
straint is rewritten into one or more scalar path predicates. For
example, a recursive DSQ anchored at /HR/ can be expanded
into the path keys /HR/ and /HR/Policies/. This strategy
keeps the ANN index unchanged and places all directory-
specific logic in a metadata scope-resolution layer. The central
design choice is when to expand the directory scope: at query
time, by enumerating descendant paths, or at ingestion time,
by materializing ancestor memberships. We study these two
choices as Online Path Expansion (PE-ONLINE) and Offline
Path Expansion (PE-OFFLINE). Figures 3 and 4 illustrate the
corresponding ingestion and recursive-query workflows using
the running example in Figure 2.

## PDF page 4

Read
DSQ query
policy in /HR/
Query
parser
Vector embedding
Scope
resolution
ANN
search
Results
filter IDs
PE-Online
Path
/HR/
Aux. dir index
enumerate
Subtree keys
K: /HR/,
/HR/Policies/
Parent-path index
Get(K)
Candidate IDs
{doc_1, doc_2}
PE-Offline
Path
/HR/
Ancestor-path index
Get(/HR/)
Candidate IDs
{doc_1, doc_2}
Fig. 4: Recursive DSQ workflows. PE-ONLINE (top) uses
query-time path expansion and set union; PE-OFFLINE (bot-
tom) uses a single lookup for a pre-computed set.
A. Online Path Expansion (PE-ONLINE)
PE-ONLINE materializes only exact directory membership
and defers recursive expansion to query time. It is there-
fore a time-for-space design: ingestion and storage remain
lightweight, but recursive queries must dynamically enumerate
and combine the paths in the target subtree.
Storage and Indexing. The metadata layer maintains three
structures. First, an entry-to-directory mapping records the
logical parent directory of each vectorized entry. Second, a
parent-path inverted index maps each directory path key to
the set of entries stored directly under that directory. Third,
an auxiliary directory index stores all directory path keys and
supports prefix enumeration and direct-child lookup. These
structures are independent of the ANN index; they only
compute the valid candidate set supplied to vector search.
As shown in Figure 3 (top), when doc_2 is inserted under
/HR/Policies/, PE-ONLINE records its parent directory,
inserts doc_2 only into the parent-path posting list for
/HR/Policies/, and registers the path key in the auxiliary
directory index if it is new. No ancestor posting lists are
updated. Thus, each entry contributes one path posting.
DSQ. A DSQ first resolves the directory constraint into a
candidate entry set, and the ANN executor then ranks vectors
within that set. In PE-ONLINE, the resolution procedure
depends on whether the query is recursive.
Recursive Query. For a recursive DSQ anchored at path
p, PE-ONLINE enumerates all directory keys in the subtree
of p using the auxiliary directory index. In the running
example, a recursive DSQ over /HR/ returns the key set
K = {‘‘/HR/’’, ‘‘/HR/Policies/’’}. The system
then retrieves the path posting list for each key in K and
unions the resulting sets, yielding Set{doc_1, doc_2}. The
recursive scope of p is the union of path sets over all directory
keys in the subtree of p.
Let mq denote the number of directory keys returned by this
subtree enumeration. The metadata work is linear in mq: one
enumeration step that returns mq keys, up to mq path lookups,
and a union over the returned posting lists. Thus, PE-ONLINE
is sensitive to broad or shallow directory scopes.
Non-Recursive Query. A non-recursive DSQ over path p
does not expand the subtree. It directly looks up the path
posting list for p. For example, a non-recursive DSQ over
/HR/ returns Set{doc_1} and excludes doc_2 because
doc_2 is stored under the child directory /HR/Policies/.
This operation requires one path-key lookup plus the cost of
reading the corresponding posting list.
DSM. DSM operations in PE-ONLINE update the path-key
space maintained by the auxiliary and parent-path inverted
indexes. The important point is that the update is performed
at the directory-key level: entries remain attached to their
logical parent directories, but the scalar path keys used by
the metadata indexes must be remapped or merged when the
namespace changes. Therefore, the cost scales with the number
of affected directory keys, not necessarily with the number of
vectorized entries in the subtree.
Directory Move.
Consider
moving
/Dept_A/
under
/Dept_B/. PE-ONLINE first enumerates the affected source
keys, such as /Dept_A/ and /Dept_A/OKR/, from the
auxiliary directory index. It then computes the corresponding
target keys by replacing the source prefix with the destination
prefix. For each source-target key pair, the parent-path posting
list is remapped from the old key to the new key, and the
auxiliary directory index is updated by deleting the old key
and inserting the new one. After these updates, future DSQs
resolve the moved subtree through the new path keys.
The dominant metadata cost is O(mu), where mu is the
number of directory keys in the moved subtree. This includes
enumerating the affected keys, computing their target names,
remapping parent-path posting lists, and updating the auxiliary
directory index.
Directory Merge. To merge /Dept_A/ into /Dept_B/,
PE-ONLINE enumerates all source keys under /Dept_A/ and
computes their target keys under /Dept_B/. For each source-
target pair, the system retrieves the source posting list and the
existing target posting list, unions them when they share the
same path name, writes the merged posting list under the target
key, and removes the source key. In the running example, the
source key /Dept_A/OKR/ is merged into the target key
/Dept_B/OKR/, combining {doc_8} and {doc_9}.
The merge also performs O(mu) metadata updates over
source directory keys, with additional set-union work for
target-key conflicts. This makes MERGE no cheaper than
MOVE, and potentially more expensive when many source
paths collide with existing target paths.
B. Offline Path Expansion (PE-OFFLINE)
PE-OFFLINE makes the opposite design choice. Instead of
expanding a subtree at query time, it materializes ancestor
memberships during ingestion. Each entry is indexed not only
under its exact parent directory, but also under every ancestor
directory. This is a space-for-time design: recursive DSQ can
be resolved by one path-key lookup, while ingestion, storage,
non-recursive DSQ, and DSM become more expensive.
Storage and Indexing. The metadata layer remains inde-
pendent of the ANN index. It maintains: (1) an entry-to-

## PDF page 5

TABLE I: Performance trade-offs of expansion-based designs. mq is the number of directory keys in the queried subtree, mu
is the number of directory keys in the mutated subtree, t is the relevant directory depth or affected-ancestor bound, and c is
the number of immediate child directories.
Aspect
PE-ONLINE
(Query-Time Expansion)
PE-OFFLINE
(Ingestion-Time Expansion)
Index Storage
Low: one parent posting list per entry
High: t ancestor posting lists per entry
Ingestion Work
Low: one posting update plus one auxiliary key update
High: t posting updates plus one auxiliary key update
Recursive DSQ
High metadata work: subtree enumeration, up to mq posting list
lookups, and set union
Low metadata work: one parent-path posting list lookup
Non-Recursive DSQ
Low metadata work: one parent-path posting list lookup
High metadata work: one parent-path posting list lookup, c child-path
posting list lookups, and set difference
MOVE
High maintenance work: O(mu) path-key remapping
High maintenance work: O(mu) path-key remapping plus O(t)
ancestor updates
MERGE
High maintenance work: O(mu) path-key merging, plus conflict
unions
High maintenance work: O(mu) path-key merging plus O(t)
ancestor updates, plus conflict unions
directory mapping for the logical parent directory of each
vectorized entry; (2) an auxiliary directory index over all
directory path keys, used for direct-child lookup and DSM
path-key enumeration; (3) a path expander that maps an exact
parent path to its ancestor path sequence; and (4) an ancestor-
materialized inverted index mapping each directory key to the
set of entries located at or below that directory.
Figure 3 (bottom) illustrates the ingestion of doc_2 under
/HR/Policies/. The path expander generates the ances-
tor sequence [‘‘/’’, ‘‘/HR/’’, ‘‘/HR/Policies/’’],
and doc_2 is inserted into the posting list of each key.
The auxiliary directory index registers the exact parent path
if it is new. Therefore, an entry at depth t contributes t
ancestor postings. This replication is what makes recursive
DSQ resolution cheap in metadata lookups, but it also means
high-level directory keys, such as ‘‘/’’, materialize large
descendant sets.
DSQ. As before, DSQ first resolves a directory constraint into
a candidate entry set and then passes the set to the ANN
executor. Because PE-OFFLINE stores ancestor posting lists,
recursive and non-recursive DSQ have opposite cost profiles.
Recursive Query. A recursive DSQ over path p is resolved
by a single lookup of the ancestor posting list for p. For ex-
ample, querying /HR/ directly returns Set{doc_1, doc_2},
because both entries were inserted into the /HR/ posting list
at ingestion.
Non-Recursive Query. For a non-recursive DSQ, the exact-
parent entries under p must be separated from descendant
entries already materialized in p’s posting list. PE-OFFLINE
retrieves the posting list SetT otal for p, enumerates the direct
child directory keys of p from the auxiliary directory index,
retrieves their posting lists, unions them into SetChildren,
and computes SetT otal \ SetChildren. For /HR/, this gives
Set{doc_1, doc_2} \ Set{doc_2} = Set{doc_1}. If c is
the number of direct child directories, scope resolution requires
one lookup for p, one child enumeration, up to c child lookups,
and the corresponding union/difference operations.
DSM. PE-OFFLINE must maintain two kinds of metadata
during DSM: the path keys for directories inside the mutated
subtree, and the ancestor posting lists outside the subtree. This
second requirement is the main difference from PE-ONLINE.
When a subtree changes parent, the aggregate entry set of
the subtree must be removed from ancestor directories that no
longer contain it and added to ancestor directories that newly
contain it.
Directory Move. PE-OFFLINE handles a move, such as
relocating /Dept_A/ under /Dept_B/, in two coordinated
steps. First, it enumerates the mu directory keys in the moved
subtree and computes their new keys by prefix substitution.
The posting list associated with each subtree key is remapped
from the old key to the new key, and the auxiliary directory
index is updated accordingly. Second, it updates ancestor
memberships outside the moved subtree. Let S be the aggre-
gate posting list of the moved subtree root, and let A−and A+
denote the old-only and new-only proper ancestor sets after
removing common proper ancestors. The system removes S
from posting lists in A−and adds S to posting lists in A+.
Common proper ancestors are left unchanged because they
contain the subtree before and after the move. The posting list
of the moved subtree root is not updated through this ancestor-
membership step; it is carried from the old root key to the new
root key during path-key remapping.
The metadata cost has two components: O(mu) path-
key remapping for the moved subtree and O(t) ancestor-
membership updates, where t bounds the number of affected
ancestors.
Directory Merge. A merge, such as merging /Dept_A/
into /Dept_B/, combines the source subtree with an existing
target subtree. PE-OFFLINE enumerates all source directory
keys, computes their target keys, and processes each source-
target pair. If the target key does not exist, the source ma-
terialized posting list can be moved to the target key. If the
target key already exists, the source and target posting lists are
unioned and stored under the target key, after which the source
key is removed from metadata indexes. In the running exam-
ple, /Dept_A/OKR/ maps to the existing /Dept_B/OKR/
key, so their posting lists are merged.
The ancestor-membership update follows the same rule as
MOVE: the aggregate source set is removed from old-only
proper ancestors and added to new-only proper ancestors,
while common proper ancestors remain unchanged. The target
root itself is updated by the source-target path-key merge
described above. The total metadata work is O(mu+t) plus the
cost of set unions for target-key conflicts. This makes MERGE

## PDF page 6

at least as expensive as MOVE under path expansion, and often
more expensive when many source keys collide with existing
target keys.
C. Analysis of Expansion-Based Solutions
Table I summarizes the consequence of implementing di-
rectory semantics through scalar path expansion. The main
advantage of this approach is compatibility: both variants
can reuse a vector database’s existing scalar-filtering interface
without modifying the ANN index. However, because the
hierarchy is encoded only as scalar path strings, the metadata
layer has no native subtree object to reuse across DSQ and
DSM. Directory membership must therefore be reconstructed
at query time or pre-materialized at ingestion time.
PE-ONLINE chooses query-time expansion. This keeps
storage and ingestion compact, but makes recursive DSQ
sensitive to the directory cardinality of the queried subtree.
PE-OFFLINE chooses ingestion-time expansion. This makes
recursive DSQ cheap to resolve in the metadata layer, but
introduces ancestor-level write amplification and turns non-
recursive DSQ into a set-difference operation over direct child
subtrees. Thus, the two schemes expose a clear latency–
space–ingestion trade-off. More importantly, both schemes
inherit the same maintenance bottleneck. A structural mutation
changes the scalar namespace used to encode a subtree, so
the affected path-key range must be remapped or merged.
Ancestor materialization further requires updates to proper an-
cestor posting lists in PE-OFFLINE. Consequently, expansion-
based designs can express directory semantics, but they do so
through indirect metadata rewrites whose cost remains tied to
the mutated subtree size.
IV. TRIE-BASED HIERARCHICAL INDEX
We next describe the data structures and operations of
TRIEHI, analyze its operational profile, and then discuss its
deployment in OpenViking.
A. TRIEHI Design
Section III shows that scalar path expansion can express
directory semantics, but it treats the directory hierarchy as
a collection of path keys. This representation is convenient
for reuse of scalar filtering, yet it does not provide a native
object for a directory subtree. As a result, recursive scope
resolution must be supported either by query-time descendant
enumeration in PE-ONLINE or by ancestor materialization
in PE-OFFLINE, and structural updates still require path-
key remapping or merging. Trie-based Hierarchical Index
(TRIEHI) takes a different physical design: it keeps the
directory topology as a native prefix tree and uses the tree
nodes as reusable scope objects for DSQ and DSM.
TRIEHI remains a metadata index layered above the ANN
executor. It does not change vector ranking or the layout of
the underlying vector index. Given a directory constraint, it
resolves the corresponding candidate entry-ID set and passes
that set to the vector executor, following the execution model
/
Inc={1,...,9}
HR
Inc={1,2}
Finance
Inc={3}
Legal
Inc={4}
Dept_A
Inc={5,8}
Dept_B
Inc={6,9}
Archive
Inc={7}
Policies
Inc={2}
OKR
Inc={8}
OKR
Inc={9}
HR
Inc={7}
Fig. 5: The TRIEHI structure for the running example.
in Section II. Its main design goal is to replace subtree-wide
path-key manipulation with tree-local navigation.
Storage and Indexing. TRIEHI represents each directory as
a TrieNode. The root node corresponds to /, and each edge is
labeled by one path segment. As illustrated in Figure 5, a node
maintains the following fields: (1) segment, the local directory
name; (2) children, a hash map from child segment names
to child nodes; (3) parent, a pointer to the parent node; and
(4) inclusive ids, the set of vectorized entry identifiers whose
logical directory lies at the node or in one of its descendants.
An entry identifier denotes the logical payload indexed by the
vector database.
For notation, let Inc(v) denote the inclusive ids field of
node v, let Local(v) denote the entries directly bound to v,
and let Child(v) denote the immediate child nodes of v. The
invariant maintained by TRIEHI is:
Inc(v) = Local(v) ∪
[
w∈Child(v)
Inc(w).
(1)
Thus, a node’s inclusive set contains both entries directly
stored at that directory and entries aggregated from its child
subtrees. This invariant makes a directory node a reusable
materialized scope. A recursive DSQ can use the aggregate
set stored at the target node, while DSM operations update
only the ancestor aggregates whose descendant membership
changes. A separate entry-to-node catalog records the log-
ical parent directory of each vectorized entry and supports
namespace-level bookkeeping.
Ingestion follows the trie topology. To insert an entry such
as doc_8 under /Dept_A/OKR/, TRIEHI first traverses the
path from the root, creating missing nodes for Dept_A or
OKR if necessary. It then registers the entry with the terminal
node and adds the entry ID to inclusive ids along the terminal
node and its ancestors. Thus, ingestion performs O(t) node
visits and O(t) aggregate-set updates for a path of depth t.
The cost is comparable in form to ancestor materialization in
PE-OFFLINE, but the hierarchy itself is represented as nodes
and pointers rather than scalar path keys.
DSQ. TRIEHI resolves DSQ predicates by path traversal
followed by set access or set difference.
Recursive Query. For a recursive DSQ anchored at path p,
TRIEHI traverses the trie from the root to the node represent-
ing p. It then reads inclusive ids from that node. For example,
a recursive DSQ over /Dept_A/ resolves to Set{doc_5,

## PDF page 7

doc_8} in the running example. The metadata access pattern
is a path traversal of length t plus one aggregate-set access;
vector ranking is then performed within the resolved candidate
set. Unlike PE-ONLINE, no descendant path enumeration is
required at query time.
Non-Recursive Query. A non-recursive DSQ over path p
should include entries directly bound to p but exclude entries
in child directories. TRIEHI computes this scope from the
aggregate invariant. It retrieves SetT otal = Inc(p), unions the
Inc(·) sets of the immediate child nodes into SetChildren, and
returns SetT otal \ SetChildren. For /Dept_A/, this yields
Set{doc_5, doc_8} \ Set{doc_8} = Set{doc_5}. If c is
the number of immediate children, the metadata work consists
of one path traversal, up to c child-set accesses, and the
corresponding union/difference operations.
Derived Path Patterns. The prefix-tree representation also
provides a natural execution substrate for derived path pat-
terns, such as prefix-constrained or wildcard path segments.
A wildcard segment can be evaluated by matching child
names at the corresponding level and continuing traversal only
along matching branches. This is a structural advantage over
scanning flat path strings. Formalizing and evaluating these
derived path predicates is a natural extension of the DSQ
model and is left to future work.
DSM. DSM operations in TRIEHI update two states: the tree
topology and the inclusive ids aggregates required for future
DSQs. The important difference from expansion-based designs
is that a subtree has a stable node identity. A namespace
mutation can therefore relink the subtree root instead of
regenerating scalar path keys for every descendant directory.
The aggregate sets still need to be updated along the ancestor
chains whose descendant membership changes.
Directory Move.
Consider
moving
/Dept_A/
to
/Dept_B/. TRIEHI locates the source node s, its old
parent, and the new parent. Let S
=
Inc(s) contain
the entries directly bound to the moved root s and its
descendants. The system computes the old-only and new-only
proper ancestor chains after removing common ancestors. It
removes S from inclusive ids on old-only ancestors and adds
S to inclusive ids on new-only ancestors. Finally, the source
node is removed from the old parent’s children map, inserted
into the new parent’s children map, and assigned the new
parent pointer.
This operation visits nodes on the source and destination
paths and updates aggregate sets on affected ancestors. It is
independent of the number of descendant directory keys.
Directory Merge. MERGE reconciles a source subtree with
an existing target subtree. We describe the common case where
the source and target roots are disjoint. Let s be the source
root and d be the target root. TRIEHI first uses S = Inc(s)
to update ancestor aggregates: S is removed from old-only
proper ancestors of s and added to d and the new-only proper
ancestors of d, while common ancestors remain unchanged. It
then reconciles the topology below s and d. A non-conflicting
source child can be relinked directly under the target node by
TABLE II: Operational profile of TRIEHI: t denotes directory
depth, c is the number of immediate child directories, and r
is the number of reconciled source nodes in MERGE.
Aspect
TRIEHI
Index Storage
Trie topology plus per-node inclusive entry-ID sets
Ingestion Work
O(t) node visits and aggregate-set updates
Recursive DSQ
O(t) node visits plus one aggregate-set access
Non-Recursive DSQ
O(t + c) metadata accesses plus union/difference
MOVE
O(t) node visits and ancestor aggregate updates
MERGE
O(t + r) node work plus conflict-dependent set unions
updating one parent pointer and the corresponding child maps.
If a source child and target child have the same directory name,
TRIEHI recursively merges that child pair.
The merge cost has a depth-dependent component for locat-
ing the source and target roots and updating their ancestor ag-
gregates, plus a conflict-dependent component for recursively
reconciling overlapping branches. Let r denote the number of
source nodes visited by this recursive reconciliation. In the
worst case, r can approach the number of directory nodes in
the source subtree; in common cases with few name conflicts,
non-conflicting child subtrees are relinked as whole units.
The distinction from path expansion is that TRIEHI performs
conflict handling through node-level recursive merging: non-
conflicting subtrees are relinked as units, and conflicting
branches are reconciled on trie nodes rather than by remapping
every descendant path key.
Consistency During Updates. DSM updates in TRIEHI pre-
serve the aggregate invariant in Equation 1 by applying each
namespace mutation within its affected trie region. Before
a mutation, TRIEHI identifies that region: move covers the
source subtree and destination path, and merge covers the
source and target subtrees. Mutations on overlapping paths are
serialized. Within one mutation, TRIEHI follows a fixed order:
collect the affected entry set, change the parent/child links,
refresh the entry-to-node catalog and ancestor aggregates, and
remove obsolete namespace bindings after relocation. After
this sequence, topology, catalog entries, and inclusive ids re-
main consistent. DSQ reads candidate sets from this metadata,
while the ANN executor only receives the resolved set.
B. Analysis of TRIEHI
Table II summarizes the operational profile of TRIEHI.
Compared with scalar path expansion, the key change is that
a directory subtree is represented by a node identity rather
than by all materialized descendant path keys. This directly
benefits recursive DSQ and MOVE: recursive DSQ resolves
a node aggregate after path traversal, and MOVE relinks the
subtree root while updating only affected ancestor aggregates.
The node-centric representation reduces subtree-wide path-key
maintenance, but it introduces other metadata costs. Ingestion
updates aggregates along the ancestor chain, and storage
includes both the trie topology and per-node inclusive entry-ID
sets. Non-recursive DSQ computes a local set difference be-
tween the target node and its immediate child subtrees. MERGE
depends on the number of conflicting source nodes that require
recursive reconciliation. Overall, these costs are tied to tree

## PDF page 8

Context namespace
Tiered entries
viking://
resources/
my_project/
docs/
api/
src/
user/
memories/
preferences/
writing_style
agent/
skills/
search_code
instructions/
viking://resources/my_project/
.abstract
L0
.overview
L1
docs/
.abstract
L0
.overview
L1
api/
.abstract
L0
.overview
L1
auth.md
L2
endpoints.md
L2
src/
Fig.
6:
TrieHI-backed
OpenViking
namespaces.
Left:
filesystem-style context organization. Right: tiered context
entries under the same hierarchy, where L0 denotes abstracts,
L1 denotes overviews, and L2 denotes full content.
navigation, ancestor updates, and actual merge conflicts, rather
than the expansion-based maintenance pattern of remapping
every descendant path key in the affected subtree.
C. Deployment in OpenViking
OpenViking2 is a context database for AI agents that
organizes memories, resources, and skills through a virtual
filesystem. It requires both frequent namespace maintenance
and low-latency recursive retrieval. Memories are created,
consolidated, archived, and attached to projects or skills, while
resource subtrees may be reorganized as a workspace evolves.
Queries often start from a directory scope and request coherent
descendants, such as a project subtree or a user-memory
branch. PE-ONLINE would enumerate descendants on this re-
trieval path; PE-OFFLINE avoids query-time enumeration but
amplifies namespace maintenance. TRIEHI fits this workload
by representing directory scopes as reusable trie nodes and
expressing context reorganization as subtree-local topology
updates. We integrate TRIEHI into this namespace to support
directory-scoped context management and retrieval.
Figure 6 illustrates two views of the same TRIEHI-backed
directory hierarchy in OpenViking. The first is a filesystem-
style organization over different context types. The second
stores tiered context entries: L0 abstracts support lightweight
identification, L1 overviews expose directory-level summaries,
and L2 entries keep the original content. We next discuss the
three supported capabilities, summarized in Table III.
Filesystem-Style Context Organization. OpenViking orga-
nizes memories, resources, and skills as viking:// URIs
in a hierarchical virtual filesystem. In the TRIEHI abstraction,
URI segments define directory nodes, and the same hierarchy
serves both browsing and retrieval. For example, recursive
context acquisition maps to recursive DSQ, while URI-subtree
reorganization maps to DSM.
Tiered Context Loading. OpenViking stores L0 abstracts,
L1 overviews, and L2 full entries under shared directory
scopes. In the TRIEHI abstraction, these entries are associated
with their directory scopes. A retrieval pipeline can therefore
first operate on lightweight entries and then obtain detailed
descendants when additional evidence is required.
Directory-Recursive Retrieval. OpenViking supports re-
trieval over viking:// directory scopes. Given a scoped
query, TRIEHI first resolves the requested directory as can-
didate entries; semantic vector scoring then ranks candidates
within that scope for focused retrieval or aggregation. This
realizes the directory scope needed to retrieve coherent sur-
rounding context without scanning flat path strings.
V. EXPERIMENTS
This section evaluates the directory-semantic strategies in-
troduced in Sections III and IV. The experiments are organized
around four questions.
• RQ1: Can the designs resolve recursive and non-recursive
directory scopes without dominating vector-search latency?
• RQ2: How do the designs behave when logical namespace
mutations affect large subtrees?
• RQ3: What indexing-time and storage overheads are intro-
duced by directory-semantic metadata?
• RQ4: When TRIEHI is instantiated in OpenViking, do
directory-scoped retrieval primitives improve realistic agent-
memory and knowledge-base QA workloads?
The first three questions are answered on WIKI-Dir and
ARXIV-Dir, two directory-structured evaluation datasets con-
structed for DSQ and DSM. The last question is addressed
by an end-to-end OpenViking evaluation, which measures the
integrated context-database system.
A. Experimental Setup
Datasets. Since existing datasets do not expose directory-
semantic operations, we construct two large-scale datasets1.
WIKI-Dir. WIKI-Dir is derived from a Wikipedia corpus
with queries and relevance labels from DBpedia [36]. We add
a hierarchical namespace by crawling the Wikipedia category
hierarchy3 for each article. Since Wikipedia categories form
a directed acyclic graph, we construct a canonical tree by
selecting one category path per entry and discarding invalid
links. The resulting hierarchy contains 363,467 directories
with an average depth of 11.95. The final dataset contains
1.94 million vectorized entries and 456 test queries with
directory constraints at varying depths. Entries are encoded
into 1024-dimensional vectors using bge-m3 [37]. We also
generate 1,000 MOVE operations and 1,000 MERGE operations
for structural maintenance evaluation.
ARXIV-Dir. ARXIV-Dir is built from 2.76 million arXiv
paper abstracts. Each entry is associated with two independent
hierarchical namespaces derived from official arXiv meta-
data4: a subject hierarchy, such as /cs/AI/, and a temporal
hierarchy, such as /2024/05/. The combined namespace
3https://en.wikipedia.org/wiki/Category:Main topic articles
4https://arxiv.org/category taxonomy

## PDF page 9

TABLE III: OpenViking capabilities realized by TRIEHI.
OpenViking capability
TRIEHI role
Directory-semantic operation
Filesystem-style organization
viking:// URI segments are represented as directory nodes in
the TRIEHI abstraction.
Hierarchical namespace navigation; DSM for namespace
changes.
Tiered context loading
L0/L1/L2 entries are associated with shared directory scopes in the
TRIEHI abstraction.
DSQ selects summary or full-detail entries under the same
scope.
Directory-recursive retrieval
TRIEHI resolves the requested directory scope and descendant
candidates before vector ranking.
Recursive DSQ supplies scoped candidates for vector
ranking and aggregation.
contains 168 subject directories with average depth 2.19 and
432 temporal directories with average depth 1.92. Abstracts are
encoded into 1024-dimensional vectors using mxb-ai-embed-
large-v1 [38]. We sample 1,000 queries from the abstracts and
generate directory constraints for each query. Ground truth is
computed by brute-force search over the entries satisfying the
corresponding constraints.
Compared Designs. We compare the three designs analyzed
in Sections III and IV. PE-ONLINE stores only the immediate
path key and expands descendant paths at query time. PE-
OFFLINE materializes ancestor memberships during ingestion
so that recursive scopes can be resolved by a materialized path
key. TRIEHI stores the hierarchy as a prefix tree and uses
directory nodes as reusable scope objects. All three designs
are integrated with two ANN executors in the Viking engine: a
graph-based proximity graph (PG) index and a partition-based
inverted file (IVF) index. This lets us distinguish directory-
semantic overhead from ANN-index behavior.
Metrics. For DSQ, we report retrieval quality using nDCG@k
or Recall@k, together with end-to-end query latency. We also
report directory-only latency, defined as the time required
to resolve a directory predicate into a candidate entry-ID
set before ANN ranking. For DSM, we measure wall-clock
latency of MOVE and MERGE. For resource overhead, we report
total index construction time and index size, including the
baseline vector index and the directory-semantic module.
Implementation Details. Entry-ID sets are represented by
Roaring bitmaps [39], enabling compressed set union, in-
tersection, and difference. The expansion-based methods use
path-key metadata structures for scalar path lookup; TRIEHI
uses trie nodes and inclusive entry-ID sets as described in
Section IV. All methods maintain a common catalog that maps
each entry to its current directory representation, such as a path
key or a trie node, for maintenance. Because this catalog is
required by every design, we exclude it when comparing DSM
latency and directory-module indexing overhead; the reported
differences therefore reflect the design-specific metadata struc-
tures and update procedures. The experiments run on a Linux
server with a 48-core Intel(R) Xeon(R) Silver 4310 CPU @
2.10GHz and 125GB RAM. The implementations are in C++
and integrated into ByteDance’s Viking vector search engine.
B. Directory-Semantic Query Performance
We first evaluate the two base DSQ primitives: recursive
and non-recursive scope queries.
Recursive DSQ. Figure 7 shows that PE-OFFLINE and
TRIEHI provide the favorable accuracy-latency trade-off for
200
400
600
800
Mean Latency (ms)
0.50
0.52
nDCG@10
92
93
0.484
0.486
PE-Online
PE-Offline
TrieHI
(a) PG, WIKI-Dir
200
400
600
800
Mean Latency (ms)
0.50
0.51
0.52
nDCG@10
71.5
71.8
0.4925
0.4927
PE-Online
PE-Offline
TrieHI
(b) IVF, WIKI-Dir
50
100
150
200
250
Mean Latency (ms)
0.94
0.95
0.96
Recall@100
30
35
0.940
0.945
PE-Online
PE-Offline
TrieHI
(c) PG, ARXIV-Dir
100
150
200
250
300
Mean Latency (ms)
0.94
0.96
0.98
Recall@100
82.5 85.0
0.938
0.940
PE-Online
PE-Offline
TrieHI
(d) IVF, ARXIV-Dir
Fig. 7: Recursive DSQ performance: retrieval quality versus
mean query latency.
1500
2000
2500
3000
Mean Latency (ms)
0.96
0.97
0.98
Recall@10
1288
1296
0.9590
0.9593
PE-Online
PE-Offline
TrieHI
(a) PG, WIKI-Dir
100
200
300
400
Mean Latency (ms)
0.6
0.8
Recall@10
55
60
0.44
0.46
PE-Online
PE-Offline
TrieHI
(b) IVF, WIKI-Dir
100
200
300
400
500
Mean Latency (ms)
0.96
0.97
0.98
Recall@100
50.0
50.5
0.96075
0.96100
PE-Online
PE-Offline
TrieHI
(c) PG, ARXIV-Dir
100
200
300
Mean Latency (ms)
0.925
0.950
0.975
Recall@100
78.1 78.3
0.9125
0.9130
PE-Online
PE-Offline
TrieHI
(d) IVF, ARXIV-Dir
Fig. 8: Non-recursive DSQ performance: retrieval quality
versus mean query latency.
recursive retrieval. This matches the analysis in Sections III-B
and IV-B: PE-OFFLINE resolves a recursive scope through a
materialized ancestor key, while TRIEHI reaches the target
directory by path traversal and reads the node aggregate. Both
avoid enumerating descendant path keys at query time. Ta-
ble IV isolates this effect. On WIKI-Dir, where shallow direc-
tories can contain very large subtrees, PE-ONLINE spends 365
ms on directory-only scope resolution, whereas PE-OFFLINE
and TRIEHI require only 235–273 µs. The same pattern holds
on ARXIV-Dir, although the hierarchy is shallower and the
absolute expansion cost is smaller. Thus, query-time path
expansion is the main scalability bottleneck for recursive DSQ.
Non-Recursive DSQ. Non-recursive retrieval exhibits the
opposite local trade-off. A non-recursive query asks for entries
directly bound to a directory, so PE-ONLINE can resolve
the scope by a direct path-key lookup. PE-OFFLINE and
TRIEHI compute the same scope by subtracting child-subtree
aggregates, which introduces set-difference work proportional
to immediate fan-out. Table IV reports this overhead: on

## PDF page 10

TABLE IV: Directory-only latency (µs) for candidate entry-ID set generation. The lowest latency in each group is bolded.
Query
Type ↓
Dataset→
WIKI-Dir
ARXIV-Dir
Method↓
Mean
P90
P95
P99
P99.9
Mean
P90
P95
P99
P99.9
Recur.
PE-ONLINE
365,000.80
669,527.67
748,949.23
913,704.33
2,654,322.00
1,089.20
5,241.70
5,261.33
5,292.37
5,380.60
PE-OFFLINE
235.23
628.8
805.47
1,332.00
4,233.83
0.12
0.27
0.47
0.93
1.43
TRIEHI
273.10
667.13
878.33
1,526.20
4,004.53
0.43
0.70
1.03
2.80
3.43
Non-Recur.
PE-ONLINE
29.07
60.67
118.27
361.57
993.47
4.35
15.43
21.70
28.50
31.17
PE-OFFLINE
1,037.07
3,029.43
3,935.93
5,987.97
8,228.40
163.91
25.00
34.70
3,548.13
3,594.70
TRIEHI
945.17
2,467.57
3,419.33
5,097.20
7,528.43
112.49
21.17
1,568.23
1,980.40
2,094.00
0
200
400
600
800
Operation Index
10
1
10
0
10
1
10
2
Latency (ms)
PE-Online
PE-Offline
TrieHI
(a) MOVE
0
200
400
600
800
Operation Index
10
1
10
0
10
1
Latency (ms)
PE-Online
PE-Offline
TrieHI
(b) MERGE
Fig. 9: Wall-clock latency for DSM operations.
WIKI-Dir, PE-ONLINE needs 29 µs on average, while PE-
OFFLINE and TRIEHI are near 1 ms. However, Figure 8 shows
that this metadata cost remains small relative to end-to-end
vector search. The result clarifies the trade-off: TRIEHI is not
designed to minimize the simplest non-recursive lookup alone;
it aims to provide efficient recursive scopes while keeping non-
recursive overhead bounded.
C. Directory-Semantic Maintenance
Figure 9 evaluates DSM operations. TRIEHI avoids subtree-
wide path-key rewriting for moves and limits merge work to
the branches that must be reconciled. A MOVE relinks the
source subtree and updates affected ancestor aggregates. Its
metadata work is determined by the source and destination
paths, plus bitmap updates on the moved entry set, rather than
by the number of descendant directory keys. A MERGE may
recursively visit conflicting branches, but non-conflicting child
subtrees can be transferred as node units. The expansion-based
designs expose the cost of representing topology as scalar
path keys. Moving or merging a directory requires updating
the path-key representation of affected descendants or their
materialized ancestor memberships. As the subtree grows,
these updates become increasingly expensive and produce
high latency variance. The DSM results therefore provide
the main evidence for the native-tree design: TRIEHI does
not merely accelerate recursive scope resolution; it represents
each directory as a trie node, so structural mutations can
be expressed as subtree relinking, conflict-local merging, and
ancestor-aggregate updates.
1
2
4
9
Path Depth
10
2
10
4
Path Counts
4
4
5
8
236903
193209
175177
86501
83
39
72
114
Input Path
Expanded Sub-Path
Direct Child-Path
Fig. 10: WIKI-Dir structural complexity by path depth. “Input
Path” is the number of average query anchor directories at that
depth; “Expanded Sub-Path” (m) is the number of descendant
directory keys expanded from those anchors; “Direct Child-
Path” (c) is the number of immediate child directories used
for non-recursive set difference.
PE-Online
PE-Offline
TrieHI
nDCG
1
2
4
9
Path Depth
10
1
10
2
Mean Latency (ms)
0.44
0.46
0.48
nDCG@10
(a) PG
1
2
4
9
Path Depth
0
250
500
750
Mean Latency (ms)
0.46
0.48
nDCG@10
(b) IVF
Fig. 11: Effect of query path depth on recursive DSQ perfor-
mance on WIKI-Dir.
D. Indexing and Storage Overhead
Table V reports resource overhead. All directory-semantic
methods add only slight construction-time overhead relative to
the vector-index baseline. On WIKI-Dir with PG, the baseline
takes 1491.27s, while the three directory-aware variants take
1502.21–1515.25s, an increase of less than 1.7%.
The storage results show the expected differences among
the directory representations. PE-ONLINE has the smallest
footprint because it stores only immediate path memberships.
PE-OFFLINE spends additional space on ancestor materializa-
tion; this cost is more visible on WIKI-Dir, whose paths are
deeper. TRIEHI stores trie topology and per-node inclusive
entry-ID sets, giving it the largest storage overhead in both
datasets. This overhead is the price paid for reusable directory
scopes and tree-local maintenance. The results therefore do not
identify a single universally dominant representation; rather,
they show that TRIEHI trades additional metadata space for
robust recursive DSQ and efficient DSM.
E. Depth Sensitivity and Latency Breakdown
The depth analysis connects the empirical results to the cost
variables in Sections III and IV. Figure 10 shows that shallow
WIKI-Dir paths contain very large descendant subtrees, while
immediate fan-out remains comparatively bounded. This struc-
ture is precisely where query-time expansion is vulnerable.

## PDF page 11

TABLE V: Index construction time and size, measured with the same baseline vector index.
Dataset→
WIKI-Dir
ARXIV-Dir
Metric→
Indexing Time (s)
Index Size (MB)
Indexing Time (s)
Index Size (MB)
Vector Index→
PG
IVF
PG
IVF
PG
IVF
PG
IVF
Baseline (Vec.)
1491.27
442.32
7,925
7,744
720.13
199.94
11,227
10,964
PE-ONLINE (Vec.+Dir.)
1502.49
453.53
8,143
7,962
729.07
208.89
11,239
10,976
PE-OFFLINE (Vec.+Dir.)
1,515.25
466.29
8,213
8,032
733.62
213.43
11,241
10,978
TRIEHI (Vec.+Dir.)
1,502.21
453.25
8,326
8,145
729.60
209.41
11,519
11,256
0%
20%
40%
60%
80%
100%
Percentage of Latency
1
2
4
9
Path Depth
39.0%
23.4%
37.6%
37.1%
23.3%
39.6%
36.3%
23.1%
40.6%
30.5%
21.8%
47.7%
100.0%
99.8%
99.5%
98.0%
99.7%
99.1%
97.4%
11.1%
88.9%
568493.5 s
483666.4 s
452210.7 s
255260.2 s
309.0 s
329.3 s
295.3 s
220.4 s
314.4 s
344.5 s
295.5 s
238.8 s
PE-Online
Sub-Path Obtain
PE-Offline
Bitmap Fetch
TrieHI
Bitmap Compute
(a) Recursive Query
0%
20%
40%
60%
80%
100%
Percentage of Latency
1
2
4
9
Path Depth
100.0%
6.2%
93.8%
5.8%
94.2%
15.5%
84.5%
97.7%
96.8%
94.1%
96.7%
99.6%
99.2%
98.7%
98.8%
1.8 s
1.6 s
4.0 s
9.7 s
1839.5 s
858.1 s
961.4 s
3159.3 s
1651.7 s
766.5 s
908.6 s
2433.4 s
PE-Online
Child-Path Obtain
PE-Offline
Bitmap Fetch
TrieHI
Bitmap Compute
(b) Non-Recursive Query
Fig. 12: Directory-only latency decomposition on WIKI-Dir
by query path depth.
Figure 11 shows that retrieval quality generally improves
as the query path becomes more specific, confirming that
directory scope carries useful retrieval context. The latency
trends separate the designs. PE-ONLINE is slowest at shallow
depths because it must enumerate many descendant paths.
As depth increases and the descendant scope narrows, its
overhead falls. PE-OFFLINE and TRIEHI avoid this query-
time expansion. After scope resolution, the remaining latency
is determined by how the ANN executor searches within the
resolved candidate set. For PG, their latency increases with
depth because deeper directory constraints are more selective:
the candidate mask reduces the density of valid nodes in PG
and weakens graph connectivity, so the search may perform
more traversal work to collect valid results. For IVF, PE-
OFFLINE and TRIEHI show a flatter latency profile: partition
probing dominates the end-to-end cost, and directory-scope
resolution adds little depth-dependent overhead. Thus, PG and
IVF exhibit different latency curves. Across both executors,
the key requirement is to avoid descendant-path enumeration;
executor-specific latency then depends on how PG or IVF
consumes the resolved candidate set.
Figure 12 decomposes directory-only latency. For recur-
sive DSQ, PE-ONLINE follows the expected descendant-
enumeration profile: shallow anchors expand to many descen-
dant keys, so “Sub-Path Obtain” and “Bitmap Fetch” domi-
nate, and latency decreases as the descendant scope narrows
with depth. PE-OFFLINE and TRIEHI avoid this enumeration;
recursive scope resolution becomes an ancestor-materialized
lookup or a trie traversal plus aggregate-set access, keeping
directory-only latency in the hundreds of microseconds. For
non-recursive DSQ, the pattern reverses. PE-ONLINE uses a
direct exact-parent lookup, whereas PE-OFFLINE and TRIEHI
exclude immediate child subtrees through union and set differ-
ence, shown as “Bitmap Compute”. Because immediate fan-
out is much smaller and more stable than the full descendant
scope, this extra work remains predictable.
F. End-to-End Evaluation in OpenViking
We now evaluate the application effect of TRIEHI in Open-
Viking on agent-context and knowledge-base QA workloads.
Evaluation
Setup.
LOCOMO
[40]
evaluates
long-
conversation
user-memory
QA
for
OpenClaw
[16],
Hermes [17], and Claude Code [15], comparing each native-
memory baseline with its OpenViking variant. HotpotQA [41]
evaluates
multi-hop
knowledge-base
QA
against
direct
retrieval baselines, including Naive RAG, HippoRAG 2 [42],
[43], and LightRAG [44]; OpenViking is measured with top-5
and top-20 retrieval budgets. The benchmark uses an LLM-as-
judge protocol with doubao-seed-2-0-pro-260215;
detailed configurations are available in the OpenViking
benchmark repository5. Accuracy denotes judged answer
correctness. Avg. Query Time and Latency measure retrieval
or memory-access time per question. Total Input Tokens sums
input tokens over LOCOMO queries, while Tokens/QA is the
average input-token cost per HotpotQA question.
User Memory. Table VI evaluates long-conversation user
memory on the LOCOMO dataset. Across OpenClaw, Her-
mes, and Claude Code, integrating OpenViking improves QA
accuracy and reduces both average query time and total input-
token usage. TRIEHI supplies the scope-resolution step: a
query first resolves the relevant user or session scope, and
vector ranking then operates over that scoped candidate set.
Together with L0/L1/L2 context levels, this avoids repeatedly
injecting broad conversation history into the prompt while still
allowing recursive access to detailed evidence when needed.
Multi-Hop Knowledge-Base QA. HotpotQA requires ev-
idence from multiple facts, making it a natural test for
5https://github.com/volcengine/OpenViking/tree/main/benchmark

## PDF page 12

TABLE VI: User-memory QA performance on LOCOMO.
Integration
Accuracy
Avg. Query
Time (s)
Total Input
Tokens
OpenClaw native memory
24.20%
95.14
392,559,404
OpenClaw + OpenViking
82.08%
38.8
37,423,456
Hermes native memory
33.38%
82.4
79,228,398
Hermes + OpenViking
82.86%
27.9
52,026,755
Claude Code auto-memory
57.21%
49.1
353,306,422
Claude Code + OpenViking
80.32%
20.4
129,968,899
TABLE VII: Multi-hop RAG performance on HotpotQA.
Method
Accuracy
Tokens / QA
Latency (s) / QA
Naive RAG
62.50%
1,290
0.11
HippoRAG 2
61.00%
726
20
LightRAG
89.00%
28,443
75
OpenViking (top-5)
72.75%
3,154
0.22
OpenViking (top-20)
91.00%
12,533
0.23
directory-scoped retrieval. In Table VII, OpenViking top-20
obtains 91.00% accuracy with 0.23s retrieval latency per query.
Compared with LightRAG, it achieves higher accuracy, uses
fewer tokens per query, and avoids the high retrieval latency
observed in graph-based pipelines. This behavior matches the
DSQ design: TRIEHI resolves a coherent directory scope be-
fore vector ranking, so increasing the top-k budget mainly adds
related in-scope evidence rather than unrelated corpus-wide
passages. The top-5/top-20 comparison shows this coverage
effect while retrieval latency remains low.
VI. RELATED WORK
This work is related to ANN indexing, filtered vector search,
hybrid retrieval, and hierarchical knowledge organization.
ANN Indexing and Vector Databases. Approximate near-
est neighbor (ANN) search has a long line of work on
reducing vector-search latency, memory footprint, and con-
struction cost. Representative techniques include tree-based
partitioning [45]–[47], hashing [48]–[51], quantization [52]–
[55], and graph-based indexes such as HNSW [56]–[63].
Recent systems further optimize memory access [6], [64],
[65], distance computation [66]–[69], construction [70], [71],
disk-resident search [72]–[74], and dynamic updates [75]–
[77]. These techniques improve how vectors are ranked once
a candidate space is defined. Our work addresses a different
layer: how a hierarchical directory constraint is resolved into
the candidate entry set supplied to the ANN executor. Our
strategies are independent of the ANN layout and can work
with both graph-based and partition-based vector indexes, as
evaluated in Section V.
Filtered Vector Search. Vector databases commonly combine
ANN search with scalar predicates. Decoupled systems and
optimizers [27]–[29], [78]–[81] separate predicate evaluation
from vector indexing and choose execution strategies ac-
cording to predicate selectivity [82]–[85]. Other work builds
coupled indexes for specific predicate forms, such as tags [80],
[86], [87] and ranges [88]–[91], or for broader hybrid filtering
settings [92]–[95]. Directory constraints can be approximated
with scalar path filters, but existing vector databases typically
treat paths as flat attributes rather than hierarchy-aware con-
straints. Exact-parent predicates can be evaluated by equality
filtering, while recursive scopes and namespace mutations
must be encoded through additional metadata expansion or
application-side coordination. Sections III and IV formalize
this design space inside the vector-database layer: PE-ONLINE
and PE-OFFLINE study expansion-based support for DSQ and
DSM, while TRIEHI provides a native prefix-tree representa-
tion for reusable directory scopes and structural maintenance.
Hybrid Retrieval. Hybrid retrieval combines dense semantic
search with sparse lexical retrieval to improve ranking quality
in retrieval pipeline [22], [96]–[101]. Existing work studies
rank fusion, including Reciprocal Rank Fusion and convex
combinations [23], [102], as well as joint dense-sparse index-
ing [24], [25], [101]. These methods decide how to rank or
fuse evidence after retrieval signals are produced. Directory
semantics solve a different problem: they define which entries
are eligible for retrieval under a hierarchical scope. A DSQ
can first resolve the valid candidate set for a directory subtree,
after which dense, sparse, or hybrid retrieval can rank within
that set. This makes our approach complementary to relevance-
oriented hybrid retrieval rather than a replacement for it.
Hierarchical Knowledge Organization. Directories and hi-
erarchical namespaces are widely used to organize code
repositories, enterprise knowledge bases, personal files, and
agent-managed context [7], [10], [33]–[35]. RAG and agent
systems increasingly rely on such structure to manage ex-
ternal context [16], [103], but the hierarchy is typically
maintained as application-side metadata. Recursive retrieval
then becomes an external preprocessing step, and namespace
updates are coordinated outside the vector database. Our work
brings this organization model into the vector-database layer.
DSQ captures directory-scoped retrieval, and DSM captures
topology-preserving updates. The OpenViking integration in
Section IV-C illustrates how the same abstraction supports
filesystem-style context organization and directory-recursive
retrieval in an agent context database.
VII. CONCLUSION
This paper studies directory semantics as a first-class
capability for vector databases. We formalized Directory-
Semantic Query (DSQ) and Directory-Semantic Maintenance
(DSM), analyzed two expansion-based implementations, and
introduced TRIEHI, a native prefix-tree metadata index that
preserves directory topology above the ANN executor. The
evaluation on WIKI-Dir and ARXIV-Dir shows the core trade-
off: path expansion can reuse scalar metadata interfaces but
incurs high expansion cost and subtree-wide maintenance am-
plification, whereas TRIEHI resolves recursive scopes through
directory nodes and supports structural updates through tree-
local operations. The OpenViking integration further shows
that this abstraction can support filesystem-style context orga-
nization and directory-recursive retrieval in agent-memory and
knowledge-base QA workloads. Future work includes extend-
ing DSQ to richer path predicates and cost-aware planning for
mixed semantic and hierarchical constraints.

## PDF page 13

REFERENCES
[1] J. J. Pan, J. Wang, and G. Li, “Survey of vector database management
systems,” The VLDB Journal, vol. 33, no. 5, pp. 1591–1615, 2024.
[2] C. Chen, C. Jin, Y. Zhang, S. Podolsky, C. Wu, S. Wang, E. Hanson,
Z. Sun, R. Walzer, and J. Wang, “Singlestore-v: An integrated vector
database system in singlestore,” Proc. VLDB Endow., vol. 17, no. 12,
pp. 3772–3785, 2024.
[3] Y. Deng, Z. You, L. Xiang, Q. Li, P. Yuan, Z. Hong, Y. Zheng, W. Li,
R. Li, H. Liu, K. Mouratidis, M. L. Yiu, H. Li, Q. Shen, R. Mao, and
B. Tang, “Alayadb: The data foundation for efficient and effective long-
context LLM inference,” in Companion of the International Conference
on Management of Data (SIGMOD), 2025, pp. 364–377.
[4] G. Hu, S. Cai, T. T. A. Dinh, Z. Xie, C. Yue, G. Chen, and B. C.
Ooi, “HAKES: scalable vector database for embedding search service,”
Proc. VLDB Endow., vol. 18, no. 9, pp. 3049–3062, 2025.
[5] J. Pound, F. Chabert, A. Bhushan, A. Goswami, A. Pacaci, and
S. R. Chowdhury, “Micronn: An on-device disk-resident updatable
vector database,” in Companion of the International Conference on
Management of Data (SIGMOD), 2025, pp. 608–621.
[6] X. Zhong, H. Li, J. Jin, M. Yang, D. Chu, X. Wang, Z. Shen, W. Jia,
G. Gu, Y. Xie, X. Lin, H. T. Shen, J. Song, and P. Cheng, “VSAG:
an optimized search framework for graph-based approximate nearest
neighbor search,” Proc. VLDB Endow., vol. 18, no. 12, pp. 5017–5030,
2025.
[7] F. Jiang, C. Qin, K. Yao, C. Fang, F. Zhuang, H. Zhu, and H. Xiong,
“Enhancing question answering for enterprise knowledge bases using
large language models,” in Database Systems for Advanced Applica-
tions - International Conference (DASFAA), vol. 14853, 2024, pp. 273–
290.
[8] M. Liu, S. Zhong, Q. Yang, Y. Han, X. Liu, and Y. Ma, “Webanns: Fast
and efficient approximate nearest neighbor search in web browsers,” in
Proceedings of the International ACM SIGIR Conference on Research
and Development in Information Retrieval, 2025, pp. 2483–2492.
[9] S. S. Monir, I. Lau, S. Yang, and D. Zhao, “Vectorsearch: Enhancing
document retrieval with semantic embeddings and optimized search,”
arXiv:2409.17383, 2024.
[10] J. Chen, X. Hu, Z. Li, C. Gao, X. Xia, and D. Lo, “Code search
is all you need? improving code suggestions with code search,” in
Proceedings of the IEEE/ACM International Conference on Software
Engineering (ICSE), 2024, pp. 73:1–73:13.
[11] X. Zhao, X. Zhou, and G. Li, “Chat2data: An interactive data analysis
system with rag, vector databases and llms,” Proc. VLDB Endow.,
vol. 17, no. 12, pp. 4481–4484, 2024.
[12] Z. Jing, Y. Su, Y. Han, B. Yuan, H. Xu, C. Liu, K. Chen, and
M. Zhang, “When large language models meet vector databases: A
survey,” arXiv:2402.01763, 2024.
[13] S. Chen, J. Fan, B. Wu, N. Tang, C. Deng, P. Wang, Y. Li, J. Tan,
F. Li, J. Zhou, and X. Du, “Automatic database configuration debugging
using retrieval-augmented language models,” Proc. ACM Manag. Data,
vol. 3, no. 1, pp. 13:1–13:27, 2025.
[14] M. Wang, H. Wu, X. Ke, Y. Gao, X. Xu, and L. Chen, “An interactive
multi-modal query answering system with retrieval-augmented large
language models,” Proc. VLDB Endow., vol. 17, no. 12, pp. 4333–
4336, 2024.
[15] “Claude code,” https://github.com/anthropics/claude-code, 2026, [On-
line; accessed 05-May-2026].
[16] “Openclaw — personal ai assistant,” https://github.com/openclaw/
openclaw, 2026, [Online; accessed 01-February-2026].
[17] “Hermes agent,” https://github.com/nousresearch/hermes-agent, 2026,
[Online; accessed 05-May-2026].
[18] “A
review
of
hybrid
search
in
milvus,”
https://zilliz.com/blog/
a-review-of-hybrid-search-in-milvus, 2025, [Online; accessed 20-
September-2025].
[19] “Hybrid search,” https://turbopuffer.com/docs/hybrid, 2025, [Online;
accessed 20-September-2025].
[20] “Getting started with hybrid search,” https://www.pinecone.io/learn/
hybrid-search-intro/, 2023, [Online; accessed 06-February-2025].
[21] “Hybrid
search
explained,”
https://weaviate.io/blog/
hybrid-search-explained, 2025, [Online; accessed 06-February-2025].
[22] S. E. Robertson, S. Walker, S. Jones, M. Hancock-Beaulieu, and
M. Gatford, “Okapi at TREC-3,” in Proceedings of The Third Text
REtrieval Conference (TREC), vol. 500-225, 1994, pp. 109–126.
[23] S. Bruch, S. Gai, and A. Ingber, “An analysis of fusion functions for
hybrid retrieval,” ACM Trans. Inf. Syst., vol. 42, no. 1, pp. 20:1–20:35,
2024.
[24] H. Zhang, J. Liu, Z. Zhu, S. Zeng, M. Sheng, T. Yang, G. Dai,
and Y. Wang, “Efficient and effective retrieval of dense-sparse hy-
brid vectors using graph-based approximate nearest neighbor search,”
arXiv:2410.20381, 2024.
[25] Y. Chen, R. Zheng, Q. Chen, S. Xu, Q. Zhang, X. Wu, W. Han, H. Yuan,
M. Li, Y. Wang, J. Li, F. Yang, H. Sun, W. Deng, F. Sun, Q. Zhang, and
M. Yang, “Onesparse: A unified system for multi-index vector search,”
in Companion Proceedings of the ACM on Web Conference (WWW),
2024, pp. 393–402.
[26] Q. Zhang, S. Xu, Q. Chen, G. Sui, J. Xie, Z. Cai, Y. Chen, Y. He,
Y. Yang, F. Yang, M. Yang, and L. Zhou, “VBASE: unifying online vec-
tor similarity search and relational queries via relaxed monotonicity,” in
USENIX Symposium on Operating Systems Design and Implementation
(OSDI), 2023, pp. 377–395.
[27] J. Mohoney, A. Pacaci, S. R. Chowdhury, A. Mousavi, I. F. Ilyas,
U. F. Minhas, J. Pound, and T. Rekatsinas, “High-throughput vector
similarity search in knowledge graphs,” Proc. ACM Manag. Data,
vol. 1, no. 2, pp. 197:1–197:25, 2023.
[28] C. Wei, B. Wu, S. Wang, R. Lou, C. Zhan, F. Li, and Y. Cai,
“Analyticdb-v: A hybrid analytical engine towards query fusion for
structured and unstructured data,” Proc. VLDB Endow., vol. 13, no. 12,
pp. 3152–3165, 2020.
[29] J. Wang, X. Yi, R. Guo, H. Jin, P. Xu, S. Li, X. Wang, X. Guo, C. Li,
X. Xu et al., “Milvus: A purpose-built vector data management sys-
tem,” in Proceedings of the International Conference on Management
of Data (SIGMOD), 2021, pp. 2614–2627.
[30] “Vector search,” https://opensearch.org/platform/vector-search/, 2025,
[Online; accessed 11-September-2025].
[31] “Intelligent
vector
infrastructure,”
https://go.byteplus.com/
Vikingdbvectordatabase,
2025,
[Online;
accessed
10-November-
2025].
[32] “Vikingdb
vector
database,”
https://www.byteplus.com/en/product/
vectordatabase, 2025, [Online; accessed 10-November-2025].
[33] O. Lynch and M. Lohmayer, “Directories: A convenient and well-
behaved formalism for hierarchical organization in categorical systems
theory,” arXiv:2504.19389, 2025.
[34] G. Jacobson, B. Krishnamurthy, D. Srivastava, and D. Suciu, “Focusing
search in hierarchical structures with directory sets,” in Proceedings
of the ACM CIKM International Conference on Information and
Knowledge Management, 1998, pp. 1–9.
[35] R. Lutz, D. Rausch, F. Beck, and S. Diehl, “Get your directories
right: From hierarchy visualization to hierarchy manipulation,” in
IEEE Symposium on Visual Languages and Human-Centric Computing
(VL/HCC), 2014, pp. 25–32.
[36] F. Hasibi, F. Nikolaev, C. Xiong, K. Balog, S. E. Bratsberg, A. Kotov,
and J. Callan, “Dbpedia-entity v2: A test collection for entity search,” in
Proceedings of the International ACM SIGIR Conference on Research
and Development in Information Retrieval, 2017, pp. 1265–1268.
[37] J. Chen, S. Xiao, P. Zhang, K. Luo, D. Lian, and Z. Liu, “BGE m3-
embedding: Multi-lingual, multi-functionality, multi-granularity text
embeddings through self-knowledge distillation,” arXiv:2402.03216,
2024.
[38] S. Lee, A. Shakir, D. Koenig, and J. Lipp. (2024) Open source
strikes bread - new fluffy embeddings model. [Online]. Available:
https://www.mixedbread.ai/blog/mxbai-embed-large-v1
[39] S. Chambi, D. Lemire, O. Kaser, and R. Godin, “Better bitmap
performance with roaring bitmaps,” Software: Practice and Experience,
vol. 46, no. 5, pp. 709–719, 2016.
[40] A. Maharana, D. Lee, S. Tulyakov, M. Bansal, F. Barbieri, and Y. Fang,
“Evaluating very long-term conversational memory of LLM agents,”
in Proceedings of the 62nd Annual Meeting of the Association for
Computational Linguistics (ACL), 2024, pp. 13 851–13 870.
[41] Z. Yang, P. Qi, S. Zhang, Y. Bengio, W. W. Cohen, R. Salakhutdinov,
and C. D. Manning, “Hotpotqa: A dataset for diverse, explainable
multi-hop question answering,” in Proceedings of the 2018 Conference
on Empirical Methods in Natural Language Processing (EMNLP),
2018, pp. 2369–2380.
[42] B. J. Guti´errez, Y. Shu, W. Qi, S. Zhou, and Y. Su, “From rag
to memory: Non-parametric continual learning for large language
models,” in International Conference on Machine Learning (ICML),
2025, pp. 21 497–21 515.

## PDF page 14

[43] B. J. Guti´errez, Y. Shu, Y. Gu, M. Yasunaga, and Y. Su, “Hipporag:
Neurobiologically inspired long-term memory for large language mod-
els,” Advances in neural information processing systems (NeurIPS),
vol. 37, pp. 59 532–59 569, 2024.
[44] Z. Guo, L. Xia, Y. Yu, T. Ao, and C. Huang, “LightRAG: Simple and
fast retrieval-augmented generation,” in Findings of the Association for
Computational Linguistics (EMNLP).
Association for Computational
Linguistics, 2025, pp. 10 746–10 761.
[45] P. Ram and K. Sinha, “Revisiting kd-tree for nearest neighbor search,”
in Proceedings of the ACM SIGKDD International Conference on
Knowledge Discovery & Data Mining, 2019, pp. 1378–1388.
[46] S. Dasgupta and Y. Freund, “Random projection trees and low dimen-
sional manifolds,” in Proceedings of the Annual ACM Symposium on
Theory of Computing (STOC), 2008, pp. 537–546.
[47] J. Wang, N. Wang, Y. Jia, J. Li, G. Zeng, H. Zha, and X. Hua, “Trinary-
projection trees for approximate nearest neighbor search,” IEEE Trans.
Pattern Anal. Mach. Intell., vol. 36, no. 2, pp. 388–403, 2014.
[48] A. Gionis, P. Indyk, and R. Motwani, “Similarity search in high
dimensions via hashing,” in Proceedings of International Conference
on Very Large Data Bases (VLDB), 1999, pp. 518–529.
[49] M. Li, Y. Zhang, Y. Sun, W. Wang, I. W. Tsang, and X. Lin,
“I/O efficient approximate nearest neighbour search based on learned
functions,” in IEEE International Conference on Data Engineering
(ICDE), 2020, pp. 289–300.
[50] B. Zheng, Z. Xi, L. Weng, N. Q. V. Hung, H. Liu, and C. S. Jensen,
“Pm-lsh: A fast and accurate lsh framework for high-dimensional
approximate nn search,” Proc. VLDB Endow., vol. 13, no. 5, pp. 643–
655, 2020.
[51] Q. Huang, J. Feng, Y. Zhang, Q. Fang, and W. Ng, “Query-aware
locality-sensitive hashing for approximate nearest neighbor search,”
Proc. VLDB Endow., vol. 9, no. 1, pp. 1–12, 2015.
[52] T. Zhang, C. Du, and J. Wang, “Composite quantization for approx-
imate nearest neighbor search,” in Proceedings of the International
Conference on Machine Learning (ICML), vol. 32, 2014, pp. 838–846.
[53] J. Gao and C. Long, “Rabitq: Quantizing high-dimensional vectors with
a theoretical error bound for approximate nearest neighbor search,”
Proc. ACM Manag. Data, vol. 2, no. 3, p. 167, 2024.
[54] H. J´egou, M. Douze, and C. Schmid, “Product quantization for nearest
neighbor search,” IEEE Trans. Pattern Anal. Mach. Intell., vol. 33,
no. 1, pp. 117–128, 2011.
[55] T. Ge, K. He, Q. Ke, and J. Sun, “Optimized product quantization
for approximate nearest neighbor search,” in Proceedings of the IEEE
Conference on Computer Vision and Pattern Recognition (CVPR),
2013, pp. 2946–2953.
[56] Y. A. Malkov and D. A. Yashunin, “Efficient and robust approxi-
mate nearest neighbor search using hierarchical navigable small world
graphs,” IEEE Trans. Pattern Anal. Mach. Intell., vol. 42, no. 4, pp.
824–836, 2020.
[57] M. Wang, X. Xu, Q. Yue, and Y. Wang, “A comprehensive survey and
experimental comparison of graph-based approximate nearest neighbor
search,” Proc. VLDB Endow., vol. 14, no. 11, pp. 1964–1978, 2021.
[58] C. Fu, C. Xiang, C. Wang, and D. Cai, “Fast approximate nearest
neighbor search with the navigating spreading-out graph,” Proc. VLDB
Endow., vol. 12, no. 5, pp. 461–474, 2019.
[59] T. Chen, C. Fu, K. Wang, X. Ke, Y. Gao, W. Zhou, Y. Ni, and
A. Zeng, “Maximum inner product is query-scaled nearest neighbor,”
arXiv:2503.06882, 2025.
[60] Y. Peng, B. Choi, T. N. Chan, J. Yang, and J. Xu, “Efficient approximate
nearest neighbor search in multi-dimensional databases,” Proc. ACM
Manag. Data, vol. 1, no. 1, pp. 54:1–54:27, 2023.
[61] H. Wang, W. Wu, C. Luo, A. Bian, C. Meng, Y. Wu, and J. Sun,
“Boosting accuracy and efficiency for vector retrieval with local
scaling graph,” in IEEE International Conference on Data Engineering
(ICDE), 2025, pp. 336–348.
[62] Y. Fu, C. Chen, Y. Chen, W.-F. Wong, and B. He, “Vista: Vector
indexing and search for large-scale imbalanced datasets,” in 2025 IEEE
41st International Conference on Data Engineering (ICDE), 2025, pp.
543–556.
[63] B. Li, X. Yan, and S. Lu, “Fast-convergent proximity graphs for
approximate nearest neighbor search,” arXiv:2510.05975, 2025.
[64] M. Wang, H. Wu, X. Ke, Y. Gao, Y. Zhu, and W. Zhou, “Accelerating
graph indexing for ANNS on modern cpus,” Proc. ACM Manag. Data,
vol. 3, no. 3, pp. 123:1–123:29, 2025.
[65] Y. Gou, J. Gao, Y. Xu, and C. Long, “Symphonyqg: Towards sympho-
nious integration of quantization and graph for approximate nearest
neighbor search,” Proc. ACM Manag. Data, vol. 3, no. 1, pp. 80:1–
80:26, 2025.
[66] J. Gao and C. Long, “High-dimensional approximate nearest neighbor
search: with reliable and efficient distance comparison operations,”
Proc. ACM Manag. Data, vol. 1, no. 2, pp. 137:1–137:27, 2023.
[67] M. Yang, W. Li, J. Jin, X. Zhong, X. Wang, Z. Shen, W. Jia, and
W. Wang, “Effective and general distance computation for approximate
nearest neighbor search,” in IEEE International Conference on Data
Engineering (ICDE), 2025, pp. 1098–1110.
[68] L. Deng, P. Chen, X. Zeng, T. Wang, Y. Zhao, and K. Zheng, “Effi-
cient data-aware distance comparison operations for high-dimensional
approximate nearest neighbor search,” Proc. VLDB Endow., vol. 18,
no. 3, pp. 812–821, 2024.
[69] C. Li, M. Zhang, D. G. Andersen, and Y. He, “Improving approximate
nearest neighbor search through learned adaptive early termination,” in
Proceedings of the 2020 International Conference on Management of
Data (SIGMOD), 2020, pp. 2539–2554.
[70] S. Yang, J. Xie, Y. Liu, J. X. Yu, X. Gao, Q. Wang, Y. Peng, and
J. Cui, “Revisiting the index construction of proximity graph-based
approximate nearest neighbor search,” Proc. VLDB Endow., vol. 18,
no. 6, pp. 1825–1838, 2025.
[71] Z. Li, X. Ke, Y. Zhu, B. Yu, B. Zheng, and Y. Gao, “Scalable
graph indexing using gpus for approximate nearest neighbor search,”
arXiv:2508.08744, 2025.
[72] Z. Yue, B. Zheng, L. Xu, K. Xu, S. Zhang, Y. Du, Y. Gao, X. Zhou,
and C. S. Jensen, “Select edges wisely: Monotonic path aware graph
layout optimization for disk-based ANN search,” Proc. VLDB Endow.,
vol. 18, no. 11, pp. 4337–4349, 2025.
[73] M. Wang, W. Xu, X. Yi, S. Wu, Z. Peng, X. Ke, Y. Gao, X. Xu,
R. Guo, and C. Xie, “Starling: An i/o-efficient disk-resident graph
index framework for high-dimensional vector similarity search on data
segment,” Proc. ACM Manag. Data, vol. 2, no. 1, pp. 14:1–14:27, 2024.
[74] S. J. Subramanya, Devvrit, H. V. Simhadri, R. Krishnaswamy, and
R. Kadekodi, “Diskann: Fast accurate billion-point nearest neighbor
search on a single node,” in Advances in Neural Information Processing
Systems (NeurIPS), 2019, pp. 13 748–13 758.
[75] D. Liu, B. Zheng, Z. Yue, F. Ruan, X. Zhou, and C. S. Jensen,
“Wolverine: Highly efficient monotonic search path repair for graph-
based ANN index updates,” Proc. VLDB Endow., vol. 18, no. 7, pp.
2268–2280, 2025.
[76] R. Ma, Y. Zhu, B. Zheng, L. Chen, C. Ge, and Y. Gao, “GTI: graph-
based tree index with logarithm updates for nearest neighbor search
in high-dimensional spaces,” Proc. VLDB Endow., vol. 18, no. 4, pp.
986–999, 2024.
[77] H. Xu, M. D. Manohar, P. A. Bernstein, B. Chandramouli, R. Wen,
and H. V. Simhadri, “In-place updates of a graph index for streaming
approximate nearest neighbor search,” arXiv:2502.13826, 2025.
[78] C. Ye, X. Yan, and E. Lo, “Compass: General filtered search across
vector and structured data,” arXiv:2510.27141, 2025.
[79] G. Sehgal and S. Salihoglu, “Navix: A native vector index design for
graph dbmss with robust predicate-agnostic search performance,” Proc.
VLDB Endow., vol. 18, no. 11, pp. 4438–4450, 2025.
[80] L. Patel, P. Kraft, C. Guestrin, and M. Zaharia, “ACORN: performant
and predicate-agnostic search over vector embeddings and structured
data,” Proc. ACM Manag. Data, vol. 2, no. 3, p. 120, 2024.
[81] Z. Li, S. Huang, W. Ding, Y. Park, and J. Chen, “SIEVE: effective
filtered vector search with collection of indexes,” Proc. VLDB Endow.,
vol. 18, no. 11, pp. 4723–4736, 2025.
[82] M. Li, X. Yan, B. Lu, Y. Zhang, J. Cheng, and C. Ma, “Attribute filter-
ing in approximate nearest neighbor search: An in-depth experimental
study,” arXiv:2508.16263, 2025.
[83] P. Iff, P. Bruegger, M. Chrapek, M. Besta, and T. Hoefler, “Bench-
marking filtered approximate nearest neighbor search algorithms on
transformer-based embedding vectors,” arXiv:2507.21989, 2025.
[84] Y. Lin, K. Zhang, Z. He, Y. Jing, and X. S. Wang, “Survey of filtered
approximate nearest neighbor search over the vector-scalar hybrid
data,” arXiv:2505.06501, 2025.
[85] J. Shi, Y. Cai, and W. Zheng, “Filtered approximate nearest neigh-
bor search: A unified benchmark and systematic experimental study
[experiment, analysis & benchmark],” arXiv:2509.07789, 2025.
[86] J. Luo, M. Qiao, C. Zuo, and D. Deng, “Tag-filtered approximate
nearest neighbor search,” in IEEE International Conference on Data
Engineering (ICDE), 2025, pp. 3642–3654.

## PDF page 15

[87] S. Gollapudi, N. Karia, V. Sivashankar, R. Krishnaswamy, N. Begwani,
S. Raz, Y. Lin, Y. Zhang, N. Mahapatro, P. Srinivasan, A. Singh, and
H. V. Simhadri, “Filtered-diskann: Graph algorithms for approximate
nearest neighbor search with filters,” in Proceedings of the ACM Web
Conference (WWW), 2023, pp. 3406–3416.
[88] A. Liang, P. Zhang, B. Yao, Z. Chen, Y. Song, and G. Cheng, “UNIFY:
unified index for range filtered approximate nearest neighbors search,”
Proc. VLDB Endow., vol. 18, no. 4, pp. 1118–1130, 2024.
[89] Y. Wang, Z. He, Y. Tong, Z. Zhou, and Y. Zhong, “Timestamp
approximate nearest neighbor search over high-dimensional vector
data,” in IEEE International Conference on Data Engineering (ICDE),
2025, pp. 3043–3055.
[90] Z. Wang, J. Zhang, and W. Hu, “Wow: A window-to-window incre-
mental index for range-filtering approximate nearest neighbor search,”
arXiv:2508.18617, 2025.
[91] C. Zuo, M. Qiao, W. Zhou, F. Li, and D. Deng, “Serf: Segment graph
for range-filtering approximate nearest neighbor search,” Proc. ACM
Manag. Data, vol. 2, no. 1, pp. 69:1–69:26, 2024.
[92] Y. Cai, J. Shi, Y. Chen, and W. Zheng, “Navigating labels and vectors:
A unified approach to filtered approximate nearest neighbor search,”
Proc. ACM Manag. Data, vol. 2, no. 6, pp. 246:1–246:27, 2024.
[93] W. Wu, J. He, Y. Qiao, G. Fu, L. Liu, and J. Yu, “HQANN: efficient
and robust similarity search for hybrid queries with structured and
unstructured constraints,” in Proceedings of the ACM International
Conference on Information & Knowledge Management (CIKM), 2022,
pp. 4580–4584.
[94] M. Wang, L. Lv, X. Xu, Y. Wang, Q. Yue, and J. Ni, “An efficient
and robust framework for approximate nearest neighbor search with
attribute constraint,” in Advances in Neural Information Processing
Systems (NeurIPS), 2023.
[95] A. Heidari, W. Zhang, and Y. Xiong, “Fusedann: Convexified hybrid
ANN via attribute-vector fusion,” arXiv:2509.19767, 2025.
[96] M. Wang, B. Tan, Y. Gao, H. Jin, Y. Zhang, X. Ke, X. Xu, and Y. Zhu,
“Balancing the blend: An experimental analysis of trade-offs in hybrid
search,” arXiv:2508.01405, 2025.
[97] S. Bruch, F. M. Nardini, A. Ingber, and E. Liberty, “Bridging dense and
sparse maximum inner product search,” ACM Trans. Inf. Syst., vol. 42,
no. 6, pp. 151:1–151:38, 2024.
[98] Y. Luan, J. Eisenstein, K. Toutanova, and M. Collins, “Sparse, dense,
and attentional representations for text retrieval,” Trans. Assoc. Comput.
Linguistics, vol. 9, pp. 329–345, 2021.
[99] Y. Yang, P. Carlson, S. He, Y. Qiao, and T. Yang, “Cluster-based partial
dense retrieval fused with sparse text retrieval,” in Proceedings of the
International ACM SIGIR Conference on Research and Development
in Information Retrieval, 2024, pp. 2327–2331.
[100] K. Sawarkar, A. Mangal, and S. R. Solanki, “Blended RAG: improving
RAG (retriever-augmented generation) accuracy with semantic search
and hybrid query-based retrievers,” in IEEE International Conference
on Multimedia Information Processing and Retrieval (MIPR), 2024,
pp. 155–161.
[101] Z. Li, Y. Li, Y. Zhu, Z. Chen, and Y. Gao, “All-in-one graph-based
indexing for hybrid search on gpus,” arXiv:2511.00855, 2025.
[102] T. Chen, M. Zhang, J. Lu, M. Bendersky, and M. Najork, “Out-of-
domain semantics to the rescue! zero-shot hybrid retrieval models,”
in Advances in Information Retrieval - European Conference on IR
Research (ECIR), vol. 13185, 2022, pp. 95–110.
[103] “Ragflow,” https://github.com/infiniflow/ragflow, 2025, [Online; ac-
cessed 23-September-2025].
