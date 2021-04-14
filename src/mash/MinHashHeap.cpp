
#include "MinHashHeap.h"
#include <iostream>

using namespace::std;

MinHashHeap::MinHashHeap(bool use64New, uint64_t cardinalityMaximumNew, uint64_t multiplicityMinimumNew, uint64_t memoryBoundBytes) :
	use64(use64New),
	hashes(use64New),
	hashesQueue(use64New),
	hashesPending(use64New),
	hashesQueuePending(use64New)
{
	cardinalityMaximum = cardinalityMaximumNew;
	multiplicityMinimum = multiplicityMinimumNew;
	
	multiplicitySum = 0;
	
	if ( memoryBoundBytes == 0 )
	{
		bloomFilter = 0;
	}
	else
	{
		bloom_parameters bloomParams;
		
		bloomParams.projected_element_count = 1000000000;//(uint64_t)parameters.genomeSize * 10l; // TODO: error rate based on platform and coverage
		bloomParams.false_positive_probability = 0;//parameters.bloomError;
		bloomParams.maximum_size = memoryBoundBytes * 8l;
		bloomParams.compute_optimal_parameters();
		
		kmersTotal = 0;
		kmersUsed = 0;
		
		//if ( i == 0 && verbosity > 0 )
		{
			//cerr << "   Bloom table size (bytes): " << bloomParams.optimal_parameters.table_size / 8 << endl;
		}
		
		bloomFilter = new bloom_filter(bloomParams);
	}
}

MinHashHeap::~MinHashHeap()
{
	if ( bloomFilter != 0 )
	{
		delete bloomFilter;
	}
}

void MinHashHeap::computeStats()
{
	vector<uint32_t> counts;
	hashes.toCounts(counts);
	
	for ( int i = 0; i < counts.size(); i++ )
	{
		cout << counts.at(i) << endl;
	}
}

void MinHashHeap::clear()
{
	hashes.clear();
	hashesQueue.clear();
	
	hashesPending.clear();
	hashesQueuePending.clear();
	
	if ( bloomFilter != 0 )
	{
		bloomFilter->clear();
	}
	
	multiplicitySum = 0;
}

void MinHashHeap::tryInsert(hash_u hash)
{
	//if
	//(
//		hashes.size() < cardinalityMaximum ||
//		hashLessThan(hash, hashesQueue.top(), use64)
//	)
//	{
		hashes.insert(hash, 1);
		
		if(hashes.count(hash) == 0)
			hashesQueue.push(hash);
		
//		if ( hashes.size() > cardinalityMaximum )
//		{
//			hashes.erase(hashesQueue.top());
//		}
//	}
}
