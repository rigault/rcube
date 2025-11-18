/*!
   Create a synthetic grib based on static wind specified as an array ot time min, time max, twd, tws, u
   Compilation : gcc -Wall -Wextra -o mkgrib mkgrib.c -leccodes -lm
   Check: grib_ls -p Ni,Nj,numberOfDataPoints,numberOfValues,shortName,step SYN_*.grb | head
   Out file form: SYN_YYYYDDMM_HHZ_<totalHours>.grb
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <eccodes.h>
#include "../csources/glibwrapper.h"

#define KN_TO_MS              (1852.0/3600.0)            // conversion knots to meter/second
#define DEG_TO_RAD            (M_PI/180.0)               // conversion degree to radius
#define MAX_SIZE_LINE         1024
#define MAX_LEN               100
#define MAX_SIZE_NAME         64
#define MAX_AREA              8

// Wind specifications
typedef struct {
   int t;
   double twd;
   double tws;
   double w;
} Wind;

struct {
   char   name [MAX_SIZE_NAME];
   double lonFirst;   
   double lonLast;   
   double latFirst;
   double latLast;
   Wind   wind [MAX_LEN];
   size_t len;
} area [MAX_AREA];

int nArea = 0;

struct {
   int tMin;
   int tMax;
   double lonFirst;   
   double lonLast;   
   double latFirst;
   double latLast;
} areaUnion;

// Time configuration
static long stepHours = 1;          // time step between fields in hours
static long dataDate  = 0;          // grib datadate
static long dataTime  = 0;          // grib datatime

/* Grid: 60N -> 0, 60W -> 10E, 1° resolution */
static const double di =  1.0;
static const double dj =  1.0;

static long ni;
static long nj;

/*! Conversion twd, tws to U component (m/s) */
static double getU (double twd, double tws) {
   return - KN_TO_MS * tws * sin (DEG_TO_RAD * twd);
}

/*! Conversion twd, tws to UV component (m/s) */
static double getV (double twd, double tws) {
   return - KN_TO_MS * tws * cos (DEG_TO_RAD * twd);
}

/*! return fx: linear interpolation */
static inline double interpolate (double x, double x0, double x1, double fx0, double fx1) {
   x = CLAMP (x, x0, x1); // no extrapolation 
   if (x1 == x0) return fx0;
   else return fx0 + (x-x0) * (fx1-fx0) / (x1-x0);
}

/* Calculate superset of areas (lat, lon) and (tMin, tMax) in areaUnion */
static void makeUnion () {
   int tMin = 99999;
   int tMax = 0;
   double latFirst = -90.0, latLast = 90.0, lonFirst = 360.0, lonLast = -180.0;
   for (int i = 0; i < nArea; i += 1) {
      const size_t len = area[i].len;
      latFirst = MAX (latFirst, area[i].latFirst);
      latLast  = MIN (latLast, area[i].latLast);
      lonFirst = MIN (lonFirst, area[i].lonFirst);
      lonLast  = MAX (lonLast, area[i].lonLast);
      tMin     = MIN (tMin, area[i].wind[0].t);
      tMax     = MAX (tMax, area[i].wind[len -1].t);
   }
   areaUnion.latFirst = latFirst, areaUnion.latLast = latLast;  
   areaUnion.lonFirst = lonFirst, areaUnion.lonLast = lonLast;  
   areaUnion.tMin = tMin, areaUnion.tMax= tMax;
}

/*! Find u, v, w as a function of step, and array of wind
    can make interpolation */
