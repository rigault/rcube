#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "glibwrapper.h"
#include "r3types.h"
#include "r3util.h"
#include "inline.h"

FlowP *tGribData [2] = {NULL, NULL};   // wind, current

/*! GRIB2 reader without ecCodes/GLib
 * Supports:
 *   - Section 3: GDT 3.0 (regular lat/lon), di/dj signs from La2/Lo2 (wrap 360°)
 *   - Section 4: PDT 4.x (lead time extracted), minimal metadata
 *   - Section 5: DRT 5.0 (simple packing) and 5.3 (IEEE float)
 *   - Section 6: bitmap indicator 255 (none) or 0 (bitmap here). 254 (prev) -> unsupported
 *   - Section 7: data unpacking with/without bitmap
 * Variables mapped:
 *   - discipline 0, cat 2, param 2 -> "10u"  (10 m U wind)
 *   - discipline 0, cat 2, param 3 -> "10v"  (10 m V wind)
 *   - discipline 0, cat 2, param 22 -> "gust" (wind gust)
 *   - discipline 10, cat 0, param {3,5,8} -> "swh" (WW3 heights: HTSGW/WVHGT/SWELL)
 * Only these 4 contribute to timeStamp list and to FlowP fields.
 * Fot current, ucurr and vcurr replace 10 and 10v.
 * 
 *
 * Build example:
 *   gcc -O2 -Wall -Wextra -c readgriballwithouteccodes.c
*/

#ifndef GRIB_DEBUG
#define GRIB_DEBUG 0   /* set to 0 to disable diagnostics */
#endif

/*! return type of reader */
char *gribReaderVersion (char *str, size_t maxLen) {
   strlcpy  (str, "In house", maxLen);
   return str;
}

// ========================== Big-endian readers ===============================
static inline uint8_t  rdU8(const uint8_t *p)   { return p[0]; }
static inline uint16_t rdU16BE(const uint8_t *p){ return (uint16_t)((p[0]<<8)|p[1]); }
static inline uint32_t rdU32BE(const uint8_t *p){ return (uint32_t)((p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3]); }
static inline uint64_t rdU64BE(const uint8_t *p){ return ((uint64_t)rdU32BE(p)<<32) | (uint64_t)rdU32BE(p+4); }

// Remplace la lecture brute int16 par une lecture "flexible":
// - si MSB=1 et la magnitude est raisonnable (≤1000), on traite en sign-magnitude
// - sinon on garde le complément-à-deux classique
static int16_t readS16beFlexible(const uint8_t *p){
   uint16_t u = rdU16BE(p);
   int16_t t2 = (int16_t)u;                // interprétation "complément à deux"
   if (u & 0x8000) {
      int16_t sm = -(int16_t)(u & 0x7FFF); // interprétation "sign-magnitude"
      // Les facteurs d'échelle réels sont petits ([-100..100] typiquement).
      // Si le "sm" est raisonnable, on le préfère.
      if (sm >= -1000 && sm <= 1000) return sm;
   }
   return t2;
}

static inline float rdIEEE32BE(const uint8_t *p){
   uint32_t u = rdU32BE(p);
   float f; memcpy(&f, &u, sizeof(float));
   return f;
}

// ============================= Bit reader (S7) ===============================
typedef struct {
   const uint8_t *buf;
   size_t len;     // bytes
   size_t bitpos;  // next bit index
} BitReader;

static void bitInit(BitReader *br, const uint8_t *buf, size_t len){
   br->buf = buf; br->len = len; br->bitpos = 0;
}
static uint32_t bitGet(BitReader *br, int nbits){
   uint32_t v = 0;
   for(int i=0;i<nbits;i++){
      size_t byte = (br->bitpos >> 3);
      int    offs = 7 - (int)(br->bitpos & 7);
      br->bitpos++;
      if(byte >= br->len) return v;
      uint8_t b = br->buf[byte];
      v = (v<<1) | ((b>>offs)&1u);
   }
   return v;
}

#if GRIB_DEBUG
static void debugPeekS7Integers(const uint8_t *s7, size_t s7Len, int nbits, int howMany){
   if(!s7 || s7Len==0 || nbits<=0) return;
   BitReader br; bitInit(&br, s7, s7Len);
   fprintf(stderr, "[S7] first %d raw ints (nbits=%d):", howMany, nbits);
   for(int i=0;i<howMany;i++){
      uint32_t x = bitGet(&br, nbits);
      fprintf(stderr, " %u", (unsigned)x);
   }
   fprintf(stderr, "\n");
}
#endif

// ============================ Utilities (small) ==============================
static void strCopySafe(char *dst, size_t cap, const char *src){
   if(cap==0){ return; }
   if(!src){ dst[0]=0; return; }
   strncpy(dst, src, cap-1);
   dst[cap-1] = 0;
}

static size_t updateLongUnique(long value, size_t n, size_t maxN, long arr[]){
   for(size_t i=0;i<n;i++) if(arr[i]==value) return n;
   if(n < maxN){ arr[n] = value; return n+1; }
   return n;
}

