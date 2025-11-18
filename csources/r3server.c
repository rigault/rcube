/*! RCube is a routing software for sailing. 
 * It computes the optimal route from a starting point (pOr) to a destination point (pDest) 
 * at a given START_TIME, using GRIB weather data and a boat polar diagram.
 * to launch : r3server <port number> [parameter file]
 */
  
#include <stdbool.h>
#include <dirent.h>   // DIR, opendir, readdir, closedir
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <limits.h>
#include <locale.h>
#include "glibwrapper.h"
#include "r3types.h"
#include "r3util.h"
#include "engine.h"
#include "grib.h"
#include "readgriball.h"
#include "polar.h"
#include "inline.h"
#include <sys/stat.h>
#include <fcntl.h>

#define SYNOPSYS               "<port> [<parameter file>]"
#define MAX_SIZE_REQUEST       2048        // Max size from request client
#define MAX_SIZE_RESOURCE_NAME 256         // Max size polar or grib name
#define PATTERN                "GFS"
#define MAX_SIZE_FEED_BACK     1024
#define ADMIN_LEVEL            10          // level of authorization for administrator
#define BIG_BUFFER_SIZE        (30*MILLION)
#define MAX_SIZE_MESS          100000
#define GPX_ROUTE_FILE_NAME    "gpxroute.tmp"

char *bigBuffer = NULL;

// global filter for REQ_DIR request
const char *filter[] = {".csv", ".pol", ".grb", ".grb2", ".log", ".txt", ".par", ".yaml", ".json", NULL}; 

enum {REQ_KILL = -1793, REQ_TEST = 0, REQ_ROUTING = 1, REQ_COORD = 2, REQ_FORBID = 3, REQ_POLAR = 4, 
      REQ_GRIB = 5, REQ_DIR = 6, REQ_PAR_RAW = 7, REQ_PAR_JSON = 8, 
      REQ_INIT = 9, REQ_FEEDBACK = 10, REQ_DUMP_FILE = 11, REQ_NEAREST_PORT = 12, 
      REQ_MARKS = 13, REQ_CHECK_GRIB = 14, REQ_GPX_ROUTE = 15}; // type of request

// level of authorization
//const int typeLevel [16] = {0, 1, 0, 0, 0, 0, 0, 0, 0, ADMIN_LEVEL, 0, 0, 0, 0, 0, 0};        
const int typeLevel [16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}; // everybody access to all       
 
char parameterFileName [MAX_SIZE_FILE_NAME];

/*! Client Request description */
typedef struct {
   int level;                                // level of authorization
   int type;                                 // type of request
   int cogStep;                              // step of cog in degrees
   int rangeCog;                             // range of cog from x - RANGE_GOG, x + RAGE_COG+1
   int jFactor;                              // factor for target point distance used in sectorOptimize
   int kFactor;                              // factor for target point distance used in sectorOptimize
   int nSectors;                             // number of sector for optimization by sector
   int penalty0;                             // penalty in seconds for tack
   int penalty1;                             // penalty in seconds fot Gybe
   int penalty2;                             // penalty in seconds for sail change
   int initialAmure;                         // initial Amure of the routing calculation 0 = starboard = tribord, or 1 = port = babord
   int timeStep;                             // isoc time step in seconds
   time_t epochStart;                        // epoch time to start routing
   bool isoc;                                // true if isochrones requested
   bool isoDesc;                             // true if isochrone decriptor requested
   bool sortByName;                          // true if directory should be sorted by name, false if sorted by modif date
   bool forbid;                              // true if forbid zone (polygons or Earth) are considered
   bool withWaves;                           // true if waves specified in wavePolName file are considered
   bool withCurrent;                         // true if current specified in currentGribName is considered
   double staminaVR;                         // Init stamina
   double motorSpeed;                        // motor speed if used
   double threshold;                         // threshold for motor use
   double nightEfficiency;                   // efficiency of team at night
   double dayEfficiency;                     // efficiency of team at day
   double xWind;                             // multiply factor for wind
   double maxWind;                           // max Wind supported
   double constWindTws;                      // if not equal 0, constant wind used in place of grib file
   double constWindTwd;                      // the direction of constant wind if used
   double constWave;                         // constant wave height if used
   double constCurrentS;                     // if not equal 0, contant current speed Knots
   double constCurrentD;                     // the direction of constant current if used
   int nBoats;                               // number of boats
   struct {
      char name [MAX_SIZE_NAME];             // name of the boat
      double lat;                            // latitude
      double lon;                            // longitude
   } boats [MAX_N_COMPETITORS];              // boats
   int nWp;                                  // number of waypoints
   struct {
      double lat;                            // latitude
      double lon;                            // longitude
   } wp [MAX_N_WAY_POINT];                   // way points
   char model      [MAX_SIZE_RESOURCE_NAME]; // grib model
   char dirName    [MAX_SIZE_RESOURCE_NAME]; // remote directory name
   char wavePolName[MAX_SIZE_RESOURCE_NAME]; // polar file name
   char polarName  [MAX_SIZE_RESOURCE_NAME]; // polar file name
   char gribName   [MAX_SIZE_RESOURCE_NAME]; // grib file name
   char fileName   [MAX_SIZE_RESOURCE_NAME]; // grib file name
   char currentGribName [MAX_SIZE_RESOURCE_NAME];  // grib file name
   char feedback   [MAX_SIZE_FEED_BACK];      // for feed back info
} ClientRequest; 

ClientRequest clientReq;

/*! Structure to store file information. */
typedef struct {
   char *name;
   off_t size;
   time_t mtime;
} FileInfo;

/*! Generate Json array for polygon */
static void polygonToJson (MyPolygon *po, char *str, size_t maxLen) {
   const int n = po->n;
   char temp [128];
   double lat, lon;

   strlcpy (str, "  [", maxLen);
   printf ("Number of points in polygon: %d\n", n);
   for (int k = 0; k < n; k++) {
      lat = (po->points [k].lat);
      lon = (po->points [k].lon);
      snprintf (temp, sizeof temp, "[%.4lf, %.4lf]%s", lat, lon, (k < n-1) ? ", " : "");
      strlcat (str,temp, maxLen); 
   }
   strlcat (str, "]", maxLen);
}

/*!
 * update Grib file if required 
 */
static bool updateWindGrib (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   char strGrib [MAX_SIZE_FILE_NAME];
   if (clientReq->gribName [0] == '\0') return false;
   // change grib if requested MAY TAKE TIME !!!!
   buildRootName (clientReq->gribName, strGrib, sizeof strGrib);
   printf ("grib found: %s\n", strGrib);
   if (strncmp (par.gribFileName, strGrib, strlen (strGrib)) != 0) {
      if (readGribAll (strGrib, &zone, WIND)) {
         g_strlcpy (par.gribFileName, strGrib, sizeof par.gribFileName);
         printf ("Grib loaded   : %s\n", strGrib);
      }
      else {
         snprintf (checkMessage, maxLen, "3: Error reading Grib: %s", clientReq->gribName);
         printf ("In updateWindGrib: Error reading Grib: %s\n", clientReq->gribName);
         return false;
      }
   }  
   return true;
}

/*!
 * update current file if required 
 */
static bool updateCurrentGrib (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   char strGrib [MAX_SIZE_FILE_NAME];
   if (clientReq->currentGribName [0] == '\0') return false;

   buildRootName (clientReq->currentGribName, strGrib, sizeof strGrib);
   printf ("current grib found: %s\n", strGrib);
   if (strncmp (par.currentGribFileName, strGrib, strlen (strGrib)) != 0) {
      printf ("current readGrib: %s\n", strGrib);
      if (readGribAll (strGrib, &currentZone, CURRENT)) {
         g_strlcpy (par.currentGribFileName, strGrib, sizeof par.currentGribFileName);
         printf ("Current Grib loaded   : %s\n", strGrib);
      }
      else {
         snprintf (checkMessage, maxLen, "4: Error reading Current Grib: %s", clientReq->currentGribName);
         return false;
      }
   }
   return true;
}

/*!
 * \brief Approximate great-circle segment length in nautical miles.
 *
 * This function estimates the distance between two geographic points
 * (lat0, lon0) and (lat1, lon1) in nautical miles (NM) using a local
 * equirectangular projection. For routing validation this is accurate
 * enough and cheaper than a full haversine.
 *
 * Latitudes and longitudes are expressed in decimal degrees.
 *
 * \return Approximate distance in nautical miles.
 */