static void findUVW (int step, Wind *wind, size_t len, double *uVal, double *vVal, double *wVal) {
   if (step < wind [0].t || step > areaUnion.tMax || len < 1) {
      fprintf (stderr, "Error in findUVW: check step number and len. step: %d, len %ld\n", step, len);
      exit (EXIT_FAILURE);
   }
   int foundMin = len - 1; // in case not found we take last
   int foundMax = len - 1;
   for (size_t i = 0; i < len; i += 1) {
      if (step == wind[i].t) { 
         foundMin = foundMax = i;
         break;
      }
      if (step < wind[i].t) {
         foundMin = i - 1; 
         foundMax = i;
         if (foundMin < 0) foundMin = 0;
         break;
      }
   }
   const double tMin = wind[foundMin].t;
   const double tMax = wind[foundMax].t;
   const double twdMin = wind[foundMin].twd;
   const double twdMax = wind[foundMax].twd;
   const double twsMin = wind[foundMin].tws;
   const double twsMax = wind[foundMax].tws;
   const double wMin = wind[foundMin].w;
   const double wMax = wind[foundMax].w;
   const double twdVal = interpolate ((double) step, tMin, tMax, twdMin, twdMax);
   const double twsVal = interpolate ((double) step, tMin, tMax, twsMin, twsMax);
      
   *uVal = getU (twdVal, twsVal);
   *vVal = getV (twdVal, twsVal);
   *wVal = interpolate ((double) step, tMin, tMax, wMin, wMax);
      // printf ("tMin: %.2lf, step:%d, tMax:%.2lf, uVal: %.2lf, vVal:%.2lf\n", tMin, step, tMax, *uVal, *vVal); 
}

/*! push a new item in area table */
static int pushArea (int n, char *name, size_t maxLen) {
   strlcpy (area[n].name, name, maxLen);
   if (n >= MAX_AREA) {
      fprintf (stderr, "Number of Area Exeeded: %d\n", n);
      exit (0);
   }
   area[n].len = 0;
   return n + 1;
}

/*! read wind spec and return false/true */
static bool readWind (const char *fileName) {
   char line [MAX_SIZE_LINE];
   char name [MAX_SIZE_NAME];
   FILE *f = NULL;
   char *pt;
   int count = 0;
   double val;
   int t;
   double twd, tws, w;
   bool inWind = false;
   bool inArea = false;

   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In readWind, Error Cannot open: %s\n", fileName);
      return false;
   }
   while (fgets(line, sizeof line, f)) {
      count += 1;
      if ((pt = strchr (line, '#')) != NULL) *pt = '\0';       // comment elimination
      g_strstrip(line);
      if (line [0] == '\0') continue;
      // printf ("%s\n", line);
      const int len = (nArea == 0) ? 0 : area[nArea-1].len; 
      if (strncmp(line, "---", 3) == 0) {}                     // ignore
      else if (sscanf(line, "dataDate: %ld", &dataDate)) {}
      else if (sscanf(line, "dataTime: %ld", &dataTime)) {}
      else if (sscanf(line, "stepHours: %ld", &stepHours)){}
      else if (strncmp(line, "areas:", 6) == 0) inArea = true;
      else if (sscanf(line, "- name: %s", name) && inArea) {
         nArea = pushArea (nArea, name, MAX_SIZE_NAME);
         inWind = false;
      }
      else if (sscanf(line, "latFirst: %lf", &val) && (nArea > 0)) area[nArea - 1].latFirst = val;
      else if (sscanf(line, "latLast: %lf",  &val) && (nArea > 0)) area[nArea - 1].latLast = val;
      else if (sscanf(line, "lonFirst: %lf", &val) && (nArea > 0)) area[nArea - 1].lonFirst = val;
      else if (sscanf(line, "lonLast: %lf",  &val) && (nArea > 0)) area[nArea - 1].lonLast = val;
      else if (strncmp(line, "winds:", 5) == 0) inWind = true;
      else if ((sscanf(line, "- [%d, %lf, %lf, %lf]", &t, &twd, &tws, &w) == 4) && (nArea > 0) && (len < MAX_LEN) && inWind) {
      if (t >= area[nArea - 1].wind[len].t) {
            area[nArea - 1].wind[len].t = t;
         }
         else {
            fprintf (stderr, "Error reading file: %s, t should progress within area: %d\n", fileName, nArea - 1);  
            return false;
         }
         area[nArea - 1].wind[len].twd = twd;
         area[nArea - 1].wind[len].tws = tws;
         area[nArea - 1].wind[len].w = w;
         area[nArea - 1].len += 1;
         // printf ("nArea: %d,  wind len: %ld\n", nArea -1, area[nArea - 1].len);
         if (area[nArea -1].len >= MAX_LEN) {
            fprintf (stderr, "Error reading file: %s, maxLen reached: %d\n", fileName, len);  
            return false;
         }
      }
      else {
         fprintf (stderr, "Error reading file: %s, line: %d\n%s\n", fileName, count, line);  
      }
   }
   return true;
}
 
