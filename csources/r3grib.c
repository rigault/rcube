/*! compilation: gcc -c grib.c `pkg-config --cflags glib-2.0` */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <locale.h>
#include "glibwrapper.h"
#include "r3types.h"
#include "r3util.h"
#include "inline.h"
#include "readgriball.h"

#define  EPSILON 0.001                 // for approximat value

/*! return difference in hours between two zones (current zone and Wind zone) */
double zoneTimeDiff (const Zone *zone1, const Zone *zone0) {
   if (zone1->wellDefined && zone0->wellDefined) {
      time_t time1 = gribDateTimeToEpoch (zone1->dataDate [0], zone1->dataTime [0]);
      time_t time0 = gribDateTimeToEpoch (zone0->dataDate [0], zone0->dataTime [0]);
      return (time1 - time0) / 3600.0;
   }
   else return 0;
} 

/*! print Grib u v ... for all lat lon time information */
void printGrib (const Zone *zone, const FlowP *gribData) {
   long iGrib;
   printf ("printGribAll\n");
   for (size_t  k = 0; k < zone->nTimeStamp; k++) {
      double t = zone->timeStamp [k];
      printf ("Time: %.0lf\n", t);
      printf ("lon   lat   u     v     g     w\n");
      for (int i = 0; i < zone->nbLat; i++) {
         for (int j = 0; j < zone->nbLon; j++) {
	         iGrib = (k * zone->nbLat * zone->nbLon) + (i * zone->nbLon) + j;
            printf (" %6.2f %6.2f %6.2f %6.2f %6.2f %6.2f\n", \
            gribData [iGrib].lon, \
	         gribData [iGrib].lat, \
            gribData [iGrib].u, \
            gribData [iGrib].v, \
            gribData [iGrib].g, \
            gribData [iGrib].w);
         }
      }
      printf ("\n");
   }
}

/* true if all value seem OK */
static bool checkGrib (const Zone *zone, int iFlow, CheckGrib *check) {
   const int maxUV = 100;
   const int maxW = 20;
   memset (check, 0, sizeof (CheckGrib));
   for (size_t iGrib = 0; iGrib < zone->nTimeStamp * zone->nbLat * zone->nbLon; iGrib += 1) {
      if (tGribData [iFlow] [iGrib].u == MISSING) check->uMissing += 1;
	   else if (fabs (tGribData [iFlow] [iGrib].u) > maxUV) check->uStrange += 1;
   
      if (tGribData [iFlow] [iGrib].v == MISSING) check->vMissing += 1;
	   else if (fabs (tGribData [iFlow] [iGrib].v) > maxUV) check->vStrange += 1;

      if (tGribData [iFlow] [iGrib].w == MISSING) check->wMissing += 1;
	   else if ((tGribData [iFlow] [iGrib].w > maxW) ||  (tGribData [iFlow] [iGrib].w < 0)) check->wStrange += 1;

      if (tGribData [iFlow] [iGrib].g == MISSING) check->gMissing += 1;
	   else if ((tGribData [iFlow] [iGrib].g > maxUV) || (tGribData [iFlow] [iGrib].g < 0)) check->gStrange += 1;

      if ((tGribData [iFlow] [iGrib].lat > zone->latMax) || (tGribData [iFlow] [iGrib].lat < zone->latMin) ||
         (tGribData [iFlow] [iGrib].lon > zone->lonRight) || (tGribData [iFlow] [iGrib].lon < zone->lonLeft))
		   check->outZone += 1;
   }
   return (check->uMissing == 0) && (check->vMissing == 0) && (check->gMissing == 0) /* && (check->wMissing == 0)*/ && 
          (check->uStrange == 0) && (check->vStrange == 0) && (check->gStrange == 0) && (check->wStrange == 0) && 
          (check->outZone == 0);
}