static double approxSegmentLengthNm(double lat0, double lon0, double lat1, double lon1) {
   const double dlatDeg = lat1 - lat0;
   const double dlonDeg = lon1 - lon0;
   const double latMidRad = ((lat0 + lat1) * 0.5) * DEG_TO_RAD;
   const double dlatNm = dlatDeg * 60.0;
   const double dlonNm = dlonDeg * 60.0 * cos(latMidRad);

   return hypot(dlatNm, dlonNm);
}
/*!
 * \brief Check if the straight segment between (lat0, lon0) and (lat1, lon1)
 *        stays entirely over allowed sea areas.
 *
 * The function samples intermediate points along the geodesic segment
 * (approximated as a straight interpolation in lat/lon space) and calls
 * isSea(lat, lon) on each sample.
 *
 * Rationale:
 * - Each endpoint is already supposed to be valid water, but in practice
 *   a straight line between two valid nodes may cross land (e.g. cutting
 *   across a headland or an island).
 * - We discretize the segment into small steps in nautical miles (NM).
 *   Finer step -> safer, but more calls to isSea().
 *
 * Assumptions:
 * - isSea(lat, lon) is fast (array lookup).
 * - Lat/lon variations between the two points are small enough that
 *   linear interpolation in degrees is acceptable for validation.
 *
 * \param lat0 start latitude  in decimal degrees
 * \param lon0 start longitude in decimal degrees
 * \param lat1 end latitude    in decimal degrees
 * \param lon1 end longitude   in decimal degrees
 *
 * \return true  if all sampled points are at sea (and not in forbidden areas)
 * \return false if any sampled point is on land / forbidden
 */
static bool segmentOverSea(double lat0, double lon0, double lat1, double lon1) {
   // 1. Defensive check on endpoints */
   const double epsilon = 1e-12;
   if (!isSea (tIsSea, lat0, lon0)) return false;
   if (!isSea (tIsSea, lat1, lon1)) return false;

   // 2. Degenerate segment (same point or extremely close) */
   const double dlat = lat1 - lat0;
   const double dlon = lon1 - lon0;
   if (fabs(dlat) < epsilon  && fabs(dlon) < epsilon) return true; // Same point, already tested */

   // 3. Estimate segment length in NM */
   double lengthNm = approxSegmentLengthNm(lat0, lon0, lat1, lon1);

   // 4. Choose max spacing between checks.
   const double stepNm = 1.0;

   // 5. Compute how many intervals we need.
   int steps = (int)ceil(lengthNm / stepNm);
   steps = CLAMP (steps, 1, 2000); // hard cap to avoid crazy loops
   
   //Sample intermediate points.  We skip i=0 and i=steps because those are endpoints, already checked.
   for (int i = 1; i < steps; i++) {
      const double t = (double)i / (double)steps;
      const double lat = lat0 + t * (lat1 - lat0);   // Linear interpolation in lat/lon space.
      const double lon = lon0 + t * (lon1 - lon0);
      if (!isSea(tIsSea, lat, lon)) return false;
   }
   return true;
}

/*!
 * \brief Check that every consecutive segment of a route is fully over sea.
 *
 * The route is assumed to be an ordered list of waypoints. For each pair
 * of consecutive waypoints, segmentOverSea() is called. If any segment
 * crosses land or a forbidden area, the function returns false.
 *
 * \param route pointer to SailRoute.
 * \return -1 if all segments are OK (sea only)
 * \return i  index of the first invalid segment [i -> i+1]
 */
static int checkRoute(const SailRoute *route) {
   if (!route || route->n < 3) return -1;
   for (int i = 0; i < route->n - 2; i++) {
      if (!segmentOverSea (route->t[i].lat, route->t[i].lon, route->t[i+1].lat, route->t[i+1].lon)) return i;
   }
   return -1;
}

/*! Exclusion zone is an array of polygons */
static void forbidToJson (char *res, size_t maxLen) {
   char str [1000];
   strlcpy (res, "[\n", maxLen);
   printf ("Number of polygon: %d\n", par.nForbidZone);
   
   for (int i = 0; i < par.nForbidZone; i++) {
      polygonToJson (&forbidZones [i], str, sizeof str);
      strlcat (res, str, maxLen);
      if (i < par.nForbidZone -1) strlcat (res, ",", maxLen);
      strlcat (res, "\n", maxLen);
   }

   strlcat (res, "]\n", maxLen);
}

/*! generate json description of isochrones. Concatenate it to res */
static char *isochronesToStrCatJson (char *res, size_t maxLen) {
   Pp pt;
   char str [10000];
   int index;
   char *savePt = res + strlen (res);
   g_strlcat (res, ",\n\"_isoc\": \n[\n", maxLen);
   Pp *newIsoc = NULL; // array of points
   if ((newIsoc = malloc (MAX_SIZE_ISOC * sizeof(Pp))) == NULL) {
      fprintf (stderr, "In isochronesToStrJson: error in memory newIsoc allocation\n");
      return NULL;
   }
   
   for (int i = 0; i < nIsoc; i += 1) {
      g_strlcat (res, "   [\n", maxLen);
      index = (isoDesc [i].size <= 1) ?  0 : isoDesc [i].first;
      for (int j = 0; j < isoDesc [i].size; j++) {
         newIsoc [j] = isocArray [i * MAX_SIZE_ISOC + index];
         index += 1;
         if (index == isoDesc [i].size) index = 0;
      }
      const int max = isoDesc [i].size;
      for (int k = 0; k < max; k++) {
         pt = newIsoc [k];
         snprintf (str, sizeof str, "      [%.6lf, %.6lf, %d, %d, %d]%s\n", 
            pt.lat, pt.lon, pt.id, pt.father, k, (k < max - 1) ? ","  : ""); 
         g_strlcat (res, str, maxLen);
      }
      snprintf (str, sizeof str,  "   ]%s\n", (i < nIsoc -1) ? "," : ""); // no comma for last value 
      g_strlcat (res, str, maxLen);
   }
   if (strlen (res) >= (maxLen - sizeof str - 2)) {// too big
      res = savePt;
      *res = '\0';
      strlcat (res, ",\n\"_Warning_1\": \"No isochrone sent because too big!\"\n", maxLen); // no isochrone if too big !
   }
   else g_strlcat (res, "]\n", maxLen); 
   free (newIsoc);
   return res;
}

/*! generate json description of isochrones decriptor. Conatenate te description to res */
static char *isoDescToStrCatJson (char *res, size_t maxLen) {
   char str [10000];
   char *savePt = res + strlen (res);
   g_strlcat (res, ",\n\"_isodesc\": \n[\n", maxLen);

   for (int i = 0; i < nIsoc; i++) {
      //double distance = isoDesc [i].distance;
      //if (distance >= DBL_MAX) distance = -1;
      snprintf (str, sizeof str, "   [%d, %d, %d, %d, %d, %.2lf, %.2lf, %.6lf, %.6lf]%s\n",
         i, isoDesc [i].toIndexWp, isoDesc[i].size, isoDesc[i].first, isoDesc[i].closest, 
         isoDesc[i].bestVmc, isoDesc[i].biggestOrthoVmc, 
         isoDesc [i].focalLat, 
         isoDesc [i].focalLon,
         (i < nIsoc - 1) ? "," : "");
      g_strlcat (res, str, maxLen);
   }
   if (strlen (res) >= (maxLen - sizeof str - 2)) {// too big
      res = savePt;
      *res = '\0';
      strlcat (res, ",\n\"_Warning_2\": \"No isoDesc sent because too big!\"\n", maxLen); // no isochrone if too big !
   }
   else g_strlcat (res, "]\n", maxLen); 
   return res;
}

