#include <dirent.h>   // DIR, opendir, readdir, closedir
#include <sys/stat.h> // stat
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#include <time.h>
#include "glibwrapper.h"
#include "r3types.h"
#include "inline.h"
#include "r3util.h"
#include "engine.h"
#include "grib.h"
#include "polar.h"
#include "readgriball.h"

ClientRequest clientReq;
// global filter for REQ_DIR request
const char *filter[] = {".csv", ".pol", ".grb", ".grb2", ".log", ".txt", ".par", ".yaml", ".json", NULL}; 

/*! Structure to store file information. */
typedef struct {
   char *name;
   off_t size;
   time_t mtime;
} FileInfo;

/*! date for logging */
const char* getCurrentDate (void) {
   static char dateBuffer [100];
   const time_t now = time (NULL);
   struct tm *tm_info = gmtime(&now);
   strftime (dateBuffer, sizeof dateBuffer, "%Y-%m-%d %H:%M:%S UTC", tm_info);
   return dateBuffer;
}

/*! build list of short names. Only initials. Result can be uv, uvg, uvw, uvwg. 
   uv mandatory. Response is false if not u and v */
void buildInitialOfShortNameList(const Zone *zone, char *str, size_t len) {
   int i = 0;
   str [0] = '\0';
   if (len < 5) return;
   if (! uvPresentGrib (zone)) return;
   str [i++] = 'u';
   str [i++] = 'v';
   if (isPresentGrib (zone, "gust")) str[i++] = 'g';
   if (isPresentGrib (zone, "swh"))  str[i++] = 'w';
   str [i] = '\0';
}

/*! Make initialization  
   return false if readParam or readGribAll fail */
bool initContext (const char *parameterFileName, const char *pattern) {
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
   //strlcpy (par.polarFileName, "", 1);
   if (readPolar (par.polarFileName, &polMat, &sailPolMat, errMessage, sizeof errMessage)) {
      printf ("Polar loaded   : %s\n", par.polarFileName);
   }
   else {
      fprintf (stderr, "In initContext, Error readPolar: %s\n", errMessage);
   }
   if (readPolar (par.wavePolFileName, &wavePolMat, NULL, errMessage, sizeof errMessage)) {
      printf ("Polar loaded   : %s\n", par.wavePolFileName);
   }
   else {
      fprintf (stderr, "In initContext, Error readPolar: %s\n", errMessage);
   }

   if (par.forbidFileName [0] != '\0') {
      if (readGeoJson(par.forbidFileName, forbidZones, MAX_N_FORBID_ZONE, &par.nForbidZone)) {
         printf ("Loaded         : %d forbid zones from: %s\n", par.nForbidZone, par.forbidFileName);
      }
      else {
         fprintf(stderr, "In initContext, Error reading geojson %s\n", par.forbidFileName);
      }
   }
   nIsoc = 0;
   route.n = 0;
   route.destinationReached = false;
   if (par.isSeaFileName [0] != '\0')
      readIsSea (par.isSeaFileName);
   updateIsSeaWithForbiddenAreas ();
   return true;
}

