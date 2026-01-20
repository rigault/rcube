/*! \Engine
* tws = True Wind Speed
* twd = True Wind Direction
* twa = True Wind Angle - the angle of the boat to the wind
* sog = Speed over Ground of the boat
* cog = Course over Ground  of the boat
* */
/*! compilation: gcc -c engine.c */
#include <float.h>   
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <math.h>
#include "r3types.h"
#include "glibwrapper.h"
#include "inline.h"
#include "r3util.h"
#include "grib.h"

#define MAX_N_INTERVAL  1000                    // for chooseDeparture
#define LIMIT           1                       // for forwardSectorOptimize
#define MIN_VMC_RATIO   0.5                     // for forwardSectorOptimize
#define MAX_N_HISTORY   20                      // for saveRoute
#define MAX_UNREACHABLE 0                       // for bestTimeDeparture. 0 means stop after first unreeach detected.
#define MIN_DT          0.1                     // in hours, the minimum delta time to progress, including penalties

/*! global variables */
Pp      *isocArray = NULL;                      // list of isochrones Two dimensions array : maxNIsoc  * MAX_SIZE_ISOC
IsoDesc *isoDesc = NULL;                        // Isochrone meta data. Array one dimension.
int     maxNIsoc = 0;                           // Max number of isochrones based on based on Grib zone time stamp and isochrone time step
int     nIsoc = 0;                              // total number of isochrones 
Pp      lastClosest;                            // closest point to destination in last isochrone computed

/*! store sail route calculated in engine.c by routing */  
SailRoute route;
HistoryRouteList historyRoute = { .n = 0, .r = NULL };

ChooseDeparture chooseDeparture;                // for choice of departure time

/*! static global variable. If linking issue, remove static */
static double pOrToPDestCog = 0.0;              // cog from pOr to pDest.
static int    pId = 1;                          // global ID for points. -1 and 0 are reserved for pOr and pDest
static double tDeltaCurrent = 0.0;              // delta time in hours between wind zone and current zone

typedef struct {
   double vmc;
   double orthoVmc;
   int nPt;
} Sector;

Sector sector [2][MAX_N_SECTORS];               // we keep even and odd last sectors

/*! return distance (nm) from point X do segment [AB]. Meaning to foot H, orthogonal projection of X on [AB] */
static inline double distSegmentWithFoot(double latX, double lonX, double latA, double lonA, double latB, double lonB, double *latH, double *lonH) {
   // Conversion degree -> NM
   const double cosLat = cos(latX * DEG_TO_RAD);

   const double xA = lonA * cosLat * 60.0, yA = latA * 60.0;
   const double xB = lonB * cosLat * 60.0, yB = latB * 60.0;
   const double xX = lonX * cosLat * 60.0, yX = latX * 60.0;

   // Vectors AB and AX
   const double ABx = xB - xA, ABy = yB - yA;
   const double AXx = xX - xA, AXy = yX - yA;

   // Scalar projection AX on AB
   const double AB_AB = ABx * ABx + ABy * ABy;
   if (AB_AB == 0.0) { // avoid zero div
       if (latH) *latH = latA;
       if (lonH) *lonH = lonA;
       return hypot(AXx, AXy);
   }

   double t = (AXx * ABx + AXy * ABy) / AB_AB;
   t = CLAMP(t, 0.0, 1.0); // CLAMP to [0 1] interval to stay in AB segment

   const double xH = xA + t * ABx;
   const double yH = yA + t * ABy;

   if (latH) *latH = yH / 60.0;
   if (lonH) *lonH = (cosLat > 0.0) ? (xH / (60.0 * cosLat)) : lonA;

   return hypot(xX - xH, yX - yH);
}

/*! find first point in isochrone. Useful for drawAllIsochrones */ 
static inline int findFirst (int nIsoc) {
   int best = 0, next;
   double dSquare, dSquareMax = 0.0;
   const int size = isoDesc[nIsoc].size;
   if (size <= 1) return 0;
   const int baseIndex = nIsoc * MAX_SIZE_ISOC;

   for (int i = 0; i < size; i++) {
      next = (i >= size -1) ? 0 : i + 1;
      double nextLat = isocArray [baseIndex + next].lat;
      double deltaLat = isocArray [baseIndex + i].lat - nextLat;
      double deltaLon = (isocArray [baseIndex + i].lon - isocArray [baseIndex + next].lon) * cos (DEG_TO_RAD * nextLat);
      // square pythagore distance in degrees
      dSquare = deltaLat * deltaLat + deltaLon * deltaLon;
      if (dSquare > dSquareMax) {
         dSquareMax = dSquare;
         best = next;
      }
   }
   return best;
}

/*! initialization of sector */
static inline void initSector(int nIsoc, int nMax) {
   memset (sector[nIsoc], 0, nMax * sizeof(Sector));
}

/*! reduce the size of Isolist 
   note influence of parameters par.nSector, par.jFactor and par.kFactor 
   make new isochrone optIsoc
   return the length of this isochrone 
   side effect: update  isoDesc */
static inline int forwardSectorOptimize (const Pp *pOr, const Pp *pDest, int nIsoc, const Pp *isoList, int isoLen, Pp *optIsoc) {
   int iSector, k;
   double focalLat, focalLon; // center of sectors
   const double epsilonDenominator = 0.01;
   const int thresholdSector = 5;
   const int nSectors = (nIsoc < thresholdSector) ? 180 : par.nSectors;
   const double thetaStep = 360.0 / nSectors;
   const double invThetaStep = 1.0 / thetaStep;  // replace division by multiplication for perf
   const double denominator = cos (DEG_TO_RAD * (pOr->lat + pDest->lat) * 0.5);

   if (denominator < epsilonDenominator) { // really small !
      fprintf (stderr, "In forwardSectorOptimize, Error denominator: %.8lf\n", denominator);
      return 0;
   }

   if (par.jFactor == 0 || nIsoc < LIMIT) { // LIMIT SOULD BE > 0 
      focalLat = pOr->lat; 
      focalLon = pOr->lon;
   }
   else {
      const double dist = isoDesc [nIsoc - LIMIT].bestVmc * (par.jFactor / 100.0) - isoDesc[nIsoc - LIMIT].biggestOrthoVmc * (par.kFactor / 100.0);
      const double dLat = dist * cos (DEG_TO_RAD * pOrToPDestCog);               // nautical miles in N S direction
      const double dLon = dist * sin (DEG_TO_RAD * pOrToPDestCog) / denominator; // nautical miles in E W direction
      focalLat = pOr->lat + dLat / 60.0; 
      focalLon = pOr->lon + dLon / 60.0;
      if (focalLat  < -90.0 || focalLat > 90.0) {
         fprintf (stderr, "In forwardSectorOptimize, Error lat: %.2lf\n", focalLat);
         return 0;
      }
      if (focalLon  < -360.0 || focalLon > 360.0) {
         fprintf (stderr, "In forwardSectorOptimize, Error lon: %.2f", focalLon);
         return 0;
      }
   }

   isoDesc [nIsoc].focalLat = focalLat;
   isoDesc [nIsoc].focalLon = focalLon;
   const int currentSector = nIsoc % 2;
   const int previousSector = (nIsoc - 1) % 2;
  
   initSector (nIsoc % 2, nSectors);

   for (int i = 0; i < isoLen; i++) {
      const Pp *iso = &isoList[i];

      const double alpha = orthoCap (focalLat, focalLon, iso->lat, iso->lon);
      double theta = pOrToPDestCog - alpha;

      if (theta < 0) theta += 360.0;
      else if (theta >= 360.0) theta -= 360.0;

      int iSector = round ((360.0 - theta) * invThetaStep);

      Sector *sect = &sector[currentSector][iSector];

      if (iso->vmc > sect->vmc) {
         sect->vmc = iso->vmc;
         sect->orthoVmc = iso->orthoVmc;
         optIsoc [iSector] = *iso;
      }
      sect->nPt += 1;
   }

   k = 0;
   for (iSector = 0; iSector < nSectors; iSector += 1) {
      const Sector *current = &sector[currentSector][iSector];  // Direct access with pointer to improve perf
      const Sector *previous = &sector[previousSector][iSector];

      if ((current->nPt > 0) &&
         (current->vmc < pOr->dd * 1.1) &&
         ((current->orthoVmc >= isoDesc[nIsoc - 1].biggestOrthoVmc) ||  (current->vmc >= MIN_VMC_RATIO * isoDesc[nIsoc - 1].bestVmc)) &&
         ((current->vmc >= previous->vmc))) {

         optIsoc[k] = optIsoc [iSector];
         //optIsoc[k].sector = iSector;
         k++;
      }
   }
   return k;
}

/*! choice of algorithm used to reduce the size of Isolist */
static inline int optimize (const Pp *pOr, const Pp *pDest, int nIsoc, int algo, const Pp *isoList, int isoLen, Pp *optIsoc) {
   switch (algo) {
      case 0: 
         memcpy (optIsoc, isoList, isoLen * sizeof (Pp)); 
         return isoLen;
      case 1:
         return forwardSectorOptimize (pOr, pDest, nIsoc, isoList, isoLen, optIsoc);
   } 
   return 0;
}

/*! build the new list describing the next isochrone, starting from isoList 
  returns length of the newlist built or -1 if error*/
