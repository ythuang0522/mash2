
#include "MinHashHeap.h"
#include <iostream>
#include "HashSet.h"
#include <assert.h>

using namespace::std;

MinHashHeap::MinHashHeap(bool use64New, uint64_t cardinalityMaximumNew, uint64_t multiplicityMinimumNew, uint64_t memoryBoundBytes) :
	use64(use64New),
	hashes(use64New),
	hashesQueue(use64New),
	hashPairQueue(use64New),
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
	
	HashList hashList;
	hashes.toHashList(hashList);
	
	//cout<<"counts size:  "<<counts.size()<<endl;
	//cout<< "hashlist index size:  "<<hashList.size()<<endl;
	int j = 0;
	for ( int i = 0, j = 0; i < counts.size(), j < hashList.size(); i++ ,j++)
	{
		cout<< hashList.at(j).hash64<<"   ";
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

bool comparePair(std::pair<size_t, hash_u> p1, std::pair<size_t, hash_u> p2, bool use64)
{
	if(p1.first < p2.first)
			return true;
	else if(p1.first > p2.first)
			return false;
	else if(use64)
	{
		if(p1.second.hash64 <= p2.second.hash64)
			return true;
		else
			return false;
	}else
	{
		if(p1.second.hash32 <= p2.second.hash32)
			return true;
		else
			return false;
	}
}

void MinHashHeap::kmerInsertonce(hash_u hash, HashSet & KmerStatsTable , uint64_t index , int kermize)
{
	size_t kcount = KmerStatsTable.count(hash);
	auto kmerpair = std::make_pair(kcount, hash);


	if(hashes.size() < cardinalityMaximum || comparePair(kmerpair, hashPairQueue.top(), use64 ))
	{
		int find = -1;
		for(int i = 0 ; i< kermize ; i++){
			if(index_record.find(index + i ) != index_record.end()){
				find = i ;
				break;
			}
		}
		if(find >= 0){
			auto find_itr = index_record.find(index);
			size_t find_kcount = KmerStatsTable.count(find_itr->second);
			auto find_kmerpair = std::make_pair(find_kcount, find_itr->second);
			if(comparePair(find_kmerpair, kmerpair, use64 )){
				bad_index_record.insert(std::make_pair(index , hash));
				return ;
			}
		}
		for(int i = 0 ; i< kermize ; i++){
			index_record.insert(std::make_pair(index + i , hash));
		}
		
		hashes.insert(hash, 1);
		hashPairQueue.push(std::make_pair(KmerStatsTable.count(hash), hash));

		if ( hashes.size() > cardinalityMaximum )
		{
			hashes.erase(hashPairQueue.top().second);		
			hashPairQueue.pop();
		}
	}
}
/*
void MinHashHeap::bad_kmer_check(hash_u hash, HashSet & KmerStatsTable , uint64_t index , int kermize)
{
	size_t kcount = KmerStatsTable.count(hash);
	auto kmerpair = std::make_pair(kcount, hash);


	if(hashes.size() < cardinalityMaximum || comparePair(kmerpair, hashPairQueue.top(), use64 ))
	{
		bool find = false;
		for(int i = 0 ; i< kermize ; i++){
			if(std::find(index_record.begin() , index_record.end() , index) != index_record.end()){
				find = true ;
				break;
			}
		}
		if(find){
			bad_index_record.push_back(index);
			return ;
		}
		index_record.push_back(index);
		hashes.insert(hash, 1);
		hashPairQueue.push(std::make_pair(KmerStatsTable.count(hash), hash));

		if ( hashes.size() > cardinalityMaximum )
		{
			hashes.erase(hashPairQueue.top().second);		
				
			hashPairQueue.pop();
		}
	}
}*/

void MinHashHeap::tryInsert(hash_u hash)
{
	if(hashes.size() < cardinalityMaximum ||hashLessThan(hash, hashesQueue.top(), use64))
	{
		if ( hashes.count(hash) == 0 )
		{
			if ( bloomFilter != 0 )
			{
                		const unsigned char * data = use64 ? (const unsigned char *)&hash.hash64 : (const unsigned char *)&hash.hash32;
            			size_t length = use64 ? 8 : 4;
            	
                		if ( bloomFilter->contains(data, length) )
                		{
							hashes.insert(hash, 2);
							hashesQueue.push(hash);
							multiplicitySum += 2;
	                		kmersUsed++;
               			}
            			else
            			{
	                		bloomFilter->insert(data, length);
	                		kmersTotal++;
	            		}
			}
			else if ( multiplicityMinimum == 1 || hashesPending.count(hash) == multiplicityMinimum - 1 )
			{
				hashes.insert(hash, multiplicityMinimum);
				hashesQueue.push(hash);
				multiplicitySum += multiplicityMinimum;
				
				if ( multiplicityMinimum > 1 )
				{
					// just remove from set for now; will be removed from
					// priority queue when it's on top
					//
					hashesPending.erase(hash);
				}
			}
			else
			{
				if ( hashesPending.count(hash) == 0 )
				{
					hashesQueuePending.push(hash);
				}
			
				hashesPending.insert(hash, 1);
			}
		}
		else
		{
			hashes.insert(hash, 1);
			multiplicitySum++;
		}
		
		if ( hashes.size() > cardinalityMaximum )
		{
			multiplicitySum -= hashes.count(hashesQueue.top());
			hashes.erase(hashesQueue.top());
			
			// loop since there could be zombie hashes (gone from hashesPending)
			//
			while ( hashesQueuePending.size() > 0 && hashLessThan(hashesQueue.top(), hashesQueuePending.top(), use64) )
			{
				if ( hashesPending.count(hashesQueuePending.top()) )
				{
					hashesPending.erase(hashesQueuePending.top());
				}
				
				hashesQueuePending.pop();
			}
			
			hashesQueue.pop();
		}
	}
}