/*! generate json description of track boats */
static char  *routeToJson (SailRoute *route, bool isoc, bool isoDesc, char *res, size_t maxLen) {
   char str [MAX_SIZE_MESS] = "";
   char strSail [MAX_SIZE_NAME] = "";
   double twa = 0.0, hdg = 0.0, twd = 0.0;
   int nSeg = -1; // for checkRoute
   if (route->n <= 0) return NULL;

   int iComp = (route->competitorIndex < 0) ? 0 : route->competitorIndex;
   char *gribBaseName = g_path_get_basename (par.gribFileName);
   char *gribCurrentBaseName = g_path_get_basename (par.currentGribFileName);
   char *wavePolarBaseName = g_path_get_basename (par.wavePolFileName);

   snprintf (res, maxLen, 
      "{\n" 
      "\"%s\": {\n\"duration\": %d,\n"
      "\"totDist\": %.2lf,\n"
      "\"routingRet\": %d,\n"
      "\"isocTimeStep\": %.2lf,\n"
      "\"calculationTime\": %.4lf,\n"
      "\"destinationReached\": %s,\n"
      "\"lastPointInfo\": %s,\n"
      "\"lastStepDuration\": [",
      competitors.t[iComp].name, 
      (int) (route->duration * 3600), 
      route->totDist,
      route->ret,
      route->isocTimeStep * 3600,
      route->calculationTime,
      (route->destinationReached) ? "true" : "false",
      route->lastPointInfo
   );

   for (int i = 0; i < route->nWayPoints; i += 1) {
      snprintf (str, sizeof str, "%.4lf, ", route->lastStepWpDuration [i] * 3600.0);
      g_strlcat (res, str, maxLen);
   }
   snprintf (str, sizeof str,
      "%.4f],\n"
      "\"motorDist\": %.2lf, \"starboardDist\": %.2lf, \"portDist\": %.2lf,\n"
      "\"nSailChange\": %d, \"nAmureChange\": %d,\n" 
      "\"bottomLat\": %.2lf, \"leftLon\": %.2lf, \"topLat\": %.2lf, \"rightLon\": %.2lf,\n"
      "\"polar\": \"%s\",\n"
      "\"wavePolar\": \"%s\",\n"
      "\"grib\": \"%s\",\n"
      "\"currentGrib\": \"%s\",\n"
      "\"track\": [\n",
      route->lastStepDuration * 3600.0,
      route->motorDist, route->tribordDist, route->babordDist,
      route->nSailChange, route->nAmureChange,
      zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight,
      route->polarFileName, wavePolarBaseName, gribBaseName, gribCurrentBaseName
   );
   g_strlcat (res, str, maxLen);
   free (wavePolarBaseName);
   free (gribBaseName);
   free (gribCurrentBaseName);

   for (int i = 0; i < route->n; i++) {
      if (route->t[i].sog > LIMIT_SOG)
         fprintf (stderr, "In routeToJson sog > LIMIT_SOG, lat = %.6lf, lon = %.6lf, sog = %.6lf, od = %.6lf; ld = %.6lf\n",
            route->t[i].lat, route->t[i].lon, route->t[i].sog, route->t[i].od, route->t[i].ld);

      twa = fTwa (route->t[i].lCap, route->t[i].twd);
      hdg = route->t [i].oCap;
      if (hdg < 0) hdg += 360;
      twd = route->t[i].twd;
      if (twd < 0) twd += 360;
      fSailName (route->t[i].sail, strSail, sizeof strSail);
      if ((route->t[i].toIndexWp < -1) || (route->t[i].toIndexWp >= route->nWayPoints)) {
         printf ("In routeToJson, Error: toIndexWp outside range; %d\n", route->t[i].toIndexWp);
      } 
      snprintf (str, sizeof str, 
         "   [%d, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, \"%s\", %s, %d, %d]%s\n", 
         route->t[i].toIndexWp,
         route->t[i].lat, route->t[i].lon,
         route->t[i].time * 3600.0,
         route->t[i].od, route->t[i].sog, 
         twd ,route->t[i].tws, 
         hdg, twa,
         route->t[i].g, route->t[i].w ,route->t[i].stamina, 
         strSail, (route->t[i].motor) ? "true" : "false",
         route->t[i].id, route->t[i].father,
         //(route->destinationReached || i < route-> n - 1) ? "," :"");
         (i < route-> n - 1) ? "," :""
      );
      g_strlcat (res, str, maxLen);
   }

   g_strlcat (res, "]\n}\n", maxLen);

   nSeg = checkRoute (route);
   if (nSeg != -1) { // route is not OK
      fprintf (stderr, "In routeToSrJson, checkRoute warns bad segment (no sea): %d\n", nSeg);
      snprintf (str, sizeof str, ",\n\"_Warning_0\": \"route over sea or forbidden zone on segment: %d (%.2lf, %.2lf) to (%.2lf, %.2lf)\"\n", 
         nSeg, route->t[nSeg].lat, route->t[nSeg].lon, route->t[nSeg + 1].lat, route->t[nSeg + 1].lon);
      g_strlcat (res, str, maxLen);
   }
   if (isoDesc) {
      isoDescToStrCatJson (res, maxLen - 2);    // concat in res des descriptors
   }
   printf ("Before isochronesToStrCatJson in routeToJson\n");
   if (isoc) {
      isochronesToStrCatJson (res, maxLen - 2); // concat in res the isochrones
   }
   g_strlcat (res, "}\n", maxLen);
   return res;
}

/*! 
 * information associated to coord (lat lon)
 * isSea, isSeaTolerant, grib wind and current
 */
static void infoCoordToJson (double lat, double lon, ClientRequest *clientReq, char *res, size_t maxLen) {
   char str [MAX_SIZE_MESS] = "";
   const bool sea = isSea (tIsSea, lat, lon);
   const bool seaTolerant = isSeaTolerant (tIsSea, lat, lon);
   const bool wind = isInZone (lat, lon, &zone);
   const bool current = isInZone (lat, lon, &currentZone);
   time_t epochGribStart = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]);
   long tMax = zone.timeStamp [zone.nTimeStamp -1];
   double u, v, g, w, twd, tws;
   if (tMax <= 0) {
      fprintf (stderr, "In infoCoordToJson: tMax should be strictly positive\n");
      snprintf (str, sizeof str, "{\"_Error\": \"tMax should ne strictly positive\"\n"); 
      return;
   }
   updateWindGrib (clientReq, str, sizeof str);
   if (str [0] != '\0')  {
      snprintf (res, maxLen, "{\"_Error\": \"%s\"}\n", str);
      return;
   }

   char *pFileName = strrchr (par.gribFileName, '/');
   if (pFileName != NULL) pFileName += 1;
   snprintf (res, maxLen, "{\n"
      "  \"isSea\": %s,\n  \"isSeaTolerant\": %s,\n  \"inWind\": %s,\n  \"inCurrent\": %s,\n"
      "  \"epochGribStart\": %ld,\n  \"grib\": \"%s\",\n  \"meteoArray\":\n  [\n", 
      sea ? "true" : "false", seaTolerant ? "true": "false", wind ? "true": "false", current ? "true": "false", 
      epochGribStart, pFileName ? pFileName : ""); 
    
   for (int i = 0; i < tMax; i += 1) {
      findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
      snprintf (str, sizeof str, "    [%.4lf, %.4lf, %.4lf, %.4lf]%s\n", u, v, g, w, (i < tMax - 1) ? "," : ""); 
      strlcat (res, str, maxLen);
   }
   strlcat (res, "  ]\n}\n", maxLen);
}

/*!
 * @brief Retrieves the real client IP address from HTTP headers.
 * 
 * @param headers A string containing the HTTP headers.
 * @param clientAddress Buffer to store the client's IP address.
 * @param bufferSize Size of the clientAddress buffer.
 * @return true if the IP address was successfully found and stored, false if not found or error.
 */
static bool getRealIPAddress (const char* headers, char* clientAddress, size_t bufferSize) {
   const char* headerName = "X-Real-IP: ";
   const char* headerStart = strstr (headers, headerName);

   if (headerStart) {
      headerStart += strlen (headerName);
      while (*headerStart == ' ') headerStart++;  // Skip any whitepaces
        
      const char* headerEnd = strstr(headerStart, "\r\n");
      if (headerEnd) {
         size_t ipLength = headerEnd - headerStart;
         if (ipLength < bufferSize) {
            g_strlcpy (clientAddress, headerStart, ipLength + 1);
            g_strstrip (clientAddress); 
            return true;
         } else {
            fprintf (stderr, "In getRealIPAddress: IP address length exceeds buffer size.");
            return false;
         }
      }
   }
   clientAddress [0] = '\0';
   return false;
}

/*! extract user agent */
static char* extractUserAgent (const char* saveBuffer) {
   const char* headerName = "User-Agent: ";
   const char* userAgentStart = strstr (saveBuffer, headerName);
   if (userAgentStart) {
      userAgentStart += strlen (headerName);
      const char* userAgentEnd = strstr  (userAgentStart, "\r\n");
      if (userAgentEnd) {
         return strndup (userAgentStart, userAgentEnd - userAgentStart);
      }
   }
   return NULL;  // No user agent found
}

/*! extract level */
static int extractLevel (const char* buffer) {
   if (!par.authent) return ADMIN_LEVEL; // No autentication, get highest level
   const char* headerName = "X-User-Level:";
   const char* levelStart = strstr (buffer, headerName);
   if (levelStart) return atoi (levelStart + strlen (headerName));
   return 0;  // No X_User-Level found
}

/*! compare level of authorization with request */
static bool allowedLevel (ClientRequest *clientReq) {
   if ((clientReq->type == REQ_KILL) && (clientReq->level == ADMIN_LEVEL)) {
      return true;
   }
   if ((clientReq->type == 1) && (strstr (clientReq->model, "GFS") != NULL)) {
      printf ("GFS allowed whatever the level for  type 1 request\n");    
      return true; // GFS allowed to anyone
   }
   if ((clientReq->level >= 0) && (clientReq->level <= ADMIN_LEVEL)) {
      printf ("Allowed associated to typeLevel: %d\n", typeLevel [clientReq->type]);    
      return clientReq->level >= typeLevel [clientReq->type]; 
   }
   return false; // Level out of range
} 

/*! Comparator to sort by name (ascending order) */
static int compareByName (const void *a, const void *b) {
    const FileInfo *fa = (const FileInfo*) a, *fb = (const FileInfo*) b;
    return strcmp (fa->name, fb->name);
}

/*! Comparator to sort by modification date (most recent first) */
static int compareByMtime (const void *a, const void *b) {
    const FileInfo *fa = (const FileInfo*)a, *fb = (const FileInfo*)b;
    if (fa->mtime > fb->mtime) return -1;
    if (fa->mtime < fb->mtime) return  1;
    return strcmp(fa->name, fb->name);    // tie-breaker stable-ish
}