/*! Compute T0 as the previous synoptic time (0,6,12,18 UTC) and return dataDate/dataTime */
static void computeT0(long* outDataDate, long* outDataTime) {
   time_t now = time(NULL);
   struct tm* utc = gmtime(&now);
   if (!utc) {
      fprintf(stderr, "gmtime() failed\n");
      exit(1);
   }

   // Snap to previous synoptic hour: 0, 6, 12, 18 
   utc->tm_min  = 0;
   utc->tm_sec  = 0;
   utc->tm_hour = (utc->tm_hour / 6) * 6;

   long year  = utc->tm_year + 1900;
   long month = utc->tm_mon + 1;
   long day   = utc->tm_mday;
   long hour  = utc->tm_hour;

   *outDataDate = year * 10000 + month * 100 + day; // YYYYMMDD 
   *outDataTime = hour * 100;                       // HHMM (00, 600, 1200, 1800 -> 0,600,1200,1800)
}

/*! Configure the regular_ll grid and packing for a handle */
static void configureGridAndPacking(codes_handle* handle) {
   const char* gridType = "regular_ll";
   const char* packingType = "grid_simple";
   size_t len;
   int i = 0;

   len = strlen(gridType);
   CODES_CHECK(codes_set_string(handle, "gridType", gridType, &len), "set gridType");

   CODES_CHECK(codes_set_long(handle,   "Ni", ni), "set Ni");
   CODES_CHECK(codes_set_long(handle,   "Nj", nj), "set Nj");
   CODES_CHECK(codes_set_double(handle, "latitudeOfFirstGridPointInDegrees",  area[i].latFirst), "set latFirst");
   CODES_CHECK(codes_set_double(handle, "longitudeOfFirstGridPointInDegrees", area[i].lonFirst), "set lonFirst");
   CODES_CHECK(codes_set_double(handle, "latitudeOfLastGridPointInDegrees",   area[i].latLast),  "set latLast");
   CODES_CHECK(codes_set_double(handle, "longitudeOfLastGridPointInDegrees",  area[i].lonLast),  "set lonLast");
   CODES_CHECK(codes_set_double(handle, "iDirectionIncrementInDegrees", di), "set di");
   CODES_CHECK(codes_set_double(handle, "jDirectionIncrementInDegrees", dj), "set dj");

   len = strlen(packingType);
   CODES_CHECK(codes_set_string(handle, "packingType", packingType, &len), "set packingType");
   CODES_CHECK(codes_set_long(handle, "bitmapPresent", 0), "set bitmapPresent");
}

/*! Configure common metadata: level, date/time, step */
static void configureMeta(codes_handle* handle, long paramId, long dataDate, long dataTime, long step) {
   const char* typeOfLevel = "heightAboveGround";
   const char* stepType    = "instant";
   size_t len;

   CODES_CHECK(codes_set_long(handle, "editionNumber", 2), "set editionNumber");
   CODES_CHECK(codes_set_long(handle, "paramId", paramId), "set paramId");

   len = strlen(typeOfLevel);
   CODES_CHECK(codes_set_string(handle, "typeOfLevel", typeOfLevel, &len), "set typeOfLevel");

   CODES_CHECK(codes_set_long(handle, "level", 10), "set level (10m)");

   CODES_CHECK(codes_set_long(handle, "dataDate", dataDate), "set dataDate");
   CODES_CHECK(codes_set_long(handle, "dataTime", dataTime), "set dataTime");

   len = strlen(stepType);
   CODES_CHECK(codes_set_string(handle, "stepType", stepType, &len), "set stepType");
   CODES_CHECK(codes_set_long(handle, "step", step), "set step");
}