static int buildNextIsochrone (const Pp *pOr, const Pp *pDest, const Pp *isoList, int isoLen,
                               double t, double dt, Pp *newList, double *bestVmc, double *biggestOrthoVmc) {
   static const double epsilon = 0.01;
   Pp newPt;
   int lenNewL = 0;
   double u, v, gust, w, twa, sog, uCurr, vCurr, currTwd, currTws, vDirectCap;
   double dLat, dLon, penalty, efficiency;
   double waveCorrection, invDenominator, twd, tws;
   int bidon; // useless

   *bestVmc = 0;
   *biggestOrthoVmc = 0;

   for (int k = 0; k < isoLen; k++) {
      const Pp *isoPt = &isoList[k];

      if (!isInZone (isoPt->lat, isoPt->lon, &zone) && (par.constWindTws == 0)) continue;

      findWindGrib (isoPt->lat, isoPt->lon, t, &u, &v, &gust, &w, &twd, &tws);
      if (tws > par.maxWind) continue; // avoid location where wind speed too high...

      if (par.withCurrent) findCurrentGrib (isoPt->lat, isoPt->lon, t - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);

      vDirectCap = orthoCap (isoPt->lat, isoPt->lon, pDest->lat, pDest->lon);
      const bool useMotor = (maxSpeedInPolarAt (tws * par.xWind, &polMat) < par.threshold) && (par.motorSpeed > 0);
      invDenominator = 1.0 / MAX (epsilon, cos (DEG_TO_RAD * isoPt->lat));
      
      if (par.dayEfficiency == par.nightEfficiency) efficiency = par.dayEfficiency;
      else efficiency = isDay(t, zone.dataDate[0], zone.dataTime[0], isoPt->lat, isoPt->lon) ? par.dayEfficiency : par.nightEfficiency;

      const double minCog = vDirectCap - par.rangeCog;
      const double maxCog = vDirectCap + par.rangeCog;
      
      for (double cog = minCog; cog <= maxCog; cog += par.cogStep) {
         twa = fTwa (cog, twd);
         newPt.amure = (twa > 0.0) ? TRIBORD : BABORD;
         newPt.toIndexWp = pDest->toIndexWp;

         if (useMotor) {
            sog = par.motorSpeed;
            newPt.sail = 0;
         } else {
            sog = efficiency * findPolar (twa, tws * par.xWind, &polMat, &sailPolMat, &newPt.sail);
         }
         
         newPt.motor = useMotor;
         waveCorrection = 1.0;
         if (par.withWaves && (w > 0.0))
            waveCorrection = findPolar( twa, w, &wavePolMat, NULL, &bidon) / 100.0;

         sog *= waveCorrection;
         penalty = 0.0;

         if (!useMotor) {
            if (newPt.amure != isoPt->amure) {
               if (fabs (twa) < 90.0) penalty = par.penalty0 / 3600.0;     // Tack
               else penalty = par.penalty1 / 3600.0;                       // Gybe
            }
            if (newPt.sail != isoList [k].sail)                            // Sail change may bug
               penalty += par.penalty2 / 3600.0;
         }

         double realDt = dt - penalty;
         if (realDt < MIN_DT) realDt = MIN_DT;                             // In case penalty is very big...  

         dLat = sog * (realDt) * cos (DEG_TO_RAD * cog);                   // nautical miles in N S direction
         dLon = sog * (realDt) * sin (DEG_TO_RAD * cog) * invDenominator;  // nautical miles in E W direction

         if (par.withCurrent) {                                            // correction for current
            dLat += MS_TO_KN * vCurr * dt;
            dLon += MS_TO_KN * uCurr * dt * invDenominator;
         }

         newPt.lat = isoPt->lat + dLat / 60.0;
         newPt.lon = isoPt->lon + dLon / 60.0;
         newPt.id = pId++;
         newPt.father = isoPt->id;
         newPt.vmc = 0.0;
         newPt.orthoVmc = 0.0;
         // newPt.sector = 0;

         if (par.allwaysSea || isSeaTolerant(tIsSea, newPt.lat, newPt.lon)) {
            newPt.dd = orthoDist (newPt.lat, newPt.lon, pDest->lat, pDest->lon);
            const double alpha = orthoCap (pOr->lat, pOr->lon, newPt.lat, newPt.lon) - pOrToPDestCog;
            const double newPtToPorDist = orthoDist (newPt.lat, newPt.lon, pOr->lat, pOr->lon);
            newPt.vmc = newPtToPorDist * cos(DEG_TO_RAD * alpha);
            newPt.orthoVmc = newPtToPorDist * fabs(sin (DEG_TO_RAD * alpha));
            if (newPt.vmc > *bestVmc) *bestVmc = newPt.vmc;
            if (newPt.orthoVmc > *biggestOrthoVmc) *biggestOrthoVmc = newPt.orthoVmc;

            if (lenNewL < MAX_SIZE_ISOC) newList [lenNewL++] = newPt;       // new point added to the isochrone
            else return -1;
         }
      }
   }
   return lenNewL;
}

/*! find father of point in previous isochrone */
static int findFather (int ptId, int i, int lIsoc) {
    const Pp *iso = &isocArray [i * MAX_SIZE_ISOC];  // Access isoc i

    for (int k = 0; k < lIsoc; k++) {
        if (iso[k].id == ptId) return k;
    }

    fprintf (stderr, "In findFather, Error ptId not found: %d, Isoc No:%d, Isoc Len: %d\n", ptId, i, lIsoc);
    return -1;
}

/*! copy isoc Descriptors in a string 
   true if enough space, false if truncated */
bool isoDescToStr (char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE];
   char strLat [MAX_SIZE_LINE], strLon [MAX_SIZE_LINE];
   if (maxLen < MAX_SIZE_LINE) 
      return false;
   g_strlcpy (str, "No; toWp; Size; First; Closest; Distance; VMC;      FocalLat;    FocalLon\n", MAX_SIZE_LINE);

   for (int i = 0; i < nIsoc; i++) {
      //double distance = isoDesc [i].distance;
      //if (distance >= DBL_MAX) distance = -1;
      snprintf (line, MAX_SIZE_LINE, "%03d; %03d; %03d;  %03d;   %03d;     %07.2lf;  %07.2lf;  %s;  %s\n", i, isoDesc [i].toIndexWp, 
         isoDesc[i].size, isoDesc[i].first, 
         isoDesc[i].closest, -1.0, isoDesc[i].bestVmc, 
         latToStr (isoDesc [i].focalLat, par.dispDms, strLat, sizeof (strLat)), 
         lonToStr (isoDesc [i].focalLon, par.dispDms, strLon, sizeof (strLon)));
      //printf ("i = %d\n", i);
      //printf ("%s\n", line);
   
      if ((strlen (str) + strlen (line)) > maxLen) 
         return false;
      g_strlcat (str, line, maxLen);
   }
   return true;
}

/*! write in CSV file Isochrones */
bool dumpIsocToFile (const char *fileName) {
   FILE *f;
   Pp pt;
   if (fileName [0] == '\0') return false;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "In dumpAllIsoc, Error cannot write isoc: %s\n", fileName);
      return false;
   }
   fprintf (f, "  n;  WP;    Lat;    Lon;     Id; Father;  Amure;   Sail;  Motor;     dd;    VMC\n");
   for (int i = 0; i < nIsoc; i++) {
      for (int k = 0; k < isoDesc [i].size; k++) {
         pt = isocArray [i * MAX_SIZE_ISOC + k];
         fprintf (f, "%03d; %03d; %06.2f; %06.2f; %6d; %6d; %6d; %6d; %6d; %6.2lf; %6.2lf\n",\
            i, pt.toIndexWp, pt.lat, pt.lon, pt.id, pt.father, pt.amure, pt.sail, pt.motor, pt.dd, pt.vmc);
      }
   }
   fprintf (f, "\n");
   fclose (f);
   return true;
}

/*! store current route in history */
void saveRoute (SailRoute *route) {
   // allocate or rallocate space for routes
   if (historyRoute.n >= MAX_N_HISTORY) {
      fprintf (stderr, "In saveRoute, Error: MAX_N_HISTORY reached: %d\n", MAX_N_HISTORY);
      return;
   }
   SailRoute *newRoutes = realloc (historyRoute.r, (historyRoute.n + 1) * sizeof(SailRoute));
   if (newRoutes == NULL) {
      fprintf (stderr, "In saveRoute, Error: Memory allocation failed\n");
      return;
   }
   historyRoute.r = newRoutes;
   historyRoute.r[historyRoute.n] = *route; // Copy simple fields witout pointer

   // Allocation and copy of points
   size_t pointsSize = (route->nIsoc + 1) * sizeof (SailPoint);
   historyRoute.r[historyRoute.n].t = malloc (pointsSize);
   if (! historyRoute.r[historyRoute.n].t) {
      fprintf (stderr, "In saveRoute, Error: Memory allocation for SailPoint array failed\n");
      return;
   }
   memcpy (historyRoute.r[historyRoute.n].t, route->t, pointsSize); // deep copy of points
   
   historyRoute.n += 1;
}

/*! free space for history route */
void freeHistoryRoute (void) {
   free (historyRoute.r);
   historyRoute.r = NULL;
   historyRoute.n = 0;
}