/*! Checks if the filename matches one of the suffixes in the filter */
static bool matchFilter (const char *filename, const char **filter) {
   if (filter == NULL) return true;
   for (int i = 0; filter[i] != NULL; i++) {
      if (g_str_has_suffix (filename, filter[i])) return true;
   }
   return false;
}

/*! 
 * return neareAst port to lat, lon
 */
static char *nearestPortToStrJson (double lat, double lon, char *out, size_t maxLen) {
   char selectedPort [MAX_SIZE_NAME];
   int idPort = nearestPort (lat, lon, par.tidesFileName, selectedPort, sizeof selectedPort);
   if (idPort != 0) {
      snprintf (out, maxLen, "{\"nearestPort\": \"%s\", \"idPort\": %d}\n", selectedPort, idPort);
   }
   else snprintf (out, maxLen, "{\"error\": \"Tide File %s not found\"}\n", par.tidesFileName);
   return out;
}

/*! Escape JSON string ; malloc(), to free() */
static char *jsonEscapeStrdup(const char *s) {
   if (!s) return strdup("");
   const size_t len = strlen(s);
   // worst case: every byte become \u00XX (6 chars) */
   const size_t cap = len * 6 + 1;
   char *out = (char*)malloc(cap);
   if (!out) return NULL;
   char *p = out;

   for (size_t i = 0; i < len; i++) {
      unsigned char c = (unsigned char)s[i];
      switch (c) {
         case '\"': *p++='\\'; *p++='\"'; break;
         case '\\': *p++='\\'; *p++='\\'; break;
         case '\b': *p++='\\'; *p++='b';  break;
         case '\f': *p++='\\'; *p++='f';  break;
         case '\n': *p++='\\'; *p++='n';  break;
         case '\r': *p++='\\'; *p++='r';  break;
         case '\t': *p++='\\'; *p++='t';  break;
         default:
            if (c < 0x20) { // control -> \u00XX
               static const char hex[] = "0123456789ABCDEF";
               *p++='\\'; *p++='u'; *p++='0'; *p++='0';
               *p++=hex[(c>>4)&0xF]; *p++=hex[c&0xF];
            } else {
               *p++=(char)c;
            }
     }
   }
   *p = '\0';
   return out;
}

/*!
 * Function: listDirToJson
 * -----------------------
 * Lists the regular files in the directory constructed from root/dir,
 * applies a pattern filter ans a suffix filter if provided, sorts the list either by name or by
 * modification date (most recent first), and generates a JSON string containing
 * an array of arrays in the format [filename, size, modification date].
 *
 * In case of an error (e.g., unable to open the directory), the error is printed
 * to the console and a corresponding JSON error response is returned.
 */
static char *listDirToStrJson (char *root, char *dir, bool sortByName, const char *pattern, const char **filter, char *out, size_t maxLen) {
   char line [MAX_SIZE_LINE];
   char fullPath [MAX_SIZE_LINE];
   char filePath [MAX_SIZE_LINE * 2];
   const char *sep = (dir && dir [0] != '/') ? "/" : "";
   if (maxLen == 0) return out;
   out[0] = '\0';

   // Path directory
   snprintf (fullPath, sizeof fullPath, "%s%s%s", root ? root : "", sep,  dir ? dir : "");

   DIR *d = opendir(fullPath);
   if (!d) {
      fprintf(stderr, "In listDirToStrJson Error opening directory '%s': %s\n", fullPath, strerror(errno));
      snprintf (out, maxLen, "{\"error\":\"Error opening directory\"}");
      return out;
   }

   // file  Collect
   FileInfo *arr = NULL;
   size_t n = 0, cap = 0;

   struct dirent *ent;
   while ((ent = readdir(d)) != NULL) {
      const char *fileName = ent->d_name;
      if (strcmp(fileName, ".") == 0 || strcmp(fileName, "..") == 0) continue;

      // suffix filter
      if (!matchFilter(fileName, filter)) continue;
      // prefix (pattern) filter
      if (pattern && ! g_str_has_prefix(fileName, pattern)) continue;
      snprintf (filePath, sizeof filePath, "%s/%s", fullPath, fileName);
      
      struct stat st;
      if (stat(filePath, &st) != 0) {
         fprintf(stderr, "In listDirToStrJson Error retrieving info for '%s': %s\n", filePath, strerror(errno));
         continue;
      }

      if (!S_ISREG(st.st_mode)) continue;

      /* push_back */
      if (n == cap) {
         size_t newcap = cap ? cap * 2 : 32;
         FileInfo *tmp = (FileInfo*)realloc(arr, newcap * sizeof(*arr));
         if (!tmp) {
            fprintf(stderr, "listDirToStrJson: realloc échouée\n");
            break;
         }
         arr = tmp; cap = newcap;
      }
      arr[n].name  = strdup(fileName);
      arr[n].size  = st.st_size;
      arr[n].mtime = st.st_mtime;
      if (!arr[n].name) { fprintf(stderr, "OOM on strdup\n"); continue; }
      n++;
   }
   closedir(d);

   if (n > 1) qsort(arr, n, sizeof(*arr), sortByName ? compareByName : compareByMtime);

   /* JSON */
   snprintf (out, maxLen, "[\n");
   for (size_t i = 0; i < n; i++) {
      /* locale date "YYYY-MM-DD HH:MM:SS" */
      struct tm tm_buf, *tm_info;
      tm_info = localtime_r(&arr[i].mtime, &tm_buf);
      char time_str[20] = "1970-01-01 00:00:00";
      if (tm_info) strftime(time_str, sizeof time_str, "%Y-%m-%d %H:%M:%S", tm_info);

      char *escaped = jsonEscapeStrdup(arr[i].name);
      snprintf (line, sizeof line,"   [\"%s\", %lld, \"%s\"]%s\n",
                escaped ? escaped : "", (long long)arr[i].size, time_str, (i + 1 < n) ? "," : "");
      g_strlcat (out, line, maxLen);
      free(escaped);  
   }
   g_strlcat (out, "]\n", maxLen);

   for (size_t i = 0; i < n; i++) free(arr[i].name);
   free(arr);
   return out;
}

/*! Make initialization  
   return false if readParam or readGribAll fail */