/* Check if a point (lat,lon) is inside area [latFirst..latLast, lonFirst..lonLast]
   Latitudes: latFirst >= latLast (north to south)
   Longitudes: lonFirst <= lonLast (west to east) */
static bool pointInArea(double lat, double lon, double latFirst, double latLast, double lonFirst, double lonLast) {
   return (lat <= latFirst && lat >= latLast && lon >= lonFirst && lon <= lonLast);
}

/* Build u/v/w fields for one step, with multiple areass */
static void buildWindFieldsForStep(long step, double* uValues, double* vValues, double *wValues) {
   // Loop over grid points 
   for (long j = 0; j < nj; ++j) {
      // Lat decreases 
      double lat = areaUnion.latFirst - j * dj;

      for (long i = 0; i < ni; ++i) {
         // Lon increases
         double lon = areaUnion.lonFirst + i * di;
         size_t idx = (size_t)j * (size_t)ni + (size_t)i;
         double uVal = 0.0, vVal = 0.0, wVal = 0.0;

         for (int k = 0; k < nArea; k += 1) {
            if (pointInArea(lat, lon, area[k].latFirst, area[k].latLast, area[k].lonFirst, area[k].lonLast)) {
               findUVW (step, area[k].wind, area[k].len, &uVal, &vVal, &wVal); 
            }
         }
         uValues[idx] = uVal;
         vValues[idx] = vVal;
         wValues[idx] = wVal;
      }
   }
}

/*! Check consistency of areas and time progression */
static void checkWind () {
   if (nArea == 0) {
      fprintf (stderr, "Error in checkWind, no area\n");  
      exit (EXIT_FAILURE);
   }
   for (int i = 0; i < nArea; i += 1) {
      const int len = area [i].len;
      if (len < 2) {
         fprintf (stderr, "Error in checkWind, length of area: %d should be >= 2 len: %d\n", i, len);  
         exit (EXIT_FAILURE);
      }
      for (int k = 1; k < len; k += 1) { 
         if (area[i].wind[k].t < area[i].wind [k-1].t) {
            fprintf (stderr, "Error in checkWind, time should progress. See area: %d\n", i);  
            exit( EXIT_FAILURE);
         }
      }
   }
}

/*! Free All */
static void free3 (void *a, void *b, void *c) {
   free (a);
   free (b);
   free (c);
}