/*! add additionnal information to the route */
static void statRoute (SailRoute *route) {
   bool manoeuvre;
   const double epsilon = 0.00001; // hours
   const double epsilonSog = 0.001; // hours
   Pp p = {0};
   if (route->n == 0) return;
   fprintf (stdout, "route.n = %d\n", route->n);
   char *polarFileName = g_path_get_basename (par.polarFileName);
   g_strlcpy (route->polarFileName, polarFileName, sizeof (route->polarFileName));
   free (polarFileName);
   route->dataDate = zone.dataDate [0]; // save Grib origin
   route->dataTime = zone.dataTime [0];
   route->nSailChange = 0;
   route->nAmureChange = 0;
   route->isocTimeStep = par.tStep;
   route->totDist = route->motorDist = route->tribordDist = route->babordDist = 0;
   route->maxTws = route->maxGust = route->maxWave = 0;
   route->avrTws = route->avrGust = route->avrWave = 0;
   route->nSailChange = 0;
   route-> t[0].stamina = par.staminaVR;
   route -> t[0].time = 0;
   route->distToDest = 0.0;
   double deltaTime = par.tStep;
   double sog = 0;

   for (int i = 1; i < route->n; i++) {
      manoeuvre = false;
      p.lat = route->t [i-1].lat;
      p.lon = route->t [i-1].lon;

      if ((route->t [i-1].toIndexWp >= 0) && (route->t [i-1].toIndexWp < route->nWayPoints) 
         && (route->t [i-1].toIndexWp != route->t [i].toIndexWp)) {
         fprintf (stdout, "i = %d, WayPoint no: %d\n", i, route-> t[i-1].toIndexWp);
         deltaTime = route->lastStepWpDuration [route-> t[i-1].toIndexWp];
      }
      else if ((i == route->n - 1) && route->destinationReached)
         deltaTime = route->lastStepDuration;
      else deltaTime = par.tStep;
      
      route->t [i].time = route->t [i-1].time + deltaTime;

      route->t [i-1].ld  = loxoDist (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
      route->t [i-1].od = orthoDist (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
      if (deltaTime > epsilon) {
         sog = route->t [i-1].od / deltaTime;
      }
      route->t [i-1].sog = sog;
      if (sog < epsilonSog && i > 1) {
         route->t [i-1].oCap = route->t [i-2].oCap;
         route->t [i-1].lCap = route->t [i-2].lCap;
      }
      else {
         route->t [i-1].lCap = directCap (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
         route->t [i-1].oCap = orthoCap (route->t[i-1].lat, route->t[i-1].lon, route->t[i].lat, route->t[i].lon);
      }
      route->totDist += route->t [i-1].od;
      
      findWindGrib (p.lat, p.lon, par.startTimeInHours + route->t [i-1].time, 
                    &route->t [i-1].u, &route->t [i-1].v, &route->t [i-1].g, &route->t [i-1].w, 
                    &route->t [i-1].twd, &route->t [i-1].tws);

      /*double twa = fTwa (route->t[i-1].lCap, route->t[i-1].twd);
      route-> t [i-1].amure = (twa > 0.0) ? TRIBORD : BABORD; // rewrite because bug
      */
      // printf ("i = %d, od = %.2lf, tot = %.2lf\n", i, route->t [i-1].od, route->totDist);
      if ((i > 1) && (route-> t [i-1].sail != route-> t [i-2].sail)) {
         route->t [i-1].stamina = fmax (0.0, route->t [i-2].stamina - 100.0 * fPointLoss (STAMINA_SHIP, STAMINA_SAIL, route ->t [i-2].tws, STAMINA_FULL_PACK));
         manoeuvre = true;
         route->nSailChange += 1;
      }
      if ((i > 1) && (route-> t [i-1].amure != route-> t [i-2].amure)) {
         route->t [i-1].stamina =  fmax (0.0, route->t [i-2].stamina - 100.0 * fPointLoss (STAMINA_SHIP, STAMINA_TACK, route ->t [i-2].tws, STAMINA_FULL_PACK));
         manoeuvre = true;
         route->nAmureChange += 1;
      }
      
      if (route->t [i-1].motor) {
         route->motorDist += route->t [i-1].od;
      }
      else { 
         if (route->t [i-1].amure == TRIBORD)
            route->tribordDist += route->t [i-1].od;
         else if (route->t [i-1].amure == BABORD)
            route->babordDist += route->t [i-1].od;
      }
      // printf ("i-1: %d, amure: %d, stamina: %.2lf, twa: %.2lf\n", i - 1, route-> t [i-1].amure, route->t [i-1].stamina, twa); 
      // route->t [i-1].sail = closestInPolar (twa, route->t[i-1].tws * par.xWind, &sailPolMat); 
      route->avrTws  += route->t [i-1].tws;
      route->avrGust += route->t [i-1].g;
      route->avrWave += route->t [i-1].w;
      route->maxTws  = MAX (route->maxTws, route->t [i-1].tws);
      route->maxGust = MAX (route->maxGust, route->t [i-1].g);
      route->maxWave = MAX (route->maxWave, route->t [i-1].w);

      if (! manoeuvre && i > 1) {
         const double recup = fTimeToRecupOnePoint (route->t [i-1].tws); // time in seconds  to get one point
         if (recup > 1.0)
            route-> t [i-1].stamina = fmin (100.0, route-> t [i-2].stamina + 3600.0 * route->isocTimeStep / recup);
      }
   } // end for

   /*if (route->t [route->n - 1].motor) {
      route->motorDist += route->t [route->n - 1].od;
   }
   else {
      if (route->t [route->n - 1].amure == TRIBORD)
         route->tribordDist += route->t [route->n - 1].od;
      else if (route->t [route->n - 1].amure == BABORD)
         route->babordDist += route->t [route->n - 1].od;
   }*/
   p.lat = route->t [route->n - 1].lat;
   p.lon = route->t [route->n - 1].lon;
   findWindGrib (p.lat, p.lon, par.startTimeInHours + route->t [route->n-1].time, 
                 &route->t [route->n-1].u, &route->t [route->n-1].v, &route->t [route->n-1].g, 
                 &route->t [route->n-1].w, &route->t [route->n-1].twd, &route->t [route->n-1].tws);
   
   route->maxTws  = MAX (route->maxTws, route->t [route->n-1].tws);
   route->maxGust = MAX (route->maxGust, route->t [route->n-1].g);
   route->maxWave = MAX (route->maxWave, route->t [route->n-1].w);
   route->avrTws  += route->t [route->n-1].tws;
   route->avrGust += route->t [route->n-1].g;
   route->avrWave += route->t [route->n-1].w;

   if (route->n > 1) { 
      route->t [route->n-1].ld = route->t [route->n-1].od = 0;
      route->t [route->n-1].sog = route->t [route->n-2].sog;   // convention last destination  has no speed in reality
      route->t [route->n-1].lCap = route->t [route->n-2].lCap; // convention last destination  has no cap in reality
      route->t [route->n-1].oCap = route->t [route->n-2].oCap;
      route->t [route->n-1].sail = route->t [route->n-2].sail; // convention. No need for sail
      route->t [route->n-1].stamina = route->t [route->n-2].stamina;
      if (! route->destinationReached) {
         route->distToDest = orthoDist (route->t [route->n-1].lat, route->t[route->n-1].lon, par.pDest.lat, par.pDest.lon);
      }
   }
   route->avrTws /= route->n;
   route->avrGust /= route->n;
   route->avrWave /= route->n;
   route->duration = route->t [route->n-1].time;
   route->avrSog = route->totDist / route->duration;
}

/*! store route 
   response false if error */
bool storeRoute (SailRoute *route, const Pp *pOr, const Pp *pDest) {
   // route->destinationReached = (pDest.id == 0);
   int iFather;
   route->nIsoc = nIsoc;
   route->n =  (pDest->id == 0) ? nIsoc + 2 : nIsoc + 1;

   Pp pt = *pDest;
   Pp ptLast = *pDest;
   route->t [route->n - 1].lat = pDest->lat;
   route->t [route->n - 1].lon = lonCanonize (pDest->lon);
   route->t [route->n - 1].id = pDest->id;
   route->t [route->n - 1].father = pDest->father;
   route->t [route->n - 1].motor = pDest->motor;
   route->t [route->n - 1].amure = pDest->amure;
   route->t [route->n - 1].toIndexWp = pDest->toIndexWp;
   route->t [route->n - 1].sail = pDest->sail;

   fprintf (stdout, "pDest with id: %d, father: %d, toIndexWp: %d, route.n: %d\n", 
      pDest->id, pDest->father, pDest->toIndexWp, route->n); 

   for (int i = route->n - 3; i >= 0; i--) {
      iFather = findFather (pt.father, i, isoDesc[i].size);
      //printf ("ISOC: %d, ID: %d, FATHER: %d, TO_INDEX_WP: %d\n", i, pt.id, pt.father, pt.toIndexWp); 
      if (iFather == -1) {
         fprintf (stdout, "In storeRoute: ERROR findFather at isoc: %d\n", i);
         return false;
      }
      pt = isocArray [i * MAX_SIZE_ISOC + iFather];
      if ((pt.toIndexWp < -1) || pt.toIndexWp > route->nWayPoints) {
         fprintf (stdout, "In storeRoute: ERROR isoc: %d pt.toIndexWp: %d\n", i, pt.toIndexWp);
      }
      route->t [i+1].lat = pt.lat;
      route->t [i+1].lon = lonCanonize (pt.lon);
      route->t [i+1].id = pt.id;
      route->t [i+1].father = pt.father;
      route->t [i+1].motor = ptLast.motor;
      route->t [i+1].amure = ptLast.amure;
      route->t [i+1].toIndexWp = ptLast.toIndexWp;
      route->t [i+1].sail = pt.sail;
      ptLast = pt;
   }
   route->t [0].lat = pOr->lat;
   route->t [0].lon = lonCanonize (pOr->lon);
   route->t [0].id = pOr->id;
   route->t [0].father = pOr->father;
   route->t [0].motor = ptLast.motor;
   route->t [0].amure = ptLast.amure;
   route->t [0].toIndexWp = ptLast.toIndexWp;
   route->t [0].sail = ptLast.sail;

   return true;
}

/*! produce string that says if Motor, Tribord, Babord */
static char *motorTribordBabord (bool motor, int amure, char* str, size_t maxLen) {
   if (motor) g_strlcpy (str, "Mot", maxLen);
   else
      if (amure == TRIBORD)
         g_strlcpy (str, "Tri", maxLen);
      else
         g_strlcpy (str, "Bab", maxLen);
   return str;
}

/*! copy route in a string
   true if enough space, false if truncated */
bool routeToStr (const SailRoute *route, char *str, size_t maxLen, char *footer, size_t maxLenFooter) {
   char line [MAX_SIZE_LINE], strDate [MAX_SIZE_DATE];
   char strLat [MAX_SIZE_NAME], strLon [MAX_SIZE_NAME], strSail [MAX_SIZE_NAME];
   char shortStr [SMALL_SIZE], strDur [MAX_SIZE_LINE];
   double awa, aws;
   double maxAws = 0.0;
   double twa = fTwa (route->t[0].lCap, route->t[0].twd);
   fAwaAws (twa, route->t[0].tws, route->t[0].sog, &awa, &aws);
   if (maxLen < MAX_SIZE_LINE) {
      fprintf (stderr, "In routeToStr, maxLen too small: %zu\n", maxLen);
      return false;
   }

   fSailName (route->t[0].sail, strSail, sizeof (strSail)),

   g_strlcpy (str, "  No;  WP; Lat;        Lon;         Date-Time;            Sail;  M/T/B;   HDG;\
    Dist;     SOG;   Twd;   Twa;     Tws;    Gust;   Awa;     Aws;   Waves; Stamina\n", MAX_SIZE_LINE);
   snprintf (line, sizeof line, \
      " pOr; %3d; %-12s;%-12s; %s; %-8s; %6s; %4d°; %7.2lf; %7.2lf; %4d°; %4.0lf°; %7.2lf; %7.2lf; %4.0lf°; %7.2lf; %7.2lf; %7.2lf\n",\
      route->t[0].toIndexWp,\
      latToStr (route->t[0].lat, par.dispDms, strLat, sizeof (strLat)),\
      lonToStr (route->t[0].lon, par.dispDms, strLon, sizeof (strLon)),\
      newDate (route->dataDate, route->dataTime/100.0 + par.startTimeInHours + route->t[0].time, strDate, sizeof (strDate)),\
      strSail, \
      motorTribordBabord (route->t[0].motor, route->t[0].amure, shortStr, SMALL_SIZE),\
      ((int) (route->t[0].oCap + 360) % 360), route->t[0].od, route->t[0].sog,\
      (int) (route->t[0].twd + 360) % 360,\
      twa, route->t[0].tws,\
      MS_TO_KN * route->t[0].g, awa, aws, route->t[0].w, route->t[0].stamina);

   g_strlcat (str, line, maxLen);
   for (int i = 1; i < route->n; i++) {
      twa = fTwa (route->t[i].lCap, route->t[i].twd);
      fAwaAws (twa, route->t[i].tws, route->t[i].sog, &awa, &aws);
      if (aws > maxAws) maxAws = aws;
      if ((fabs(route->t[i].lon) > 180.0) || (fabs (route->t[i].lat) > 90.0))
         snprintf (line, sizeof line, " Isoc %3d: Error on latitude or longitude\n", i-1);   
      else
         snprintf (line, sizeof line, \
           "%4d; %3d; %-12s;%-12s; %s; %-8s; %6s; %4d°; %7.2f; %7.2lf; %4d°; %4.0lf°; %7.2lf; %7.2lf; %4.0lf°; %7.2lf; %7.2lf; %7.2lf\n",\
            i-1,\
            route->t[i].toIndexWp,\
            latToStr (route->t[i].lat, par.dispDms, strLat, sizeof (strLat)),\
            lonToStr (route->t[i].lon, par.dispDms, strLon, sizeof (strLon)),\
            newDate (route->dataDate, route->dataTime/100.0 + par.startTimeInHours + route->t[i].time, strDate, sizeof (strDate)), \
            fSailName (route->t[i].sail, strSail, sizeof (strSail)), \
            motorTribordBabord (route->t[i].motor, route->t[i].amure, shortStr, SMALL_SIZE), \
            ((int) (route->t[i].oCap  + 360) % 360), route->t[i].od, route->t[i].sog,\
            (int) (route->t[i].twd + 360) % 360,\
            fTwa (route->t[i].lCap, route->t[i].twd), route->t[i].tws,\
            MS_TO_KN * route->t[i].g, awa, aws, route->t[i].w, route->t[i].stamina);
      g_strlcat (str, line, maxLen);
   }
  
   g_strlcat (str, "\n \n", maxLen); 
   snprintf (line, sizeof line, 
       " Avr/Max Tws      : %.2lf/%.2lf Kn\n"
       " Total/Motor Dist.: %.2lf/%.2lf NM\n"
       " Total duration   : %sHours\n"
       " Sail Changes     : %d\n"
       " Amures Changes   : %d\n"
       " Polar file       : %s\n",
       route->avrTws, route->maxTws, route->totDist, route->motorDist, 
       durationToStr (route->duration, strDur, sizeof (strDur)),
       route->nSailChange, route->nAmureChange, route->polarFileName);

   g_strlcat (str, line, maxLen);

   snprintf (footer, maxLenFooter, "%s Arrival: %s     Route length: %d,   Isoc time Step: %.2lf", 
      competitors.t [route->competitorIndex].name,\
      newDate (route->dataDate, route->dataTime/100.0 + route->t[0].time + route->duration, strDate, sizeof (strDate)),\
      route->n,
      route->isocTimeStep);
   return true;
}

/*! return true if pDest can be reached from pA - in less time than dt
   timeTo give the time to get to pDest 
   motor true if goal reached with motor 
   current not considered for this last step
   penalty not considered because may bug 
*/
static inline bool simpleGoalP (const Pp *pA, const Pp *pDest, double t, double dt, double *timeTo,
                   double *distance, bool *motor, int *amure, int *sail) {
   static const double epsilon = 0.1; // kn
   double u, v, gust, w, twd, tws, sog, efficiency;
   int bidon;
   const double d = orthoDist (pDest->lat, pDest->lon, pA->lat, pA->lon);
   *distance = d;

   // Cap (COG) from pA to pDest
   const double coeffLat = cos(DEG_TO_RAD * ((pA->lat + pDest->lat) * 0.5));
   const double dLat = pDest->lat - pA->lat;
   const double dLon = (pDest->lon - pA->lon) * coeffLat;
   const double cog = RAD_TO_DEG * atan2(dLon, dLat);

   // Wind & waves  at A point 
   findWindGrib(pA->lat, pA->lon, t, &u, &v, &gust, &w, &twd, &tws);

   // TWA & amure
   const double twa = fTwa(cog, twd);
   *amure = (twa > 0) ? TRIBORD : BABORD;

   // Motor if speed with polar under threshold 
   const bool useMotor = ((maxSpeedInPolarAt(tws * par.xWind, &polMat) < par.threshold) && (par.motorSpeed > 0.0));
   *motor = useMotor;

   // Day/Night efficiency 
   if (par.dayEfficiency == par.nightEfficiency) efficiency = par.dayEfficiency;
   else efficiency = isDay(t, zone.dataDate[0], zone.dataTime[0], pA->lat, pA->lon) ? par.dayEfficiency : par.nightEfficiency;
   
   // Speed (kn)
   int sailChoice = 0;
   if (useMotor) {
      sog = par.motorSpeed;
      *sail = 0;
   } else {
      sog = efficiency * findPolar(twa, tws * par.xWind, &polMat, &sailPolMat, &sailChoice);
      *sail = sailChoice;
   }

   // Wave Correction
   if (par.withWaves && w > 0.0 ) {
      const double waveCorrection = findPolar(twa, w, &wavePolMat, NULL, &bidon);
      if (waveCorrection > 0.0) sog *= waveCorrection * 0.01;
   }

   if (sog <= epsilon) {
      *timeTo = DBL_MAX; // hours
      return false;
   }
   
   // Time to go to best point in hours
   *timeTo = d / sog; // NM / (NM/h) = h

   // Reachability test in dt interval time (hours)
   return (sog * dt) >= d;
}

/*! true if goal can be reached directly in dt from isochrone 
  update isoDesc
  side effect : pDest.father can be modified ! */
static inline bool simpleGoal (Pp *pDest, Pp *isoList, int len, double t, double dt, double *lastStepDuration, bool *motor, int *amure) {
   double bestTime = DBL_MAX, time, distance;
   bool destinationReached = false, locMotor, bestMotor = false;
   int sail, locAmure, bestAmure = 0;

   for (int k = 0; k < len; k++) {
      const Pp *curr = &isoList[k];
      if (!par.allwaysSea && !isSea(tIsSea, curr->lat, curr->lon)) continue;
      if (simpleGoalP (curr, pDest, t, dt, &time, &distance, &locMotor, &locAmure, &sail)) {
         destinationReached = true;
      }
      if (time < bestTime) {
         bestTime = time;
         if (destinationReached) {
            pDest->father = curr->id;
            pDest->motor = *motor;
            pDest->amure = *amure;
            pDest->sail = sail;
            bestMotor = locMotor;
            bestAmure = locAmure;
         }
         // if (distance < minDistance) minDistance = distance;
      }
   }
   // isoDesc[nIsoc -1].distance = minDistance;
   *lastStepDuration = bestTime;
   *motor = bestMotor;
   *amure = bestAmure;
   return destinationReached;
}

/*! return closest index point to pDest in Isoc, and this point */ 
/*! return true if pDest can be reached from segment [pA pB] - in less time than dt
   timeTo give the time to get to pDest 
   motor true if goal reached with motor 
   bestFirst true if pA is the best, false if pB is best
   current not considered for this last step
   penalty not considered because may bug 
*/
static inline bool goalP (const Pp *pA, const Pp *pB, const Pp *pDest, double t,
                    double dt, double *timeTo,
                    double *distance, bool *motor, int *amure, int *sail, bool *bestFirst) {
   static const double epsilon = 0.1; // kn
   double u, v, gust, w, twd, tws, twa, sog, efficiency;
   int bidon; // unused but needed by API

   // 1) Minimum distance
   double bestLat, bestLon;
   double latH, lonH;
   const double distToSegment = distSegmentWithFoot(pDest->lat, pDest->lon, pA->lat, pA->lon, pB->lat, pB->lon, &latH, &lonH);
   const double dA = orthoDist (pDest->lat, pDest->lon, pA->lat, pA->lon);
   const double dB = orthoDist (pDest->lat, pDest->lon, pB->lat, pB->lon);
   if (dA < dB) {
      *distance = dA;
      bestLat = pA->lat;
      bestLon = pA->lon;
      *bestFirst = true;
   }
   else {
      *distance = dB;
      bestLat = pB->lat;
      bestLon = pB->lon;
      *bestFirst = false;
   }
   // 2) Cap (COG) from best to pDest
   const double coeffLat = cos(DEG_TO_RAD * ((bestLat + pDest->lat) * 0.5));
   const double dLat = pDest->lat - bestLat;
   const double dLon = (pDest->lon - bestLon) * coeffLat;
   const double cog = RAD_TO_DEG * atan2(dLon, dLat);

   // 3) Wind & waves  at H point 
   findWindGrib(bestLat, bestLon, t, &u, &v, &gust, &w, &twd, &tws);

   // 4) TWA & amure
   twa = fTwa(cog, twd);
   *amure = (twa > 0.0) ? TRIBORD : BABORD;

   // 5) Motor if speed with polar under threshold 
   const bool useMotor = ((maxSpeedInPolarAt(tws * par.xWind, &polMat) < par.threshold) && (par.motorSpeed > 0.0));
   *motor = useMotor;
   // 6) Day/Night efficiency
   if (par.dayEfficiency == par.nightEfficiency) efficiency = par.dayEfficiency;
   else efficiency = isDay(t, zone.dataDate[0], zone.dataTime[0], bestLat, bestLon) ? par.dayEfficiency : par.nightEfficiency;


   // 7) Speed (kn)
   int sailChoice = 0;
   if (useMotor) {
      sog = par.motorSpeed;
      *sail = 0;
   } else {
      sog = efficiency * findPolar(twa, tws * par.xWind, &polMat, &sailPolMat, &sailChoice);
      *sail = sailChoice;
   }

   // 8) Wave Correction
   if (par.withWaves && (w > 0.0)) {
      const double waveCorrection = findPolar(twa, w, &wavePolMat, NULL, &bidon);
      if (waveCorrection > 0) sog *= (waveCorrection / 100.0);
   }

   if (sog <= epsilon) {
      *timeTo = DBL_MAX; // hours
      return false;
   }

   // 9) Time to go to best point in hours
   *timeTo = *distance / sog; // NM / (NM/h) = h
   // 10) Reachability test in dt interval time (hours)
   return (sog * dt) > fmin (distToSegment, *distance); // distSegment should allways be lesser than distance
   // return (sog * dt) > *distance;
}

/*! true if goal can be reached directly in dt from isochrone 
  update isoDesc
  side effect : pDest.father can be modified ! */
static inline bool goal (Pp *pDest, Pp *isoList, int len, double t, double dt, double *lastStepDuration, bool *motor, int *amure) {
   double bestTime = DBL_MAX;
   double time, distance;
   bool destinationReached = false;
   int sail;
   // double minDistance = 9999.99;
   const Pp *prev = &isoList[0];
   bool bestFirst;

   for (int k = 1; k < len; k++) {
      const Pp *curr = &isoList[k];
      if (par.allwaysSea || isSeaTolerant(tIsSea, curr->lat, curr->lon)) {
         if (goalP (prev, curr, pDest, t, dt, &time, &distance, motor, amure, &sail, &bestFirst)) {
            destinationReached = true;
         }
         if (time < bestTime) {
            bestTime = time;
            if (destinationReached) {
               pDest->father = bestFirst ? prev->id : curr->id;
               pDest->motor = *motor;
               pDest->amure = *amure;
               pDest->sail = sail;
            }
         }
         // if (distance < minDistance) minDistance = distance;
      }
      prev = curr; 
   }
   // isoDesc[nIsoc -1].distance = minDistance;
   *lastStepDuration = bestTime;
   return destinationReached;
}

/*! return closest index point to pDest in Isoc, and this point */ 
static int fClosest (const Pp *isoc, int n, const Pp *pDest, Pp *closest) {
   *closest = isoc [0];
   double lastClosestDist = DBL_MAX;  // closest distance to destination in last isochrone computed
   double d;
   int i;
   int index = -1;
   for (i = 0; i < n; i++) {
      const Pp *curr = &isoc[i];
      d = orthoDist (pDest->lat, pDest->lon, curr->lat, curr->lon);
      if (d < lastClosestDist) {
         lastClosestDist = d;
         *closest = *curr;
         index = i;
      }
   }
   return index;
}

/*! when no wind, build next isochrone as a replica of previous isochrone 
    Manage carefully id and father fields */
static void replicate(int n) {
    if (n <= 0) return;
    const int len = isoDesc[n - 1].size;
    Pp *src = &isocArray[(n - 1) * MAX_SIZE_ISOC];
    Pp *dst = &isocArray[n * MAX_SIZE_ISOC];

    memcpy(dst, src, len * sizeof(Pp));  // copy isochrone n - 1 to isochrone n

    for (int i = 0; i < len; i++) {
        int idSrc = src [i].id;    // Be careful to specific id and father fields
        dst[i].id = idSrc + len;
        dst[i].father = idSrc;
    }
    isoDesc[n] = isoDesc[n - 1];
}

/*! find optimal routing from p0 to pDest using grib file and polar
    return number of steps to reach pDest, NIL if unreached, -1 if problem, -2 if stopped by user, 
    0 reserved for not terminated
    return also lastStepDuration if destination reached (0 if unreached) 
    side effects: nIsoc, maxNIsoc, pOrToPDestCog, isoDesc, isocArray, route
    pOr and pDest modified
*/
static int routing (Pp *pOr, Pp *pDest, int toIndexWp, double t, double dt, double *lastStepDuration) {
   const double minStep = 0.25;
   bool motor = false;
   int amure, sail = 0;
   double distance;
   double timeToReach = 0;
   int lTempList = 0;
   double timeLastStep;
   Pp *tempList = NULL;             // one dimension array of points
   bool bidon;

   if (dt < minStep) {
      fprintf (stderr, "In routing: time step for routing <= %.2lf\n", minStep);
      return -1;
   }

   if ((tempList = malloc (MAX_SIZE_ISOC * sizeof(Pp))) == NULL) {
      fprintf (stderr, "in routing: error in memory templIst allocation\n");
      return -1;
   }

   maxNIsoc = (int) ((1 + zone.timeStamp [zone.nTimeStamp - 1]) / dt);
   if (maxNIsoc > MAX_N_ISOC) {
      fprintf (stderr, "in routing maxNIsoc exeed MAX_N_ISOC\n");
      free (tempList);
      return -1;
   } 

   Pp *tempIsocArray = (Pp*) realloc (isocArray, maxNIsoc * MAX_SIZE_ISOC * sizeof(Pp));
   if (tempIsocArray == NULL) {
      fprintf (stderr, "in routing: realloc error for isocArray\n");
      free (tempList);
      return -1;
   }
   isocArray = tempIsocArray;
   
   IsoDesc *tempIsoDesc = (IsoDesc *) realloc (isoDesc, maxNIsoc * sizeof (IsoDesc));
   if (tempIsoDesc == NULL) {
      fprintf (stderr, "in routing: realloc for IsoDesc failed\n");
      free (isocArray);
      free (tempList);
      return -1;
   } 
   isoDesc = tempIsoDesc;
    
   SailPoint *tempSailPoint = (SailPoint *) realloc (route.t, (maxNIsoc + 1) * sizeof(SailPoint));
   if (tempSailPoint  == NULL) {
      fprintf (stderr, "in routing: realloc for route.t failed\n");
      free (isoDesc);
      free (isocArray);
      free (tempList);
      return -1;
   } 
   route.t = tempSailPoint;

   pOr->dd = orthoDist (pOr->lat, pOr->lon, pDest->lat, pDest->lon);
   pOr->vmc = 0;
   //pOrToPDestCog = orthoCap (pOr->lat, pOr->lon, pDest->lat, pDest->lon);
   pOrToPDestCog = directCap (pOr->lat, pOr->lon, pDest->lat, pDest->lon); // better
   pDest->toIndexWp = toIndexWp;
   tempList [0] = *pOr;             // list with just one element;
   initSector (nIsoc % 2, par.nSectors); 

   if (goalP (pOr, pOr, pDest, t, dt, &timeToReach, &distance, &motor, &amure, &sail, &bidon)) {
      pDest->father = pOr->id;
      pDest->motor = motor;
      pDest->amure = amure;
      pDest->sail = sail;
      *lastStepDuration = timeToReach;
      free (tempList);
      fprintf (stdout, "destination reached directly. No isochrone\n");
      return nIsoc + 1;
   }
   isoDesc [nIsoc].size = buildNextIsochrone (pOr, pDest, tempList, 1, t, dt, 
                          &isocArray [nIsoc * MAX_SIZE_ISOC], &isoDesc [nIsoc].bestVmc, &isoDesc [nIsoc].biggestOrthoVmc);

   if (isoDesc [nIsoc].size == -1) {
      free (tempList);
      fprintf (stderr, "In routing: isoc Size error, nIsoc: %d\n", nIsoc);
      return -1;
   }
   // printf ("%-20s%d, %d\n", "Isochrone no, len: ", 0, isoDesc [0].size);
   // keep track of closest point in isochrone
   isoDesc [nIsoc].first = 0;
   isoDesc [nIsoc].closest = fClosest (&isocArray [nIsoc * MAX_SIZE_ISOC], isoDesc[nIsoc].size, pDest, &lastClosest); 
   isoDesc [nIsoc].toIndexWp = toIndexWp; 
   isoDesc [nIsoc].focalLat = pOr->lat;
   isoDesc [nIsoc].focalLon = pOr->lon;
   if (isoDesc [nIsoc].size == 0) { // no wind at the beginning. 
      isoDesc [nIsoc].size = 1;
      isocArray [nIsoc * MAX_SIZE_ISOC + 0] = *pOr;
   }
   
   nIsoc += 1;
   //printf ("Routing t = %.2lf, zone: %.2ld\n", t, zone.timeStamp [zone.nTimeStamp-1]);
   while (t < (zone.timeStamp [zone.nTimeStamp - 1]/* + par.tStep*/) && (nIsoc < maxNIsoc)) { // ATT
      if (g_atomic_int_get (&route.ret) == ROUTING_STOPPED) { // -2
         free (tempList);
         return ROUTING_STOPPED; // stopped by user in another thread !!!
      }
      t += dt;
      // printf ("nIsoc = %d\n", nIsoc);
      // if (simpleGoal (pDest, &isocArray [(nIsoc - 1) * MAX_SIZE_ISOC], isoDesc[nIsoc - 1].size, t, dt, &timeLastStep, &motor, &amure)) {
      if (goal (pDest, &isocArray [(nIsoc - 1) * MAX_SIZE_ISOC], isoDesc[nIsoc - 1].size, t, dt, &timeLastStep, &motor, &amure)) {

         isoDesc [nIsoc].size = optimize (pOr, pDest, nIsoc, par.opt, tempList, lTempList, &isocArray [nIsoc * MAX_SIZE_ISOC]);
         if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
            fprintf (stderr, "In routing, goal reached but no wind at isoc: %d\n", nIsoc);
            replicate (nIsoc);
         }
         isoDesc [nIsoc].first = findFirst (nIsoc);
         isoDesc [nIsoc].closest = fClosest (&isocArray [nIsoc * MAX_SIZE_ISOC], isoDesc[nIsoc].size, pDest, &lastClosest); 
         isoDesc [nIsoc].toIndexWp = toIndexWp; 
         *lastStepDuration = timeLastStep;
         fprintf (stdout, "In routing, Destination reached to WP %d for %s\n", toIndexWp, competitors.t [competitors.runIndex].name);
         fprintf (stdout, "pDest.id: %d, pDest.father: %d, pDest.toIndexWP: %d\n", pDest->id, pDest->father, pDest->toIndexWp);
         free (tempList);
         return nIsoc + 1;
      }
      lTempList = buildNextIsochrone (pOr, pDest, &isocArray [(nIsoc -1) * MAX_SIZE_ISOC], 
                                      isoDesc [nIsoc - 1].size, t, dt, tempList, &isoDesc [nIsoc].bestVmc,  &isoDesc [nIsoc].biggestOrthoVmc);
      if (lTempList == -1) {
         free (tempList);
         fprintf (stderr, "In routing: buildNextIsochrone return: -1 value. See MAX_SIZE_ISOC\n");
         return -1;
      }
      isoDesc [nIsoc].size = optimize (pOr, pDest, nIsoc, par.opt, tempList, lTempList, &isocArray [nIsoc * MAX_SIZE_ISOC]);
      if (isoDesc [nIsoc].size == 0) { // no Wind ... we copy
         fprintf (stderr, "In routing, no wind at isoc: %d\n", nIsoc);
         replicate (nIsoc);
      }
      isoDesc [nIsoc].first = findFirst (nIsoc);; 
      isoDesc [nIsoc].closest = fClosest (&isocArray [nIsoc * MAX_SIZE_ISOC], isoDesc[nIsoc].size, pDest, &lastClosest); 
      isoDesc [nIsoc].toIndexWp = toIndexWp; 
      // printf ("Isoc: %d Biglist length: %d optimized size: %d\n", nIsoc, lTempList, isoDesc [nIsoc].size);
      nIsoc += 1;
   }
   *lastStepDuration = 0.0;
   free (tempList);
   return NIL;
}

/*! global variable initialization for routing */
static void initRouting (void) { 
   isocArray = NULL;
   isoDesc = NULL;
   maxNIsoc = 0;
   nIsoc = 0;
   pOrToPDestCog = 0;
   memset (sector, 0, sizeof(sector));
   lastClosest = par.pOr;
   tDeltaCurrent = zoneTimeDiff (&currentZone, &zone); // global variable
   par.pOr.id = -1;
   par.pOr.father = -1;
   par.pDest.id = 0;
   par.pDest.father = 0;
   pId = 1;
   memset (&route, 0, sizeof (SailRoute));
   g_atomic_int_set (&route.ret, ROUTING_RUNNING);
   route.destinationReached = false;
}

/*! check arrival between pDest and segments [pPrev-pCurr] and [pCurr-pNext] 
   pFoot0 is the projection of pDest on [pPrev-pCurr]
   pFoot1 is the projection of pDest on [pCurr-pNext]
   produces in Json distance informations:
      dSeg0: pPrev-pCurr 
      dSeg1: pCurr-pNext
      dPrev: pDest-pPrev
      dNext: pDest-pNext
      dCurr: pDest-pCurr
      dFoot0: pDest-pFoot0
      dFoot1: pDest-pFoot1
   When destination is reached, pDest = par.pDest
   When destination is unreached, pDest is the arrival point on last isochrone and pDest = pCurr = pFoot0 = pFoot1
*/
static void checkArrival (SailRoute *route, Pp *pDest) {
   double foot0Lat, foot0Lon, foot1Lat, foot1Lon;
   strlcpy (route->lastPointInfo, "\"\"", sizeof route->lastPointInfo);
   
   const int lastI = route->nIsoc - 1; // last isochrone
   if (lastI < 1) return;
   const int size = isoDesc[lastI].size;
   if (size < 1 || size > MAX_SIZE_ISOC) return; 

   int index;
   if (route->destinationReached) index = findFather (pDest->father, lastI, size);
   else index = findFather (pDest->id, lastI, size);

   int prev = index - 1;
   if (prev < 0) prev = size - 1; 
   int next = index + 1;
   if (next >= size) next = 0;

   Pp pCurr = isocArray [lastI * MAX_SIZE_ISOC + index];
   Pp pPrev = isocArray [lastI * MAX_SIZE_ISOC + prev];
   Pp pNext = isocArray [lastI * MAX_SIZE_ISOC + next];

   const double dSeg0 = orthoDist (pPrev.lat, pPrev.lon, pCurr.lat, pCurr.lon);
   const double dSeg1 = orthoDist (pNext.lat, pNext.lon, pCurr.lat, pCurr.lon);
   const double dCurr = orthoDist (pDest->lat, pDest->lon, pCurr.lat, pCurr.lon);
   const double dPrev = orthoDist (pDest->lat, pDest->lon, pPrev.lat, pPrev.lon);
   const double dNext = orthoDist (pDest->lat, pDest->lon, pNext.lat, pNext.lon);

   const double dFoot0 = distSegmentWithFoot(pDest->lat, pDest->lon, pPrev.lat, pPrev.lon, 
                              pCurr.lat, pCurr.lon, &foot0Lat, &foot0Lon); // NM

   const double dFoot1 = distSegmentWithFoot(pDest->lat, pDest->lon, pNext.lat, pNext.lon, 
                              pCurr.lat, pCurr.lon, &foot1Lat, &foot1Lon); // NM

   snprintf (route->lastPointInfo, sizeof route->lastPointInfo, 
      "{\n"
      "    \"latDest\": %.6lf, \"lonDest\": %.6lf, \"latCurr\": %.6lf, \"lonCurr\": %.6lf,\n"
      "    \"latPrev\": %.6lf, \"lonPrev\": %.6lf, \"latNext\": %.6lf, \"lonNext\": %.6lf,\n"
      "    \"latFoot0\": %.6lf, \"lonFoot0\": %.6lf, \"latFoot1\": %.6lf, \"lonFoot1\": %.6lf,\n" 
      "    \"dSeg0\": %.2lf, \"dSeg1\": %.2lf, \"dCurr\": %.2lf,\n"
      "    \"dPrev\": %.2lf, \"dNext\": %.2lf, \"dFoot0\": %.2lf, \"dFoot1\": %.2lf\n"
      "  }", 
      pDest->lat, pDest->lon, pCurr.lat, pCurr.lon, pPrev.lat, pPrev.lon, pNext.lat, pNext.lon, 
      foot0Lat, foot0Lon, foot1Lat, foot1Lon, 
      dSeg0, dSeg1, dCurr,
      dPrev, dNext, dFoot0, dFoot1
   );
}

/*! launch routing with parameters */
void *routingLaunch (void) {
   double lastStepDuration;
   Pp pNext;
   initRouting ();
   route.competitorIndex = competitors.runIndex;
   fprintf (stdout, "In routingLaunch: competitor index: %d, name: %s\n", competitors.runIndex, competitors.t[competitors.runIndex].name);
   int ret = -1;
   const double start = monotonic (); 
   double wayPointStartTime = par.startTimeInHours;

   fprintf (stdout, "Before Routing, pDest ID: %d, Father: %d\n", par.pDest.id, par.pDest.father);
   //Launch routing
   if (wayPoints.n == 0) {
      ret = routing (&par.pOr, &par.pDest, -1, wayPointStartTime, par.tStep, &lastStepDuration);
   }
   else {
      for (int i = 0; i < wayPoints.n; i ++) {
         pNext.lat = wayPoints.t[i].lat;
         pNext.lon = wayPoints.t[i].lon;
         pNext.id = -2 - i;
         if (i == 0) {
            ret = routing (&par.pOr, &pNext, i, wayPointStartTime, par.tStep, &lastStepDuration);
         }
         else {
            ret = routing (&isocArray [(nIsoc-1) * MAX_SIZE_ISOC + 0], &pNext, i, wayPointStartTime, par.tStep, &lastStepDuration);
         }
         fprintf (stdout, "After Waypoint; %d, ret: %d, pNext ID: %d, Father: %d\n", i, ret, pNext.id, pNext.father);
         route.lastStepWpDuration [i] = lastStepDuration;
         if (ret > 0) {
            wayPointStartTime = par.startTimeInHours + (nIsoc * par.tStep) + lastStepDuration;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].lat = pNext.lat;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].lon = pNext.lon;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].father = pNext.father;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].amure = pNext.amure;
            isocArray [nIsoc * MAX_SIZE_ISOC + 0].id = pId++;
            isoDesc [nIsoc].size = 1;
            isoDesc [nIsoc].toIndexWp = (i < (wayPoints.n - 1)) ? i + 1 : -1;
            isoDesc [nIsoc].first = isoDesc [nIsoc].closest = 0;
            isoDesc [nIsoc].bestVmc = isoDesc [nIsoc].biggestOrthoVmc = 0.0;
            isoDesc [nIsoc].focalLat = wayPoints.t[i].lat;
            isoDesc [nIsoc].focalLon = wayPoints.t[i].lon;
            nIsoc += 1;
         }
         else break;
      }
      if (ret > 0) {
         ret = routing (&isocArray [(nIsoc-1) * MAX_SIZE_ISOC + 0], &par.pDest, -1, wayPointStartTime, par.tStep, &lastStepDuration);
      } 
   }
   fprintf (stdout, "After Routing, ret: %d, pDest ID: %d, Father: %d\n", ret, par.pDest.id, par.pDest.father);
   if (ret == -1) {
      g_atomic_int_set (&route.ret, ROUTING_ERROR); // -1
      return NULL;
   }
   route.lastStepDuration = lastStepDuration;
   route.nWayPoints = wayPoints.n;
   fprintf (stdout, "Number of wayPoints: %d\n", wayPoints.n);

   route.calculationTime = monotonic () - start; 
   route.destinationReached = (ret > 0 && par.pDest.father != 0);
   if (storeRoute (&route, &par.pOr, (route.destinationReached) ? &par.pDest : &lastClosest))
      // if (storeRoute (&route, &par.pOr, &lastClosest))
      g_atomic_int_set (&route.ret, ret); // route.ret is positionned just before ending. route.ret is shared between threads !
   else 
      g_atomic_int_set (&route.ret, ROUTING_ERROR);
   statRoute (&route);
   checkArrival (&route, (route.destinationReached) ? &par.pDest : &lastClosest);
   return NULL;
}