static bool initContext (const char *parameterFileName, const char *pattern) {
   char directory [MAX_SIZE_DIR_NAME];
   char str [MAX_SIZE_LINE];
   char errMessage [MAX_SIZE_TEXT] = "";
   bool readGribRet;

   if (! readParam (parameterFileName, false)) {
      fprintf (stderr, "In initContext, Error readParam: %s\n", parameterFileName);
      return false;
   }
   printf ("Parameters File: %s\n", parameterFileName);
   if (par.mostRecentGrib) {  // most recent grib will replace existing grib
      snprintf (directory, sizeof directory, "%s%sgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
      mostRecentFile (directory, ".gr", pattern, par.gribFileName, sizeof par.gribFileName);
   }
   if (par.gribFileName [0] != '\0') {
      readGribRet = readGribAll (par.gribFileName, &zone, WIND);
      if (! readGribRet) {
         fprintf (stderr, "In initContext, Error: Unable to read grib file: %s\n ", par.gribFileName);
         return false;
      }
      printf ("Grib loaded    : %s\n", par.gribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (zone.dataDate [0], zone.dataTime [0], str, sizeof str));
   }

   if (par.currentGribFileName [0] != '\0') {
      readGribRet = readGribAll (par.currentGribFileName, &zone, WIND);
      printf ("Cur grib loaded: %s\n", par.currentGribFileName);
      printf ("Grib DateTime0 : %s\n", gribDateTimeToStr (currentZone.dataDate [0], currentZone.dataTime [0], str, sizeof str));
   }
   if (readPolar (true, par.polarFileName, &polMat, &sailPolMat, errMessage, sizeof errMessage)) {
      printf ("Polar loaded   : %s\n", par.polarFileName);
   }
   else {
      fprintf (stderr, "In initContext, Error readPolar: %s\n", errMessage);
   }   
   if (readPolar (true, par.wavePolFileName, &wavePolMat, NULL, errMessage, sizeof errMessage)) {
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   }
   else {
      fprintf (stderr, "In initContext, Error readPolar: %s\n", errMessage);
   }
   printf ("par.web        : %s\n", par.web);
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   if (par.isSeaFileName [0] != '\0')
      readIsSea (par.isSeaFileName);
   updateIsSeaWithForbiddenAreas ();
   return true;
}

/*! date for logging */
static const char* getCurrentDate () {
   static char dateBuffer [100];
   const time_t now = time (NULL);
   struct tm *tm_info = gmtime(&now);
   strftime (dateBuffer, sizeof dateBuffer, "%Y-%m-%d %H:%M:%S UTC", tm_info);
   return dateBuffer;
}

/*! store ck information */
static void handleFeedbackRequest (const char *fileName, const char *date, const char *clientIPAddress, const char *string) {
   FILE *file = fopen (fileName, "a");
   if (file == NULL) {
      fprintf (stderr, "handleFeedbackRequest, Error opening file: %s\n", fileName);
      return;
   }
   fprintf (file, "%s; %s; \n%s\n\n", date, clientIPAddress, string);
   fclose (file);
}

/*! log client Request 
    side effect: dataReq is modified */
static void logRequest (const char* fileName, const char *date, int serverPort, const char *remote_addr, \
   char *dataReq, const char *userAgent, ClientRequest *client, double duration) {

   char newUserAgent [MAX_SIZE_LINE];
   g_strlcpy (newUserAgent, userAgent, sizeof newUserAgent);
   g_strdelimit (newUserAgent, ";", ':'); // to avoid ";" the CSV delimiter inside field
   char *startAgent = strchr (newUserAgent, '('); // we delete what is before ( if exist
   if (startAgent == NULL) startAgent = newUserAgent;

   FILE *logFile = fopen (fileName, "a");
   if (logFile == NULL) {
      fprintf (stderr, "In logRequest, Error opening log file: %s\n", fileName);
      return;
   }
   g_strstrip (dataReq);
   g_strdelimit (dataReq, "\r\n", ' ');
   
   fprintf (logFile, "%s; %d; %-16.16s; %-40.40s; %2d; %6.2lf; %.50s\n", 
      date, serverPort, remote_addr, startAgent, client->type, duration, dataReq);
   fclose (logFile);
}

/*! decode request from client and fill ClientRequest structure 
   return true if correct false if impossible to decode */
static bool decodeHttpReq (const char *req, ClientRequest *clientReq) {
   memset (clientReq, 0, sizeof (ClientRequest));
   clientReq->type = -1;              // default unknown
   clientReq->timeStep = 3600;   
   clientReq->cogStep = 5;
   clientReq->rangeCog = 90;
   clientReq->kFactor = 1;
   clientReq->nSectors = 720;
   clientReq->staminaVR = 100.0;
   clientReq->motorSpeed = 6.0;
   clientReq->nightEfficiency = 1.0;
   clientReq->dayEfficiency = 1.0;
   clientReq->xWind = 1.0;
   clientReq->maxWind = 100.0;

   char **parts = g_strsplit (req, "&", -1);
   if (parts == NULL) return false;

   for (int i = 0; parts[i]; i++) {
      // printf ("part %d %s\n", i, parts [i]);
      g_strstrip (parts [i]);      
      if (sscanf(parts[i], "type=%d", &clientReq->type) == 1);// type extraction
      else if (g_str_has_prefix (parts[i], "boat=")) {
         char *boatsPart = parts[i] + strlen ("boat="); // After boats=="
         if (*boatsPart == '\0') { 
            g_strfreev(parts);
            return false;  // parsing error
         }
         // waypoints parsing
         char **boatsCoords = g_strsplit (boatsPart, ";", -1);
         for (int i = 0; boatsCoords[i] && clientReq->nBoats < MAX_N_COMPETITORS; i++) {
            char name [MAX_SIZE_NAME];
            double lat, lon;
            if (sscanf(boatsCoords[i], "%63[^,], %lf, %lf", name, &lat, &lon) == 3) {
               g_strlcpy (clientReq->boats [clientReq->nBoats].name, name, MAX_SIZE_NAME);
               clientReq->boats [clientReq->nBoats].lat = lat;
               clientReq->boats [clientReq->nBoats].lon = lon;
               clientReq->nBoats += 1;
            }
         }
         g_strfreev(boatsCoords);
      }
      else if (g_str_has_prefix (parts[i], "waypoints=")) {
         char *wpPart = parts[i] + strlen ("waypoints="); // Partie après "waypoints="
         if (*wpPart == '\0') {
            g_strfreev(parts);
            return false;  // parsing error
         }
         // waypoints parsing
         char **wpCoords = g_strsplit (wpPart, ";", -1);
         for (int i = 0; wpCoords[i] && clientReq->nWp < MAX_N_WAY_POINT; i++) {
            double lat, lon;
            if (sscanf(wpCoords[i], "%lf,%lf", &lat, &lon) == 2) {
               clientReq->wp [clientReq->nWp].lat = lat;
               clientReq->wp [clientReq->nWp].lon = lon;
               clientReq->nWp += 1;
            }
         }
         g_strfreev(wpCoords);
      }

      else if (sscanf (parts[i], "timeStep=%d", &clientReq->timeStep) == 1);                    // time step extraction
      else if (sscanf (parts[i], "cogStep=%d",  &clientReq->cogStep) == 1);                     // cog step extraction
      else if (sscanf (parts[i], "cogRange=%d", &clientReq->rangeCog) == 1);                    // range sog  extraction
      else if (sscanf (parts[i], "jFactor=%d",  &clientReq->jFactor) == 1);                     // jFactor extraction
      else if (sscanf (parts[i], "kFactor=%d",  &clientReq->kFactor) == 1);                     // kFactor extraction
      else if (sscanf (parts[i], "nSectors=%d", &clientReq->nSectors) == 1);                    // nSectors extraction
      else if (sscanf (parts[i], "penalty0=%d", &clientReq->penalty0) == 1);                    // penalty0 extraction
      else if (sscanf (parts[i], "penalty1=%d", &clientReq->penalty1) == 1);                    // penalty1 extraction
      else if (sscanf (parts[i], "penalty2=%d", &clientReq->penalty2) == 1);                    // penalty2 (sail change)  extraction
      else if (sscanf (parts[i], "initialAmure=%d", &clientReq->initialAmure) == 1);            // initial Amure extraction
      else if (sscanf (parts[i], "epochStart=%ld",  &clientReq->epochStart) == 1);              // time startextraction
      else if (sscanf (parts[i], "polar=%255s", clientReq->polarName) == 1);                    // polar name
      else if (sscanf (parts[i], "wavePolar=%255s", clientReq->wavePolName) == 1);              // wave polar name
      else if (sscanf (parts[i], "file=%255s",  clientReq->fileName) == 1);                     // file name
      else if (sscanf (parts[i], "model=%255s", clientReq->model) == 1);                        // grib model
      else if (sscanf (parts[i], "grib=%255s", clientReq->gribName) == 1);                      // grib name
      else if (sscanf (parts[i], "currentGrib=%255s", clientReq->currentGribName) == 1);        // current grib name
      else if (sscanf (parts[i], "dir=%255s",   clientReq->dirName) == 1);                      // directory name
      else if (g_str_has_prefix (parts[i], "dir=")) 
         g_strlcpy (clientReq->dirName, parts [i] + strlen ("dir="), sizeof clientReq->dirName); // defaulr empty works
      else if (g_str_has_prefix (parts[i], "feedback=")) 
         g_strlcpy (clientReq->feedback, parts [i] + strlen ("feedback="), sizeof clientReq->feedback);
      else if (g_str_has_prefix (parts[i], "isoc=true")) clientReq->isoc = true;                // Default false
      else if (g_str_has_prefix (parts[i], "isoc=false")) clientReq->isoc = false;              // Default false
      else if (g_str_has_prefix (parts[i], "isodesc=true")) clientReq->isoDesc = true;          // Default false
      else if (g_str_has_prefix (parts[i], "isodesc=false")) clientReq->isoDesc = false;        // Default false
      else if (g_str_has_prefix (parts[i], "forbid=true")) clientReq->forbid = true;            // Default false
      else if (g_str_has_prefix (parts[i], "forbid=false")) clientReq->forbid = false;          // Default false
      else if (g_str_has_prefix (parts[i], "withWaves=true")) clientReq->withWaves = true;      // Default false
      else if (g_str_has_prefix (parts[i], "withWaves=false")) clientReq->withWaves = false;    // Default false
      else if (g_str_has_prefix (parts[i], "withCurrent=true")) clientReq->withCurrent = true;  // Default false
      else if (g_str_has_prefix (parts[i], "withCurrent=false")) clientReq->withCurrent = false;// Default false
      else if (g_str_has_prefix (parts[i], "sortByName=true")) clientReq->sortByName = true;    // Default false
      else if (g_str_has_prefix (parts[i], "sortByName=false")) clientReq->sortByName = false;  // Default false
      else if (sscanf (parts[i], "staminaVR=%lf",        &clientReq->staminaVR) == 1);          // stamina Virtual Regatta
      else if (sscanf (parts[i], "motorSpeed=%lf",       &clientReq->motorSpeed) == 1);         // motor speed
      else if (sscanf (parts[i], "threshold=%lf",         &clientReq->threshold) == 1);         // threshold for motoe
      else if (sscanf (parts[i], "nightEfficiency=%lf",  &clientReq->nightEfficiency) == 1);    // efficiency at night
      else if (sscanf (parts[i], "dayEfficiency=%lf",    &clientReq->dayEfficiency) == 1);      // efficiency daylight
      else if (sscanf (parts[i], "xWind=%lf",            &clientReq->xWind) == 1);              // xWind factor
      else if (sscanf (parts[i], "maxWind=%lf",          &clientReq->maxWind) == 1);            // max Wind
      else if (sscanf (parts[i], "constWindTws=%lf",     &clientReq->constWindTws) == 1);       // const Wind TWS
      else if (sscanf (parts[i], "constWindTwd=%lf",     &clientReq->constWindTwd) == 1);       // const Wind TWD
      else if (sscanf (parts[i], "constWave=%lf",        &clientReq->constWave) == 1);          // const Waves
      else if (sscanf (parts[i], "constCurrentS=%lf",     &clientReq->constCurrentS) == 1);     // const current speed
      else if (sscanf (parts[i], "constCurrentD=%lf",     &clientReq->constCurrentD) == 1);     // const current direction
      else fprintf (stderr, "In decodeHttpReq Unknown value: %s\n", parts [i]);
   }
   g_strfreev (parts);
   
   return clientReq->type != -1;  
}

/*! check validity of parameters */
static bool checkParamAndUpdate (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   char strPolar [MAX_SIZE_FILE_NAME];
   char directory [MAX_SIZE_DIR_NAME];
   checkMessage  [0] = '\0';
   // printf ("startInfo after: %s, startTime: %lf\n", asctime (&startInfo), par.startTimeInHours);
   if ((clientReq->nBoats == 0) || (clientReq->nWp == 0)) {
      snprintf (checkMessage, maxLen, "1: No boats or no Waypoints");
      return false;
   }
   par.allwaysSea = !clientReq->forbid;
   par.cogStep = MAX (1, clientReq->cogStep);
   par.rangeCog = clientReq->rangeCog;
   par.jFactor = clientReq->jFactor; 
   par.kFactor = clientReq->kFactor; 
   par.nSectors = clientReq->nSectors; 
   par.penalty0 = clientReq->penalty0;  // seconds
   par.penalty1 = clientReq->penalty1;  // seconds 
   par.penalty2 = clientReq->penalty2;  // seconds
   par.motorSpeed = clientReq->motorSpeed;
   par.threshold = clientReq->threshold;
   par.nightEfficiency = clientReq->nightEfficiency;
   par.dayEfficiency = clientReq->dayEfficiency;
   par.xWind = clientReq->xWind;
   par.maxWind = clientReq->maxWind;
   par.withWaves = clientReq->withWaves;
   par.withCurrent = clientReq->withCurrent;
   par.constWindTws = clientReq->constWindTws;
   par.constWindTwd = clientReq->constWindTwd;
   par.constWave = clientReq->constWave;
   par.constCurrentS = clientReq->constCurrentS;
   par.constCurrentD = clientReq->constCurrentD;
   par.staminaVR = clientReq->staminaVR;

   // change polar if requested
   if (clientReq->polarName [0] != '\0') {
      buildRootName (clientReq->polarName, strPolar, sizeof strPolar);
      printf ("polar found: %s\n", strPolar);
      if (strncmp (par.polarFileName, strPolar, strlen (strPolar)) != 0) {
         printf ("read polar: %s\n", strPolar);
         if (readPolar (false, strPolar, &polMat, &sailPolMat, checkMessage, maxLen)) {
            g_strlcpy (par.polarFileName, strPolar, sizeof par.polarFileName);
            printf ("Polar loaded   : %s\n", strPolar);
         }
         else {
            snprintf (checkMessage, maxLen, "2: Error reading Polar: %s", clientReq->polarName);
            return false;
         }
      }  
   }
   if (clientReq->wavePolName [0] != '\0') {
      buildRootName (clientReq->wavePolName, strPolar, sizeof strPolar);
      printf ("wave polar found: %s\n", strPolar);
      if (strncmp (par.wavePolFileName, strPolar, strlen (strPolar)) != 0) {
         printf ("read wave polar: %s\n", strPolar);
         if (readPolar (false, strPolar, &wavePolMat, NULL, checkMessage, maxLen)) {
            g_strlcpy (par.wavePolFileName, strPolar, sizeof par.wavePolFileName);
            printf ("Wave Polar loaded : %s\n", strPolar);
         }
         else {
            snprintf (checkMessage, maxLen, "2: Error reading Wave Polar: %s", clientReq->wavePolName);
            return false;
         }
      }  
   }
   if (clientReq->model [0] != '\0' && clientReq->gribName [0] == '\0') { // there is a model specified but no grib file
      snprintf (directory, sizeof directory, "%s/%s", par.workingDir, "grib"); 
      mostRecentFile (directory, ".gr", clientReq->model, clientReq->gribName, sizeof clientReq->gribName);
   }
   updateWindGrib (clientReq, checkMessage, maxLen);
   if (checkMessage [0] != '\0') return false;
   updateCurrentGrib (clientReq, checkMessage, maxLen);
   if (checkMessage [0] != '\0') return false;
   
   if (clientReq->epochStart <= 0)
      clientReq->epochStart = time (NULL); // default value if empty is now
   const time_t theTime0 = gribDateTimeToEpoch (zone.dataDate [0], zone.dataTime [0]);
   par.startTimeInHours = (clientReq->epochStart - theTime0) / 3600.0;
   printf ("Start Time Epoch: %ld, theTime0: %ld\n", clientReq->epochStart, theTime0);
   printf ("Start Time in Hours after Grib: %.2lf\n", par.startTimeInHours);
   char *gribBaseName = g_path_get_basename (par.gribFileName);

   competitors.n = clientReq->nBoats;
   for (int i = 0; i < clientReq->nBoats; i += 1) {
      if (! par.allwaysSea && ! isSeaTolerant (tIsSea,  clientReq -> boats [i].lat,  clientReq -> boats [i].lon)) {
         snprintf (checkMessage, maxLen, 
            "5: Competitor not in sea., name: %s, lat: %.6lf, lon: %.6lf",
            clientReq -> boats [i].name, clientReq -> boats [i].lat, clientReq -> boats [i].lon);
         return false;
      }
      if (! isInZone (clientReq -> boats [i].lat, clientReq -> boats [i].lon, &zone) && (par.constWindTws == 0)) { 
         snprintf (checkMessage, maxLen, 
            "6: Competitor not in Grib wind zone., grib: %s, bottomLat: %.2lf, leftLon: %.2lf, topLat: %.2lf, rightLon: %.2lf",
            gribBaseName, zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight);
         free (gribBaseName);
         return false;
      }
      g_strlcpy (competitors.t [i].name, clientReq -> boats [i].name, MAX_SIZE_NAME);
      printf ("competitor name: %s\n", competitors.t [i].name);
      competitors.t [i].lat = clientReq -> boats [i].lat;
      competitors.t [i].lon = clientReq -> boats [i].lon;
   }
 
   for (int i = 0; i < clientReq->nWp; i += 1) {
      if (! par.allwaysSea && ! isSeaTolerant (tIsSea, clientReq->wp [i].lat, clientReq->wp [i].lon)) {
         snprintf (checkMessage, maxLen, 
            "7: WP or Dest. not in sea, lat: %.2lf, lon: %.2lf",
            clientReq->wp [i].lat, clientReq->wp [i].lon);
         return false;
      }
      if (! isInZone (clientReq->wp [i].lat , clientReq->wp [i].lon , &zone) && (par.constWindTws == 0)) {
         snprintf (checkMessage, maxLen, 
            "8: WP or Dest. not in Grib wind zone:  %s, bottomLat: %.2lf, leftLon: %.2lf, topLat: %.2lf, rightLon: %.2lf",
            gribBaseName, zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight);
         free (gribBaseName);
         return false;
      }
   }
   for (int i = 0; i < clientReq->nWp -1; i += 1) {
      wayPoints.t[i].lat = clientReq->wp [i].lat; 
      wayPoints.t[i].lon = clientReq->wp [i].lon; 
   }
   wayPoints.n = clientReq->nWp - 1;

   if ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1])) {
         snprintf (checkMessage, maxLen, "\"4: start Time not in Grib time window\"");
      return false;
   }

   par.tStep = clientReq->timeStep / 3600.0;

   par.pOr.lat = clientReq->boats [0].lat;
   par.pOr.lon = clientReq->boats [0].lon;
   par.pOr.amure = clientReq->initialAmure;
   par.pDest.lat = clientReq->wp [clientReq->nWp-1].lat;
   par.pDest.lon = clientReq->wp [clientReq->nWp-1].lon;
   return true;
}

