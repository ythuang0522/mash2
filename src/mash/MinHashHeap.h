#ifndef HashHeapCounted_h
#define HashHeapCounted_h

#include "HashList.h"
#include "HashPriorityQueue.h"
#include "HashSet.h"
#include <math.h>
#include "bloom_filter.hpp"
#include <vector>

class MinHashHeap
{
public:

	MinHashHeap(bool use64New, uint64_t cardinalityMaximumNew, uint64_t multiplicityMinimumNew = 1, uint64_t memoryBoundBytes = 0);
	~MinHashHeap();
	void computeStats();
	void clear();
	double estimateMultiplicity() const;
	double estimateSetSize() const;
	void toCounts(std::vector<uint32_t> & counts) const;
    void toHashList(HashList & hashList) const;
	void tryInsert(hash_u hash);
	void kmerInsertonce(hash_u hash ,HashSet & KmerStatsTable);
	void pos_recall();
	
private:

	bool use64;
	
	HashSet hashes;
	HashPriorityQueue hashesQueue;
	HashPriorityPairQueue hashPairQueue;

	HashSet hashesPending;
	HashPriorityQueue hashesQueuePending;
	
	uint64_t cardinalityMaximum;
	uint64_t multiplicityMinimum;
	
	uint64_t multiplicitySum;
	
    bloom_filter * bloomFilter;
    
    uint64_t kmersTotal;
    uint64_t kmersUsed;

	std::vector<int> posi;
	std::vector<hash64_t> value;
};

inline double MinHashHeap::estimateMultiplicity() const {return hashes.size() ? (double)multiplicitySum / hashes.size() : 0;}
inline double MinHashHeap::estimateSetSize() const {return hashes.size() ? pow(2.0, use64 ? 64.0 : 32.0) * (double)hashes.size() / (use64 ? (double)hashesQueue.top().hash64 : (double)hashesQueue.top().hash32) : 0;}
inline void MinHashHeap::toHashList(HashList & hashList) const {hashes.toHashList(hashList);}
inline void MinHashHeap::toCounts(std::vector<uint32_t> & counts) const {hashes.toCounts(counts);}

#endif