/*! choose best time to reach pDest in minimum time */
void *bestTimeDeparture (void) {
   double minDuration = DBL_MAX, maxDuration = 0;
   int localRet, nUnreachable = 0;

   chooseDeparture.bestTime = -1.0;
   chooseDeparture.count = 0;
   chooseDeparture.bestCount = -1;
   chooseDeparture.tStop = chooseDeparture.tEnd; // default value

   for (double t = chooseDeparture.tBegin; t < chooseDeparture.tEnd ; t += chooseDeparture.tInterval) {
      if (chooseDeparture.count > MAX_N_INTERVAL) {
         fprintf (stderr, "In bestTimeDeparture, chooseDeparture.count exceed limit %d\n", chooseDeparture.count);
         break;
      }
      par.startTimeInHours = t;
      routingLaunch ();
      localRet = g_atomic_int_get (&route.ret);
      if (localRet == ROUTING_STOPPED) {
         g_atomic_int_set (&chooseDeparture.ret, STOPPED);
         return NULL;
      }
      if (localRet > 0) {
         chooseDeparture.t [chooseDeparture.count] = route.duration;
         if (route.duration < minDuration) {
            minDuration = route.duration;
            chooseDeparture.bestTime = t;
            chooseDeparture.bestCount = chooseDeparture.count;
            fprintf (stdout, "Count: %d, time %.2lf, duration: %.2lf, min: %.2lf, bestTime: %.2lf\n", \
                 chooseDeparture.count, t, route.duration, minDuration, chooseDeparture.bestTime);
         }
         if (route.duration > maxDuration) {
            maxDuration = route.duration;
         }
      }
      else {
         chooseDeparture.tStop = t;
         fprintf (stdout, "Count: %d, time %.2lf, Unreachable\n", chooseDeparture.count, t);
         break;
         chooseDeparture.t [chooseDeparture.count] = NIL;
         if (nUnreachable > MAX_UNREACHABLE) {
            break;
         }
         nUnreachable += 1;
      }
      chooseDeparture.count += 1;
   }
   if (chooseDeparture.bestCount >= 0) {
      par.startTimeInHours = chooseDeparture.bestTime;
      fprintf (stdout, "Solution exist: best startTime: %.2lf\n", par.startTimeInHours);
      chooseDeparture.minDuration = minDuration;
      chooseDeparture.maxDuration = maxDuration;
      routingLaunch ();
      g_atomic_int_set (&chooseDeparture.ret, EXIST_SOLUTION);
   }  
   else {
      g_atomic_int_set (&chooseDeparture.ret, NO_SOLUTION);
      fprintf (stdout, "No solution\n");
   }
   return NULL;
}