static void sortLongAsc(long *a, size_t n){
   for(size_t i=1;i<n;i++){
      long key=a[i]; size_t j=i;
      while(j>0 && a[j-1] > key){ a[j]=a[j-1]; j--; }
      a[j]=key;
   }
}

static const uint8_t* findNextGrib(const uint8_t *p, const uint8_t *end){
   while(p + 16 <= end){
      if(p[0]=='G' && p[1]=='R' && p[2]=='I' && p[3]=='B') return p;
      p++;
   }
   return NULL;
}

static bool isCurrentTriplet(int disc, int cat, int par, bool *isU){
  if (disc != 10) return false;                  // Oceanography
  if (!(cat==1 || cat==2 || cat==0)) return false; // centres variés: 1/2 (voire 0)

  switch(par){
    // U components: WMO U(2), eastward(5), locaux 192/194
    case 2: case 5: case 192: case 194:
      if (isU) *isU = true;  
      return true;
    // V components: WMO V(3), northward(6), locaux 193/195
    case 3: case 6: case 193: case 195:
      if (isU) *isU = false; 
      return true;
    default: return false;
  }
}

// ======================== ShortName mapping (minimal) ========================
// Discipline 0 = meteorological, 10 = oceanographic (WW3)
static const char* shortNameFor(int disc, int cat, int par){
   if(disc==0 && cat==2 && par==2) return "10u";
   if(disc==0 && cat==2 && par==3) return "10v";
   if(disc==0 && cat==2 && par==22) return "gust";
   if(disc==10 && cat==0 && (par==3 || par==5 || par==8)) return "swh";

   bool isU=false;
   if (isCurrentTriplet(disc,cat,par,&isU)) return isU ? "ucurr" : "vcurr";

   return "unknown";
}

// =========================== Time units -> hours =============================
static int unitToHours(uint8_t unit, int32_t val){
   switch(unit){
      case 13: return val / 3600;      // seconds
      case  0: return val / 60;        // minutes
      case  1: return val;             // hours
      case 10: return val * 3;         // 3 hours
      case 11: return val * 6;         // 6 hours
      case 12: return val * 12;        // 12 hours
      case  2: return val * 24;        // days
      case  3: return val * 24 * 30;   // months approx
      case  4: return val * 24 * 365;  // years approx
      default: return -1;
   }
}

static int scoreHours(int h){
   if(h < 0) return -1000000;
   int sc=0;
   if(h <= 384) sc += 10; else if(h <= 1000) sc += 5; else sc -= 5;
   if((h % 3)==0)  sc++;
   if((h % 6)==0)  sc++;
   if((h % 12)==0) sc++;
   return sc;
}

// ================= PDT lead time (robust; tuned for GFS PDT 4.x) =============
/* PDT pointer `pdt` starts at octet 10 of Section 4 (WMO numbering).
   For PDT 4.0/4.1/4.2 (common style):
     - indicator of unit of time range  -> pdt[8]
     - forecast time in units (signed)  -> pdt[9..12]
   Fallback: pdt[18], pdt[19..22].
   Final fallback: short scan on first ~64 bytes, accepting 0..384 h.
*/
static int pdtLeadHours(int pdtn, const uint8_t *pdt, uint32_t pdtLen){
   if(!pdt || pdtLen < 12) return -1;

   {  /* Primary: pdt[8] / pdt[9..12] */
      uint8_t unit = pdt[8];
      int32_t val  = (int32_t)((pdt[9]<<24)|(pdt[10]<<16)|(pdt[11]<<8)|pdt[12]);
      int h = unitToHours(unit, val);
      if(h >= 0) return h;
   }

   if(pdtLen >= 23){ /* Fallback: pdt[18]/pdt[19..22] */
      uint8_t unit = pdt[18];
      int32_t val  = (int32_t)((pdt[19]<<24)|(pdt[20]<<16)|(pdt[21]<<8)|pdt[22]);
      int h = unitToHours(unit, val);
      if(h >= 0) return h;
   }

   /* Short conservative scan */
   int best=-1, bestScore=-1000000;
   int limit = (pdtLen > 64) ? 64 : (int)pdtLen;
   for(int uPos=0; uPos+5<=limit; uPos++){
      uint8_t unit = pdt[uPos];
      if(!(unit==0||unit==1||unit==2||unit==3||unit==4||unit==10||unit==11||unit==12||unit==13)) continue;
      int32_t val = (int32_t)((pdt[uPos+1]<<24)|(pdt[uPos+2]<<16)|(pdt[uPos+3]<<8)|pdt[uPos+4]);
      int h = unitToHours(unit, val);
      if(h < 0 || h > 384) continue;
      int sc = scoreHours(h);
      if(sc > bestScore || (sc==bestScore && (best<0 || h<best))){
         bestScore = sc; best = h;
      }
   }
   (void)pdtn;
   return best;
}

// =============================== Section 1 ==================================
typedef struct {
   int centerId, subcenterId, tableVersion;
   int refYear, refMonth, refDay, refHour, refMinute, refSecond;
} Sec1Info;