/*! Translate extension in MIME type */
static const char *getMimeType (const char *path) {
   const char *ext = strrchr(path, '.');
   if (!ext) return "application/octet-stream";
   if (strcmp(ext, ".html") == 0) return "text/html";
   if (strcmp(ext, ".css") == 0) return "text/css";
   if (strcmp(ext, ".js") == 0) return "application/javascript";
   if (strcmp(ext, ".png") == 0) return "image/png";
   if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
   if (strcmp(ext, ".gif") == 0) return "image/gif";
   if (strcmp(ext, ".txt") == 0) return "text/plain";
   if (strcmp(ext, ".par") == 0) return "text/plain";
   return "application/octet-stream";
}

/*! serve static file */
static void serveStaticFile (int client_socket, const char *requested_path) {
   char filepath [512];
   snprintf (filepath, sizeof filepath, "%s%s", par.web, requested_path);
   printf ("File Path: %s\n", filepath);

   // Check if file exist
   struct stat st;
   if (stat (filepath, &st) == -1 || S_ISDIR(st.st_mode)) {
      const char *not_found = "HTTP/1.1 404 Not Found\r\nContent-Length: 13\r\n\r\n404 Not Found";
      send (client_socket, not_found, strlen(not_found), 0);
      return;
   }

   // File open
   const int file = open (filepath, O_RDONLY);
   if (file == -1) {
      const char *error = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\n500 Internal Server Error";
      send (client_socket, error, strlen(error), 0);
      return;
   }

   // send HTTP header and MIME type
   char header [256];
   snprintf (header, sizeof header, "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n",
          getMimeType(filepath), st.st_size);
   send (client_socket, header, strlen(header), 0);

   // Read end send file
   char buffer [1024];
   ssize_t bytesRead;
   while ((bytesRead = read(file, buffer, sizeof buffer )) > 0) {
      send (client_socket, buffer, bytesRead, 0);
   }
   close (file);
}