/*! store feedback information */
void handleFeedbackRequest (const char *fileName, const char *date, const char *clientIPAddress, const char *string) {
   FILE *file = fopen (fileName, "a");
   if (file == NULL) {
      fprintf (stderr, "handleFeedbackRequest, Error opening file: %s\n", fileName);
      return;
   }
   fprintf (file, "%s; %s; \n%s\n\n", date, clientIPAddress, string);
   fclose (file);
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
char *nearestPortToStrJson (double lat, double lon, char *out, size_t maxLen) {
   char selectedPort [MAX_SIZE_NAME];
   int idPort = nearestPort (lat, lon, par.tidesFileName, selectedPort, sizeof selectedPort);
   if (idPort != 0) {
      snprintf (out, maxLen, "{\n  \"nearestPort\": \"%s\",\n  \"idPort\": %d\n}\n", selectedPort, idPort);
   }
   else snprintf (out, maxLen, "{\"error\": \"Tide File %s not found\"}\n", par.tidesFileName);
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
char *listDirToStrJson (char *root, char *dir, bool sortByName, const char *pattern, const char **filter, char *out, size_t maxLen) {
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
      snprintf (line, sizeof line,"  [\"%s\", %lld, \"%s\"]%s\n",
                escaped ? escaped : "", (long long)arr[i].size, time_str, (i + 1 < n) ? "," : "");
      g_strlcat (out, line, maxLen);
      free(escaped);  
   }
   g_strlcat (out, "]\n", maxLen);

   for (size_t i = 0; i < n; i++) free(arr[i].name);
   free(arr);
   return out;
}

/*! decode request from client and fill ClientRequest structure 
   return true if correct false if impossible to decode */
bool decodeFormReq (const char *req, ClientRequest *clientReq) {
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
         if (! boatsCoords) {
            fprintf (stderr, "In decodeFormReq, Error parsing boats\n");
            return false;
         }
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
         if (! wpCoords) {
            fprintf (stderr, "In decodeFormReq, Error parsing waypoints\n");
            return false;
         }
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
      else if (sscanf (parts[i], "epochStart=%ld",  &clientReq->epochStart) == 1);              // time start extraction
      else if (sscanf (parts[i], "polar=%255s", clientReq->polarName) == 1);                    // polar name
      else if (sscanf (parts[i], "wavePolar=%255s", clientReq->wavePolName) == 1);              // wave polar name
      else if (sscanf (parts[i], "file=%255s",  clientReq->fileName) == 1);                     // file name
      else if (sscanf (parts[i], "model=%255s", clientReq->model) == 1);                        // grib model
      else if (sscanf (parts[i], "grib=%255s", clientReq->gribName) == 1);                      // grib name
      else if (sscanf (parts[i], "currentGrib=%255s", clientReq->currentGribName) == 1);        // current grib name may be empty
      else if (sscanf (parts[i], "dir=%255s",   clientReq->dirName) == 1);                      // directory name
      else if (g_str_has_prefix (parts[i], "dir=")) 
         g_strlcpy (clientReq->dirName, parts [i] + strlen ("dir="), sizeof clientReq->dirName); // defaulr empty works
      else if (g_str_has_prefix (parts[i], "feedback=")) 
         g_strlcpy (clientReq->feedback, parts [i] + strlen ("feedback="), sizeof clientReq->feedback);
      else if (g_str_has_prefix (parts[i], "onlyUV=true")) clientReq->onlyUV = true;            // Default false
      else if (g_str_has_prefix (parts[i], "onlyUV=false")) clientReq->onlyUV = false;          // Default false
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
      else fprintf (stderr, "In decodeFormReq Unknown value: %s\n", parts [i]);
   }
   g_strfreev (parts);
   return clientReq->type != -1;  
}

/*! Generate Json array for polygon */
static void polygonToJson (MyPolygon *po, char *str, size_t maxLen) {
   const int n = po->n;
   char temp [128];
   double lat, lon;

   strlcpy (str, "  [", maxLen);
   for (int k = 0; k < n; k++) {
      lat = (po->points [k].lat);
      lon = (po->points [k].lon);
      snprintf (temp, sizeof temp, "[%.4lf, %.4lf]%s", lat, lon, (k < n-1) ? ", " : "");
      strlcat (str,temp, maxLen); 
   }
   strlcat (str, "]", maxLen);
}

