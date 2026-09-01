#include <openssl/evp.h>
#include <openssl/rand.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <mach/mach_time.h>
int main(){
  mach_timebase_info_data_t tb; mach_timebase_info(&tb); double t2ns=(double)tb.numer/tb.denom;
  EVP_PKEY* key=nullptr; EVP_PKEY_CTX* c=EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519,nullptr);
  EVP_PKEY_keygen_init(c); EVP_PKEY_keygen(c,&key);
  unsigned char msg[192]; RAND_bytes(msg,sizeof msg);
  unsigned char sig[64]; size_t siglen=64;
  { EVP_MD_CTX* m=EVP_MD_CTX_new(); EVP_DigestSignInit(m,nullptr,nullptr,nullptr,key);
    EVP_DigestSign(m,sig,&siglen,msg,sizeof msg); EVP_MD_CTX_free(m); }
  auto verify=[&]{ EVP_MD_CTX* m=EVP_MD_CTX_new();
    EVP_DigestVerifyInit(m,nullptr,nullptr,nullptr,key);
    int r=EVP_DigestVerify(m,sig,siglen,msg,sizeof msg); EVP_MD_CTX_free(m); return r; };
  for(int i=0;i<2000;i++) verify();
  const int N=20000; std::vector<double> s; s.reserve(N);
  for(int i=0;i<N;i++){ uint64_t t0=mach_absolute_time(); verify(); uint64_t t1=mach_absolute_time(); s.push_back((t1-t0)*t2ns); }
  std::sort(s.begin(),s.end());
  printf("Ed25519 verify (192B msg): p50=%8.0f ns  p99=%8.0f ns\n", s[N/2], s[(size_t)(N*0.99)]);
  // SipHash-ish cheap tag baseline: HMAC-SHA256 over same msg
  unsigned char k[32]; RAND_bytes(k,32);
  EVP_PKEY* mk=EVP_PKEY_new_raw_private_key(EVP_PKEY_HMAC,nullptr,k,32);
  auto mac=[&]{ EVP_MD_CTX* m=EVP_MD_CTX_new(); size_t l=32; unsigned char out[32];
    EVP_DigestSignInit(m,nullptr,EVP_sha256(),nullptr,mk); EVP_DigestSign(m,out,&l,msg,sizeof msg);
    EVP_MD_CTX_free(m); return out[0]; };
  for(int i=0;i<5000;i++) mac();
  std::vector<double> s2; s2.reserve(N);
  for(int i=0;i<N;i++){ uint64_t t0=mach_absolute_time(); mac(); uint64_t t1=mach_absolute_time(); s2.push_back((t1-t0)*t2ns); }
  std::sort(s2.begin(),s2.end());
  printf("HMAC-SHA256  (192B msg): p50=%8.0f ns  p99=%8.0f ns\n", s2[N/2], s2[(size_t)(N*0.99)]);
  return 0;
}