int main(int argc, char* argv[]) {
   const char* sampleName = "GRIB2";
   char outFileName[64];

   if (argc != 2) {
      fprintf (stderr, "Synopsys: %s <fileName>\n", argv [0]);
      return EXIT_FAILURE;
   } 
   if (!readWind (argv [1])) return EXIT_FAILURE;
   checkWind ();
   makeUnion ();
   printf ("Number of Area read: %d\n", nArea);

   if ((areaUnion.latFirst > area [0].latFirst) || (areaUnion.latLast < area [0].latLast) ||
      (areaUnion.lonFirst < area [0].lonFirst) || (areaUnion.lonLast > area [0].lonLast)) {
      fprintf (stderr, "Error: Area 0 should enveloppe all others. Check latFirst, latLast, lonFirst, lonLast.\n");
      return EXIT_FAILURE;
   }

   ni = 1 + (areaUnion.lonLast - areaUnion.lonFirst) / di;
   nj = 1 + (areaUnion.latFirst - areaUnion.latLast) / dj;

   const size_t nPoints = (size_t)(ni * nj);
   double* uValues = malloc(nPoints * sizeof(double));
   double* vValues = malloc(nPoints * sizeof(double));
   double* wValues = malloc(nPoints * sizeof(double));
   if (!uValues || !vValues || !wValues) {
      fprintf(stderr, "Error: malloc failed\n");
      free3 (uValues, vValues, wValues);
      return EXIT_FAILURE;
   }
   if (dataDate == 0) computeT0(&dataDate, &dataTime);

   const long hour = dataTime / 100;
   const long totalHours = areaUnion.tMax;
   snprintf(outFileName, sizeof(outFileName), "SYN_%08ld_%02ldZ_%ld.grb", dataDate, hour, totalHours);

   fprintf(stdout, "dataDate: %ld, dataTime: %04ld\n", dataDate, dataTime);
   fprintf(stdout, "stepHours: %ld, di: %.2lf, dj: %.2lf\n", stepHours, di, dj);
   fprintf(stdout, "totalHours: %ld\n", totalHours);
   fprintf(stdout, "union tMin: %d, tMax: %d\n", areaUnion.tMin, areaUnion.tMax);
   fprintf(stdout, "union latFirst: %.2lf, latLast: %.2lf, lonFirst: %.2lf, lonLast: %.2lf\n\n", 
         areaUnion.latFirst, areaUnion.latLast, areaUnion.lonFirst, areaUnion.lonLast); 

   bool firstMessage = true;

   // Main loop over forecast steps
   for (long step = 0; step <= totalHours; step += stepHours) {
      codes_handle* handle = NULL;
      buildWindFieldsForStep(step, uValues, vValues, wValues);

      // ---------- 10u field (paramId 165) ---------- 
      handle = codes_handle_new_from_samples(0, sampleName);
      if (!handle) {
         fprintf(stderr, "Error: codes_handle_new_from_samples failed for 10u\n");
         free3 (uValues, vValues, wValues);
         return EXIT_FAILURE;
      }

      configureGridAndPacking(handle);
      configureMeta(handle, 165, dataDate, dataTime, step); // 165 = 10u
      CODES_CHECK(codes_set_double_array(handle, "values", uValues, nPoints), "set values 10u");
      CODES_CHECK(codes_write_message(handle, outFileName, firstMessage ? "w" : "a"), "writeMessage");
      firstMessage = false;
      codes_handle_delete(handle);

      // ---------- 10v field (paramId 166) ----------
      handle = codes_handle_new_from_samples(0, sampleName);
      if (!handle) {
         fprintf(stderr, "Error: codes_handle_new_from_samples failed for 10v\n");
         free3 (uValues, vValues, wValues);
         return EXIT_FAILURE;
      }
      configureGridAndPacking(handle);
      configureMeta(handle, 166, dataDate, dataTime, step); // 166 = 10v
      CODES_CHECK(codes_set_double_array(handle, "values", vValues, nPoints), "set values 10v");
      CODES_CHECK(codes_write_message(handle, outFileName, firstMessage ? "w" : "a"), "writeMessage");
      firstMessage = false;
      codes_handle_delete(handle);

      // ---------- swh field (waves paramId 140229) ----------
      handle = codes_handle_new_from_samples(0, sampleName);
      if (!handle) {
         fprintf(stderr, "Error: codes_handle_new_from_samples failed for swh\n");
         free3 (uValues, vValues, wValues);
         return EXIT_FAILURE;
      }
      configureGridAndPacking(handle);
      configureMeta(handle, 140229, dataDate, dataTime, step); // 140229 = swh
      CODES_CHECK(codes_set_double_array(handle, "values", wValues, nPoints), "set values swh");
      CODES_CHECK(codes_write_message(handle, outFileName, firstMessage ? "w" : "a"), "writeMessage");
      firstMessage = false;
      codes_handle_delete(handle);
   }

   free3 (uValues, vValues, wValues);

   for (int i = 0; i < nArea; i += 1) {
      fprintf(stdout, "areaName: %s\n", area[i].name);
      fprintf(stdout, "latFirst: %.2lf, latLast: %.2lf, lonFirst: %.2lf, lonLast: %.2lf\n", 
         area[i].latFirst, area[i].latLast, area[i].lonFirst, area[i].lonLast); 
      for (size_t j = 0; j < area[i].len; j += 1) {
         fprintf(stdout, "%03d, %6.2lf, %6.2lf, %6.2lf\n", 
            area[i].wind[j].t, area[i].wind[j].twd, area[i].wind[j].tws, area[i].wind[j].w);
      }
      printf ("\n");
   }
   fprintf(stdout, "output file: %s\n", outFileName);
}

