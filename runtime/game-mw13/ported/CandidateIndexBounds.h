#pragma once
#include <emmintrin.h>
#include <climits>
// Public geometry callers still receive the same rejection for any invalid
// index. Check four unsigned values together before tracing the candidates.
static bool CandidateIndicesValid(const unsigned* values,unsigned count,unsigned limit){
 if(!count)return true;if(!values||!limit)return false;
 const auto sign=_mm_set1_epi32(INT_MIN);
 const auto maximum=_mm_set1_epi32(int((limit-1)^0x80000000u));
 unsigned n=0;
 for(;count-n>=4;n+=4){auto indices=_mm_loadu_si128(reinterpret_cast<const __m128i*>(values+n));auto invalid=_mm_cmpgt_epi32(_mm_xor_si128(indices,sign),maximum);if(_mm_movemask_epi8(invalid))return false;}
 for(;n<count;++n)if(values[n]>=limit)return false;
 return true;
}