static bool parseSec1(const uint8_t *s, uint32_t bodyLen, Sec1Info *out){
   if(!s || bodyLen < 16 || !out) return false;
   Sec1Info x = {0};
   x.centerId     = (int)rdU16BE(s+0);
   x.subcenterId  = (int)rdU16BE(s+2);
   x.tableVersion = (int)rdU8(s+4);
   x.refYear      = (int)rdU16BE(s+7);
   x.refMonth     = (int)rdU8(s+9);
   x.refDay       = (int)rdU8(s+10);
   x.refHour      = (int)rdU8(s+11);
   x.refMinute    = (int)rdU8(s+12);
   x.refSecond    = (int)rdU8(s+13);
   *out = x;
   return true;
}

// =============================== Section 3 ==================================
typedef struct {
   int Ni, Nj;
   double lat1, lon1;  // degrees
   double di, dj;      // degrees (signs from La2/Lo2)
   int scanFlags;      // for the S7 order only
} Sec3Grid;

/* Parse GDT 3.0 (regular lat/lon) */
static bool parseSec3LatLon(const uint8_t *s, uint32_t bodyLen, Sec3Grid *out){
   if(!s || bodyLen < 9+58 || !out) return false;

   int gdt = (int)rdU16BE(s+7);
   if(gdt != 0) return false; // only 3.0 supported

   const uint8_t *g = s+9;

   int Ni = (int)rdU32BE(g+16);
   int Nj = (int)rdU32BE(g+20);
   uint32_t basicAngle = rdU32BE(g+24);
   uint32_t subAngle   = rdU32BE(g+28);

   int32_t  la1i = (int32_t)rdU32BE(g+32);
   uint32_t lo1u =          rdU32BE(g+36);
   int32_t  la2i = (int32_t)rdU32BE(g+41);
   uint32_t lo2u =          rdU32BE(g+45);

   uint32_t DiU  =          rdU32BE(g+49);
   uint32_t DjU  =          rdU32BE(g+53);
   int scan = (int)rdU8(g+57);

   if(Ni<=0 || Nj<=0) return false;
   if(DiU==0xFFFFFFFFu || DjU==0xFFFFFFFFu) return false; // quasi-regular not supported

   double unit = 1e-6;
   if(basicAngle!=0 && subAngle!=0 && subAngle!=0xFFFFFFFFu){
      unit = (double)basicAngle / (double)subAngle;
   }

   double lat1 = (double)la1i * unit;
   double lon1 = (double)lo1u * unit;
   double lat2 = (double)la2i * unit;
   double lon2 = (double)lo2u * unit;

   // Amplitudes positives
   double diAbs = (double)DiU * unit;
   double djAbs = (double)DjU * unit;

   // dj sign from (lat2 - lat1)
   double dj = (lat2 >= lat1) ? djAbs : -djAbs;

   // di sign from shortest path lon2-lon1 (wrap around)
   double dLon = lon2 - lon1;
   if     (dLon >  180.0) dLon -= 360.0;
   else if(dLon < -180.0) dLon += 360.0;
   double di = (dLon >= 0.0) ? diAbs : -diAbs;

   // Repair La1 if producer wrote junk but (lat2,Nj,dj) consistent
   if(!(fabs(lat1) <= 90.0) && fabs(lat2) <= 90.0 && Nj>1){
      lat1 = lat2 - (double)(Nj-1) * dj;
   }

   out->Ni = Ni;
   out->Nj = Nj;
   out->lat1 = lat1;
   out->lon1 = lon1;
   out->di = di;
   out->dj = dj;
   out->scanFlags = scan;
   return true;
}

// ---- Scan flags helpers (WMO): bit=1 means negative direction for i/j ------
static inline bool scanAdjI (int scan){ return ( (scan & 0x20) == 0 ); }  // 0 => adjacent along i
static inline bool scanBoustro(int scan){ return ( (scan & 0x10) != 0 ); } // 1 => boustrophedon

// =============================== Section 4 ==================================
typedef struct {
   int pdtn;                 // Product Definition Template Number (4.N)
   const uint8_t *pdt;       // pointer to PDT body (after pdtn)
   uint32_t pdtLen;
   int discipline, category, parameter;
} Sec4Info;

static bool parseSec4(const uint8_t *s, uint32_t bodyLen, int discipline, Sec4Info *out){
   if(!s || bodyLen < 6 || !out) return false;
   Sec4Info x;
   x.pdtn   = (int)rdU16BE(s+2);
   x.pdt    = s+4;               /* pdt[0] == octet 10 of section 4 */
   x.pdtLen = bodyLen - 4;
   x.discipline = discipline;
   x.category   = (int)rdU8(x.pdt+0);
   x.parameter  = (int)rdU8(x.pdt+1);
   *out = x;
   return true;
}

// =============================== Section 5 ==================================
// DRT 5.0 (simple packing) and 5.3 (IEEE float)
typedef struct {
   int drt;        // 0: simple packing, 3: IEEE float, else: unsupported
   float refV;
   int16_t bScale;
   int16_t dScale;
   int nBits;
} Sec5Info;

