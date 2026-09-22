#include <cstdint>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <immintrin.h>
static uint64_t word_at(const char *f){uint64_t w=0;std::memcpy(&w,f,8);return w;}
static uint64_t by_fnv(std::string_view t){uint64_t n=1469598103934665603ULL;for(char c:t){n^=(unsigned char)c;n*=1099511628211ULL;}return n;}
static uint64_t by_crc32c(std::string_view t){uint64_t n=~uint64_t{0};size_t a=0;for(;a+8<=t.size();a+=8)n=_mm_crc32_u64(n,word_at(t.data()+a));for(;a<t.size();a++)n=_mm_crc32_u8((uint32_t)n,(unsigned char)t[a]);return n;}
static uint64_t by_two_crc32c(std::string_view t){uint64_t l=~uint64_t{0},h=0x9e3779b97f4a7c15ULL;size_t a=0;for(;a+16<=t.size();a+=16){l=_mm_crc32_u64(l,word_at(t.data()+a));h=_mm_crc32_u64(h,word_at(t.data()+a+8));}for(;a+8<=t.size();a+=8)l=_mm_crc32_u64(l,word_at(t.data()+a));for(;a<t.size();a++)l=_mm_crc32_u8((uint32_t)l,(unsigned char)t[a]);return (l*0x9e3779b97f4a7c15ULL)^(h<<32)^h;}
static uint64_t fold(uint64_t n){n^=n>>33;n*=0xff51afd7ed558ccdULL;n^=n>>33;n*=0xc4ceb9fe1a85ec53ULL;n^=n>>33;return n;}
static uint64_t by_multiply_fold(std::string_view t){uint64_t n=0x9e3779b97f4a7c15ULL^t.size();size_t a=0;for(;a+8<=t.size();a+=8){n^=word_at(t.data()+a);n*=0xff51afd7ed558ccdULL;n^=n>>29;}if(a<t.size()){uint64_t l=0;std::memcpy(&l,t.data()+a,t.size()-a);n^=l;n*=0xff51afd7ed558ccdULL;}return fold(n);}

static uint64_t rot(uint64_t n,int b){return (n<<b)|(n>>(64-b));}
static uint64_t by_siphash13(std::string_view t){
 uint64_t v0=0x736f6d6570736575ULL^0x0706050403020100ULL,v1=0x646f72616e646f6dULL^0x0f0e0d0c0b0a0908ULL;
 uint64_t v2=0x6c7967656e657261ULL^0x0706050403020100ULL,v3=0x7465646279746573ULL^0x0f0e0d0c0b0a0908ULL;
 auto rnd=[&]{v0+=v1;v1=rot(v1,13);v1^=v0;v0=rot(v0,32);v2+=v3;v3=rot(v3,16);v3^=v2;v0+=v3;v3=rot(v3,21);v3^=v0;v2+=v1;v1=rot(v1,17);v1^=v2;v2=rot(v2,32);};
 size_t a=0;for(;a+8<=t.size();a+=8){uint64_t w=word_at(t.data()+a);v3^=w;rnd();v0^=w;}
 uint64_t l=0;std::memcpy(&l,t.data()+a,t.size()-a);l|=(uint64_t)(t.size()&0xff)<<56;
 v3^=l;rnd();v0^=l;v2^=0xff;rnd();rnd();rnd();return v0^v1^v2^v3;}
static uint64_t by_murmur3(std::string_view t){
 const uint64_t c1=0x87c37b91114253d5ULL,c2=0x4cf5ad432745937fULL;uint64_t h1=0,h2=0;size_t a=0;
 for(;a+16<=t.size();a+=16){uint64_t k1=word_at(t.data()+a),k2=word_at(t.data()+a+8);
  k1*=c1;k1=rot(k1,31);k1*=c2;h1^=k1;h1=rot(h1,27);h1+=h2;h1=h1*5+0x52dce729;
  k2*=c2;k2=rot(k2,33);k2*=c1;h2^=k2;h2=rot(h2,31);h2+=h1;h2=h2*5+0x38495ab5;}
 uint64_t k1=0,k2=0;size_t left=t.size()-a;
 if(left>8){std::memcpy(&k1,t.data()+a,8);std::memcpy(&k2,t.data()+a+8,left-8);}else if(left>0){std::memcpy(&k1,t.data()+a,left);}
 k1*=c1;k1=rot(k1,31);k1*=c2;h1^=k1;k2*=c2;k2=rot(k2,33);k2*=c1;h2^=k2;
 h1^=t.size();h2^=t.size();h1+=h2;h2+=h1;h1=fold(h1);h2=fold(h2);h1+=h2;return h1;}

int main(){
  const size_t N=1000000;
  std::vector<std::string> k; k.reserve(N);
  char room[64];
  for(size_t i=0;i<N;i++){int n=std::snprintf(room,sizeof room,"GET /articles/%zu?param=xyz&foo=bar",i);k.emplace_back(room,(size_t)n);}
  struct { const char *name; uint64_t (*fn)(std::string_view); } arms[] = {
    {"fnv", by_fnv},{"crc32c", by_crc32c},{"two_crc32c", by_two_crc32c},{"multiply_fold", by_multiply_fold},{"siphash13", by_siphash13},{"murmur3", by_murmur3}};
  for(auto &a:arms){
    std::vector<uint64_t> v; v.reserve(N);
    uint64_t low_bits_set=0;
    for(auto &s:k){uint64_t h=a.fn(s); v.push_back(h); low_bits_set|=h;}
    std::sort(v.begin(),v.end());
    size_t dup=0; for(size_t i=1;i<v.size();i++) if(v[i]==v[i-1]) dup++;
    int bits=0; for(int b=0;b<64;b++) if(low_bits_set>>b&1) bits++;
    std::printf("%-14s collisions %7zu   bits ever set %2d\n", a.name, dup, bits);
  }
  return 0;
}