/*! true if (wind) zone and currentZone intersect in geography */
static bool geoIntersectGrib (const Zone *zone1, const Zone *zone2) {
	return 
	  ((zone2->latMin < zone1 -> latMax) &&
	   (zone2->latMax > zone1 -> latMin) &&
	   (zone2->lonLeft < zone1 -> lonRight) &&
	   (zone2->lonRight > zone1 -> lonLeft));
}

/*! true if (wind) zone and zone 2 intersect in time */
static bool timeIntersectGrib (const Zone *zone1, const Zone *zone2) {
	const time_t timeMin1 = gribDateTimeToEpoch (zone1->dataDate [0], zone1->dataTime [0]);
	const time_t timeMin2 = gribDateTimeToEpoch (zone2->dataDate [0], zone2->dataTime [0]);
	const time_t timeMax1 = timeMin1 + 3600 * (zone1-> timeStamp [zone1 -> nTimeStamp -1]);
	const time_t timeMax2 = timeMin2 + 3600 * (zone2-> timeStamp [zone2 -> nTimeStamp -1]);
	
	return (timeMin2 < timeMax1) && (timeMax2 > timeMin1);
}

/*! check if time steps are regular */
static bool timeStepRegularGrib (const Zone *zone) {
   if (zone->nTimeStamp  < 2) return true;
   for (size_t i = 1; i < zone->intervalLimit; i++) {
      if ((zone->timeStamp [i] - zone->timeStamp [i-1]) != zone->intervalBegin) {
         return false;
      }
   }
   for (size_t i = zone->intervalLimit; i < zone->nTimeStamp - 1; i++) {
      if ((zone->timeStamp [i] - zone->timeStamp [i-1]) != zone->intervalEnd) {
         return false;
      }
   }
   return true;
}

/*! true if u and v (or uCurr, vCurr) are in zone */
static bool uvPresentGrib (const Zone *zone) {
   bool uPresent = false, vPresent = false;
   for (size_t i = 0; i < zone->nShortName; i++) {
      if ((strcmp (zone->shortName [i], "10u") == 0) || (strcmp (zone->shortName [i], "u") == 0) ||
		   (strcmp (zone->shortName [i], "ucurr") == 0)) 
		uPresent = true;
      if ((strcmp (zone->shortName [i], "10v") == 0)  || (strcmp (zone->shortName [i], "v") == 0) || 
		   (strcmp (zone->shortName [i], "vcurr") == 0)) 
		vPresent = true;
   }
   return uPresent && vPresent;
}

/*! check lat, lon are consistent with indice 
	return number of values */
static int consistentGrib (const Zone *zone, int iFlow, double epsilon, int *nLatSuspects, int *nLonSuspects) {
   int n = 0;
   long iGrib;
   double lat, lon;
   *nLatSuspects = 0;
   *nLonSuspects = 0;
   for (size_t k = 0; k < zone->nTimeStamp; k++) {
      for (int i = 0; i < zone->nbLat; i++) {
         for (int j = 0; j < zone->nbLon; j++) {
            n += 1,
	         iGrib = (k * zone->nbLat * zone->nbLon) + (i * zone->nbLon) + j;
            lat = zone->latMin + i * zone->latStep;  
            lon = zone->lonLeft + j * zone->lonStep;
            if (! zone->anteMeridian) 
               lon = lonCanonize (lon);
            if (fabs (lat - tGribData [iFlow][iGrib].lat) > epsilon) {
               *nLatSuspects += 1;
            }
            if (fabs (lon - tGribData [iFlow][iGrib].lon) > epsilon) {
               *nLonSuspects += 1;
            }
         }
      }
   }
   return n;
}

/*! check Grib information and write (add) report in the buffer
    return false if something wrong  */