static bool parseSec5(const uint8_t *s, uint32_t bodyLen, Sec5Info *out){
   if(!s || bodyLen < 5 || !out) return false;
   int drt = (int)rdU16BE(s+4);
   if(drt == 0){
      if(bodyLen < 16) return false;
      Sec5Info d;
      d.drt    = 0;
      d.refV   = rdIEEE32BE(s+6);

      d.bScale = readS16beFlexible(s+10);
      d.dScale = readS16beFlexible(s+12);

      d.nBits  = (int)rdU8(s+14);
      *out = d;
      return true;
   } else if(drt == 3){
      Sec5Info d = {0};
      d.drt = 3;   // IEEE floats in Section 7
      *out = d;
      return true;
   } else {
      return false; // unsupported (e.g. 5.2, 5.40/41)
   }
}

// =============================== Section 6 ==================================
typedef struct {
   int indicator;              // 255: none, 0: bitmap here, 254: previous (unsupported)
   const uint8_t *bitmap;      // when indicator==0
   uint32_t bitmapLen;
} Sec6Info;

// ============================== Message parse ===============================
typedef struct {
   Sec1Info s1;
   Sec3Grid s3;
   Sec4Info s4;
   Sec5Info s5;
   Sec6Info s6;
   const uint8_t *sec7; uint32_t sec7Len;
   int haveS1, haveS3, haveS4, haveS5, haveS6, haveS7;
} MsgParse;

static int bmBitAt(const uint8_t *bm, size_t bmBytes, size_t k) {
   if(!bm) return 1;               // no bitmap => all present
   size_t byte = k >> 3;
   if(byte >= bmBytes) return 1;   // safety
   int bit = 7 - (int)(k & 7);
   return ( (bm[byte] >> bit) & 1u );
}

static int parseGrib2Message(const uint8_t *buf, size_t len, int wantData,
                             MsgParse *out, float **outValues)
{
   if(len < 16 || memcmp(buf,"GRIB",4)!=0) return -1;
   if(buf[7] != 2) return -1;
   uint64_t totalLen = rdU64BE(buf+8);
   if(totalLen > len || totalLen < 16) return -1;

   MsgParse mp; memset(&mp, 0, sizeof(mp));
   size_t off = 16;

   while(off + 5 <= totalLen){
      uint32_t sLen = rdU32BE(buf+off);
      if(sLen < 5 || off + sLen > totalLen) break;
      uint8_t sNum = rdU8(buf+off+4);
      const uint8_t *s = buf + off + 5;
      uint32_t bodyLen = sLen - 5;

      if(sNum == 1){
         if(parseSec1(s, bodyLen, &mp.s1)) mp.haveS1 = 1;
      }
      else if(sNum == 3){
         if(parseSec3LatLon(s, bodyLen, &mp.s3)) mp.haveS3 = 1;
      }
      else if(sNum == 4){
         int disc = (int)rdU8(buf+6);
         if(parseSec4(s, bodyLen, disc, &mp.s4)) mp.haveS4 = 1;
      }
      else if(sNum == 5){
         if(parseSec5(s, bodyLen, &mp.s5)) mp.haveS5 = 1;
      }
      else if(sNum == 6){
         if(bodyLen < 1) return -1;
         mp.s6.indicator = (int)rdU8(s+0);
         if(mp.s6.indicator == 0){
            mp.s6.bitmap    = s + 1;
            mp.s6.bitmapLen = bodyLen - 1;
            mp.haveS6 = 1;
         } else if(mp.s6.indicator == 255){
            mp.s6.bitmap = NULL; mp.s6.bitmapLen = 0; mp.haveS6 = 1;
         } else if(mp.s6.indicator == 254){
            return -1; // "previous bitmap" not supported
         } else {
            return -1;
         }
      }
      else if(sNum == 7){
         mp.sec7 = s; mp.sec7Len = bodyLen; mp.haveS7 = 1;
      }

      off += sLen;
   }

#if GRIB_DEBUG
   if(mp.haveS3){
      fprintf(stderr, "[S3] GDT=3.0 Ni=%d Nj=%d lat1=%g lon1=%g di=%g dj=%g scan=0x%02X\n",
              mp.s3.Ni, mp.s3.Nj, mp.s3.lat1, mp.s3.lon1, mp.s3.di, mp.s3.dj, mp.s3.scanFlags);
   }
   if(mp.haveS4 && mp.haveS5){
      const char *sn = shortNameFor(rdU8(buf+6), mp.s4.category, mp.s4.parameter);
      fprintf(stderr, "[S5] var=%s drt=%d R=%g E=%d D=%d nbits=%d (sec7=%u bytes)\n",
              sn, mp.s5.drt, (double)mp.s5.refV, (int)mp.s5.bScale,
              (int)mp.s5.dScale, (int)mp.s5.nBits, (unsigned)mp.sec7Len);
      if(mp.s5.drt==0 && mp.haveS7) debugPeekS7Integers(mp.sec7, mp.sec7Len, mp.s5.nBits, 12);
   }
#endif

   if(out) *out = mp;
   if(!wantData) { if(outValues) *outValues = NULL; return 0; }

   /* Need sections 3,5,7; section 6 optional (bitmap) */
   if(!(mp.haveS3 && mp.haveS5 && mp.haveS7)) return -1;

   int Ni = mp.s3.Ni, Nj = mp.s3.Nj;
   size_t nPts = (size_t)Ni * (size_t)Nj;

   float *vals = (float*)malloc(nPts * sizeof(float));
   if(!vals) return -1;

   const uint8_t *bm = (mp.haveS6 && mp.s6.indicator==0) ? mp.s6.bitmap : NULL;
   size_t bmBytes    = (mp.haveS6 && mp.s6.indicator==0) ? mp.s6.bitmapLen : 0;

   if(mp.s5.drt == 0){
      /* Simple packing (5.0) with optional bitmap */
      BitReader br; bitInit(&br, mp.sec7, mp.sec7Len);
      const double twoE = ldexp(1.0, mp.s5.bScale);
      const double dec  = pow(10.0, -(double)mp.s5.dScale);

      if(mp.s5.nBits == 0){
         float v = (float)(mp.s5.refV * dec);
         for(size_t k=0;k<nPts;k++){
            vals[k] = bmBitAt(bm, bmBytes, k) ? v : NAN;
         }
      } else {
         for(size_t k=0;k<nPts;k++){
            if(bmBitAt(bm, bmBytes, k)){
               uint32_t D = bitGet(&br, mp.s5.nBits);
               double   X = mp.s5.refV + (double)D * twoE;
               vals[k] = (float)(X * dec);
            } else {
               vals[k] = NAN;
            }
         }
      }
   }
   else if(mp.s5.drt == 3){
      /* IEEE floats in S7 (5.3), with optional bitmap */
      const uint8_t *q = mp.sec7;
      size_t off7 = 0, nAvail = mp.sec7Len;
      for(size_t k=0;k<nPts;k++){
         if(bmBitAt(bm, bmBytes, k)){
            if(off7 + 4 > nAvail){ free(vals); return -1; }
            vals[k] = rdIEEE32BE(q + off7);
            off7 += 4;
         } else {
            vals[k] = NAN;
         }
      }
   }
   else {
      free(vals);
      return -1; // unsupported packing (e.g. 5.2 / 5.40 / 5.41)
   }

   if(outValues) *outValues = vals; else free(vals);
   return 0;
}