/*!
 * @brief Return the current memory usage (resident set size) in kilobytes.
 * @return int Memory usage in KB, or -1 on error.
 */
int memoryUsage (void) {
   FILE *fp = fopen("/proc/self/status", "r");
   if (!fp) return -1;
   char line[256];
   int mem = -1;

   while (fgets (line, sizeof line, fp)) {
      if (strncmp (line, "VmRSS:", 6) == 0) {
         // Example line: "VmRSS:      12345 kB"
         sscanf (line + 6, "%d", &mem);  // skip "VmRSS:"
         break;
      }
   }
   fclose(fp);
   return mem;  // in KB
}

/*! Raw dump of file fileName into 'out' (NULL-terminated). Truncates if needed. */
static char *dumpFileToStr(const char *fileName, char *out, size_t maxLen) {
   char fullFileName[MAX_SIZE_FILE_NAME];
   buildRootName(fileName, fullFileName, sizeof fullFileName);

   if (maxLen == 0) return out;
   out[0] = '\0';

   FILE *fp = fopen (fullFileName, "rb");  // b = binary, no CR/LF translation
   if (!fp) {
      snprintf(out, maxLen, "{\"_Error\": \"%s\"}\n", strerror(errno));
      return out;
   }

   size_t total = 0;
   while (total + 1 < maxLen) {
      size_t n = fread(out + total, 1, maxLen - 1 - total, fp);
      total += n;
      if (n == 0) break;                  // EOF or error
   }
   out[total] = '\0';

   if (ferror(fp)) {
      snprintf(out, maxLen, "{\"_Error\": \"%s\"}\n", strerror(errno));
   } else if (!feof(fp)) {
      fprintf (stderr, "dumpFileToStr: output buffer truncated to %zu bytes\n", maxLen - 1);
   }

   fclose(fp);
   return out;
}

/*! Provide system information */
static char *testToJson (int serverPort, const char *clientIP, const char *userAgent, int level, char *out, size_t maxLen) {
   char strWind [MAX_SIZE_LINE] = "";
   char strCurrent [MAX_SIZE_LINE] = "";
   char strMem [MAX_SIZE_LINE] = "";
   char str [MAX_SIZE_TEXT_FILE] = "";

   formatThousandSep (strWind, sizeof strWind, sizeof(FlowP) * (zone.nTimeStamp + 1) * zone.nbLat * zone.nbLon);
   formatThousandSep (strCurrent, sizeof strCurrent, sizeof(FlowP) * (currentZone.nTimeStamp + 1) * currentZone.nbLat * currentZone.nbLon);
   formatThousandSep (strMem, sizeof strMem, memoryUsage ()); // KB ! 
   
   snprintf (out, maxLen,
      "{\n   \"Prog-version\": \"%s, %s, %s\",\n"
      "   \"API server port\": %d,\n"
      "   \"Grib Reader\": \"%s\",\n"
      "   \"Memory for Grib Wind\": \"%s\",\n"
      "   \"Memory for Grib Current\": \"%s\",\n"
      "   \"Compilation-date\": \"%s\",\n"
      "   \"PID\": %d,\n"
      "   \"Memory usage in KB\": \"%s\",\n"
      "   \"Client IP Address\": \"%s\",\n"
      "   \"User Agent\": \"%s\",\n"
      "   \"Authorization-Level\": %d\n}\n",
      PROG_NAME, PROG_VERSION, PROG_AUTHOR, serverPort, gribReaderVersion (str, sizeof str), 
      strWind, strCurrent, __DATE__, getpid (), strMem, clientIP, userAgent, level
   );
   return out;
}


/*! launch action and returns outBuffer after execution */
static char *launchAction (int serverPort, ClientRequest *clientReq, 
             const char *date, const char *clientIPAddress, const char *userAgent, char *outBuffer, size_t maxLen) {
   char tempFileName [MAX_SIZE_FILE_NAME];
   char checkMessage [MAX_SIZE_TEXT];
   char directory [MAX_SIZE_DIR_NAME];
   char strGrib [MAX_SIZE_FILE_NAME];
   bigBuffer [0] = '\0';
   // printf ("client.req = %d\n", clientReq->type);
   switch (clientReq->type) {
   case REQ_KILL:
      printf ("Killed on port: %d, At: %s, By: %s\n", serverPort, date, clientIPAddress);
      snprintf (outBuffer, maxLen, "{\n   \"killed_on_port\": %d, \"date\": %s, \"by\": %s\"\n}\n", serverPort, date, clientIPAddress);
      break;
   case REQ_TEST:
      testToJson (serverPort, clientIPAddress, userAgent, clientReq->level, outBuffer, maxLen);
      break;
   case REQ_ROUTING:
      if (checkParamAndUpdate (clientReq, checkMessage, sizeof checkMessage)) {
         competitors.runIndex = 0;
         routingLaunch ();
         if (route.ret == ROUTING_ERROR) {
            snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "Routing failed");
         }
         else routeToJson (&route, clientReq->isoc, clientReq->isoDesc, outBuffer, maxLen);
      }
      else {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", checkMessage);
      }
      break;
   case REQ_COORD:
      if (clientReq->nBoats > 0) {
         infoCoordToJson (clientReq->boats [0].lat, clientReq->boats [0].lon, clientReq, outBuffer, maxLen);
      }
      else {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"No Boat\"\n}\n");
      }
      break;
   case REQ_FORBID:
      forbidToJson (outBuffer, maxLen);
      break;
   case REQ_POLAR:
      if (clientReq->polarName [0] != '\0') {
         if (strstr (clientReq->polarName, "wavepol")) {
            polToStrJson (clientReq->polarName, "wavePolarName", outBuffer, maxLen);
         }
         else {
            polToStrJson (clientReq->polarName , "polarName", outBuffer, maxLen);
         }
         break;
      }
      polToStrJson (par.polarFileName, "polarName", outBuffer, maxLen);
      break;
   case REQ_GRIB:
      if (clientReq->model [0] != '\0' && clientReq->gribName [0] == '\0') { // there is a model specified but no grib file
         printf ("model: %s\n", clientReq->model);
         snprintf (directory, sizeof directory, "%s%sgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
         mostRecentFile (directory, ".gr", clientReq->model, clientReq->gribName, sizeof clientReq->gribName);
      }
      if (clientReq->gribName [0] != '\0') gribToStrJson (clientReq->gribName, outBuffer, maxLen);
      else snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No Grib");
      break;
   case REQ_DIR:
      listDirToStrJson (par.workingDir, clientReq->dirName, clientReq->sortByName, NULL, filter, outBuffer, maxLen);
      break;
   case REQ_PAR_RAW:
      // yaml style if model = 1
      bool yaml = clientReq->model [0] == 'y';
      writeParam (buildRootName (TEMP_FILE_NAME, tempFileName, sizeof tempFileName), false, false, yaml);
      dumpFileToStr (TEMP_FILE_NAME, outBuffer, maxLen);
      if (yaml) normalizeSpaces (outBuffer);
      break;
   case REQ_PAR_JSON:
      paramToStrJson (&par, outBuffer, maxLen);
      break;
   case REQ_INIT:
      if (! initContext (parameterFileName, PATTERN))
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"Init failed\",\n  \"serverPort\": %d\n}\n", serverPort);
      else
         snprintf (outBuffer, maxLen, "{\n  \"message\": \"Init done\",\n  \"serverPort\": %d\n}\n", serverPort);
      break;
   case REQ_FEEDBACK:
         handleFeedbackRequest (par.feedbackFileName, date, clientIPAddress, clientReq->feedback);
         snprintf (outBuffer, maxLen, "{\"_Feedback\": \"%s\"}\n", "OK");
      break;
   case REQ_DUMP_FILE:  // raw dump
      dumpFileToStr (clientReq->fileName, outBuffer, maxLen);
      break;
   case REQ_MARKS: 
      if (! readMarkCSVToJson (par.marksFileName, outBuffer, maxLen)) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"Reading Mark File %s\"}\n", par.marksFileName);
      }
      break;
   case REQ_NEAREST_PORT:
      if (clientReq->nWp > 0)
         nearestPortToStrJson (clientReq->wp[0].lat, clientReq->wp[0].lon, outBuffer, maxLen);
      else snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No coordinates found");
      break;
   case REQ_CHECK_GRIB: 
      if ((clientReq->gribName [0] == '\0') || (clientReq->currentGribName [0] == '\0')) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "Both wind grib and current grib required");
         break;
      }
      buildRootName (clientReq->gribName, strGrib, sizeof strGrib);
      if (! readGribAll (strGrib, &zone, WIND)) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s %s\"}\n", "Cannot Read Wind Grib:", strGrib);
         break;
      }
      buildRootName (clientReq->currentGribName, strGrib, sizeof strGrib);
      if (! readGribAll (strGrib, &currentZone, CURRENT)) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s %s\"}\n", "Cannot Read Current Grib:", strGrib);
         break;
      }
      checkGribToStr (outBuffer, maxLen);
      if (outBuffer [0] == '\0') g_strlcpy (outBuffer, "All is OK\n", maxLen);
      break;
   case REQ_GPX_ROUTE:
      buildRootName(GPX_ROUTE_FILE_NAME, tempFileName, sizeof tempFileName);
      if (route.n == 0 || !exportRouteToGpx (&route, tempFileName)) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No route");
         break;
      }
      dumpFileToStr(tempFileName, outBuffer, maxLen);
      break;
   default:;
   }
   return outBuffer;
}

