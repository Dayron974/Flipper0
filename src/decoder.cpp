#include "decoder.h"
#include <algorithm>
#include <vector>
#include <string>
#include <cmath>

// Profil experimental : 0 = +T,-3T ; 1 = +3T,-T ; synchro = +T,-31T.
static bool proche(int64_t v, double cible) {
    return std::abs(double(v)-cible) <= 0.25*cible;
}
static bool paire(int32_t h, int32_t l, double t, int k) {
    return h>0 && l<0 && proche(h,t) && proche(-(int64_t)l,k*t);
}
static void segment(const int32_t* d,size_t a,size_t b,double t,uint32_t id) {
    static std::string precedent;
    static uint32_t dernierId=0;
    std::string bits;
    bool ok=(b>a && (b-a)%2==0);
    for(size_t i=a;i+1<b;i+=2) {
        int32_t h=d[i],l=d[i+1];
        if(paire(h,l,t,3)) bits+='0';
        else if(h>0 && l<0 && proche(h,3*t) && proche(-(int64_t)l,t)) bits+='1';
        else {bits+='?';ok=false;}
    }
    // Longueur du profil de demonstration, pas une propriete universelle.
    ok=ok && bits.size()==24;
    bool repetee=ok && bits==precedent && (id==dernierId || id==dernierId+1);
    if(ok) {precedent=bits;dernierId=id;} else precedent.clear();
    Serial.printf("DEC_RAW;%lu;%.1f;%u;%s;",(unsigned long)id,t,(unsigned)bits.size(),bits.c_str());
    if(ok) {unsigned long v=0;for(char c:bits)v=(v<<1)|(c=='1');Serial.printf("%06lX",v);}
    Serial.printf(";%s;%d\n",ok?"PROFIL_OK":"INCOMPLET_OU_INVALIDE",repetee);
}
void analyserRAW(const int32_t* d,size_t n,uint32_t perdus,uint32_t id) {
    if(!d || n<16 || n>2048 || perdus) {Serial.println("DEC_RAW;CAPTURE_INEXPLOITABLE");return;}
    std::vector<int64_t> v;
    for(size_t i=0;i<n;i++){int64_t x=d[i];if(x<0)x=-x;if(x>=100 && x<=2500)v.push_back(x);}
    if(v.size()<8){Serial.println("DEC_RAW;DUREES_INSUFFISANTES");return;}
    std::sort(v.begin(),v.end());
    double t=v[v.size()/4]; // estimation robuste du groupe court si le profil est present
    Serial.printf("# B03 T estime = %.1f us\n",t);
    size_t debut=n; bool trouve=false;
    for(size_t i=0;i+1<n;i++) {
        if(paire(d[i],d[i+1],t,31)) {
            if(debut!=n && i>debut){segment(d,debut,i,t,id);trouve=true;}
            debut=i+2; i++;
        }
    }
    if(debut<n){segment(d,debut,n,t,id);trouve=true;}
    if(!trouve)Serial.println("DEC_RAW;SYNCHRO_ABSENTE_OU_SANS_DONNEES");
}
void analyserFSK(const uint8_t* d,size_t n,bool crcOK,bool tronque,uint32_t id) {
    Serial.printf("DEC_FSK;%lu;%u;%s;",(unsigned long)id,(unsigned)n,tronque?"TRONQUE":(!crcOK?"CRC_KO":(n?"CRC_OK":"VIDE")));
    for(size_t i=0;i<n;i++)Serial.printf("%02X",d[i]);
    Serial.print(';');
    for(size_t i=0;i<n;i++)for(int b=7;b>=0;b--)Serial.print((d[i]>>b)&1);
    Serial.println();
}
void testDecodeur(){
    int32_t d[52]; d[0]=350;d[1]=-10850;
    const uint32_t mot=0xA5B32C;
    for(int i=0;i<24;i++){bool b=(mot>>(23-i))&1;d[2+2*i]=b?1050:350;d[3+2*i]=b?-350:-1050;}
    d[50]=350;d[51]=-10850;
    Serial.println("# TEST 1 nominal attendu A5B32C");analyserRAW(d,52,0,1);
    Serial.println("# TEST 2 repetition attendue");analyserRAW(d,52,0,2);
    Serial.println("# TEST 3 troncature");analyserRAW(d,46,0,3);
    Serial.println("# TEST 4 impulsion incoherente");d[8]=700;analyserRAW(d,52,0,4);d[8]=((mot>>20)&1)?1050:350;
    Serial.println("# TEST 5 debordement");analyserRAW(d,52,1,5);
    Serial.println("# TEST 6 variation 10 pourcent");for(int i=0;i<52;i++)d[i]=d[i]*11/10;analyserRAW(d,52,0,6);
    uint8_t pkt[]={0xA5,0x32};Serial.println("# TEST 7 et 8 FSK CRC");analyserFSK(pkt,2,true,false,7);analyserFSK(pkt,2,false,false,8);
}