// ======================== Indexing on Zone regular grid =====================
static inline long indLat(double lat, const Zone *zone){
   return (long)llround( (lat - zone->latMin) / zone->latStep );
}

// Wrap-aware longitude indexer around lonLeft in [lonLeft, lonLeft+360)
static inline long indLonWrap(double lon, const Zone *zone){
   double L = lon;
   double base = zone->lonLeft;
   // Normalise delta in [-180,180)
   double d = L - base;
   while(d < -180.0) d += 360.0;
   while(d >= 180.0) d -= 360.0;
   long i = (long)llround( d / zone->lonStep );
   if(i < 0) i += zone->nbLon;
   if(i >= zone->nbLon) i -= zone->nbLon;
   return i;
}

static inline long idxTij(int iT, int iLon, int iLat, const Zone *zone){
   return (long)iT * (long)zone->nbLat * (long)zone->nbLon
        + (long)iLat * (long)zone->nbLon
        + (long)iLon;
}

static int findTimeIndex(long timeStep, const Zone *zone){
   for(size_t i=0;i<zone->nTimeStamp;i++){
      if(zone->timeStamp[i] == timeStep) return (int)i;
   }
   return -1;
}

// ============================== Public: Lists ================================
bool readGribLists(const char *fileName, Zone *zone){
   if(!fileName || !zone) return false;

   struct stat st;
   if(stat(fileName, &st) != 0){
      fprintf(stderr, "readGribLists: cannot stat %s\n", fileName);
      return false;
   }
   FILE *f = fopen(fileName, "rb");
   if(!f){
      fprintf(stderr, "readGribLists: cannot open %s\n", fileName);
      return false;
   }

   uint8_t *buf = (uint8_t*)malloc((size_t)st.st_size);
   if(!buf){ fclose(f); fprintf(stderr,"readGribLists: malloc failed\n"); return false; }
   if(fread(buf,1,(size_t)st.st_size,f) != (size_t)st.st_size){
      fclose(f); free(buf); fprintf(stderr,"readGribLists: short read\n"); return false;
   }
   fclose(f);

   memset(zone, 0, sizeof(*zone));
   zone->editionNumber = 2;
   zone->stepUnits     = 1;   /* hours */

   const uint8_t *p = buf, *end = buf + st.st_size;
   while((p = findNextGrib(p, end)) != NULL){
      if(p + 16 > end) break;
      if(memcmp(p,"GRIB",4)!=0 || p[7]!=2){ p++; continue; }

      int discipline = (int)rdU8(p+6);
      uint64_t totalLen = rdU64BE(p+8);
      if(totalLen < 16 || p + totalLen > end){ break; }

      size_t off = 16;
      Sec1Info s1={0}; bool haveS1=false;
      Sec4Info s4={0}; bool haveS4=false;

      while(off + 5 <= totalLen){
         uint32_t sLen = rdU32BE(p+off);
         if(sLen < 5 || off + sLen > totalLen) break;
         uint8_t sNum = rdU8(p+off+4);
         const uint8_t *s = p + off + 5;
         uint32_t bodyLen = sLen - 5;

         if(sNum == 1 && !haveS1){
            haveS1 = parseSec1(s, bodyLen, &s1);
         } else if(sNum == 4 && !haveS4){
            haveS4 = parseSec4(s, bodyLen, discipline, &s4);
         }

         if(haveS1 && haveS4) break;
         off += sLen;
      }

      if(haveS1){
         zone->centreId = s1.centerId;
         long dataDate = (long)(s1.refYear*10000 + s1.refMonth*100 + s1.refDay);
         long dataTime = (long)(s1.refHour*100 + s1.refMinute);
         zone->nDataDate = updateLongUnique(dataDate, zone->nDataDate, MAX_N_DATA_DATE, zone->dataDate);
         zone->nDataTime = updateLongUnique(dataTime, zone->nDataTime, MAX_N_DATA_TIME, zone->dataTime);
      }

      if(haveS4){
         const char *sn = shortNameFor(s4.discipline, s4.category, s4.parameter);

         bool found=false;
         for(size_t i=0;i<zone->nShortName;i++){
            if(strcmp(zone->shortName[i], sn)==0){ found=true; break; }
         }
         if(!found && zone->nShortName < MAX_N_SHORT_NAME){
            strCopySafe(zone->shortName[zone->nShortName], MAX_SIZE_SHORT_NAME, sn);
            zone->nShortName++;
         }

         // sn = shortName calculé via shortNameFor(s0.discipline, s4.category, s4.parameter)
         bool isCur = isCurrentTriplet(discipline, s4.category, s4.parameter, NULL);

         bool keepForTime = (
            strcmp(sn,"10u")==0 || strcmp(sn,"10v")==0 ||
            strcmp(sn,"gust")==0 || strcmp(sn,"swh")==0 ||
            strcmp(sn,"ucurr")==0 || strcmp(sn,"vcurr")==0 ||  // via shortName
            isCur                                                // sécurité si shortName reste "unknown"
         );

         if(keepForTime){
            int h = pdtLeadHours(s4.pdtn, s4.pdt, s4.pdtLen);
            if(h >= 0){
               zone->nTimeStamp = updateLongUnique(h, zone->nTimeStamp, MAX_N_TIME_STAMPS, zone->timeStamp);
            }
         }
      }

      p += totalLen;
   }

   for(size_t i=0;i<zone->nShortName;i++){
      if(strcmp(zone->shortName[i], "unknown")==0){
         strCopySafe(zone->shortName[i], MAX_SIZE_SHORT_NAME, "gust?");
      }
   }

   sortLongAsc(zone->timeStamp, zone->nTimeStamp);

   zone->intervalLimit = 0;
   if(zone->nTimeStamp > 1){
      zone->intervalBegin = zone->timeStamp[1] - zone->timeStamp[0];
      zone->intervalEnd   = zone->timeStamp[zone->nTimeStamp-1] - zone->timeStamp[zone->nTimeStamp-2];
      for(size_t i=1;i<zone->nTimeStamp; i++){
         if((zone->timeStamp[i] - zone->timeStamp[i-1]) == zone->intervalEnd){
            zone->intervalLimit = i;
            break;
         }
      }
   } else {
      zone->intervalBegin = zone->intervalEnd = 3;
   }

   free(buf);
   return true;
}

