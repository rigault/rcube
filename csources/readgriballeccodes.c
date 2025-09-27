/*! compilation: gcc -c grib.c `pkg-config --cflags glib-2.0` */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <sys/stat.h>
#include <locale.h>
#include "eccodes.h"
#include "../csources/glibwrapper.h"
#include "../csources/r3types.h"
#include "../csources/r3util.h"
#include "../csources/inline.h"
#include <grib_api.h>  // for ProductKind

#define  EPSILON 0.001        // for approximat value

FlowP *tGribData [2] = {NULL, NULL};   // wind, current

/*! return version of ECCODE API */
char *gribReaderVersion (char *str, size_t maxLen) {
   snprintf  (str, maxLen, "ECCODES %s", ECCODES_VERSION_STR);
   return str;
}

/*! return indice of lat in gribData wind or current  */
static inline long indLat (double lat, const Zone *zone) {
   return (long) round ((lat - zone->latMin)/zone->latStep);
}

/*! return indice of lon in gribData wind or current */
static inline long indLon (double lon, const Zone *zone) {
   if (lon < zone->lonLeft) lon += 360;
   return (long) round ((lon - zone->lonLeft)/zone->lonStep);
}

/*! Modify array with new value if not already in array. Return new array size*/
static long updateLong (long value, size_t n, size_t maxSize, long array []) {
   bool found = false;
   for (size_t i = 0; i < n; i++) {
      if (value == array [i]) {
         found = true;
         break;
      }
   } 
   if ((! found) && (n < maxSize)) { // new time stamp
      array [n] = value;
	   n += 1;
   }
   return n;
}

/*! Read lists zone.timeStamp, shortName, zone.dataDate, dataTime before full grib reading */
bool readGribLists (const char *fileName, Zone *zone) {
   FILE* f = NULL;
   int err = 0;
   long timeStep, dataDate, dataTime;
   char shortName [MAX_SIZE_SHORT_NAME];
   size_t lenName;
   bool found = false;
   // Message handle. Required in all the ecCodes calls acting on a message.
   codes_handle* h = NULL;

   if ((f = fopen (fileName, "rb")) == NULL) {
       fprintf (stderr, "In readGribLists, Error: Unable to open file %s\n", fileName);
       return false;
   }
   memset (zone, 0,  sizeof (Zone));

   // Loop on all the messages in a file
   while ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) != NULL) {
      if (err != CODES_SUCCESS) CODES_CHECK (err, 0);
      lenName = MAX_SIZE_SHORT_NAME;

      CODES_CHECK(codes_get_string(h, "shortName", shortName, &lenName), 0);
      CODES_CHECK(codes_get_long(h, "step", &timeStep), 0);
      CODES_CHECK(codes_get_long(h, "dataDate", &dataDate), 0);
      CODES_CHECK(codes_get_long(h, "dataTime", &dataTime), 0);
      codes_handle_delete (h);

      found = false;
      for (size_t i = 0; i < zone->nShortName; i++) {
         if (strcmp (shortName, zone->shortName [i]) == 0) {
            found = true;
            break;
         }
      } 
      if ((! found) && (zone->nShortName < MAX_N_SHORT_NAME)) { // new shortName
         g_strlcpy (zone->shortName [zone->nShortName], shortName, MAX_SIZE_SHORT_NAME); 
         zone->nShortName += 1;
      }
      zone->nTimeStamp = updateLong (timeStep, zone->nTimeStamp, MAX_N_TIME_STAMPS, zone->timeStamp); 
      zone->nDataDate = updateLong (dataDate, zone->nDataDate, MAX_N_DATA_DATE, zone->dataDate); 
      zone->nDataTime = updateLong (dataTime, zone->nDataTime, MAX_N_DATA_TIME, zone->dataTime); 
   }
 
   fclose (f);
   // Replace unknown by gust ? (GFS case where gust identified by specific indicatorOfParameter)
   for (size_t i = 0; i < zone->nShortName; i++) {
      if (strcmp (zone->shortName[i], "unknown") == 0)
         g_strlcpy (zone->shortName[i], "gust?", 6);
   }
   zone->intervalLimit = 0;
   if (zone->nTimeStamp > 1) {
      zone->intervalBegin = zone->timeStamp [1] - zone->timeStamp [0];
      zone->intervalEnd = zone->timeStamp [zone->nTimeStamp - 1] - zone->timeStamp [zone->nTimeStamp - 2];
      for (size_t i = 1; i < zone->nTimeStamp; i++) {
         if ((zone->timeStamp [i] - zone->timeStamp [i-1]) == zone->intervalEnd) {
            zone->intervalLimit = i;
            break;
         }
      }
   }
   else {
      zone->intervalBegin = zone->intervalEnd = 3;
      fprintf (stderr, "In readGribLists, Error nTimeStamp = %zu\n", zone->nTimeStamp);
      //return false;
   }

   //printf ("intervalBegin: %ld, intervalEnd: %ld, intervalLimit: %zu\n", 
      //zone->intervalBegin, zone->intervalEnd, zone->intervalLimit -1); 
   return true;
}