bool checkGribInfoToStr (int type, Zone *zone, char *buffer, size_t maxLen) {
   const double windEpsilon = 0.01;
   const double currentEpsilon = 0.1;
   CheckGrib sCheck;
   int nVal = 0, nLatSuspects, nLonSuspects;
   bool OK = true;
   char str [MAX_SIZE_LINE] = "";
   const char *separator = "----------------------------------------------------------------------------------"; 
   snprintf (str, MAX_SIZE_LINE, "\n%s\nCheck Grib Info: %s\n%s\n", separator, (type == WIND) ? "Wind" : "Current", separator);
   g_strlcat (buffer, str, maxLen);

   if (zone->nbLat <= 0) {
      snprintf (str, MAX_SIZE_LINE, "No %s grib available\n", (type == WIND) ? "Wind" : "Current");
      g_strlcat (buffer, str, maxLen);
      return true;
   }
   if ((zone->nDataDate != 1) || (zone->nDataTime != 1)) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected nDataDate = 1 and nDataTime = 1. nDataDate: %zu, nDataTime: %zu\n", zone->nDataDate, zone->nDataTime);
      g_strlcat (buffer, str, maxLen);
   }
   if (zone->stepUnits != 1) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected stepUnits = 1, stepUnits = %ld\n", zone->stepUnits);
      g_strlcat (buffer, str, maxLen);
   }
   if (zone->numberOfValues != zone->nbLon * zone->nbLat) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected numberofValues = nbLon x nbLat = %ld, but numberOfValues = %ld\n",\
         zone->nbLon * zone->nbLat, zone->numberOfValues);
      g_strlcat (buffer, str, maxLen);
   }
   if (! (fabs ((zone->lonRight - zone->lonLeft) - zone->lonStep * (zone->nbLon - 1)) < EPSILON)) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected difference between lonLeft and lonRight is %.2lf, found: %.2lf\n",\
         (zone->lonStep * (zone->nbLon - 1)), zone->lonRight - zone->lonLeft);
      g_strlcat (buffer, str, maxLen);
   }
   if (! (fabs((zone->latMax - zone->latMin) - (zone->latStep * (zone->nbLat - 1)) ) < EPSILON )) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected difference between latMax and latMin is %.2lf, found: %.2lf\n",\
         (zone->latStep * (zone->nbLat - 1)), zone->latMax - zone->latMin);
      g_strlcat (buffer, str, maxLen);
   }
   if ((zone->nTimeStamp) < 1 ) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "Expected nTimeStamp >= 1, nTimeStamp =  %zu\n", zone->nTimeStamp);
      g_strlcat (buffer, str, maxLen);
   }

   nVal = consistentGrib (zone, type, (type == WIND) ? windEpsilon : currentEpsilon, &nLatSuspects, &nLonSuspects);
   if ((nLatSuspects > 0) || (nLonSuspects > 0)) {
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "n Val suspect Lat: %d, ratio: %.2lf %% \n", nLatSuspects, 100 * (double)nLatSuspects/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
      snprintf (str, MAX_SIZE_LINE, "n Val suspect Lon: %d, ratio: %.2lf %% \n", nLonSuspects, 100 * (double)nLonSuspects/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
   }
   snprintf (str, MAX_SIZE_LINE, "n Val Values: %d\n", nVal); // at least this line
   g_strlcat (buffer, str, maxLen);
   snprintf (str, MAX_SIZE_LINE, "%s\n", (zone->wellDefined) ? (zone->allTimeStepOK) ? "Wind Zone Well defined" : "All Zone TimeSteps are not defined" : "Zone Undefined");
   g_strlcat (buffer, str, maxLen);
   
   if (!uvPresentGrib (zone)) { 
	   OK = false;
      snprintf (str, MAX_SIZE_LINE, "lack u or v\n");
      g_strlcat (buffer, str, maxLen);
   }
   
   if (! timeStepRegularGrib (zone)) {
      OK = false;
	   snprintf (str, MAX_SIZE_LINE, "timeStep is NOT REGULAR !!!\n");
      g_strlcat (buffer, str, maxLen);
   }
   
   // check grib missing or strange values and out of zone for wind
   if (! checkGrib (zone, type, &sCheck)) {
      OK = false;
      snprintf (str, MAX_SIZE_LINE, "out zone Values: %d, ratio: %.2lf %% \n", sCheck.outZone, 100 * (double)sCheck.outZone/(double) (nVal));
      g_strlcat (buffer, str, maxLen);

      snprintf (str, MAX_SIZE_LINE, "u missing Values: %d, ratio: %.2lf %% \n", sCheck.uMissing, 100 * (double)sCheck.uMissing/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
      snprintf (str, MAX_SIZE_LINE, "u strange Values: %d, ratio: %.2lf %% \n", sCheck.uStrange, 100 * (double)sCheck.uStrange/(double) (nVal));
      g_strlcat (buffer, str, maxLen);

      snprintf (str, MAX_SIZE_LINE, "v missing Values: %d, ratio: %.2lf %% \n", sCheck.vMissing, 100 * (double)sCheck.vMissing/(double) (nVal));
      g_strlcat (buffer, str, maxLen);
      snprintf (str, MAX_SIZE_LINE, "v strange Values: %d, ratio: %.2lf %% \n", sCheck.vStrange, 100 * (double)sCheck.vStrange/(double) (nVal));
      g_strlcat (buffer, str, maxLen);

      if (type == WIND) {
         snprintf (str, MAX_SIZE_LINE, "w missing Values: %d, ratio: %.2lf %% \n", sCheck.wMissing, 100 * (double)sCheck.wMissing/(double) (nVal));
         g_strlcat (buffer, str, maxLen);
         
         snprintf (str, MAX_SIZE_LINE, "w strange Values: %d, ratio: %.2lf %% \n", sCheck.wStrange, 100 * (double)sCheck.wStrange/(double) (nVal));
         g_strlcat (buffer, str, maxLen);

         snprintf (str, MAX_SIZE_LINE, "g missing Values: %d, ratio: %.2lf %% \n", sCheck.gMissing, 100 * (double)sCheck.gMissing/(double) (nVal));
         g_strlcat (buffer, str, maxLen);
         snprintf (str, MAX_SIZE_LINE, "g strange Values: %d, ratio: %.2lf %% \n", sCheck.gStrange, 100 * (double)sCheck.gStrange/(double) (nVal));
         g_strlcat (buffer, str, maxLen);
      }
   } 
   return OK;
}

/*! check Grib information and write report in the buffer
    return false if something wrong  */
bool checkGribToStr (char *buffer, size_t maxLen) {
   char str [MAX_SIZE_LINE] = "";
   buffer [0] = '\0';

   bool OK = (checkGribInfoToStr (WIND, &zone, buffer, maxLen));
   if (OK) buffer [0] = '\0';
   if (! checkGribInfoToStr (CURRENT, &currentZone, buffer, maxLen)) OK = false;
   if (OK) buffer [0] = '\0';

   if (currentZone.nbLat > 0) { // current exist
      // check geo intersection between current and wind
      if (! geoIntersectGrib (&zone, &currentZone)) {
         OK = false;
         snprintf (str, MAX_SIZE_LINE, "\nCurrent and wind grib have no common geo\n");
         g_strlcat (buffer, str, maxLen);
      }
      // check time intersection between current and wind
      if (! timeIntersectGrib (&zone, &currentZone)) {
         OK = false;
	      snprintf (str, MAX_SIZE_LINE, "\nCurrent and wind grib have no common time\n");
         g_strlcat (buffer, str, maxLen);
      }
   }
   return OK;
}

/*! find iTinf and iTsup in order t : zone.timeStamp [iTInf] <= t <= zone.timeStamp [iTSup] */
static inline void findTimeAround (double t, int *iTInf, int *iTSup, const Zone *zone) { 
   size_t k;
   if (t <= zone->timeStamp [0]) {
      *iTInf = 0;
      *iTSup = 0;
      return;
   }
   for (k = 0; k < zone->nTimeStamp; k++) {
      if (t == zone->timeStamp [k]) {
         *iTInf = k;
	      *iTSup = k;
	      return;
      }
      if (t < zone->timeStamp [k]) {
         *iTInf = k - 1;
         *iTSup = k;
         return;
      }
   }
   *iTInf = zone->nTimeStamp -1;
   *iTSup = zone->nTimeStamp -1;
}

/*! ex arrondiMin (46.4, 0.25) = 46.25 */
static inline double arrondiMin (double v, double step) {
   return ((double) floor (v/step)) * step;
} 
 
/*! ex arrondiMax (46.4, 0.25) = 46.50 */
static inline double arrondiMax (double v, double step) {
   return ((double) ceil (v/step)) * step;
}

/*! provide 4 wind points around p */
static inline void find4PointsAround (double lat, double lon,  double *latMin, 
   double *latMax, double *lonMin, double *lonMax, Zone *zone) {

   *latMin = arrondiMin (lat, zone->latStep);
   *latMax = arrondiMax (lat, zone->latStep);
   *lonMin = arrondiMin (lon, zone->lonStep);
   *lonMax = arrondiMax (lon, zone->lonStep);
  
   // check limits
   if (zone->latMin > *latMin) *latMin = zone->latMin;
   if (zone->latMax < *latMax) *latMax = zone->latMax; 
   if (zone->lonLeft > *lonMin) *lonMin = zone->lonLeft; 
   if (zone->lonRight < *lonMax) *lonMax = zone->lonRight;
   
   if (zone->latMax < *latMin) *latMin = zone->latMax; 
   if (zone->latMin > *latMax) *latMax = zone->latMin; 
   if (zone->lonRight < *lonMin) *lonMin = zone->lonRight; 
   if (zone->lonLeft > *lonMax) *lonMax = zone->lonLeft;
}

/*! return indice of lat in gribData wind or current  */
static inline long indLat (double lat, const Zone *zone) {
   return (long) round ((lat - zone->latMin)/zone->latStep);
}

/*! return indice of lon in gribData wind or current */
static inline long indLon (double lon, const Zone *zone) {
   if (lon < zone->lonLeft) lon += 360.0;
   return (long) round ((lon - zone->lonLeft)/zone->lonStep);
}

/*! interpolation to get u, v, g (gust), w (waves) at point (lat, lon)  and time t */
static bool findFlow (double lat, double lon, double t, double *rU, double *rV, \
   double *rG, double *rW, Zone *zone, const FlowP *gribData) {

   double t0,t1;
   double latMin, latMax, lonMin, lonMax;
   double a, b, u0, u1, v0, v1, g0, g1, w0, w1;
   int iT0, iT1;
   FlowP windP00, windP01, windP10, windP11;
   if ((!zone->wellDefined) || (zone->nbLat == 0) || (! isInZone (lat, lon, zone) && par.constWindTws == 0) || (t < 0)){
      *rU = 0, *rV = 0, *rG = 0; *rW = 0;
      return false;
   }
   
   findTimeAround (t, &iT0, &iT1, zone);
   find4PointsAround (lat, lon, &latMin, &latMax, &lonMin, &lonMax, zone);
   //printf ("lat %lf iT0 %d, iT1 %d, latMin %lf latMax %lf lonMin %lf lonMax %lf\n", p.lat, iT0, iT1, latMin, latMax, lonMin, lonMax);  
   // 4 points at time t0
   windP00 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMin, zone)];  
   windP01 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP10 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP11 = gribData [iT0 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMin, zone)];

   // printf ("00lat %lf  01lat %lf  10lat %lf 11lat %lf\n", windP00.lat, windP01.lat,windP10.lat,windP11.lat); 
   // printf ("00tws %lf  01tws %lf  10tws %lf 11tws %lf\n", windP00.tws, windP01.tws,windP10.tws,windP11.tws); 
   // speed longitude interpolation at northern latitudes
  
   // interpolation  for u, v, w, g
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.u, windP01.u);
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.u, windP11.u); 
   u0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.v, windP01.v); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.v, windP11.v); 
   v0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.g, windP01.g); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.g, windP11.g); 
   g0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.w, windP01.w); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.w, windP11.w); 
   w0 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   // 4 points at time t1
   windP00 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMin, zone)];  
   windP01 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMax, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP10 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMax, zone)];
   windP11 = gribData [iT1 * zone->nbLat * zone->nbLon + indLat(latMin, zone) * zone->nbLon + indLon(lonMin, zone)];

   // interpolatuon for  for u, v
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.u, windP01.u);
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.u, windP11.u); 
   u1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.v, windP01.v); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.v, windP11.v); 
   v1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
    
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.g, windP01.g); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.g, windP11.g); 
   g1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   a = interpolate (lon, windP00.lon, windP01.lon, windP00.w, windP01.w); 
   b = interpolate (lon, windP10.lon, windP11.lon, windP10.w, windP11.w); 
   w1 = interpolate (lat, windP00.lat, windP10.lat, a, b);
   
   // finally, interpolation twd tws between t0 and t1
   t0 = zone->timeStamp [iT0];
   t1 = zone->timeStamp [iT1];
   *rU = interpolate (t, t0, t1, u0, u1);
   *rV = interpolate (t, t0, t1, v0, v1);
   *rG = interpolate (t, t0, t1, g0, g1);
   *rW = interpolate (t, t0, t1, w0, w1);

   return true;
}