/*! launch all competitors */
void *allCompetitors (void) {
   bool existSolution = false;
   int localRet;

   for (int i = 0; i < competitors.n; i += 1) // reset eta(s)
      competitors.t [i].strETA [0] = '\0';
   
   // we begin by the end in order to keep main (index 0) competitor as last for display route 
   for (int i = competitors.n - 1; i >= 0; i -= 1) {
      fprintf (stdout, "In allCompetitors: competitor: %d\n", i); 
      competitors.runIndex = i;
      par.pOr.lat = competitors.t [i].lat;
      par.pOr.lon = competitors.t [i].lon;
      routingLaunch ();
      saveRoute (&route);
      localRet = g_atomic_int_get (&route.ret);
      if (localRet == ROUTING_STOPPED) {
         g_atomic_int_set (&competitors.ret, STOPPED);
         return NULL;
      }
      if (localRet < 0) {
         fprintf (stderr, "In allCompetitors, No solution for competitor: %s with return: %d\n",\
                  competitors.t[i].name, g_atomic_int_get (&route.ret));
         g_strlcpy (competitors.t [i].strETA, "No Solution", MAX_SIZE_DATE); 
         competitors.t [i].duration = 0; // hours
         competitors.t [i].dist = 0;
         continue;
      }
      newDate (zone.dataDate [0], zone.dataTime [0] / 100.0 + par.startTimeInHours + route.duration, 
         competitors.t [i].strETA, MAX_SIZE_DATE); 
      competitors.t [i].duration = route.duration; // hours
      competitors.t [i].dist = orthoDist (competitors.t [i].lat, competitors.t [i].lon, par.pDest.lat, par.pDest.lon);
      existSolution = true;
   }
   g_atomic_int_set (&competitors.ret, (existSolution) ? EXIST_SOLUTION : NO_SOLUTION);
   return NULL;
}

