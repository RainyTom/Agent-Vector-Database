Navigating Labels and Vectors: A Unified Approach to 
Filtered Approximate Nearest Neighbor Search
Yuzheng Cai, Jiayang Shi, Yizhuo Chen, Weiguo Zheng 
Fudan University, Shanghai, China
2  Methodology
3 Experiments
1  Problem Statement
• Dataset: a set of vectors 𝑣!, each has a label set 𝑓!
• Query: a query vector 𝑣", and a query label set 𝑓"
• Task: Approximately find top-k similar vectors 𝑣! s.t. 𝑓" ⊆𝑓!
Filtered Approximate Nearest Neighbor Search
Categorical labels with AND predicate:
SELECT Top-k vectors 
FROM dataset
WHERE 
LabelX=1 AND
LabelY=2 AND … 
• Versatility: Support any size of 𝑓" and 𝑓!
• Fidelity: Only visit the filtered vectors
• Completeness: Return enough answers
• Adaptability: Support different graph ANNS indices
• Task: Approximately find top-k similar vectors 𝑣! s.t. 𝑓" ⊆𝑓!
• If 𝑓! ⊆𝑓#, when 𝑓! satisfies the filter condition, 𝑓# also satisfies (𝑓" ⊆𝑓! ⊆𝑓#)
Key Observation
• Build the containment relations for all 𝑓!
• Find some entry label sets
Label Navigating Graph
• Build proximity graph for each group with same 𝑓!
• Add directional cross-group edges 
Unified Navigating Graph (UNG)
Real label distributions
Predicate AND
Predicate EQUAL
Predicate OR
Traditional No Predicate 
Scan QR codes for more details
Full paper
Source code
More about Yuzheng Cai ( Email: yuzhengcai21@m.fudan.edu.cn )
Personal page
Google Scholar
Linkin