/*! use findflow to get wind and waves */
void findWindGrib (double lat, double lon, double t, double *u, double *v, \
   double *gust, double *w, double *twd, double *tws ) {
   if (par.constWindTws != 0) {
      *twd = par.constWindTwd;
      *tws = par.constWindTws;
	   *u = - KN_TO_MS * par.constWindTws * sin (DEG_TO_RAD * par.constWindTwd);
	   *v = - KN_TO_MS * par.constWindTws * cos (DEG_TO_RAD * par.constWindTwd);
      *w = 0;
      *gust = hypot (*u, *v); // m/s
      return;
   }
   findFlow (lat, lon, t, u, v, gust, w, &zone, tGribData [WIND]);
   *twd = fTwd (*u, *v);
   *tws = fTws (*u, *v);
   if (par.constWave < 0) *w = 0;
   else if (par.constWave != 0) *w = par.constWave;
}

/*! use findflow to get current */
void findCurrentGrib (double lat, double lon, double t, double *uCurr,\
   double *vCurr, double *tcd, double *tcs) {

   double gust, bidon;
   *uCurr = 0;
   *vCurr = 0;
   *tcd = 0;
   *tcs = 0;
   if (par.constCurrentS != 0) {
      *uCurr = -KN_TO_MS * par.constCurrentS * sin (DEG_TO_RAD * par.constCurrentD);
      *vCurr = -KN_TO_MS * par.constCurrentS * cos (DEG_TO_RAD * par.constCurrentD);
      *tcd = par.constCurrentD; // direction
      *tcs = par.constCurrentS; // speed
      return;
   }
   if (t > currentZone.timeStamp [currentZone.nTimeStamp - 1]) return;

   findFlow (lat, lon, t, uCurr, vCurr, &gust, &bidon, &currentZone, tGribData [CURRENT]);
   *tcd = fTwd (*uCurr, *vCurr);
   *tcs = fTws (*uCurr, *vCurr);
}