// =========================== Public: Parameters =============================
bool readGribParameters(const char *fileName, Zone *zone){
   if(!fileName || !zone) return false;

   struct stat st;
   if(stat(fileName, &st) != 0){ fprintf(stderr,"readGribParameters: stat fail\n"); return false; }

   FILE *f = fopen(fileName, "rb");
   if(!f){ fprintf(stderr,"readGribParameters: open fail\n"); return false; }

   uint8_t hdr[16];
   if(fread(hdr,1,16,f)!=16){ fclose(f); fprintf(stderr,"readGribParameters: short read\n"); return false; }
   if(memcmp(hdr,"GRIB",4)!=0 || hdr[7]!=2){ fclose(f); fprintf(stderr,"readGribParameters: not GRIB2\n"); return false; }
   uint64_t totalLen = rdU64BE(hdr+8);

   uint8_t *msg = (uint8_t*)malloc((size_t)totalLen);
   if(!msg){ fclose(f); fprintf(stderr,"readGribParameters: malloc fail\n"); return false; }
   fseek(f,0,SEEK_SET);
   if(fread(msg,1,(size_t)totalLen,f)!=(size_t)totalLen){ free(msg); fclose(f); fprintf(stderr,"readGribParameters: short read 2\n"); return false; }
   fclose(f);

   size_t off = 16;
   Sec1Info s1={0}; bool haveS1=false;
   Sec3Grid g3={0}; bool haveS3=false;

   while(off + 5 <= totalLen){
      uint32_t sLen = rdU32BE(msg+off);
      if(sLen < 5 || off + sLen > totalLen) break;
      uint8_t sNum = rdU8(msg+off+4);
      const uint8_t *s = msg + off + 5;
      uint32_t bodyLen = sLen - 5;

      if(sNum == 1 && !haveS1){
         haveS1 = parseSec1(s, bodyLen, &s1);
      } else if(sNum == 3 && !haveS3){
         haveS3 = parseSec3LatLon(s, bodyLen, &g3);
      }

      if(haveS1 && haveS3) break;
      off += sLen;
   }

   if(haveS1) zone->centreId = s1.centerId;
   zone->editionNumber = 2;
   zone->stepUnits = 1; // hours

   if(!haveS3){ free(msg); return false; }

   zone->nbLon = g3.Ni;
   zone->nbLat = g3.Nj;
   zone->lonStep = fabs(g3.di);
   zone->latStep = fabs(g3.dj);

   double latStart = g3.lat1;
   double latEnd   = g3.lat1 + (g3.Nj - 1) * g3.dj;
   double lonStart = g3.lon1;
   double lonEnd   = g3.lon1 + (g3.Ni - 1) * g3.di;

   zone->latMin = fmin(latStart, latEnd);
   zone->latMax = fmax(latStart, latEnd);

   zone->lonLeft  = norm180(lonStart);
   zone->lonRight = norm180(lonEnd);

   if(norm180(zone->lonLeft) > 0.0 && norm180(zone->lonRight) < 0.0){
      zone->anteMeridian = true;   // crosses +180/-180
   } else {
      zone->anteMeridian = false;
      zone->lonLeft  = norm180(zone->lonLeft);
      zone->lonRight = norm180(zone->lonRight);
   }

   zone->numberOfValues = (long)zone->nbLon * (long)zone->nbLat;

   free(msg);
   return true;
}