/*! log one CSV line report. n is the number of competitors */
void logReport (int n) {
   time_t now = time (NULL);
   char arrivalDate [MAX_SIZE_DATE];
   char startDate [MAX_SIZE_LINE];
   char strLat0 [MAX_SIZE_NAME], strLon0 [MAX_SIZE_NAME], strLat1 [MAX_SIZE_NAME], strLon1 [MAX_SIZE_NAME];
   struct tm *timeInfos = gmtime (&now);
   FILE *f;
   if (par.logFileName [0] == '\0') return; // check that a log file name is defined
   struct stat st;
   const bool existFile = stat (par.logFileName, &st) == 0;

   if ((f = fopen (par.logFileName, "a")) == NULL) {
      fprintf (stderr, "In logReport, Error opening lo report file: %s\n", par.logFileName);
      return;
   }

   if (! existFile)
      fprintf (f, "logDate; isocStep; nOcc; Reach; nWP; pOr lat; pOr lon; pDest lat; pDest lon; Start; Arrival; toDest; HDG; polar; grib\n");
   
   newDate (zone.dataDate [0], zone.dataTime [0]/100.0 + par.startTimeInHours, 
      startDate, sizeof (startDate));

   newDate (zone.dataDate [0], zone.dataTime [0]/100.0 + par.startTimeInHours + route.duration, 
      arrivalDate, sizeof (arrivalDate));

   char *polarFileName = g_path_get_basename (par.polarFileName);
   char *gribFileName = g_path_get_basename (par.gribFileName);

   fprintf (f, "%04d-%02d-%02d %02d:%02d:%02d; %4.2lf; %4d; %c; %d; %s; %s; %s; %s; %s; %s; %.2lf; %3d°; %s; %s\n",
      timeInfos->tm_year+1900, timeInfos->tm_mon+1, timeInfos->tm_mday,
      timeInfos->tm_hour, timeInfos->tm_min, timeInfos->tm_sec,
      par.tStep, n, (route.destinationReached) ? 'R' : 'U',
      wayPoints.n,
      latToStr (par.pOr.lat, par.dispDms, strLat0, sizeof (strLat0)), 
      lonToStr (par.pOr.lon, par.dispDms, strLon0, sizeof (strLon0)),
      latToStr (par.pDest.lat, par.dispDms, strLat1, sizeof (strLat1)), 
      lonToStr (par.pDest.lon, par.dispDms, strLon1, sizeof (strLon1)),
      startDate,
      arrivalDate, 
      (route.destinationReached) ? 0 : orthoDist (lastClosest.lat, lastClosest.lon, par.pDest.lat, par.pDest.lon),
      (int) (route.t[0].oCap + 360) % 360,
      polarFileName,
      gribFileName
   );
   free (polarFileName);
   free (gribFileName);
   fclose (f);
}