/*! Exclusion zone is an array of polygons */
void forbidToJson (char *res, size_t maxLen) {
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

static inline double round4(double x) {
   return round(x * 10000.0) / 10000.0;
}

/*!
 * update Grib file if required 
 */
bool updateWindGrib (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   char strGrib [MAX_SIZE_FILE_NAME];
   if (clientReq->gribName [0] == '\0') return false;
   // change grib if requested MAY TAKE TIME !!!!
   buildRootName (clientReq->gribName, strGrib, sizeof strGrib);
   printf ("Grib Found: %s\n", strGrib);
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
bool updateCurrentGrib (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
   char strGrib [MAX_SIZE_FILE_NAME];
   char directory [MAX_SIZE_DIR_NAME];
   if (! clientReq->withCurrent) return false;
   if (clientReq->currentGribName [0] == '\0') {
      snprintf (directory, sizeof directory, "%s%scurrentgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
      if (!mostRecentFile (directory, ".gr", "", clientReq->currentGribName, sizeof clientReq->currentGribName)) {
         snprintf (checkMessage, maxLen, "4: No current grib file");
         return false;
      };
   }

   buildRootName (clientReq->currentGribName, strGrib, sizeof strGrib);
   printf ("Current Grib Found: %s\n", strGrib);
   if (strncmp (par.currentGribFileName, strGrib, strlen (strGrib)) != 0) {
      printf ("current readGrib: %s\n", strGrib);
      if (readGribAll (strGrib, &currentZone, CURRENT)) {
         g_strlcpy (par.currentGribFileName, strGrib, sizeof par.currentGribFileName);
         printf ("Current Grib loaded   : %s\n", strGrib);
      }
      else {
         snprintf (checkMessage, maxLen, "4bis: Error reading Current Grib: %s", clientReq->currentGribName);
         return false;
      }
   }
   return true;
}

/*! 
 * information associated to coord (lat lon)
 * isSea, isSeaTolerant, grib wind and current
 */
void infoCoordToJson (double lat, double lon, ClientRequest *clientReq, char *res, size_t maxLen) {
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
      "  \"epochGribStart\": %ld,\n  \"grib\": \"%s\",\n  \"meteoArray\": [\n", 
      sea ? "true" : "false", seaTolerant ? "true": "false", wind ? "true": "false", current ? "true": "false", 
      epochGribStart, pFileName ? pFileName : ""); 
    
   for (int i = 0; i < tMax; i += 1) {
      findWindGrib (lat, lon, i, &u, &v, &g, &w, &twd, &tws);
      // snprintf (str, sizeof str, "    [%.4lf, %.4lf, %.4lf, %.4lf]%s\n", u, v, g, w, (i < tMax - 1) ? "," : ""); 
      snprintf(str, sizeof str, "    [%g, %g, %g, %g]%s\n", round4(u), round4(v), round4(g), round4(w), (i < tMax - 1) ? "," : "");
      strlcat (res, str, maxLen);
   }
   strlcat (res, "  ]\n}\n", maxLen);
}

/*! check validity of parameters */
bool checkParamAndUpdate (ClientRequest *clientReq, char *checkMessage, size_t maxLen) {
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
         if (readPolar (strPolar, &polMat, &sailPolMat, checkMessage, maxLen)) {
            g_strlcpy (par.polarFileName, strPolar, sizeof par.polarFileName);
            printf ("Polar loaded   : %s\n", strPolar);
         }
         else {
            snprintf (checkMessage, maxLen, "2: Error reading Polar: %s", clientReq->polarName);
            return false;
         }
      }  
   }
   if ((clientReq->wavePolName [0] != '\0') && (clientReq-> withWaves)) {
      buildRootName (clientReq->wavePolName, strPolar, sizeof strPolar);
      printf ("wave polar found: %s\n", strPolar);
      if (strncmp (par.wavePolFileName, strPolar, strlen (strPolar)) != 0) {
         printf ("read wave polar: %s\n", strPolar);
         if (readPolar (strPolar, &wavePolMat, NULL, checkMessage, maxLen)) {
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
      snprintf (directory, sizeof directory, "%s%sgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
      if (!mostRecentFile (directory, ".gr", clientReq->model, clientReq->gribName, sizeof clientReq->gribName)) {
         snprintf (checkMessage, maxLen, "0: No grib file with model: %s", clientReq->model);
         return false;
      };
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
      if (! isInZone (clientReq->wp [i].lat , clientReq->wp [i].lon , &zone) && (par.constWindTws == 0.0)) {
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
   
   if ((par.constWindTws == 0.0) && ((par.startTimeInHours < 0) || (par.startTimeInHours > zone.timeStamp [zone.nTimeStamp -1]))) {
         snprintf (checkMessage, maxLen, "9: start Time not in Grib time window");
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

/*! generate json description of isochrones. Concatenate it to res */
static char *isochronesToStrCatJson (char *res, size_t maxLen) {
   Pp pt;
   char str [10000];
   int index;
   char *savePt = res + strlen (res);
   g_strlcat (res, ",\n\"_isoc\": [\n", maxLen);
   Pp *newIsoc = NULL; // array of points
   if ((newIsoc = malloc (MAX_SIZE_ISOC * sizeof(Pp))) == NULL) {
      fprintf (stderr, "In isochronesToStrJson: error in memory newIsoc allocation\n");
      return NULL;
   }
   
   for (int i = 0; i < nIsoc; i += 1) {
      g_strlcat (res, "  [\n", maxLen);
      index = (isoDesc [i].size <= 1) ?  0 : isoDesc [i].first;
      for (int j = 0; j < isoDesc [i].size; j++) {
         newIsoc [j] = isocArray [i * MAX_SIZE_ISOC + index];
         index += 1;
         if (index == isoDesc [i].size) index = 0;
      }
      const int max = isoDesc [i].size;
      for (int k = 0; k < max; k++) {
         pt = newIsoc [k];
         snprintf (str, sizeof str, "    [%.6lf, %.6lf, %d, %d, %d]%s\n", 
            pt.lat, pt.lon, pt.id, pt.father, k, (k < max - 1) ? ","  : ""); 
         g_strlcat (res, str, maxLen);
      }
      snprintf (str, sizeof str,  "  ]%s\n", (i < nIsoc -1) ? "," : ""); // no comma for last value 
      g_strlcat (res, str, maxLen);
   }
   if (strlen (res) >= (maxLen - sizeof str - 2)) {// too big
      res = savePt;
      *res = '\0';
      strlcat (res, ",\n\"_Warning_1\": \"No isochrone sent because too big!\"\n", maxLen); // no isochrone if too big !
   }
   else g_strlcat (res, "]", maxLen); 
   free (newIsoc);
   return res;
}

/*! generate json description of isochrones decriptor. Conatenate te description to res */
static char *isoDescToStrCatJson (char *res, size_t maxLen) {
   char str [10000];
   char *savePt = res + strlen (res);
   g_strlcat (res, ",\n\"_isodesc\": [\n", maxLen);

   for (int i = 0; i < nIsoc; i++) {
      //double distance = isoDesc [i].distance;
      //if (distance >= DBL_MAX) distance = -1;
      snprintf (str, sizeof str, "  [%d, %d, %d, %d, %d, %.2lf, %.2lf, %.6lf, %.6lf]%s\n",
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
   else g_strlcat (res, "]", maxLen); 
   return res;
}

/*! generate json description of track boats */
char  *routeToJson (SailRoute *route, bool isoc, bool isoDesc, char *res, size_t maxLen) {
   char str [MAX_SIZE_MESS] = "";
   char strSail [MAX_SIZE_NAME] = "";
   double twa = 0.0, hdg = 0.0, twd = 0.0;
   int nSeg = -1; // for checkRoute
   if (route->n <= 0) return NULL;

   int iComp = (route->competitorIndex < 0) ? 0 : route->competitorIndex;
   char *gribBaseName = g_path_get_basename (par.gribFileName);
   char *gribCurrentBaseName = g_path_get_basename (par.currentGribFileName);
   char *polarBaseName = g_path_get_basename (par.polarFileName);
   char *wavePolarBaseName = g_path_get_basename (par.wavePolFileName);

   snprintf (res, maxLen, 
      "{\n" 
      "\"%s\": {\n  \"duration\": %d,\n"
      "  \"totDist\": %.2lf,\n"
      "  \"epochStart\": %ld,\n"
      "  \"routingRet\": %d,\n"
      "  \"isocTimeStep\": %.2lf,\n"
      "  \"calculationTime\": %.4lf,\n"
      "  \"destinationReached\": %s,\n"
      "  \"lastPointInfo\": %s,\n"
      "  \"lastStepDuration\": [",
      competitors.t[iComp].name, 
      (int) (route->duration * 3600), 
      route->totDist,
      clientReq.epochStart,
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
   const bool waves = isPresentGrib(&zone,"swh") && par.withWaves;
   snprintf (str, sizeof str,
      "%.4f],\n"
      "  \"motorDist\": %.2lf, \"starboardDist\": %.2lf, \"portDist\": %.2lf,\n"
      "  \"nSailChange\": %d, \"nAmureChange\": %d,\n" 
      "  \"bottomLat\": %.2lf, \"leftLon\": %.2lf, \"topLat\": %.2lf, \"rightLon\": %.2lf,\n"
      "  \"polar\": \"%s\",\n"
      "  \"wavePolar\": \"%s\",\n"
      "  \"grib\": \"%s\",\n"
      "  \"currentGrib\": \"%s\",\n"
      "  \"track\": [\n",
      route->lastStepDuration * 3600.0,
      route->motorDist, route->tribordDist, route->babordDist,
      route->nSailChange, route->nAmureChange,
      zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight,
      polarBaseName, 
      (waves) ? wavePolarBaseName : "",  
      gribBaseName, 
      (par.withCurrent) ? gribCurrentBaseName : ""
   );
   g_strlcat (res, str, maxLen);
   free(polarBaseName);
   free(wavePolarBaseName);
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
         "    [%d, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, %.6f, \"%s\", %s, %d, %d]%s\n", 
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

   g_strlcat (res, "  ]\n}", maxLen);

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
   if (isoc) {
      isochronesToStrCatJson (res, maxLen - 2); // concat in res the isochrones
   }
   g_strlcat (res, "\n}\n", maxLen);
   return res;
}

