// Copyright © 2015, Battelle National Biodefense Institute (BNBI);
// all rights reserved. Authored by: Brian Ondov, Todd Treangen,
// Sergey Koren, and Adam Phillippy
//
// See the LICENSE.txt file included with this software for license information.

#include "HashPriorityQueue.h"

void HashPriorityQueue::clear()
{
    if ( use64 )
    {
        while ( queue64.size() )
        {
            queue64.pop();
        }
    }
    else
    {
        while ( queue32.size() )
        {
            queue32.pop();
        }
    }
}

std::pair<std::size_t, hash_u>  HashPriorityQueue::top() const
{
    hash_u hash;
    
    if ( use64 )
    {
        // hash.hash64 = queue64.top().second;
        hash.hash64 = queue64.top().second;
        return std::make_pair(queue64.top().first, hash);
    }
    else
    {
        hash.hash32 = queue32.top().second;
        return std::make_pair(queue32.top().first, hash);
    }    
}