/*! export route with GPX format */
bool exportRouteToGpx (const SailRoute *route, const char *fileName) {
   FILE *f;
   char strTime [MAX_SIZE_DATE]; 
   if (! route || ! fileName || fileName [0] == '\0') return false;

   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "In exportRouteToGpx: Impossible to write open %s:", fileName);
      return true;
   }
   // GPX header
   fprintf (f, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
   fprintf (f, "<gpx version=\"1.1\" creator=\"%s\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n", PROG_NAME);
   fprintf (f, "  <rte>\n");
   fprintf (f, "    <name>Maritime Route</name>\n");

   // Parcours des points et écriture des <trkpt>
   for (int i = 0; i < route->n; i++) {
      SailPoint *p = &route->t[i];
      
      newDate (route->dataDate, route->dataTime/100.0 + par.startTimeInHours + p->time, strTime, sizeof (strTime));

      fprintf (f, "    <rtept lat=\"%.6f\" lon=\"%.6f\">\n", p->lat, p->lon);
      fprintf (f, "      <name>%d</name>\n", i);
      fprintf (f, "      <time>%s</time>\n", strTime);
      fprintf (f, "      <course>%.2f</course>\n", fmod (p->oCap + 360.0, 360.0));   //  degrees
      fprintf (f, "      <speed>%.2f</speed>\n", p->sog);      // speed (knots)
      fprintf (f, "    </rtept>\n");
   }
   fprintf (f, "    <rtept lat=\"%.6f\" lon=\"%.6f\">\n", par.pDest.lat, par.pDest.lon);
   fprintf (f, "      <name>Destination</name>\n");
   fprintf (f, "    </rtept>\n");
   // Close GPX
   fprintf (f, "  </rte>\n");
   fprintf (f, "</gpx>\n");

   fclose (f);
   printf ("GPX file:%s generated\n", fileName);
   return true;
}