/*! write Grib information in string */
char *gribToStr (const Zone *zone, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE] = "";
   char strTmp [MAX_SIZE_LINE] = "";
   char strLat0 [MAX_SIZE_NAME] = "", strLon0 [MAX_SIZE_NAME] = "";
   char strLat1 [MAX_SIZE_NAME] = "", strLon1 [MAX_SIZE_NAME] = "";
   char centreName [MAX_SIZE_NAME] = "";
   
   for (size_t i = 0; i < sizeof(meteoTab) / sizeof(meteoTab[0]); i++) // search name of center
     if (meteoTab [i].id == zone->centreId) 
        g_strlcpy (centreName, meteoTab [i].name, sizeof (centreName));
         
   newDate (zone->dataDate [0], zone->dataTime [0]/100, strTmp, sizeof (strTmp));
   snprintf (str, maxLen, "Centre ID: %ld %s   %s   Ed number: %ld\nnMessages: %d\nstepUnits: %ld\n# values : %ld\n",\
      zone->centreId, centreName, strTmp, zone->editionNumber, zone->nMessage, zone->stepUnits, zone->numberOfValues);
   snprintf (line, MAX_SIZE_LINE, "Zone From: %s, %s To: %s, %s\n",\
      latToStr (zone->latMin, par.dispDms, strLat0, sizeof (strLat0)),\
      lonToStr (zone->lonLeft, par.dispDms, strLon0, sizeof (strLon0)),\
      latToStr (zone->latMax, par.dispDms, strLat1, sizeof (strLat1)),\
      lonToStr (zone->lonRight, par.dispDms, strLon1, sizeof (strLon1)));
   g_strlcat (str, line, maxLen);   
   snprintf (line, MAX_SIZE_LINE, "LatStep  : %04.4f° LonStep: %04.4f°\n", zone->latStep, zone->lonStep);
   g_strlcat (str, line, maxLen);   
   snprintf (line, MAX_SIZE_LINE, "Nb Lat   : %ld      Nb Lon : %ld\n", zone->nbLat, zone->nbLon);
   g_strlcat (str, line, maxLen);   
   if (zone->nTimeStamp < 8) {
      snprintf (line, MAX_SIZE_LINE, "TimeStamp List of %zu : [ ", zone->nTimeStamp);
      g_strlcat (str, line, maxLen);   
      for (size_t k = 0; k < zone->nTimeStamp; k++) {
         snprintf (line, MAX_SIZE_LINE, "%ld ", zone->timeStamp [k]);
         g_strlcat (str, line, maxLen);   
      }
      g_strlcat (str, "]\n", maxLen);
   }
   else {
      snprintf (line, MAX_SIZE_LINE, "TimeStamp List of %zu : [%ld, %ld, ..%ld]\n", zone->nTimeStamp,\
         zone->timeStamp [0], zone->timeStamp [1], zone->timeStamp [zone->nTimeStamp - 1]);
      g_strlcat (str, line, maxLen);   
   }

   snprintf (line, MAX_SIZE_LINE, "Shortname List: [ ");
   g_strlcat (str, line, maxLen);   
   for (size_t k = 0; k < zone->nShortName; k++) {
      snprintf (line, MAX_SIZE_LINE, "%s ", zone->shortName [k]);
      g_strlcat (str, line, maxLen);   
   }
   g_strlcat (str, "]\n", maxLen);
   if ((zone->nDataDate > 1) || (zone->nDataTime > 1)) {
      snprintf (line, MAX_SIZE_LINE, "Warning number of Date: %zu, number of Time: %zu\n", zone->nDataDate, zone->nDataTime);
      g_strlcat (str, line, maxLen);   
   }
   snprintf (line, MAX_SIZE_LINE, "Zone is       :  %s\n", (zone->wellDefined) ? "Well defined" : "Undefined");
   g_strlcat (str, line, maxLen);   
   return str;
}