/*! Read grib parameters in zone before full grib reading */
bool readGribParameters (const char *fileName, Zone *zone) {
   int err = 0;
   FILE* f = NULL;
   double lat1, lat2;
   codes_handle *h = NULL;
 
   if ((f = fopen (fileName, "rb")) == NULL) {
       fprintf (stderr, "In readGribParameters, Error unable to open file %s\n",fileName);
       return false;
   }
 
   // create new handle from the first message in the file
   if ((h = codes_handle_new_from_file(0, f, PRODUCT_GRIB, &err)) == NULL) {
       fprintf (stderr, "In readGribParameters, Error code handle from file : %s Code error: %s\n",\
         fileName, codes_get_error_message(err)); 
       fclose (f);
       return false;
   }
   fclose (f);
 
   CODES_CHECK(codes_get_long (h, "centre", &zone->centreId),0);
   CODES_CHECK(codes_get_long (h, "editionNumber", &zone->editionNumber),0);
   CODES_CHECK(codes_get_long (h, "stepUnits", &zone->stepUnits),0);
   CODES_CHECK(codes_get_long (h, "numberOfValues", &zone->numberOfValues),0);
   CODES_CHECK(codes_get_long (h, "Ni", &zone->nbLon),0);
   CODES_CHECK(codes_get_long (h, "Nj", &zone->nbLat),0);

   CODES_CHECK(codes_get_double (h,"latitudeOfFirstGridPointInDegrees",&lat1),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfFirstGridPointInDegrees",&zone->lonLeft),0);
   CODES_CHECK(codes_get_double (h,"latitudeOfLastGridPointInDegrees",&lat2),0);
   CODES_CHECK(codes_get_double (h,"longitudeOfLastGridPointInDegrees",&zone->lonRight),0);
   CODES_CHECK(codes_get_double (h,"iDirectionIncrementInDegrees",&zone->lonStep),0);
   CODES_CHECK(codes_get_double (h,"jDirectionIncrementInDegrees",&zone->latStep),0);

   if ((lonCanonize (zone->lonLeft) > 0.0) && (lonCanonize (zone->lonRight) < 0.0)) {
      zone -> anteMeridian = true;
   }
   else {
      zone -> anteMeridian = false;
      zone->lonLeft = lonCanonize (zone->lonLeft);
      zone->lonRight = lonCanonize (zone->lonRight);
   }
   zone->latMin = fmin (lat1, lat2);
   zone->latMax = fmax (lat1, lat2);

   codes_handle_delete (h);
   return true;
}

/*! find index in gribData table */
static inline long indexOf (int timeStep, double lat, double lon, const Zone *zone) {
   long iLat = indLat (lat, zone);
   long iLon = indLon (lon, zone);
   int iT = -1;
   for (iT = 0; iT < (int) zone->nTimeStamp; iT++) {
      if (timeStep == zone->timeStamp [iT])
         break;
   }
   if (iT == -1) {
      fprintf (stderr, "In indexOf, Error Cannot find index of time: %d\n", timeStep);
      return -1;
   }
   //printf ("iT: %d iLon :%d iLat: %d\n", iT, iLon, iLat); 
   return  (iT * zone->nbLat * zone->nbLon) + (iLat * zone->nbLon) + iLon;
}

/*! read grib file using eccodes C API 
   return true if OK */
bool readGribAll (const char *fileName, Zone *zone, int iFlow) {
   FILE* f = NULL;
   int err = 0;
   long iGrib;
   long bitmapPresent  = 0, timeStep, oldTimeStep;
   double lat, lon, val, indicatorOfParameter;
   char shortName [MAX_SIZE_SHORT_NAME];
   size_t lenName;
   const long GUST_GFS = 180;
   //char str [MAX_SIZE_LINE];
   zone->wellDefined = false;
   if (! readGribLists (fileName, zone)) {
      return false;
   }
   if (! readGribParameters (fileName, zone)) {
      return false;
   }
   if (zone -> nDataDate > 1) {
      fprintf (stderr, "In readGribAll, Error Grib file with more than 1 dataDate not supported nDataDate: %zu\n", 
         zone -> nDataDate);
      return false;
   }

   if (tGribData [iFlow] != NULL) {
      free (tGribData [iFlow]); 
      tGribData [iFlow] = NULL;
   }
   if ((tGribData [iFlow] = calloc ((zone->nTimeStamp + 1) * zone->nbLat * zone->nbLon, sizeof (FlowP))) == NULL) { // nTimeStamp + 1
      fprintf (stderr, "In readGribAll, Error calloc tGribData [iFlow]\n");
      return false;
   }
   // printf ("In readGribAll.: %s allocated\n", 
      // formatThousandSep (str, sizeof (str), sizeof(FlowP) * (zone->nTimeStamp + 1) * zone->nbLat * zone->nbLon));
   
   // Message handle. Required in all the ecCodes calls acting on a message.
   codes_handle* h = NULL;
   // Iterator on lat/lon/values.
   codes_iterator* iter = NULL;
   if ((f = fopen (fileName, "rb")) == NULL) {
      free (tGribData [iFlow]); 
      tGribData [iFlow] = NULL;
      fprintf (stderr, "In readGribAll, Error Unable to open file %s\n", fileName);
      return false;
   }
   zone->nMessage = 0;
   zone->allTimeStepOK = true;
   timeStep = zone->timeStamp [0];
   oldTimeStep = timeStep;

   // Loop on all the messages in a file
   while ((h = codes_handle_new_from_file (0, f, PRODUCT_GRIB, &err)) != NULL) {
      if (err != CODES_SUCCESS) CODES_CHECK (err, 0);

      // Check if a bitmap applies
      CODES_CHECK (codes_get_long (h, "bitmapPresent", &bitmapPresent), 0);
      if (bitmapPresent) {
          CODES_CHECK(codes_set_double (h, "missingValue", MISSING), 0);
      }

      lenName = MAX_SIZE_SHORT_NAME;
      CODES_CHECK(codes_get_string (h, "shortName", shortName, &lenName), 0);
      CODES_CHECK(codes_get_long (h, "step", &timeStep), 0);

      long progressTime = timeStep - oldTimeStep;

      // check timeStep move well. This test is not very useful.
      if ((timeStep != 0) && (progressTime > 0) &&  // progressTime may regress. ARPEGE case
          (progressTime != zone->intervalBegin) && 
          (progressTime != zone->intervalEnd)
         ) { // check timeStep progress well 

         zone->allTimeStepOK = false;
         fprintf (stderr, "In readGribAll: All time Step Are Not defined message: %d, timeStep: %ld, oldTimeStep: %ld, shortName: %s\n", 
            zone->nMessage, timeStep, oldTimeStep, shortName);
      }
      oldTimeStep = timeStep;

      err = codes_get_double(h, "indicatorOfParameter", &indicatorOfParameter);
      if (err != CODES_SUCCESS) 
         indicatorOfParameter = -1;
 
      // A new iterator on lat/lon/values is created from the message handle h.
      iter = codes_grib_iterator_new(h, 0, &err);
      if (err != CODES_SUCCESS) CODES_CHECK(err, 0);
 
      // Loop on all the lat/lon/values.
      while (codes_grib_iterator_next(iter, &lat, &lon, &val)) {
         if (! (zone -> anteMeridian))
            lon = lonCanonize (lon);
         // printf ("lon : %.2lf\n", lon);
         iGrib = indexOf (timeStep, lat, lon, zone);
   
         if (iGrib == -1) {
            fprintf (stderr, "In readGribAll: Error iGrib : %ld\n", iGrib); 
            free (tGribData [iFlow]); 
            tGribData [iFlow] = NULL;
            codes_handle_delete (h);
            codes_grib_iterator_delete (iter);
            iter = NULL;
            h = NULL;
            fclose (f);
            return false;
         }
         // printf("%.2f %.2f %.2lf %ld %ld %s\n", lat, lon, val, timeStep, iGrib, shortName);
         tGribData [iFlow] [iGrib].lat = lat; 
         tGribData [iFlow] [iGrib].lon = lon; 
         if ((strcmp (shortName, "10u") == 0) || (strcmp (shortName, "ucurr") == 0))
            tGribData [iFlow] [iGrib].u = val;
         else if ((strcmp (shortName, "10v") == 0) || (strcmp (shortName, "vcurr") == 0))
            tGribData [iFlow] [iGrib].v = val;
         else if (strcmp (shortName, "gust") == 0)
            tGribData [iFlow] [iGrib].g = val;
         else if (strcmp (shortName, "swh") == 0)     // waves
            tGribData [iFlow] [iGrib].w = val;
         else if (indicatorOfParameter == GUST_GFS)   // find gust in GFS file specific parameter = 180
            tGribData [iFlow] [iGrib].g = val;
      }
      codes_grib_iterator_delete (iter);
      codes_handle_delete (h);
      iter = NULL;
      h = NULL;
      // printf ("nMessage: %d\n", zone->nMessage);
      zone->nMessage += 1;
   }
   // printf ("readGribAll:%s done.\n", fileName);
 
   fclose (f);
   zone->wellDefined = true;
   return true;
}