/*! calculate next pos (lat, lon) at TWA and provides wind current and route parameters */
static bool nextPos(double *lat, double *lon, double twa, double t, double dt,
   double *hdg, double *twd, double *tws, double *gust, double *w, double *uCurr, double *vCurr, int *sailChoice) {

   static const double epsilon = 0.01;
   int bidon;   
   double efficiency, sog, dLat, dLon, u, v, currTwd, currTws;
   double invDenominator = 1.0 / MAX (epsilon, cos (DEG_TO_RAD * *lat));

   *sailChoice = 0;
   if (!isInZone (*lat, *lon, &zone)) return false;
   findWindGrib (*lat, *lon, t, &u, &v, gust, w, twd, tws);
   if (par.withCurrent) findCurrentGrib (*lat, *lon, t - tDeltaCurrent, uCurr, vCurr, &currTwd, &currTws);

   if (par.dayEfficiency == par.nightEfficiency) efficiency = par.dayEfficiency;
   else efficiency = isDay(t, zone.dataDate[0], zone.dataTime[0], *lat, *lon) ? par.dayEfficiency : par.nightEfficiency;
  
   sog = efficiency * findPolar(twa, *tws * par.xWind, &polMat, &sailPolMat, sailChoice);
   if (par.withWaves && *w > 0.0 ) {
      const double waveCorrection = findPolar(twa, *w, &wavePolMat, NULL, &bidon);
      if (waveCorrection > 0.0) sog *= waveCorrection * 0.01;
   }
   *hdg = *twd - twa;
   if (*hdg < 0) *hdg += 360.0;
   dLat = sog * dt * cos (DEG_TO_RAD * *hdg);                   // nautical miles in N S direction
   dLon = sog * dt * sin (DEG_TO_RAD * *hdg) * invDenominator;  // nautical miles in E W direction

   if (par.withCurrent) {                                      // correction for current
      dLat += MS_TO_KN * *vCurr * dt;
      dLon += MS_TO_KN * *uCurr * dt * invDenominator;
   }
   *lat += dLat / 60.0;
   *lon += dLon / 60.0;
   return true;
}

/*! Calculate TWA route and produce Json output */ 
void routeAtTwa (double lat0, double lon0, double twa, time_t epochStart, double t, double dt, int max, char *out, size_t maxLen) {
  if (max < 1) {
    snprintf (out, maxLen, "{ \"_Error\": \"nStep should be >= 2\"}\n"); 
    return;
  }  
  if (!isInZone (lat0, lon0, &zone)) {
    snprintf (out, maxLen, "{ \"_Error\": \"Start position not in Grib Zone\" }\n"); 
    return;
  }
  const bool waves = isPresentGrib(&zone,"swh") && par.withWaves;
  const long duration = (par.tStep * 3600) * max;
  char strSail [MAX_SIZE_NAME] = "";
  double hdg, u, v, g, w, uCurr = 0.0, vCurr = 0.0, twd, tws, currTwd, currTws;
  double dist = 0.0, totDist = 0.0;
  double formerLat = lat0, formerLon = lon0;
  double lat = lat0, lon = lon0;
  int sail, formerSail = 0, nSailChange = 0;
  char str [MAX_SIZE_LINE];
  char *gribBaseName = g_path_get_basename (par.gribFileName);
  char *gribCurrentBaseName = g_path_get_basename (par.currentGribFileName);
  char *polarBaseName = g_path_get_basename (par.polarFileName);
  char *wavePolarBaseName = g_path_get_basename (par.wavePolFileName);

  tDeltaCurrent = zoneTimeDiff (&currentZone, &zone); // global variable/
  findWindGrib (lat0, lon0, t, &u, &v, &g, &w, &twd, &tws);
  if (par.withCurrent) findCurrentGrib (lat0, lon0, t - tDeltaCurrent, &uCurr, &vCurr, &currTwd, &currTws);
  hdg = twd - twa;
  if (hdg < 0) hdg += 360.0;

  snprintf(out, maxLen, "{\n"
    "  \"twa\": %.2lf,\n"    
    "  \"nSteps\": %d,\n"
    "  \"timeStep\": %.0lf,\n"
    "  \"epochStart\": %ld,\n"    
    "  \"duration\": %ld,\n"
    "  \"bottomLat\": %.2lf, \"leftLon\": %.2lf, \"topLat\": %.2lf, \"rightLon\": %.2lf,\n"
    "  \"polar\": \"%s\",\n"
    "  \"wavePolar\": \"%s\",\n"
    "  \"grib\": \"%s\",\n"
    "  \"currentGrib\": \"%s\",\n"
    "  \"comment\": \"[lat, lon, t, dist, hdg, twd, tws, g, w, uCurr, vCurr, sail]\",\n"
    "  \"array\": [\n    [%.4lf, %.4lf,    0, 0.0000, %.0lf, %.4lf, %.4lf, %.4lf, %.4lf, %.4lf, %.4lf, \"--\"],\n", 
    twa, max, par.tStep * 3600, epochStart, duration, zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight,
    polarBaseName, 
    (waves) ? wavePolarBaseName : "",  
    gribBaseName, 
    (par.withCurrent) ? gribCurrentBaseName : "",
    lat0, lon0, hdg, twd, tws, g, w, uCurr, vCurr);

  free(polarBaseName);
  free(wavePolarBaseName);
  free (gribBaseName);
  free (gribCurrentBaseName);

  for (int i = 0; i < max; i += 1) {
    if (! nextPos (&lat, &lon, twa, t, dt, &hdg, &twd, &tws, &g, &w, &uCurr, &vCurr, &sail)) {
      snprintf (out, maxLen, "{ \"_Error\": \"Position not in Grib Zone\" }\n"); 
      return;
    }
    fSailName (sail, strSail, sizeof strSail);
    if ((sail != formerSail) && (i > 0)) nSailChange += 1;
    dist = orthoDist(formerLat, formerLon, lat, lon);
    totDist += dist;

    snprintf (str, sizeof str , "    [%.4lf, %.4lf, %.0lf, %.4lf, %.0lf, %.4lf, %.4lf, %.4lf, %.4lf, %.4lf, %.4lf, \"%s\"]%s\n", 
       lat, lon, (i+1) * dt * 3600 , dist, hdg, twd, tws, g, w, uCurr, vCurr, strSail, (i < max - 1) ? "," : ""
    );
    g_strlcat (out, str, maxLen);
    formerLat = lat;
    formerLon = lon;
    formerSail = sail;
    t += dt;
  }
  snprintf (str, sizeof str, "  ],\n  \"totDist\": %.2lf,\n  \"nSailChange\": %d\n}\n", totDist, nSailChange);
  g_strlcat (out, str, maxLen);
}