// ============================== Public: All data ============================
/* Decode entire file and fill tGribData[iFlow] with FlowP.
   - lat/lon are pre-filled for each (t,i,j) from Zone geometry
   - values mapped by shortName to u/v/g/w
   - waves: missing values forced to 0.0 (cosmetic)
*/
bool readGribAll (const char *fileName, Zone *zone, int iFlow){
   if ((iFlow == WIND && par.constWindTws > 0) || (iFlow == CURRENT && par.constCurrentS > 0)) { // constant wind or current. Dont read file
      initZone (zone);
      return true;
   }
   if(!fileName || !zone) return false;

   zone->wellDefined = false;

   if(!readGribLists(fileName, zone)) return false;
   if(!readGribParameters(fileName, zone)) return false;

   if(zone->nDataDate > 1){
      fprintf(stderr, "readGribAll: more than 1 dataDate not supported (n=%zu)\n", zone->nDataDate);
      return false;
   }

   size_t totalPts = (size_t)zone->nTimeStamp * (size_t)zone->nbLat * (size_t)zone->nbLon;
   if(tGribData[iFlow]){ free(tGribData[iFlow]); tGribData[iFlow] = NULL; }
   tGribData[iFlow] = (FlowP*)calloc(totalPts, sizeof(FlowP));
   if(!tGribData[iFlow]){
      fprintf(stderr, "readGribAll: calloc tGribData failed\n");
      return false;
   }

   /* Pre-fill lat/lon for all grid points (time-invariant geometry) */
   for(int j=0;j<zone->nbLat;j++){
      double lat = zone->latMin + j * zone->latStep * ((zone->latMax>=zone->latMin)?1.0:-1.0);
      for(int i=0;i<zone->nbLon;i++){
         double lon = zone->lonLeft + i * zone->lonStep;
         if(!zone->anteMeridian) lon = norm180(lon);
         for(size_t t=0;t<zone->nTimeStamp;t++){
            long k = idxTij((int)t, i, j, zone);
            tGribData[iFlow][k].lat = (float)lat;
            tGribData[iFlow][k].lon = (float)lon;
         }
      }
   }

   /* Read entire file */
   struct stat st;
   if(stat(fileName,&st)!=0){ fprintf(stderr,"readGribAll: stat fail\n"); return false; }
   FILE *f = fopen(fileName,"rb");
   if(!f){ fprintf(stderr,"readGribAll: open fail\n"); return false; }

   uint8_t *buf = (uint8_t*)malloc((size_t)st.st_size);
   if(!buf){ fclose(f); fprintf(stderr,"readGribAll: malloc fail\n"); return false; }
   if(fread(buf,1,(size_t)st.st_size,f) != (size_t)st.st_size){
      fclose(f); free(buf); fprintf(stderr,"readGribAll: short read\n"); return false;
   }
   fclose(f);

   const uint8_t *p = buf, *end = buf + st.st_size;
   zone->nMessage = 0;
   zone->allTimeStepOK = true;

   while((p = findNextGrib(p, end)) != NULL){
      uint64_t totalLen = rdU64BE(p+8);
      if(totalLen < 16 || p + totalLen > end) break;
      int discipline = (int)rdU8(p+6);



      MsgParse mp; float *vals = NULL;
      if(parseGrib2Message(p, (size_t)totalLen, /*wantData=*/1, &mp, &vals) == 0){
         const char *sn = shortNameFor(rdU8(p+6), mp.s4.category, mp.s4.parameter);

         bool writeU = (strcmp(sn,"10u")==0 || strcmp(sn,"ucurr")==0);
         bool writeV = (strcmp(sn,"10v")==0 || strcmp(sn,"vcurr")==0);
         bool writeG = (strcmp(sn,"gust")==0);
         bool writeW = (strcmp(sn,"swh")==0);

         // Si sn=="unknown" mais triplet “courant”, force U/V
         bool isU=false;
         bool isCur = isCurrentTriplet(discipline, mp.s4.category, mp.s4.parameter, &isU);

         int h = pdtLeadHours(mp.s4.pdtn, mp.s4.pdt, mp.s4.pdtLen);
         int iT = (h>=0) ? findTimeIndex(h, zone) : -1;

         if(vals && iT>=0 && (writeU||writeV||writeG||writeW)){
            /* Respect Section 3 scanning mode for the S7 walk (order only) */
            const int Ni = mp.s3.Ni, Nj = mp.s3.Nj;
            const double lat1 = mp.s3.lat1, lon1 = mp.s3.lon1, di = mp.s3.di, dj = mp.s3.dj;
            const int scan = mp.s3.scanFlags;

            /* Use S3 geometry for directions; keep scan flags only for adjacency & boustrophedon */
            const bool iPlus = (di > 0);
            const bool jPlus = (dj > 0);
            const bool adjI  = scanAdjI(scan);     // 0 => adjacent along i (row-major)
            const bool boust = scanBoustro(scan);  // 1 => boustrophedon


            size_t lin = 0;

            if(adjI){
               int jStart = jPlus ? 0 : (Nj-1);
               int jStep  = jPlus ? 1 : -1;

               for(int jj=0; jj<Nj; jj++){
                  int j = jStart + jj*jStep;

                  bool thisIPlus = iPlus ^ (boust && ((jj & 1) != 0));
                  int iStart = thisIPlus ? 0 : (Ni-1);
                  int iStep  = thisIPlus ? 1 : -1;

                  for(int ii=0; ii<Ni; ii++){
                     int i = iStart + ii*iStep;
                     if(lin >= (size_t)Ni*(size_t)Nj) break;

                     double lat = lat1 + j * dj;
                     double lon = lon1 + i * di;
                     if(!zone->anteMeridian) lon = norm180(lon);

                     long iLat = indLat(lat, zone);
                     long iLon = indLonWrap(lon, zone);
                     if(iLat < 0 || iLat >= zone->nbLat || iLon < 0 || iLon >= zone->nbLon){
                        lin++; continue;
                     }

                     float v = vals[lin++];

                     long k = idxTij(iT, (int)iLon, (int)iLat, zone);
                     if (isnan (v)) v = 0.0f;  // prefer 0 to nan 
                     if (isCur && strcmp(sn,"unknown")==0){
                        if (isU) tGribData[iFlow][k].u = v; else tGribData[iFlow][k].v = v;
                     } else {
                        if (isnan (v)) v = 0.0f;  // prefer 0 to nan 
                        if (writeU) tGribData[iFlow][k].u = v;
                        if (writeV) tGribData[iFlow][k].v = v;
                        if (writeG) tGribData[iFlow][k].g = v;
                        if (writeW) tGribData[iFlow][k].w = v;
                     }

                  }
               }
            } else {
               bool baseIPlus = iPlus;
               int iStartBase = baseIPlus ? 0 : (Ni-1);
               int iStepBase  = baseIPlus ? 1 : -1;

               for(int ii=0; ii<Ni; ii++){
                  int i = iStartBase + ii*iStepBase;

                  bool thisJPlus = jPlus ^ (boust && ((ii & 1) != 0));
                  int jStart = thisJPlus ? 0 : (Nj-1);
                  int jStep  = thisJPlus ? 1 : -1;

                  for(int jj=0; jj<Nj; jj++){
                     int j = jStart + jj*jStep;
                     if(lin >= (size_t)Ni*(size_t)Nj) break;

                     double lat = lat1 + j * dj;
                     double lon = lon1 + i * di;
                     if(!zone->anteMeridian) lon = norm180(lon);

                     long iLat = indLat(lat, zone);
                     long iLon = indLonWrap(lon, zone);
                     if(iLat < 0 || iLat >= zone->nbLat || iLon < 0 || iLon >= zone->nbLon){
                        lin++; continue;
                     }

                     float v = vals[lin++];

                     long k = idxTij(iT, (int)iLon, (int)iLat, zone);
                     if(writeU) tGribData[iFlow][k].u = v;
                     if(writeV) tGribData[iFlow][k].v = v;
                     if(writeG) tGribData[iFlow][k].g = v;
                     if(writeW) tGribData[iFlow][k].w = isnan(v) ? 0.0f : v;
                  }
               }
            }
         }

         free(vals);
      }

      zone->nMessage += 1;
      p += totalLen;
   }

   free((void*)buf);
   zone->wellDefined = true;
   return true;
}