/*! write grib meta information in string */
char *gribToStrJson (const char *fileName, char *out, size_t maxLen) {
   char gribName [MAX_SIZE_FILE_NAME];
   char str [MAX_SIZE_TEXT] = "";
   char infoStr [MAX_SIZE_LINE] = "";
   struct stat st;
   char str0 [MAX_SIZE_NAME] = "";
   char str1 [MAX_SIZE_NAME] = "";
   char centreName [MAX_SIZE_NAME] = "";
   struct tm tm_buf, *tm_info;
   char strTime [20] = "1970-01-01 00:00:00";
   Zone gZone;

   buildRootName (fileName, gribName, sizeof (gribName));

   if (stat (gribName, &st) != 0) {
      fprintf (stderr, "In gribToStrJson Error stat: %s\n", gribName);
      snprintf (out, maxLen, "{}\n");
      return out;
   }
   if (! readGribLists (gribName, &gZone)) {
      fprintf (stderr, "In gribToStrJson Error reading: %s\n", gribName);
      snprintf (out, maxLen, "{}\n");
      return out;
   }
   if (! readGribParameters (gribName, &gZone)) {
      fprintf (stderr, "In gribToStrJson Error reading: %s\n", gribName);
      snprintf (out, maxLen, "{}\n");
      return out;
   }
   if (gZone.nbLat == 0) {
      fprintf (stderr, "In gribToStrJson Error no value available in: %s\n", gribName);
      snprintf (out, maxLen, "{}\n");
      return out;
   }
   for (size_t i = 0; i < N_METEO_ADMIN; i++) {// search name of center
      if (meteoTab [i].id == gZone.centreId) {
         g_strlcpy (centreName, meteoTab [i].name, sizeof (centreName));
         break;
      }
   }
   newDate (gZone.dataDate [0], gZone.dataTime [0]/100, str0, sizeof (str0));
   newDate (gZone.dataDate [0], gZone.dataTime [0]/100 + gZone.timeStamp [gZone.nTimeStamp -1], str1, sizeof (str1));

   snprintf (out, maxLen, 
      "{\n  \"centreID\": %ld, \"centreName\": \"%s\", \"edNumber\": %ld, \n"
      "  \"runStart\": \"%s\", \"runEnd\": \"%s\", \n"
      "  \"topLat\": %.6f, \"leftLon\": %.6f, \"bottomLat\": %.6f, \"rightLon\": %.6f, \n"
      "  \"latStep\": %.4f, \"lonStep\": %.4f, \n"
      "  \"nLat\": %ld, \"nLon\": %ld, \"nValues\": %ld, \"nTimeStamp\": %zu,\n"
      "  \"timeStamps\": \n  [",
      gZone.centreId, centreName, gZone.editionNumber,
      str0, str1,
      gZone.latMax, gZone.lonLeft, gZone.latMin, gZone.lonRight,
      gZone.latStep, gZone.lonStep,
      gZone.nbLat, gZone.nbLon, gZone.numberOfValues, gZone.nTimeStamp
   );

  for (size_t i = 0; i < gZone.nTimeStamp; i += 1) {
      snprintf (str, sizeof (str), "%ld%s", gZone.timeStamp [i], (i < gZone.nTimeStamp -1) ? ", " : ""); // no comma for last
      g_strlcat (out, str, maxLen);
   }
   g_strlcat (out, "],\n", maxLen);

   snprintf (str, sizeof (str), "  \"nShortName\": %zu, \"shortNames\": [", gZone.nShortName);
   g_strlcat (out, str, maxLen);

   for (size_t i = 0; i < gZone.nShortName; i += 1) {
      snprintf (str, sizeof (str), "\"%s\"%s", gZone.shortName [i], (i < gZone.nShortName -1) ? ", " : ""); // no comma for last
      g_strlcat (out, str, maxLen);
   }
   g_strlcat (out, "],\n", maxLen);

   tm_info = localtime_r (&st.st_mtime, &tm_buf);
   if (tm_info) strftime(strTime, sizeof strTime, "%Y-%m-%d %H:%M:%S", tm_info);
   
   char *gribBaseName = g_path_get_basename (fileName);
   snprintf (str, sizeof (str),  "  \"fileName\": \"%s\", \"fileSize\": %ld, \"fileTime\": \"%s\",\n", 
            gribBaseName, st.st_size, strTime);
   g_strlcat (out, str, maxLen);
   free (gribBaseName);

   if ((gZone.nDataDate != 1) || (gZone.nDataTime != 1)) {
      snprintf (infoStr, sizeof (infoStr), "Warning number of Date: %zu, number of Time: %zu", gZone.nDataDate, gZone.nDataTime);
   }

   snprintf (str, sizeof (str), "  \"Info\": \"%s\"\n}\n", infoStr);
   g_strlcat (out, str, maxLen);
   return out;
}