/*! robust send */
static int sendAll (int fd, const void *buf, size_t len) {
   const unsigned char *p = buf;
   while (len > 0) {
      const ssize_t n = send(fd, p, len, 0);
      if (n < 0) {
         if (errno == EINTR) continue;
         if (errno == EAGAIN || errno == EWOULDBLOCK) continue; // or poll/select
         return -1;
      }
      p += (size_t)n;
      len -= (size_t)n;
   }
   return 0;
}

/*! Handle client connection and launch actions */
static bool handleClient (int serverPort, int clientFd, struct sockaddr_in *client_addr) {
   char saveBuffer [MAX_SIZE_REQUEST];
   char buffer [MAX_SIZE_REQUEST] = "";
   char clientIPAddress [MAX_SIZE_LINE];

   // read HTTP request
   const int bytes_read = recv (clientFd, buffer, sizeof buffer - 1, 0);
   if (bytes_read <= 0) {
     return false;
   }
   buffer [bytes_read] = '\0'; // terminate string
   // printf ("Client Request: %s\n", buffer);
   g_strlcpy (saveBuffer, buffer, sizeof buffer);

   if (! getRealIPAddress (buffer, clientIPAddress, sizeof clientIPAddress)) { // try if proxy 
      // Get client IP address if IP address not found with proxy
      char remoteAddr [INET_ADDRSTRLEN];
      inet_ntop (AF_INET, &(client_addr->sin_addr), remoteAddr, INET_ADDRSTRLEN); // not used
      g_strlcpy (clientIPAddress, remoteAddr, INET_ADDRSTRLEN);
   }

   // Extract HTTP first line request
   char *requestLine = strtok (buffer, "\r\n");
   if (!requestLine) {
     return false;
   }
   printf ("Request line: %s\n", requestLine);

   // check if Rest API (POST) or static file (GET)
   if (strncmp (requestLine, "POST", 4) != 0) {
      printf ("GET Request, static file\n");
      // static file
      const char *requested_path = strchr (requestLine, ' '); // space after "GET"
      if (!requested_path) {
        return false;
      }
      requested_path++; // Pass space

      char *end_path = strchr (requested_path, ' ');
        if (end_path) {
      *end_path = '\0'; // Terminate string
      }

      if (strcmp(requested_path, "/") == 0) {
         requested_path = "/index.html"; // Default page
      }

      serveStaticFile (clientFd, requested_path);
      return true; // stop
   }

   // Extract request body
   char *postData = strstr (saveBuffer, "\r\n\r\n");
   if (postData == NULL) {
      return false;
   }

   char* userAgent = extractUserAgent (saveBuffer);
   postData += 4; // Ignore HTTP request separators
   printf ("POST Request:\n%s\n", postData);

   // data for log
   const double start = monotonic (); 
   const char *date = getCurrentDate ();

   if (! decodeHttpReq (postData, &clientReq)) {
      const char *errorResponse = "HTTP/1.1 400 Bad Request\r\nContent-Type: text/plain\r\n\r\nError";
      fprintf (stderr, "In handleClient, Error: %s\n", errorResponse);
      send (clientFd, errorResponse, strlen(errorResponse), 0);
      return false;
   }
   clientReq.level = extractLevel (saveBuffer);
   printf ("user level: %d\n", clientReq.level);

   const char *cors_headers = "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n";

   if (! allowedLevel (&clientReq)) {
      g_strlcpy (bigBuffer, "{\"_Error\": \"Too low level of authorization\"}\n", BIG_BUFFER_SIZE);
   }
   else {
      launchAction (serverPort, &clientReq, date, clientIPAddress, userAgent, bigBuffer, BIG_BUFFER_SIZE);
   }
   char header [512];
   const size_t bigBufferLen = strlen (bigBuffer);
   const int headerLen = snprintf (header, sizeof header,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: application/json\r\n"
      "%s"
      "Content-Length: %zu\r\n"
      "\r\n",
      cors_headers, bigBufferLen);

   if (sendAll (clientFd, header, (size_t) headerLen) < 0) return false;
   if (sendAll (clientFd, bigBuffer, bigBufferLen) < 0) return false;
   printf ("Response sent to client. Size: %zu\n\n", headerLen + bigBufferLen);
   // printf ("%s%s\n", header, bigBuffer);

   const double duration = monotonic () - start; 
   logRequest (par.logFileName, date, serverPort, clientIPAddress, postData, userAgent, &clientReq, duration);
   if (userAgent) free (userAgent);
   return true;
}

/*! main server 
  * first argument (mandatory): port number
  * second argument (optionnal): parameter file
  */
int main (int argc, char *argv[]) {
   int serverFd, clientFd;
   struct sockaddr_in address;
   int addrlen = sizeof address;
   int serverPort, opt = 1;
   const double start = monotonic (); 

   if ((bigBuffer =malloc (BIG_BUFFER_SIZE)) == NULL) {
      fprintf (stderr, "In main, Error: Malloc: %d,", BIG_BUFFER_SIZE);
      return EXIT_FAILURE;
   }

   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "In main, Error: setlocale failed");
      return EXIT_FAILURE;
   }

   if (argc <= 1 || argc > 3) {
      fprintf (stderr, "Synopsys: %s %s\n", argv [0], SYNOPSYS);
      return EXIT_FAILURE;
   }
   serverPort = atoi (argv [1]);
   if (serverPort < 80 || serverPort > 9000) {
      fprintf (stderr, "In main, Error: port server not in range\n");
      return EXIT_FAILURE;
   }

   if (argc > 2)
      g_strlcpy (parameterFileName, argv [2], sizeof parameterFileName);
   else 
      g_strlcpy (parameterFileName, PARAMETERS_FILE, sizeof parameterFileName);

   if (! initContext (parameterFileName, ""))
      return EXIT_FAILURE;

   // Socket 
   serverFd = socket (AF_INET, SOCK_STREAM, 0);
   if (serverFd < 0) {
      perror ("In main, Error: socket failed");
      return EXIT_FAILURE;
   }

   // Allow adress reuse juste after closing
   if (setsockopt (serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof opt) < 0) {
      perror ("In main, Error setsockopt");
      close (serverFd);
      return EXIT_FAILURE;
   }

   // Define server parameters address port
   address.sin_family = AF_INET;
   address.sin_addr.s_addr = INADDR_ANY;
   address.sin_port = htons (serverPort);

   // Bind socket with port
   if (bind (serverFd, (struct sockaddr *)&address, sizeof address) < 0) {
      perror ("In main, Error socket bind"); 
      close (serverFd);
      return EXIT_FAILURE;
   }

   // Listen connexions
   if (listen (serverFd, 3) < 0) {
      perror ("In main: Error listening");
      close (serverFd);
      return EXIT_FAILURE;
   }
   const double elapsed = monotonic () - start; 
   printf ("✅ Loaded in...: %.2lf seconds. Server listen on port: %d, Pid: %d\n", elapsed, serverPort, getpid ());

   while (clientReq.type != REQ_KILL) {
      if ((clientFd = accept (serverFd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
         perror ("In main: Error accept");
         close (serverFd);
         return EXIT_FAILURE;
      }
      handleClient (serverPort, clientFd, &address);
      fflush (stdout);
      fflush (stderr);
      close (clientFd);
   }
   close (serverFd);
   free (tIsSea);
   free (isoDesc);
   free (isocArray);
   free (route.t);
   // freeHistoryRoute ();
   free (tGribData [WIND]); 
   free (tGribData [CURRENT]); 
   free (bigBuffer);
   return EXIT_SUCCESS;
}

