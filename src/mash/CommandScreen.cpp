// Copyright © 2015, Battelle National Biodefense Institute (BNBI);
// all rights reserved. Authored by: Brian Ondov, Todd Treangen,
// Sergey Koren, and Adam Phillippy
//
// See the LICENSE.txt file included with this software for license information.

#include "CommandScreen.h"
#include "CommandDistance.h" // for pvalue
#include "Sketch.h"
#include "kseq.h"
#include <iostream>
#include <zlib.h>
#include "ThreadPool.h"
#include <math.h>
#include "robin_hood.h"

#ifdef USE_BOOST
	#include <boost/math/distributions/binomial.hpp>
	using namespace::boost::math;
#else
	#include <gsl/gsl_cdf.h>
#endif

#define SET_BINARY_MODE(file)
KSEQ_INIT(gzFile, gzread)

using std::cerr;
using std::cout;
using std::endl;
using std::list;
using std::string;
using std::vector;

namespace mash {

CommandScreen::CommandScreen()
: Command()
{
	name = "screen";
	summary = "Determine whether query sequences are within a larger mixture of sequences.";
	description = "Determine how well query sequences are contained within a mixture of sequences. The queries must be formatted as a single Mash sketch file (.msh), created with the `mash sketch` command. The <mixture> files can be contigs or reads, in fasta or fastq, gzipped or not, and \"-\" can be given for <mixture> to read from standard input. The <mixture> sequences are assumed to be nucleotides, and will be 6-frame translated if the <queries> are amino acids. The output fields are [identity, shared-hashes, median-multiplicity, p-value, query-ID, query-comment], where median-multiplicity is computed for shared hashes, based on the number of observations of those hashes within the mixture.";
    argumentString = "<queries>.msh <mixture> [<mixture>] ...";
	
	useOption("help");
	useOption("threads");
//	useOption("minCov");
//    addOption("saturation", Option(Option::Boolean, "s", "", "Include saturation curve in output. Each line will have an additional field representing the absolute number of k-mers seen at each Jaccard increase, formatted as a comma-separated list.", ""));
    addOption("winning!", Option(Option::Boolean, "w", "", "Winner-takes-all strategy for identity estimates. After counting hashes for each query, hashes that appear in multiple queries will be removed from all except the one with the best identity (ties broken by larger query), and other identities will be reduced. This removes output redundancy, providing a rough compositional outline.", ""));
	//useSketchOptions();
    addOption("identity", Option(Option::Number, "i", "Output", "Minimum identity to report. Inclusive unless set to zero, in which case only identities greater than zero (i.e. with at least one shared hash) will be reported. Set to -1 to output everything.", "0", -1., 1.));
    addOption("pvalue", Option(Option::Number, "v", "Output", "Maximum p-value to report.", "1.0", 0., 1.));
}

int CommandScreen::run() const
{
	if ( arguments.size() < 2 || options.at("help").active )
	{
		print();
		return 0;
	}
	
	if ( ! hasSuffix(arguments[0], suffixSketch) )
	{
		cerr << "ERROR: " << arguments[0] << " does not look like a sketch (.msh)" << endl;
		exit(1);
	}
	
	bool sat = false;//options.at("saturation").active;
	
    double pValueMax = options.at("pvalue").getArgumentAsNumber();
    double identityMin = options.at("identity").getArgumentAsNumber();
    
    vector<string> refArgVector;
    refArgVector.push_back(arguments[0]);
	
	Sketch sketch;
    Sketch::Parameters parameters;
	
    sketch.initFromFiles(refArgVector, parameters);
    
    string alphabet;
    sketch.getAlphabetAsString(alphabet);
    setAlphabetFromString(parameters, alphabet.c_str());
	
	parameters.parallelism = options.at("threads").getArgumentAsNumber();
	parameters.kmerSize = sketch.getKmerSize();
	parameters.noncanonical = sketch.getNoncanonical();
	parameters.use64 = sketch.getUse64();
	parameters.preserveCase = sketch.getPreserveCase();
	parameters.seed = sketch.getHashSeed();
	parameters.minHashesPerWindow = sketch.getMinHashesPerWindow();
	
	HashTable hashTable;
	robin_hood::unordered_map<uint64_t, std::atomic<uint32_t>> hashCounts;
	robin_hood::unordered_map<uint64_t, list<uint32_t> > saturationByIndex;
	
	cerr << "Loading " << arguments[0] << "..." << endl;
	
	for ( int i = 0; i < sketch.getReferenceCount(); i++ )
	{
		const HashList & hashes = sketch.getReference(i).hashesSorted;
		
		for ( int j = 0; j < hashes.size(); j++ )
		{
			uint64_t hash = hashes.get64() ? hashes.at(j).hash64 : hashes.at(j).hash32;
			
			if ( hashTable.count(hash) == 0 )
			{
				hashCounts[hash] = 0;
			}
			
			hashTable[hash].insert(i);
		}
	}
	
	cerr << "   " << hashTable.size() << " distinct hashes." << endl;
	
	robin_hood::unordered_set<MinHashHeap *> minHashHeaps;
	
	bool trans = (alphabet == alphabetProtein);
	
/*	if ( ! trans )
	{
		if ( alphabet != alphabetNucleotide )
		{
			cerr << "ERROR: <query> sketch must have nucleotide or amino acid alphabet" << endl;
			exit(1);
		}
		
		if ( sketch.getNoncanonical() )
		{
			cerr << "ERROR: nucleotide <query> sketch must be canonical" << endl;
			exit(1);
		}
	}
*/	
	int queryCount = arguments.size() - 1;
	cerr << (trans ? "Translating from " : "Streaming from ");
	
	if ( queryCount == 1 )
	{
		cerr << arguments[1];
	}
	else
	{
		cerr << queryCount << " inputs";
	}
	
	cerr << "..." << endl;
	
	int kmerSize = parameters.kmerSize;
	int minCov = 1;//options.at("minCov").getArgumentAsNumber();
	
	ThreadPool<CommandScreen::HashInput, CommandScreen::HashOutput> threadPool(hashSequence, parameters.parallelism);
	
	// open all query files for round robin
	//
	gzFile fps[queryCount];
	list<kseq_t *> kseqs;
	//
	for ( int f = 1; f < arguments.size(); f++ )
	{
		if ( arguments[f] == "-" )
		{
			if ( f > 1 )
			{
				cerr << "ERROR: '-' for stdin must be first query" << endl;
				exit(1);
			}
			
			fps[f - 1] = gzdopen(fileno(stdin), "r");
		}
		else
		{
			fps[f - 1] = gzopen(arguments[f].c_str(), "r");
			
			if ( fps[f - 1] == 0 )
			{
				cerr << "ERROR: could not open " << arguments[f] << endl;
				exit(1);
			}
		}
		
		kseqs.push_back(kseq_init(fps[f - 1]));
	}
	
	// perform round-robin, closing files as they end
	//
	int l;
	uint64_t count = 0;
	//uint64_t kmersTotal = 0;
	uint64_t chunkSize = 1 << 20;
	string input;
	input.reserve(chunkSize);
	list<kseq_t *>::iterator it = kseqs.begin();
	//
	while ( true )
	{
		if ( kseqs.begin() == kseqs.end() )
		{
			l = 0;
		}
		else
		{
			l = kseq_read(*it);
		
			if ( l < -1 ) // error
			{
				break;
			}
		
			if ( l == -1 ) // eof
			{
				kseq_destroy(*it);
				it = kseqs.erase(it);
				if ( it == kseqs.end() )
				{
					it = kseqs.begin();
				}
				//continue;
			}
		}
		
		if ( input.length() + (l >= kmerSize ? l + 1 : 0) > chunkSize || kseqs.begin() == kseqs.end() )
		{
			// chunk big enough or at the end; time to flush
			
			// buffer this out since kseq will overwrite (deleted by HashInput destructor)
			//
			char * seqCopy = new char[input.length()];
			//
			memcpy(seqCopy, input.c_str(), input.length());
			
			if ( minHashHeaps.begin() == minHashHeaps.end() )
			{
				minHashHeaps.emplace(new MinHashHeap(sketch.getUse64(), sketch.getMinHashesPerWindow()));
			}
			
			threadPool.runWhenThreadAvailable(new HashInput(hashCounts, *minHashHeaps.begin(), seqCopy, input.length(), parameters, trans));
		
			input = "";
		
			minHashHeaps.erase(minHashHeaps.begin());
		
			while ( threadPool.outputAvailable() )
			{
				useThreadOutput(threadPool.popOutputWhenAvailable(), minHashHeaps);
			}
		}
		
		if ( kseqs.begin() == kseqs.end() )
		{
			break;
		}
		
		count++;
		
		if ( l >= kmerSize )
		{
			input.append(1, '*');
			input.append((*it)->seq.s, l);
		}
		
		it++;
		
		if ( it == kseqs.end() )
		{
			it = kseqs.begin();
		}
	}
	
	if (  l != -1 )
	{
		cerr << "\nERROR: reading inputs" << endl;
		exit(1);
	}
    
	while ( threadPool.running() )
	{
		useThreadOutput(threadPool.popOutputWhenAvailable(), minHashHeaps);
	}
	
	for ( int i = 0; i < queryCount; i++ )
	{
		gzclose(fps[i]);
	}
	
	MinHashHeap minHashHeap(sketch.getUse64(), sketch.getMinHashesPerWindow());
	
	for ( auto i = minHashHeaps.begin(); i != minHashHeaps.end(); i++ )
	{
		HashList hashList(parameters.use64);
		
		(*i)->toHashList(hashList);
		
		for ( int i = 0; i < hashList.size(); i++ )
		{
			minHashHeap.tryInsert(hashList.at(i));
		}
		
		delete *i;
	}
	
	if ( count == 0 )
	{
		cerr << "\nERROR: Did not find sequence records in inputs" << endl;
		
		exit(1);
	}
	
	/*
	if ( parameters.targetCov != 0 )
	{
		cerr << "Reads required for " << parameters.targetCov << "x coverage: " << count << endl;
	}
	else
	{
		cerr << "Estimated coverage: " << minHashHeap.estimateMultiplicity() << "x" << endl;
	}
	*/
	
	uint64_t setSize = minHashHeap.estimateSetSize();
	cerr << "   Estimated distinct" << (trans ? " (translated)" : "") << " k-mers in mixture: " << setSize << endl;
	
	if ( setSize == 0 )
	{
		cerr << "WARNING: no valid k-mers in input." << endl;
		//exit(0);
	}
	
	cerr << "Summing shared..." << endl;
	
	uint64_t * shared = new uint64_t[sketch.getReferenceCount()];
	vector<uint64_t> * depths = new vector<uint64_t>[sketch.getReferenceCount()];
	
	memset(shared, 0, sizeof(uint64_t) * sketch.getReferenceCount());
	
	for ( auto i = hashCounts.begin(); i != hashCounts.end(); i++ )
	{
		if ( i->second >= minCov )
		{
			const auto & indeces = hashTable.at(i->first);

			for ( auto k = indeces.begin(); k != indeces.end(); k++ )
			{
				shared[*k]++;
				depths[*k].push_back(i->second);
			
				if ( sat )
				{
					saturationByIndex[*k].push_back(0);// TODO kmersTotal);
				}
			}
		}
	}
	
	if ( options.at("winning!").active )
	{
		cerr << "Reallocating to winners..." << endl;
		
		double * scores = new double[sketch.getReferenceCount()];
		
		for ( int i = 0; i < sketch.getReferenceCount(); i ++ )
		{
			scores[i] = estimateIdentity(shared[i], sketch.getReference(i).hashesSorted.size(), kmerSize, sketch.getKmerSpace());
		}
		
		memset(shared, 0, sizeof(uint64_t) * sketch.getReferenceCount());
		
		for ( int i = 0; i < sketch.getReferenceCount(); i++ )
		{
			depths[i].clear();
		}
		
		for ( HashTable::const_iterator i = hashTable.begin(); i != hashTable.end(); i++ )
		{
			if ( hashCounts.count(i->first) == 0 || hashCounts.at(i->first) < minCov )
			{
				continue;
			}
			
			const auto & indeces = i->second;
			double maxScore = 0;
			uint64_t maxLength = 0;
			uint64_t maxIndex;
			
			for ( auto k = indeces.begin(); k != indeces.end(); k++ )
			{
				if ( scores[*k] > maxScore )
				{
					maxScore = scores[*k];
					maxIndex = *k;
					maxLength = sketch.getReference(*k).length;
				}
				else if ( scores[*k] == maxScore && sketch.getReference(*k).length > maxLength )
				{
					maxIndex = *k;
					maxLength = sketch.getReference(*k).length;
				}
			}
			
			shared[maxIndex]++;
			depths[maxIndex].push_back(hashCounts.at(i->first));
		}
		
		delete [] scores;
	}
	
	cerr << "Computing coverage medians..." << endl;
	
	for ( int i = 0; i < sketch.getReferenceCount(); i++ )
	{
		sort(depths[i].begin(), depths[i].end());
	}
	
	cerr << "Writing output..." << endl;
	
	for ( int i = 0; i < sketch.getReferenceCount(); i++ )
	{
		if ( shared[i] != 0 || identityMin < 0.0)
		{
			double identity = estimateIdentity(shared[i], sketch.getReference(i).hashesSorted.size(), kmerSize, sketch.getKmerSpace());
			
			if ( identity < identityMin )
			{
				continue;
			}
			
			double pValue = pValueWithin(shared[i], setSize, sketch.getKmerSpace(), sketch.getReference(i).hashesSorted.size());
			
			if ( pValue > pValueMax )
			{
				continue;
			}
			
			cout << identity << '\t' << shared[i] << '/' << sketch.getReference(i).hashesSorted.size() << '\t' << (shared[i] > 0 ? depths[i].at(shared[i] / 2) : 0) << '\t' << pValue << '\t' << sketch.getReference(i).name << '\t' << sketch.getReference(i).comment;
			
			if ( sat )
			{
				cout << '\t';
				
				for ( list<uint32_t>::const_iterator j = saturationByIndex.at(i).begin(); j != saturationByIndex.at(i).end(); j++ )
				{
					if ( j != saturationByIndex.at(i).begin() )
					{
						cout << ',';
					}
					
					cout << *j;
				}
			}
			
			cout << endl;
		}
	}
	
	delete [] depths;
	delete [] shared;
	
	return 0;
}

double estimateIdentity(uint64_t common, uint64_t denom, int kmerSize, double kmerSpace)
{
	double identity;
	double jaccard = double(common) / denom;
	
	if ( common == denom ) // avoid -0
	{
		identity = 1.;
	}
	else if ( common == 0 ) // avoid inf
	{
		identity = 0.;
	}
	else
	{
		identity = pow(jaccard, 1. / kmerSize);
	}
	
	return identity;
}

CommandScreen::HashOutput * hashSequence(CommandScreen::HashInput * input)
{
	CommandScreen::HashOutput * output = new CommandScreen::HashOutput(input->minHashHeap);
	
	int l = input->length;
	bool trans = input->trans;
	
	bool use64 = input->parameters.use64;
	uint32_t seed = input->parameters.seed;
	int kmerSize = input->parameters.kmerSize;
	bool noncanonical = input->parameters.noncanonical;
	
	char * seq = input->seq;
	
	// uppercase
	//
	for ( uint64_t i = 0; i < l; i++ )
	{
		if ( ! input->parameters.preserveCase && seq[i] > 96 && seq[i] < 123 )
		{
			seq[i] -= 32;
		}
	}
	
	char * seqRev;
	
	if ( ! noncanonical || trans )
	{
		seqRev = new char[l];
		reverseComplement(seq, seqRev, l);
	}
	
	for ( int i = 0; i < (trans ? 6 : 1); i++ )
	{
		bool useRevComp = false;
		int frame = i % 3;
		bool rev = i > 2;
		
		int lenTrans = (l - frame) / 3;
		
		char * seqTrans;
		
		if ( trans )
		{
			seqTrans = new char[lenTrans];
			translate((rev ? seqRev : seq) + frame, seqTrans, lenTrans);
		}
		
		int64_t lastGood = -1;
		int length = trans ? lenTrans : l;
		
		for ( int j = 0; j < length - kmerSize + 1; j++ )
		{
			while ( lastGood < j + kmerSize - 1 && lastGood < length )
			{
				lastGood++;
				
				if ( trans ? (seqTrans[lastGood] == '*') : (!input->parameters.alphabet[seq[lastGood]]) )
				{
					j = lastGood + 1;
				}
			}
			
			if ( j > length - kmerSize )
			{
				break;
			}
			
			const char * kmer;
			
			if ( trans )
			{
				kmer = seqTrans + j;
			}
			else
			{
				const char *kmer_fwd = seq + j;
				const char *kmer_rev = seqRev + length - j - kmerSize;
				kmer = (noncanonical || memcmp(kmer_fwd, kmer_rev, kmerSize) <= 0) ? kmer_fwd : kmer_rev;
			}
			
			//cout << kmer << '\t' << kmerSize << endl;
			hash_u hash = getHash(kmer, kmerSize, seed, use64);
			//cout << kmer << '\t' << hash.hash64 << endl;
			input->minHashHeap->tryInsert(hash);
			uint64_t key = use64 ? hash.hash64 : hash.hash32;
			
			if ( input->hashCounts.count(key) == 1 )
			{
				//cout << "Incrementing " << key << endl;
				input->hashCounts[key]++;
			}
		}
		
		if ( trans )
		{
			delete [] seqTrans;
		}
	}
	
	if ( ! noncanonical || trans )
	{
		delete [] seqRev;
	}
	/*
	addMinHashes(minHashHeap, seq, l, parameters);
	
	if ( parameters.targetCov > 0 && minHashHeap.estimateMultiplicity() >= parameters.targetCov )
	{
		l = -1; // success code
		break;
	}
	*/
	
	return output;
}

double pValueWithin(uint64_t x, uint64_t setSize, double kmerSpace, uint64_t sketchSize)
{
    if ( x == 0 )
    {
        return 1.;
    }
    
    double r = double(setSize) / kmerSpace;
    
#ifdef USE_BOOST
    return cdf(complement(binomial(sketchSize, r), x - 1));
#else
    return gsl_cdf_binomial_Q(x - 1, r, sketchSize);
#endif
}

void translate(const char * src, char * dst, uint64_t len)
{
	for ( uint64_t n = 0, a = 0; a < len; a++, n+= 3 )
	{
		dst[a] = aaFromCodon(src + n);
	}
}

char aaFromCodon(const char * codon)
{
	string str(codon, 3);
	/*
	if ( codons.count(str) == 1 )
	{
		return codons.at(str);
	}
	else
	{
		return 0;
	}
	*/
	char aa = '*';//0;
	
	switch (codon[0])
	{
	case 'A':
		switch (codon[1])
		{
		case 'A':
			switch (codon[2])
			{
				case 'A': aa = 'K'; break;
				case 'C': aa = 'N'; break;
				case 'G': aa = 'K'; break;
				case 'T': aa = 'N'; break;
			}
			break;
		case 'C':
			switch (codon[2])
			{
				case 'A': aa = 'T'; break;
				case 'C': aa = 'T'; break;
				case 'G': aa = 'T'; break;
				case 'T': aa = 'T'; break;
			}
			break;
		case 'G':
			switch (codon[2])
			{
				case 'A': aa = 'R'; break;
				case 'C': aa = 'S'; break;
				case 'G': aa = 'R'; break;
				case 'T': aa = 'S'; break;
			}
			break;
		case 'T':
			switch (codon[2])
			{
				case 'A': aa = 'I'; break;
				case 'C': aa = 'I'; break;
				case 'G': aa = 'M'; break;
				case 'T': aa = 'I'; break;
			}
			break;
		}
		break;
	case 'C':
		switch (codon[1])
		{
		case 'A':
			switch (codon[2])
			{
				case 'A': aa = 'Q'; break;
				case 'C': aa = 'H'; break;
				case 'G': aa = 'Q'; break;
				case 'T': aa = 'H'; break;
			}
			break;
		case 'C':
			switch (codon[2])
			{
				case 'A': aa = 'P'; break;
				case 'C': aa = 'P'; break;
				case 'G': aa = 'P'; break;
				case 'T': aa = 'P'; break;
			}
			break;
		case 'G':
			switch (codon[2])
			{
				case 'A': aa = 'R'; break;
				case 'C': aa = 'R'; break;
				case 'G': aa = 'R'; break;
				case 'T': aa = 'R'; break;
			}
			break;
		case 'T':
			switch (codon[2])
			{
				case 'A': aa = 'L'; break;
				case 'C': aa = 'L'; break;
				case 'G': aa = 'L'; break;
				case 'T': aa = 'L'; break;
			}
			break;
		}
		break;
	case 'G':
		switch (codon[1])
		{
		case 'A':
			switch (codon[2])
			{
				case 'A': aa = 'E'; break;
				case 'C': aa = 'D'; break;
				case 'G': aa = 'E'; break;
				case 'T': aa = 'D'; break;
			}
			break;
		case 'C':
			switch (codon[2])
			{
				case 'A': aa = 'A'; break;
				case 'C': aa = 'A'; break;
				case 'G': aa = 'A'; break;
				case 'T': aa = 'A'; break;
			}
			break;
		case 'G':
			switch (codon[2])
			{
				case 'A': aa = 'G'; break;
				case 'C': aa = 'G'; break;
				case 'G': aa = 'G'; break;
				case 'T': aa = 'G'; break;
			}
			break;
		case 'T':
			switch (codon[2])
			{
				case 'A': aa = 'V'; break;
				case 'C': aa = 'V'; break;
				case 'G': aa = 'V'; break;
				case 'T': aa = 'V'; break;
			}
			break;
		}
		break;
	case 'T':
		switch (codon[1])
		{
		case 'A':
			switch (codon[2])
			{
				case 'A': aa = '*'; break;
				case 'C': aa = 'Y'; break;
				case 'G': aa = '*'; break;
				case 'T': aa = 'Y'; break;
			}
			break;
		case 'C':
			switch (codon[2])
			{
				case 'A': aa = 'S'; break;
				case 'C': aa = 'S'; break;
				case 'G': aa = 'S'; break;
				case 'T': aa = 'S'; break;
			}
			break;
		case 'G':
			switch (codon[2])
			{
				case 'A': aa = '*'; break;
				case 'C': aa = 'C'; break;
				case 'G': aa = 'W'; break;
				case 'T': aa = 'C'; break;
			}
			break;
		case 'T':
			switch (codon[2])
			{
				case 'A': aa = 'L'; break;
				case 'C': aa = 'F'; break;
				case 'G': aa = 'L'; break;
				case 'T': aa = 'F'; break;
			}
			break;
		}
		break;
	}
	
	return aa;//(aa == '*') ? 0 : aa;
}

void useThreadOutput(CommandScreen::HashOutput * output, robin_hood::unordered_set<MinHashHeap *> & minHashHeaps)
{
	minHashHeaps.emplace(output->minHashHeap);
	delete output;
}

} // namespace mash
