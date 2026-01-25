/*! compilation: gcc -c r3util.c */
#define MAX_N_SHIP_TYPE 2       // for Virtual Regatta Stamina calculation
#include <float.h>   
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <math.h>
#include <locale.h>
#include "glibwrapper.h"
#include "r3types.h"
#include "grib.h"
#include "inline.h"

/* For virtual regatta Stamina calculation */
struct {
   char name [MAX_SIZE_NAME];
   double cShip;  
   double tMin [3];
   double tMax [3];
} shipParam [MAX_N_SHIP_TYPE] = {
   {"Imoca", 1.2, {300, 300, 420}, {660, 660, 600}}, 
   {"Normal", 1.0, {300, 300, 336}, {660, 660, 480}}
};

/*! forbid ones is a set of polygons */
MyPolygon forbidZones [MAX_N_FORBID_ZONE];

/*! dictionnary of meteo services */
const struct MeteoElmt meteoTab [N_METEO_ADMIN] = {{7, "Weather service US"}, {78, "DWD Germany"}, {85, "Meteo France"}, {98,"ECMWF European"}};

/*! sail attributes */
const size_t sailNameSize = 7;
const char *sailName [] = {"C0", "HG", "Jib", "LG", "LJ", "Spi", "SS"}; // for sail polars
const char *colorStr [] = {"green", "purple", "gray", "blue", "yellow", "orange", "red"};

/*! list of wayPoint */
WayPointList wayPoints;

/*! list of competitors */
CompetitorsList competitors;

/*! polar matrix description */
PolMat polMat;

/*! polar matrix for sails */
PolMat sailPolMat;

/*! polar matrix for waves */
PolMat wavePolMat;

/*! parameter */
Par par;

/*! table describing if sea or earth */
char *tIsSea = NULL; 

/*! geographic zone covered by grib file */
Zone zone;                             // wind
Zone currentZone;                      // current

/*! Check if a file exists and is non-empty */
bool fileExists (const char *filename) {
   struct stat buffer;
   return (stat (filename, &buffer) == 0 && buffer.st_size > 0);
}

/*! wipe all spaces within str */
void wipeSpace(char *str) {
   if (!str) return;
   char *src = str, *dst = str;
   while (*src) {
      if (!isspace((unsigned char)*src)) *dst++ = *src;
      src++;
   }
   *dst = '\0';
}

/*! replace multiple spaces by just one */
void normalizeSpaces(char *s) {
   char *src = s;
   char *dst = s;
   bool inSpace = false;

   while (*src) {
      if (*src == ' ') {
         if (!inSpace) {
            *dst++ = ' ';
            inSpace = true;
         }
      } else {
         *dst++ = *src;
         inSpace = false;
      }
      src++;
   }
   *dst = '\0';
}

/*! formatted float print with simplified 0 */
void printFloat(char *buf, size_t len, double v) {
   const double epsilon = 0.00005;  // half 10⁻⁴ → consistent with %.4f 
   if (fabs(v) < epsilon) strlcpy(buf, "0", len);
   else snprintf(buf, len, "%.4f", v);
}

/*! translate time nnox in dataDate, datatime grib 
 *  Minutes are allways 00. Hours are 00, 06, 12 or 18
 */
static void gribDateTimeNowUtc(long *dataDate, long *dataTime) {
   time_t now = time(NULL);
   struct tm t;
   gmtime_r(&now, &t);               // UTC
   int hour = t.tm_hour;             // 0..23
   t.tm_hour = (hour / 6) * 6;       // floor to 00,06,12,18
   t.tm_min  = 0; // HH = 00
   t.tm_sec  = 0; // Sec = 0

   // Recompose Grib dates
   *dataDate = (long)((t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + (t.tm_mday));
   *dataTime = (long)(t.tm_hour * 100);   // minutes = 00
}

/*! reinit zone describing geographic meta data */
void initZone (Zone *zone) {
   char buffer [1000000];
   const int tInterval = 3;
   const int nbDays = 16;
   gribToStr (zone, buffer, MAX_SIZE_BUFFER);
   printf ("%s\n", buffer);
   memset(zone, 0, sizeof(*zone));
   zone->editionNumber = 2;
   zone->centreId = 7;     // GFS NOAA
   zone->stepUnits = 1;    // hours
   zone->wellDefined = true;
   zone->allTimeStepOK = true;
   zone->latMin = -85.0;
   zone->latMax = 85.0;
   zone->lonRight = -179.0;
   zone->lonLeft = 180.0;
   zone->latStep = 1.00;
   zone->lonStep = 1.0;
   
   zone->nbLat = zone->latMax - zone->latMin + 1;
   zone->nbLon = zone->lonLeft - zone->lonRight + 1;
   zone->nDataDate = 1;
   zone->nDataTime = 1;
   gribDateTimeNowUtc(&zone->dataDate[0], &zone->dataTime[0]);

   zone->nShortName = 2;
   strncpy(zone->shortName[0], "10u", 4);
   strncpy(zone->shortName[1], "10v", 4);
   zone->nTimeStamp = (int ) (24 * nbDays / tInterval);
   zone->intervalBegin = tInterval;
   zone->intervalEnd = tInterval;
   for (size_t i = 0; i < zone->nTimeStamp; i += 1) {
      zone->timeStamp [i] = tInterval * i;
   }
   zone->nMessage = zone->nShortName * zone->nTimeStamp;
   zone->numberOfValues = zone->nbLat * zone->nbLon;
   gribToStr (zone, buffer, MAX_SIZE_BUFFER);
   printf ("%s\n", buffer);
}

/*! return the name of the sail */
char *fSailName (int val, char *str, size_t maxLen) {
   if (val > 0 && val <= (int) polMat.nSail) strlcpy (str, polMat.tSail[val-1].name, maxLen);
   else strlcpy (str, "--", maxLen);
   return str;
}

/*! replace former suffix (after last dot) by suffix 
   example : "pol/bibi.toto.csv" with suffix "sailpol" will give: "pol/bibi.toto.sailpol" */
char *newFileNameSuffix (const char *fileName, const char *suffix, char *newFileName, size_t maxLen) {
   const char *lastDot = strrchr(fileName, '.'); // find last point position
   const size_t baseLen = (lastDot == NULL) ? strlen (fileName) : (size_t) (lastDot - fileName); // lenght of base

   // check resulting lenght is in maxLen
   if (baseLen + strlen (suffix) + 1 >= maxLen) { // for the  '.'
      return NULL; // not enough space
   }
   strlcpy (newFileName, fileName, baseLen + 1);
   strlcat (newFileName, ".", maxLen);
   strlcat (newFileName, suffix, maxLen);

   return newFileName;
}

/*! return true if str is empty */
bool isEmpty (const char *str) {
   if (str == NULL) return true;
   while (*str) {
      if (!g_ascii_isspace(*str)) return false;
      str++;
   }
   return true;
}

/*! format big number with thousand sep. Example 1000000 formated as 1 000 000 */
char *formatThousandSep (char *buffer, size_t maxLen, long value) {
   char str [MAX_SIZE_LINE];
   snprintf (str, MAX_SIZE_LINE, "%ld", value);
   const int length = strlen (str);
   const int nSep = length / 3;
   const int mod = length % 3;
   int i = 0, j = 0;
   for (i = 0; i < mod; i++)
      buffer [j++] = str [i];
   if ((nSep > 0) && (mod > 0)) buffer [j++] = ' '; 
   for (int k = 0; k < nSep; k++) {
      buffer [j++] = str [i++];
      buffer [j++] = str [i++];
      buffer [j++] = str [i++];
      buffer [j++] = ' ';
      if (j >= (int) maxLen) {
         j = maxLen - 1;
         break;
      }
   }
   buffer [j] = '\0';
   if (buffer [j - 1] == ' ') buffer [j - 1] = '\0';
   return buffer;
}

/*! true if name terminates with slash */
bool hasSlash(const char *name) {
   if (!name || !*name) return false;
   const size_t len = strlen(name);         
   return name[len - 1] == '/';
}

/*! select most recent file in "directory" that contains "pattern0" and "pattern1" in name 
  return true if found with name of selected file */
bool mostRecentFile (const char *directory, const char *pattern0, const char *pattern1, char *name, size_t maxLen) {
   DIR *dir = opendir (directory);
   if (dir == NULL) {
      fprintf (stderr, "In mostRecentFile, Error opening: %s\n", directory);
      return false;
   }

   struct dirent *entry;
   struct stat statbuf;
   time_t latestTime = 0;
   char filepath [MAX_SIZE_DIR_NAME + MAX_SIZE_FILE_NAME];

   while ((entry = readdir(dir)) != NULL) {
      snprintf (filepath, sizeof (filepath), "%s%s%s", directory, hasSlash(directory) ? "" : "/", entry->d_name);
      if ((strstr (entry->d_name, pattern0) != NULL) 
         && (strstr (entry->d_name, pattern1) != NULL)
         && (stat(filepath, &statbuf) == 0) 
         && (S_ISREG(statbuf.st_mode) 
         && (statbuf.st_size > 0)  // select file only if not empty
         && statbuf.st_mtime > latestTime)) {

         latestTime = statbuf.st_mtime;
         if (strlen (entry->d_name) < maxLen)
            snprintf (name, maxLen, "%s/%s", directory, entry->d_name);
         else {
            fprintf (stderr, "In mostRecentFile, Error File name:%s size is: %zu and exceed Max Size: %zu\n", \
               entry->d_name, strlen (entry->d_name), maxLen);
            break;
         }
      }
   }
   closedir(dir);
   return (latestTime > 0);
}

/*! true if name contains a number */
bool isNumber (const char *name) {
   return (name != NULL) && (strpbrk (name, "0123456789") != NULL);
}

/*! translate str in double for latitude longitude */
double getCoord (const char *str, double minLimit, double maxLimit) {
   if (str == NULL) return 0.0;
   double deg = 0.0, min = 0.0, sec = 0.0;
   bool minFound = false;
   const char *neg = "SsWwOo";            // value returned is negative if south or West
   int sign = (strpbrk (str, neg) == NULL) ? 1 : -1; // -1 if neg found
   char *pt = NULL;

   // find degrees
   while (*str && (! (isdigit (*str) || (*str == '-') || (*str == '+')))) 
      str++;
   deg  = strtod (str, NULL);
   if (deg < 0) sign = -1;                // force sign if negative value found 
   deg = fabs (deg);
   // find minutes
   if ((strchr (str, '\'') != NULL)) {
      minFound = true;
      if ((pt = strstr (str, "°")) != NULL) pt += 2;// degree ° is UTF8 coded with 2 bytes
      else 
         if ((pt = strpbrk (str, neg)) != NULL) pt += 1;
            
      if (pt != NULL) min = strtod (pt, NULL);
   }
   // find seconds
   if (minFound && (strchr (str, '"') != NULL)) {
      sec = strtod (strchr (str, '\'') + 1, NULL);
   }
   return CLAMP (sign * (deg + min/60.0 + sec/3600.0), minLimit, maxLimit);
}

/*! Build root name if not already a root name.
 * Combines the working directory with the file name if it is not absolute.
 * Stores the result in `rootName` and ensures it does not exceed `maxLen`.
 *
 * @param fileName  The name of the file (absolute or relative).
 * @param rootName  The buffer to store the resulting root name.
 * @param maxLen    The maximum length of the buffer `rootName`.
 * @return          A pointer to `rootName`, or NULL on error.
 */
/*char *buildRootName (const char *fileName, char *rootName, size_t maxLen) {
   const char *workingDir = par.workingDir[0] != '\0' ? par.workingDir : WORKING_DIR;
   char *fullPath = (g_path_is_absolute (fileName)) ? g_strdup (fileName) : g_build_filename (workingDir, fileName, NULL);
   if (!fullPath) return NULL;
   strlcpy (rootName, fullPath, maxLen);
   g_free (fullPath);
   return rootName;
}
*/
/*! abolute path POSIX : begin with '/' */ 
char *buildRootName(const char *fileName, char *rootName, size_t maxLen) {
   if (!fileName || !rootName || maxLen == 0) return NULL;
   const char *workingDir = (par.workingDir[0] != '\0') ? par.workingDir : WORKING_DIR;
   int n;

   if (fileName [0] == '/') n = snprintf(rootName, maxLen, "%s", fileName);
   else n = snprintf(rootName, maxLen, "%s%s%s", workingDir, hasSlash (workingDir) ? "" : "/", fileName);

   if (n < 0 || (size_t)n >= maxLen) {
      // overflow or writing error
      if (maxLen) rootName[0] = '\0';
      return NULL;
   }
   return rootName;
}

/*! convert epoch time to string with or without seconds */
char *epochToStr (time_t t, bool seconds, char *str, size_t maxLen) {
   struct tm *utc = gmtime (&t);
   if (!utc) return NULL;
   if (seconds) {
      snprintf (str, maxLen, "%04d-%02d-%02d %02d:%02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min, utc->tm_sec);
   }
   else {
      snprintf (str, maxLen, "%04d-%02d-%02d %02d:%02d", 
         utc->tm_year + 1900, utc->tm_mon + 1, utc->tm_mday,
         utc->tm_hour, utc->tm_min);
   }
   return str;
}

/*! return offset Local UTC in seconds */
double offsetLocalUTC (void) {
   time_t now;
   time (&now);                           
   struct tm localTm = *localtime (&now);
   struct tm utcTm = *gmtime (&now);
   time_t localTime = mktime (&localTm);
   time_t utcTime = mktime (&utcTm);
   double offsetSeconds = difftime (localTime, utcTime);
   if (localTm.tm_isdst > 0) offsetSeconds += 3600.0;  // add one hour if summer time
   return offsetSeconds;
}

/*! return str representing grib date */
char *gribDateTimeToStr (long date, long time, char *str, size_t maxLen) {
   const int year = date / 10000;
   const int mon = (date % 10000) / 100;
   const int day = date % 100;
   const int hour = time / 100;
   const int min = time % 100;
   snprintf (str, maxLen, "%04d/%02d/%02d %02d:%02d", year, mon, day, hour, min);
   return str;
}

/*! return tm struct equivalent to date hours found in grib (UTC time) 
struct tm gribDateToTm (long intDate, double nHours) {
   struct tm tm0 = {0};
   tm0.tm_year = (intDate / 10000) - 1900;
   tm0.tm_mon = ((intDate % 10000) / 100) - 1;
   tm0.tm_mday = intDate % 100;
   tm0.tm_isdst = -1;                     
   const int totalMinutes = (int)(nHours * 60);
   tm0.tm_min += totalMinutes;            // adjust tm0 struct (manage day, mon year overflow)
   mktime (&tm0);  
   return tm0;
}*/
struct tm gribDateToTm(long intDate, double nHours) {
   struct tm tm0 = {0};

   tm0.tm_year = (int)(intDate / 10000) - 1900;
   tm0.tm_mon  = (int)((intDate % 10000) / 100) - 1;
   tm0.tm_mday = (int)(intDate % 100);

   int totalMinutes = (int)lround(nHours * 60.0);
   int days         = totalMinutes / (24 * 60);
   int minutesDay   = totalMinutes % (24 * 60);

   tm0.tm_mday += days;
   tm0.tm_hour  = minutesDay / 60;
   tm0.tm_min   = minutesDay % 60;
   tm0.tm_isdst = 0;  // UTC → no DST

   // Normalize pure UTC pur
   time_t t = timegm(&tm0);
   struct tm out;
   gmtime_r(&t, &out);
   return out;
}

/*! retrurn true if year is bissextile  */
static inline bool bissextile (int year) { /* */
   return (((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0));
}

/*! return year, month (1..12), day, hour, minutes from Grib dataDate and hours */
static bool getYMDHM (long dataDate, double nHours, int *yyyy, int *mm, int *dd, int *hh, int *min) {
   if (nHours < 0.0) {
      fprintf(stderr, "getYMDHM: nHours must be >= 0 (%.3f)\n", nHours);
      return false;
   }
   static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

   int year  = (int)(dataDate / 10000);
   int month = (int)((dataDate % 10000) / 100) - 1;  // 0..11
   int day   = (int)(dataDate % 100);

   long totalMinutes = lround(nHours * 60.0);
   int addDays   = (int)(totalMinutes / (60 * 24));
   int hour  = (int)((totalMinutes / 60) % 24);
   int minute   = (int)(totalMinutes % 60);

   day  += addDays;

   if (minute >= 60) {
      hour += minute / 60;
      minute = minute % 60;
   }
   if (hour >= 24) {
      day += hour / 24;
      hour = hour % 24;
   }

   for (;;) {
      int dim = daysInMonth[month];
      if (month == 1 && bissextile(year)) dim += 1;
      if (day <= dim) break;
      day -= dim;
      month++;
      if (month >= 12) {
         month = 0;
         year++;
      }
   }
   *yyyy = year;
   *mm   = month + 1;  // 1..12 for caller
   *dd   = day;
   *hh   = hour;
   *min  = minute;
   return true;
}


/*! return date and time using ISO notation after adding myTime (hours) to the Date */
char *otherNewDate (long dataDate, double nHours, char *res, size_t maxLen) {
   //struct tm tm0 = gribDateToTm (intDate, nHours);
   int yyyy = 0, mm = 0, dd = 0 , hh = 0, min = 0;
   getYMDHM (dataDate, nHours, &yyyy, &mm, &dd, &hh, &min);
   snprintf (res, maxLen, "%4d-%02d-%02d %02d:%02d", yyyy, mm, dd, hh, min);
   return res;
}


/*! return date and time using ISO notation after adding myTime (hours) to the Date */
char *newDate (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = gribDateToTm (intDate, nHours);
   snprintf (res, maxLen, "%4d-%02d-%02d %02d:%02d", tm0.tm_year + 1900, tm0.tm_mon + 1, tm0.tm_mday,\
      tm0.tm_hour, tm0.tm_min);
   return res;
}

/*! return date and time using week day after adding myTime (hours) to the Date */
char *newDateWeekDay (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = gribDateToTm (intDate, nHours);
   strftime (res, maxLen, "%a %H:%M", &tm0);
   return res;
}

/*! return date and time using week day after adding myTime (hours) to the Date */
char *newDateWeekDayVerbose (long intDate, double nHours, char *res, size_t maxLen) {
   struct tm tm0 = gribDateToTm (intDate, nHours);
   strftime (res, maxLen, "%A, %b %d at %H:%M UTC", &tm0);
   return res;
}

/*! convert long date/time from GRIB to time_t (UTC, via timegm) */
time_t gribDateTimeToEpoch(long date, long hhmm) {
   struct tm tm_utc = {0}; // UTC time with tm_utc.tm_isdst = 0;
   tm_utc.tm_year = (int)(date / 10000) - 1900;          // YYYY -> tm_year
   tm_utc.tm_mon  = (int)((date % 10000) / 100) - 1;     // MM   -> 0..11
   tm_utc.tm_mday = (int)(date % 100);                   // DD
   tm_utc.tm_hour = (int)(hhmm / 100);                   // HH
   tm_utc.tm_min  = (int)(hhmm % 100);                   // MM

   return timegm(&tm_utc);
}

/*! calculate difference in hours between departure time in UTC and time 0 */
double getDepartureTimeInHour (struct tm *startUtc) {
   const time_t t0 = gribDateTimeToEpoch(zone.dataDate[0], zone.dataTime[0]);
   if (t0 == (time_t)-1) return NAN;
   const time_t tStart = timegm(startUtc);   // interprétation en UTC
   if (tStart == (time_t)-1) return NAN;
   return difftime(tStart, t0) / 3600.0;
}

/*! convert hours in string with days, hours, minutes */
char *durationToStr (double duration, char *res, size_t maxLen) {
   const int nDays = duration / 24;
   const int nHours = fmod (duration, 24.0);
   const int nMin = 60 * fmod (duration, 1.0);
   if (nDays == 0) snprintf (res, maxLen, "%02d:%02d", nHours, nMin); 
   else snprintf (res, maxLen, "%d Days %02d:%02d", nDays, nHours, nMin);
   // printf ("Duration: %.2lf hours, equivalent to %d days, %02d:%02d\n", duration, nDays, nHours, nMin);
   return res;
} 

/*! convert lat to str according to type */
char *latToStr (double lat, int type, char* str, size_t maxLen) {
   const double mn = 60 * lat - 60 * (int) lat;
   const double sec = 3600 * lat - (3600 * (int) lat) - (60 * (int) mn);
   const char c = (lat > 0) ? 'N' : 'S';

   if (lat > 90.0 || lat < -90.0) {
      strlcpy (str, "Lat Error", maxLen);
      return str;
   }
   switch (type) {
   case BASIC: snprintf (str, maxLen, "%.2lf°", lat); break;
   case DD: snprintf (str, maxLen, "%06.2lf°%c", fabs (lat), c); break;
   case DM: snprintf (str, maxLen, "%02d°%05.2lf'%c", (int) fabs (lat), fabs(mn), c); break;
   case DMS: 
      snprintf (str, maxLen, "%02d°%02d'%02.0lf\"%c", (int) fabs (lat), (int) fabs(mn), fabs(sec), c);
      break;
   default:;
   }
   return str;
}

/*! convert lon to str according to type */
char *lonToStr (double lon, int type, char *str, size_t maxLen) {
   const double mn = 60 * lon - 60 * (int) lon;
   const double sec = 3600 * lon - (3600 * (int) lon) - (60 * (int) mn);
   const char c = (lon > 0) ? 'E' : 'W';

   if (lon > 180.0 || lon < -180.0) {
      strlcpy (str, "Lon Error", maxLen);
      return str;
   }
   switch (type) {
   case BASIC: snprintf (str, maxLen, "%.2lf°", lon); break;
   case DD: snprintf (str, maxLen, "%06.2lf°%c", fabs (lon), c); break;
   case DM: snprintf (str, maxLen, "%03d°%05.2lf'%c", (int) fabs (lon), fabs(mn), c); break;
   case DMS:
      snprintf (str, maxLen, "%03d°%02d'%02.0lf\"%c", (int) fabs (lon), (int) fabs(mn), fabs(sec), c);
      break;
   default:;
   }
   return str;
}

/*! read issea file and fill table tIsSea. Return pointer*/
char *readIsSea (const char *fileName) {
   FILE *f = NULL;
   int i = 0;
   char c;
   char *tIsSea = NULL;
   int nSea = 0;
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In readIsSea, Error cannot open: %s\n", fileName);
      return NULL;
   }
	 if ((tIsSea = (char *) malloc (SIZE_T_IS_SEA + 1)) == NULL) {
		 fprintf (stderr, "In readIsSea, error Malloc");
     fclose (f);
		 return NULL;
	 }

   while (((c = fgetc (f)) != -1) && (i < SIZE_T_IS_SEA)) {
      if (c == '1') nSea += 1;
      tIsSea [i] = c - '0';
      i += 1;
   }
   fclose (f);
   // printf ("isSea file     : %s, Size: %d, nIsea: %d, Proportion sea: %lf\n", fileName, i, nSea, (double) nSea/ (double) i); 
   return tIsSea;
} 

/*! fill str with polygon information */
/*! return true if p is in polygon po
  Ray casting algorithm */ 
static bool isInPolygon (double lat, double lon, const MyPolygon *po) {
   int i, j;
   bool inside = false;
   // printf ("isInPolygon lat:%.2lf lon: %.2lf\n", p.lat, p.lon);
   for (i = 0, j = po->n - 1; i < po->n; j = i++) {
      if ((po->points[i].lat > lat) != (po->points[j].lat > lat) &&
         (lon < (po->points[j].lon - po->points[i].lon) * (lat - po->points[i].lat) / (po->points[j].lat - po->points[i].lat) + po->points[i].lon)) {
         inside = !inside;
      }
   }
   return inside;
}

/*! return true if p is in forbid area */ 
static bool isInForbidArea (double lat, double lon) {
   for (int i = 0; i < par.nForbidZone; i++) {
      if (isInPolygon (lat, lon, &forbidZones [i])) return true;
   }
   return false;
}

/*! complement according to forbidden areas */
void updateIsSeaWithForbiddenAreas (void) {
   if (tIsSea == NULL) return;
   if (par.nForbidZone <= 0) return;
   for (int i = 0; i < SIZE_T_IS_SEA; i++) {
      const double lon = (i % 3601) / 10.0 - 180.0;
      const double lat = 90.0 - (i / (3601.0 * 10.0));
      if (isInForbidArea (lat, lon)) tIsSea [i] = 0;
   }
}

/*! read forbid zone format
   [[lat, lon], [lat, lon] ...]
...*/
static void forbidZoneAdd (char *line, int n) {
   int idx = 0;
   char *latToken, *lonToken;
   // printf ("Forbid Zone    : %d\n", n);
   Point *temp = (Point *) realloc (forbidZones [n].points, MAX_SIZE_FORBID_ZONE * sizeof(Point));
   if (temp == NULL) {
      fprintf (stderr, "In forbidZoneAdd, Error realloc with n = %d\n", n);
      return;
   }
   forbidZones [n].points = temp;
   
   if (((latToken = strtok (line, ",")) == NULL) || ((lonToken = strtok (NULL, "]")) == NULL)) return;
   forbidZones [n].points[idx].lat = getCoord (latToken, MIN_LAT, MAX_LAT);  
   forbidZones [n].points[idx].lon = getCoord (lonToken, MIN_LON, MAX_LON);
   idx = 1;
   while ((idx < MAX_SIZE_FORBID_ZONE) && ((latToken = strtok (NULL, ",")) != NULL) && ((lonToken = strtok (NULL, "]")) != NULL)) {
      // printf ("latToken: %s, lonToken: %s\n", latToken, lonToken);
      forbidZones [n].points[idx].lat = getCoord (latToken, MIN_LAT, MAX_LAT);  
      forbidZones [n].points[idx].lon = getCoord (lonToken, MIN_LON, MAX_LON);
      idx += 1;
   }
   forbidZones [n].n = idx;
}

/*! read parameter file and build par struct */
bool readParam (const char *fileName, bool initDisp) {
   bool inWp = false, inForbidZone = false, inCompetitor = false;
   FILE *f = NULL;
   char *pt = NULL;
   char str [MAX_SIZE_LINE], strLat [MAX_SIZE_NAME] = "", strLon [MAX_SIZE_NAME] = "";
   char buffer [MAX_SIZE_TEXT];
   char *pLine = &buffer [0];
   memset (&par, 0, sizeof (Par));
   par.opt = 1;
   par.tStep = 1.0;
   par.cogStep = 5;
   par.rangeCog = 90;
   par.dayEfficiency = 1;
   par.nightEfficiency = 1;
   par.kFactor = 1;
   par.jFactor = 300;
   par.nSectors = MAX_N_SECTORS;
   if (initDisp) {
      par.style = 1;
      par.showColors =2;
      par.dispDms = 2;
      par.windDisp = 1;
      par.stepIsocDisp = 1;
   }
   par.xWind = 1.0;
   par.maxWind = 50.0;
   par.staminaVR = 100.0;
   wayPoints.n = 0;
   wayPoints.totOrthoDist = 0.0;
   wayPoints.totLoxoDist = 0.0;
   competitors.n = 0;
   competitors.runIndex = -1;
   
   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In readParam, Error Cannot open: %s\n", fileName);
      return false;
   }

   while (fgets (pLine, sizeof (buffer), f) != NULL ) {
      str [0] = '\0';
      g_strstrip (pLine);
      // printf ("%s", pLine);
      if ((!*pLine) || *pLine == '#' || *pLine == '\n') continue;
      if ((pt = strchr (pLine, '#')) != NULL)                    // comment elimination
         *pt = '\0';
      if (strncmp (pLine, "-", 1) != 0) inWp = inForbidZone = inCompetitor = false;

      if (strncmp (pLine, "---", 3) == 0) {}                     // ignore for  yaml compatibility
      // if (strncmp (pLine, "...", 3) == 0) break;                 // end for yaml compatibility
      else if (sscanf (pLine, "DESC:%255[^\n]", par.description) > 0)
         g_strstrip (par.description);
      else if (sscanf (pLine, "ALLWAYS_SEA:%d", &par.allwaysSea) > 0);
      else if (sscanf (pLine, "WD:%255s", par.workingDir) > 0);  // should be first !!!
      else if (sscanf (pLine, "POI:%255s", str) > 0) 
         buildRootName (str, par.poiFileName, sizeof (par.poiFileName));
      else if (sscanf (pLine, "PORT:%255s", str) > 0)
         buildRootName (str, par.portFileName, sizeof (par.portFileName));

      else if (sscanf(pLine, "POR: [%63[^,], %63[^]]", strLat, strLon) == 2) { // POR: [lat, lon]
         par.pOr.lat = getCoord (strLat, MIN_LAT, MAX_LAT);
         par.pOr.lon = getCoord (strLon, MIN_LON, MAX_LON);
         par.pOr.id = par.pOr.father = -1;
      }

      else if (sscanf(pLine, "PDEST: [%63[^,], %63[^]]", strLat, strLon) == 2) { // PDEST: [lat, lon]
         par.pDest.lat = getCoord (strLat, MIN_LAT, MAX_LAT);
         par.pDest.lon = getCoord (strLon, MIN_LON, MAX_LON);
         par.pDest.id = par.pDest.father = 0;
      }

      else if  (strstr (pLine, "WP:") != NULL) inWp = true;  // format is WP: then -
      else if (inWp && (sscanf (pLine, "- [%63[^,], %63[^]]", strLat, strLon) == 2)) { // - [lat, lon]
         if (wayPoints.n > MAX_N_WAY_POINT)
            fprintf (stderr, "In readParam, Error: number of wayPoints exceeded; %d\n", MAX_N_WAY_POINT);
         else {
            wayPoints.t [wayPoints.n].lat = getCoord (strLat, MIN_LAT, MAX_LAT);
            wayPoints.t [wayPoints.n].lon = getCoord (strLon, MIN_LAT, MAX_LAT);
            wayPoints.n += 1;
         }
      }
      else if  (strstr (pLine, "COMPETITORS:") != NULL) inCompetitor = true;   // format is COMPETITORS: then -
      else if (inCompetitor && 
               (sscanf (pLine, "- [%d, %63[^,], %63[^,], %63[^]]",             // - [color, lat, lon, name]
                    &competitors.t [competitors.n].colorIndex, strLat, strLon, competitors.t [competitors.n].name) == 4)
              ) {
         if (competitors.n > MAX_N_COMPETITORS)
            fprintf (stderr, "In readParam, Error number of competitors exceeded; %d\n", MAX_N_COMPETITORS);
         else {
            g_strstrip (competitors.t [competitors.n].name);
            competitors.t [competitors.n].lat = getCoord (strLat, MIN_LAT, MAX_LAT);
            competitors.t [competitors.n].lon = getCoord (strLon, MIN_LAT, MAX_LAT);
            if (competitors.n == 0) {
               par.pOr.lat = competitors.t [0].lat;
               par.pOr.lon = competitors.t [0].lon;
            }
            competitors.n += 1;
         }
      }

      else if (sscanf (pLine, "POR_NAME:%255s", par.pOrName) > 0);
      else if (sscanf (pLine, "PDEST_NAME:%255s", par.pDestName) > 0);
      else if (sscanf (pLine, "GRIB_RESOLUTION:%lf", &par.gribResolution) > 0);
      else if (sscanf (pLine, "GRIB_TIME_STEP:%d", &par.gribTimeStep) > 0);
      else if (sscanf (pLine, "GRIB_TIME_MAX:%d", &par.gribTimeMax) > 0);
      else if (sscanf (pLine, "MARKS:%255s", str) > 0)
         buildRootName (str, par.marksFileName, sizeof (par.marksFileName));
      else if (sscanf (pLine, "TRACE:%255s", str) > 0)
         buildRootName (str, par.traceFileName, sizeof (par.traceFileName));
      else if (sscanf (pLine, "CGRIB:%255s", str) > 0)
         buildRootName (str, par.gribFileName, sizeof (par.gribFileName));
      else if (sscanf (pLine, "CURRENT_GRIB:%255s", str) > 0)
         buildRootName (str, par.currentGribFileName, sizeof (par.currentGribFileName));
      else if (sscanf (pLine, "WAVE_POL:%255s", str) > 0)
         buildRootName (str, par.wavePolFileName, sizeof (par.wavePolFileName));
      else if (sscanf (pLine, "POLAR:%255s", str) > 0)
         buildRootName (str, par.polarFileName, sizeof (par.polarFileName));
      else if (sscanf (pLine, "ISSEA:%255s", str) > 0)
         buildRootName (str, par.isSeaFileName, sizeof (par.isSeaFileName));
      else if (sscanf (pLine, "TIDES:%255s", str) > 0)
         buildRootName (str, par.tidesFileName, sizeof (par.tidesFileName));
      else if (sscanf (pLine, "MID_COUNTRY:%255s", str) > 0)
         buildRootName (str, par.midFileName, sizeof (par.midFileName));
      else if (sscanf (pLine, "CLI_HELP:%255s", str) > 0)
         buildRootName (str, par.cliHelpFileName, sizeof (par.cliHelpFileName));
      else if (strstr (pLine, "VR_DASHBOARD:") != NULL)  {
         pLine = strchr (pLine, ':') + 1;
         g_strstrip (pLine);
         buildRootName (pLine, par.dashboardVR, sizeof (par.dashboardVR));
      }
      else if (sscanf (pLine, "VR_STAMINA:%lf", &par.staminaVR) > 0);
      else if (sscanf (pLine, "VR_DASHB_UTC:%d", &par.dashboardUTC) > 0);
      else if (sscanf (pLine, "HELP:%255s", par.helpFileName) > 0); // full link required
      else if (sscanf (pLine, "CURL_SYS:%d", &par.curlSys) > 0);
      else if (sscanf (pLine, "PYTHON:%d", &par.python) > 0);
      else if (sscanf (pLine, "SMTP_SCRIPT:%255[^\n]", par.smtpScript) > 0)
         g_strstrip (par.smtpScript);
      else if (sscanf (pLine, "IMAP_TO_SEEN:%255[^\n]", par.imapToSeen) > 0)
         g_strstrip (par.imapToSeen);
      else if (sscanf (pLine, "IMAP_SCRIPT:%255[^\n]", par.imapScript) > 0)
         g_strstrip (par.imapScript);
      else if (sscanf (pLine, "SHP:%255s", str) > 0) {
         buildRootName (str, par.shpFileName [par.nShpFiles], sizeof (par.shpFileName [0]));
         par.nShpFiles += 1;
         if (par.nShpFiles >= MAX_N_SHP_FILES) 
            fprintf (stderr, "In readParam, Error Number max of SHP files reached: %d\n", par.nShpFiles);
      }
      else if (sscanf (pLine, "AUTHENT:%d", &par.authent) > 0);
      else if (sscanf (pLine, "MOST_RECENT_GRIB:%d", &par.mostRecentGrib) > 0);
      else if (sscanf (pLine, "START_TIME:%lf", &par.startTimeInHours) > 0);
      else if (sscanf (pLine, "T_STEP:%lf", &par.tStep) > 0);
      else if (sscanf (pLine, "RANGE_COG:%d", &par.rangeCog) > 0);
      else if (sscanf (pLine, "COG_STEP:%d", &par.cogStep) > 0);
      else if (sscanf (pLine, "SPECIAL:%d", &par.special) > 0);
      else if (sscanf (pLine, "MOTOR_S:%lf", &par.motorSpeed) > 0);
      else if (sscanf (pLine, "THRESHOLD:%lf", &par.threshold) > 0);
      else if (sscanf (pLine, "DAY_EFFICIENCY:%lf", &par.dayEfficiency) > 0);
      else if (sscanf (pLine, "NIGHT_EFFICIENCY:%lf", &par.nightEfficiency) > 0);
      else if (sscanf (pLine, "X_WIND:%lf", &par.xWind) > 0);
      else if (sscanf (pLine, "MAX_WIND:%lf", &par.maxWind) > 0);
      else if (sscanf (pLine, "CONST_WAVE:%lf", &par.constWave) > 0);
      else if (sscanf (pLine, "CONST_WIND_TWS:%lf", &par.constWindTws) > 0);
      else if (sscanf (pLine, "CONST_WIND_TWD:%lf", &par.constWindTwd) > 0);
      else if (sscanf (pLine, "CONST_CURRENT_S:%lf", &par.constCurrentS) > 0);
      else if (sscanf (pLine, "CONST_CURRENT_D:%lf", &par.constCurrentD) > 0);
      else if (sscanf (pLine, "WP_GPX_FILE:%255s", str) > 0)
         buildRootName (str, par.wpGpxFileName, sizeof (par.wpGpxFileName));
      else if (sscanf (pLine, "FORBID_ZONE_FILE:%255s", str) > 0)
         buildRootName (str, par.forbidFileName, sizeof (par.forbidFileName)); 
      else if (sscanf (pLine, "DUMPI:%255s", str) > 0)
         buildRootName (str, par.dumpIFileName, sizeof (par.dumpIFileName));
      else if (sscanf (pLine, "DUMPR:%255s", str) > 0)
         buildRootName (str, par.dumpRFileName, sizeof (par.dumpRFileName));
      else if (sscanf (pLine, "PAR_INFO:%254s", str) > 0)
         buildRootName (str, par.parInfoFileName, sizeof (par.parInfoFileName));
      else if (sscanf (pLine, "LOG:%254s", str) > 0)
         buildRootName (str, par.logFileName, sizeof (par.logFileName));
      else if (sscanf (pLine, "FEEDBACK:%254s", str) > 0)
         buildRootName (str, par.feedbackFileName, sizeof (par.feedbackFileName));
      else if ((sscanf (pLine, "WEB:%254s", str) > 0) || (strstr (pLine, "WEB:") != NULL)) {
         buildRootName (str, par.web, sizeof (par.web));
      }
      else if (sscanf (pLine, "OPT:%d", &par.opt) > 0);
      else if (sscanf (pLine, "J_FACTOR:%d", &par.jFactor) > 0);
      else if (sscanf (pLine, "K_FACTOR:%d", &par.kFactor) > 0);
      else if (sscanf (pLine, "PENALTY0:%d", &par.penalty0) > 0);
      else if (sscanf (pLine, "PENALTY1:%d", &par.penalty1) > 0);
      else if (sscanf (pLine, "PENALTY2:%d", &par.penalty2) > 0);
      else if (sscanf (pLine, "N_SECTORS:%d", &par.nSectors) > 0);
      else if (sscanf (pLine, "WITH_WAVES:%d", &par.withWaves) > 0);
      else if (sscanf (pLine, "WITH_CURRENT:%d", &par.withCurrent) > 0);
      else if (sscanf (pLine, "ISOC_DISP:%d", &par.style) > 0);
      else if (sscanf (pLine, "STEP_ISOC_DISP:%d", &par.stepIsocDisp) > 0);
      else if (sscanf (pLine, "COLOR_DISP:%d", &par.showColors) > 0);
      else if (sscanf (pLine, "DMS_DISP:%d", &par.dispDms) > 0);
      else if (sscanf (pLine, "WIND_DISP:%d", &par.windDisp) > 0);
      else if (sscanf (pLine, "INFO_DISP:%d", &par.infoDisp) > 0);
      else if (sscanf (pLine, "INDICATOR_DISP:%d", &par.indicatorDisp) > 0);
      else if (sscanf (pLine, "CURRENT_DISP:%d", &par.currentDisp) > 0);
      else if (sscanf (pLine, "WAVE_DISP:%d", &par.waveDisp) > 0);
      else if (sscanf (pLine, "GRID_DISP:%d", &par.gridDisp) > 0);
      else if (sscanf (pLine, "LEVEL_POI_DISP:%d", &par.maxPoiVisible) > 0);
      else if (sscanf (pLine, "SPEED_DISP:%d", &par.speedDisp) > 0);
      else if (sscanf (pLine, "AIS_DISP:%d", &par.aisDisp) > 0);
      else if (sscanf (pLine, "TECHNO_DISP:%d", &par.techno) > 0);
      else if (sscanf (pLine, "CLOSEST_DISP:%d", &par.closestDisp) > 0);
      else if (sscanf (pLine, "FOCAL_DISP:%d", &par.focalDisp) > 0);
      else if (sscanf (pLine, "SHP_POINTS_DISP:%d", &par.shpPointsDisp) > 0);
      else if (sscanf (pLine, "GOOGLE_API_KEY:%1024s", par.googleApiKey) > 0);
      else if (sscanf (pLine, "WINDY_API_KEY:%1024s", par.windyApiKey) > 0);
      else if (sscanf (pLine, "WEBKIT:%255[^\n]", par.webkit) > 0) g_strstrip (par.webkit);
      else if (strstr (pLine, "FORBID_ZONES:") != NULL) inForbidZone = true;
      else if (inForbidZone && ((pt = strstr (pLine, "- [[")) != NULL)) {
         if (par.nForbidZone < MAX_N_FORBID_ZONE) {
            forbidZoneAdd (pLine + 2, par.nForbidZone);
            par.nForbidZone += 1;
         }
         else fprintf (stderr, "In readParam, MAX_N_FORBID_ZONE: %d\n", MAX_N_FORBID_ZONE);
      }
      else if (sscanf (pLine, "SMTP_SERVER:%255s", par.smtpServer) > 0);
      else if (sscanf (pLine, "SMTP_USER_NAME:%255s", par.smtpUserName) > 0);
      else if (sscanf (pLine, "SMTP_TO:%255s", par.smtpTo) > 0);
      else if (sscanf (pLine, "MAIL_PW:%255s", par.mailPw) > 0);
      else if (sscanf (pLine, "IMAP_SERVER:%255s", par.imapServer) > 0);
      else if (sscanf (pLine, "IMAP_USER_NAME:%255s", par.imapUserName) > 0);
      else if (sscanf (pLine, "IMAP_MAIL_BOX:%255s", par.imapMailBox) > 0);
      else if ((par.nNmea < N_MAX_NMEA_PORTS) && 
              (sscanf (pLine, "NMEA:%255s %d", par.nmea [par.nNmea].portName, &par.nmea [par.nNmea].speed) > 0)) {
         par.nNmea += 1;
      }
      else fprintf (stderr, "In readParam, Error Cannot interpret: %s\n", pLine);
   } // end while
   if (par.mailPw [0] != '\0') {
      par.storeMailPw = true;
   }
   if (competitors.n > 0) {
      par.pOr.lat = competitors.t [0].lat;
      par.pOr.lon = competitors.t [0].lon;
   }
   par.staminaVR = CLAMP (par.staminaVR, 0.0, 100.0);
   fclose (f);
   par.nSectors = MIN (par.nSectors, MAX_N_SECTORS);
   return true;
}

static void fprintfNoNull (FILE *f, const char *fmt, const char *param) {
   if (!param || ! *param) return;
   fprintf (f, fmt, param);
}
static void fprintfNoZero (FILE *f, const char *fmt, int param) {
   if (param == 0) return;
   fprintf (f, fmt, param);
}

/*! write parameter file from struct par 
   header or not, password or not yaml style or not */
bool writeParam (const char *fileName, bool header, bool password, bool yaml) {
   char strLat [MAX_SIZE_NAME] = "";
   char strLon [MAX_SIZE_NAME] = "";
   FILE *f = NULL;
   if ((f = fopen (fileName, "w")) == NULL) {
      fprintf (stderr, "In writeParam, Error Cannot write: %s\n", fileName);
      return false;
   }
   if (header) fprintf (f, "Name              Value\n");
   if (yaml) fprintf (f, "---\n");
   fprintfNoNull (f, "DESC:             %s\n", par.description);
   fprintfNoNull (f, "WD:               %s\n", par.workingDir);
   fprintf (f, "ALLWAYS_SEA:      %d\n", par.allwaysSea);
   
   latToStr (par.pOr.lat, par.dispDms, strLat, sizeof (strLat));
   lonToStr (par.pOr.lon, par.dispDms, strLon, sizeof (strLon));
   fprintf (f, "POR:              [%.2lf, %.2lf]\n", par.pOr.lat, par.pOr.lon);

   latToStr (par.pDest.lat, par.dispDms, strLat, sizeof (strLat));
   lonToStr (par.pDest.lon, par.dispDms, strLon, sizeof (strLon));
   fprintf (f, "PDEST:            [%.2lf, %.2lf]\n", par.pDest.lat, par.pDest.lon);

   fprintfNoNull (f, "POR_NAME:         %s\n", par.pOrName);
   fprintfNoNull (f, "PDEST_NAME:       %s\n", par.pDestName);
   if (wayPoints.n > 0) {
      fprintf (f, "WP:\n");
      for (int i = 0; i < wayPoints.n; i++) {
         fprintf (f, "  - [%.2lf, %.2lf]\n", wayPoints.t [i].lat, wayPoints.t [i].lon);
      }
   }
   if (competitors.n > 0) {
      fprintf (f, "COMPETITORS:\n");
      for (int i = 0; i < competitors.n; i++) { // competitors
         latToStr (competitors.t [i].lat, par.dispDms, strLat, sizeof (strLat));
         lonToStr (competitors.t [i].lon, par.dispDms, strLon, sizeof (strLon));
         fprintf (f, "  - [%d, %s, %s, %s]\n", competitors.t[i].colorIndex, strLat, strLon, competitors.t[i].name);
      }
   }
   fprintfNoNull (f, "WEB:              %s\n", par.web);
   for (int i = 0; i < par.nShpFiles; i++)
      fprintf (f, "SHP:              %s\n", par.shpFileName [i]);
   fprintfNoNull (f, "POI:              %s\n", par.poiFileName);
   fprintfNoNull (f, "PORT:             %s\n", par.portFileName);
   fprintfNoNull (f, "MARKS:            %s\n", par.marksFileName);
   fprintfNoNull (f, "TRACE:            %s\n", par.traceFileName);
   fprintfNoNull (f, "POLAR:            %s\n", par.polarFileName);
   fprintfNoNull (f, "WAVE_POL:         %s\n", par.wavePolFileName);
   fprintfNoNull (f, "ISSEA:            %s\n", par.isSeaFileName);
   fprintfNoNull (f, "MID_COUNTRY:      %s\n", par.midFileName);
   fprintfNoNull (f, "TIDES:            %s\n", par.tidesFileName);
   fprintfNoNull (f, "HELP:             %s\n", par.helpFileName);
   fprintfNoNull (f, "CLI_HELP:         %s\n", par.cliHelpFileName);
   fprintfNoNull (f, "VR_DASHBOARD:     %s\n", par.dashboardVR);
   fprintfNoNull (f, "WP_GPX_FILE:      %s\n", par.wpGpxFileName);
   fprintfNoNull (f, "FORBID_ZONE_FILE: %s\n", par.forbidFileName);
   fprintfNoNull (f, "DUMPI:            %s\n", par.dumpIFileName);
   fprintfNoNull (f, "DUMPR:            %s\n", par.dumpRFileName);
   fprintfNoNull (f, "PAR_INFO:         %s\n", par.parInfoFileName);
   fprintfNoNull (f, "LOG:              %s\n", par.logFileName);
   fprintfNoNull (f, "FEEDBACK:         %s\n", par.feedbackFileName);
   fprintfNoNull (f, "CURRENT_GRIB:     %s\n", par.currentGribFileName);
   fprintfNoNull (f, "CGRIB:            %s\n", par.gribFileName);
   fprintf (f, "MOST_RECENT_GRIB: %d\n", par.mostRecentGrib);
   fprintf (f, "GRIB_RESOLUTION:  %.2lf\n", par.gribResolution);
   fprintfNoZero (f, "GRIB_TIME_STEP:   %d\n", par.gribTimeStep);
   fprintfNoZero (f, "GRIB_TIME_MAX:    %d\n", par.gribTimeMax);
   fprintf (f, "VR_STAMINA:       %.2lf\n", par.staminaVR);
   fprintfNoZero (f, "VR_DASHB_UTC:     %d\n", par.dashboardUTC);

   fprintf (f, "START_TIME:       %.2lf\n", par.startTimeInHours);
   fprintf (f, "T_STEP:           %.2lf\n", par.tStep);
   fprintf (f, "RANGE_COG:        %d\n", par.rangeCog);
   fprintf (f, "COG_STEP:         %d\n", par.cogStep);
   fprintf (f, "PENALTY0:         %d\n", par.penalty0);
   fprintf (f, "PENALTY1:         %d\n", par.penalty1);
   fprintf (f, "PENALTY2:         %d\n", par.penalty2);
   fprintf (f, "MOTOR_S:          %.2lf\n", par.motorSpeed);
   fprintf (f, "THRESHOLD:        %.2lf\n", par.threshold);
   fprintf (f, "DAY_EFFICIENCY:   %.2lf\n", par.dayEfficiency);
   fprintf (f, "NIGHT_EFFICIENCY: %.2lf\n", par.nightEfficiency);
   fprintf (f, "X_WIND:           %.2lf\n", par.xWind);
   fprintf (f, "MAX_WIND:         %.2lf\n", par.maxWind);
   fprintf (f, "WITH_WAVES:       %d\n", par.withWaves);
   fprintf (f, "WITH_CURRENT:     %d\n", par.withCurrent);
   fprintfNoZero (f, "SPECIAL:          %d\n", par.special);

   if (par.constWave != 0)
      fprintf (f, "CONST_WAVE:       %.6lf\n", par.constWave);

   if (par.constWindTws != 0) {
      fprintf (f, "CONST_WIND_TWS:   %.6lf\n", par.constWindTws);
      fprintf (f, "CONST_WIND_TWD:   %.2lf\n", par.constWindTwd);
   }
   if (par.constCurrentS != 0) {
      fprintf (f, "CONST_CURRENT_S:  %.6f\n", par.constCurrentS);
      fprintf (f, "CONST_CURRENT_D:  %.2lf\n", par.constCurrentD);
   }

   fprintfNoZero (f, "OPT:              %d\n", par.opt);
   fprintfNoZero (f, "ISOC_DISP:        %d\n", par.style);
   fprintfNoZero (f, "STEP_ISOC_DISP:   %d\n", par.stepIsocDisp);
   fprintfNoZero (f, "COLOR_DISP:       %d\n", par.showColors);
   fprintfNoZero (f, "DMS_DISP:         %d\n", par.dispDms);
   fprintfNoZero (f, "WIND_DISP:        %d\n", par.windDisp);
   fprintfNoZero (f, "INFO_DISP:        %d\n", par.infoDisp);
   fprintfNoZero (f, "INDICATOR_DISP:   %d\n", par.indicatorDisp);
   fprintfNoZero (f, "CURRENT_DISP:     %d\n", par.currentDisp);
   fprintfNoZero (f, "WAVE_DISP:        %d\n", par.waveDisp);
   fprintfNoZero (f, "GRID_DISP:        %d\n", par.gridDisp);
   fprintfNoZero (f, "LEVEL_POI_DISP:   %d\n", par.maxPoiVisible);
   fprintfNoZero (f, "SPEED_DISP:       %d\n", par.speedDisp);
   fprintfNoZero (f, "AIS_DISP:         %d\n", par.aisDisp);
   fprintfNoZero (f, "SHP_POINTS_DISP:  %d\n", par.shpPointsDisp);
   fprintfNoZero (f, "TECHNO_DISP:      %d\n", par.techno);
   fprintfNoZero (f, "CLOSEST_DISP:     %d\n", par.closestDisp);
   fprintfNoZero (f, "FOCAL_DISP:       %d\n", par.focalDisp);
   fprintf (f, "J_FACTOR:         %d\n", par.jFactor);
   fprintf (f, "K_FACTOR:         %d\n", par.kFactor);
   fprintf (f, "N_SECTORS:        %d\n", par.nSectors);
   fprintfNoZero (f, "PYTHON:           %d\n", par.python);
   fprintfNoZero (f, "CURL_SYS:         %d\n", par.curlSys);
   fprintfNoNull (f, "SMTP_SCRIPT:      %s\n", par.smtpScript);
   fprintfNoNull (f, "IMAP_TO_SEEN:     %s\n", par.imapToSeen);
   fprintfNoNull (f, "IMAP_SCRIPT:      %s\n", par.imapScript);
   fprintfNoNull (f, "WINDY_API_KEY:    %s\n", par.windyApiKey);
   fprintfNoNull (f, "GOOGLE_API_KEY:   %s\n", par.googleApiKey);
   fprintfNoNull (f, "WEBKIT:           %s\n", par.webkit);
   fprintfNoNull (f, "SMTP_SERVER:      %s\n", par.smtpServer);
   fprintfNoNull (f, "SMTP_USER_NAME:   %s\n", par.smtpUserName);
   fprintfNoNull (f, "SMTP_TO:          %s\n", par.smtpTo);
   fprintfNoNull (f, "IMAP_SERVER:      %s\n", par.imapServer);
   fprintfNoNull (f, "IMAP_USER_NAME:   %s\n", par.imapUserName);
   fprintfNoNull (f, "IMAP_MAIL_BOX:    %s\n", par.imapMailBox);
   fprintf (f, "AUTHENT:          %d\n", par.authent);
   for (int i = 0; i < par.nNmea; i++)
      fprintf (f, "NMEA:             %s %d\n", par.nmea [i].portName, par.nmea [i].speed); 

   if (par.nForbidZone > 0) {
      fprintf (f, "FORBID_ZONES:\n");
      for (int i = 0; i < par.nForbidZone; i++) {
         fprintf (f, "  - [");
         for (int j = 0; j < forbidZones [i].n; j++) {
            latToStr (forbidZones [i].points [j].lat, par.dispDms, strLat, sizeof (strLat));
            lonToStr (forbidZones [i].points [j].lon, par.dispDms, strLon, sizeof (strLon));
            fprintf (f, "[%s, %s]%s", strLat, strLon, j < forbidZones [i].n - 1 ? ", " : "");
         }
         if (yaml) fprintf (f, "] # yamllint disable-line\n");
         else fprintf (f, "]\n");
      }
   }
   if (password)
      fprintf (f, "MAIL_PW:          %s\n", par.mailPw);
   if (yaml) fprintf (f, "...\n");

   fclose (f);
   return true;
}

/*! for virtual Regatta. return penalty in seconds for manoeuvre type. Depend on tws and energy. Give also sTamina coefficient */
double fPenalty (int shipIndex, int type, double tws, double energy, double *cStamina, bool fullPack) {
   if (type < 0 || type > 2) {
      fprintf (stderr, "In fPenalty, type unknown: %d\n", type);
      return -1;
   };
   const double kPenalty = 0.015;
   *cStamina = 2.0 - fmin (energy, 100.0) * kPenalty;
   const double tMin = shipParam [shipIndex].tMin [type];
   const double tMax = shipParam [shipIndex].tMax [type];
   tws = CLAMP(tws, 10.0, 30.0);
   const double fTws = 50.0 - 50.0 * cos (G_PI * ((tws - 10.0)/(30.0 - 10.0)));
   //printf ("cShip: %.2lf, cStamina: %.2lf, tMin: %.2lf, tMax: %.2lf, fTws: %.2lf\n", cShip, cStamina, tMin, tMax, fTws);
   double t = tMin + fTws * (tMax - tMin) / 100.0;
   t *= *cStamina;
   if (fullPack) t *= 0.8;
   return fmax(tMin, t);
}

/*! for virtual Regatta. return point loss with manoeuvre types. Depends on tws and fullPack */
double fPointLoss (int shipIndex, int type, double tws) {
   const double loss = (type == 2) ? 0.2 : 0.1;
   const double cShip = shipParam [shipIndex].cShip;
   const double fTws = (tws <= 10.0) ? 0.02 * tws + 1.0 : 
                 (tws <= 20.0) ? 0.03 * tws + 0.9 : 
                 (tws <= 30) ? 0.05 * tws + 0.5 :  
                 2.0;
   return loss * fTws * cShip;
}

/*! for virtual Regatta. return type in second to get back one energy point */
double fTimeToRecupOnePoint (double tws) {
   const double timeToRecupLow = 5.0 * 60.0;   // 5 minutes in secondes
   const double timeToRecupHigh = 15.0 * 60.0; // 15 minutes in seconds
   const double fTws = 1.0 - cos (G_PI * (fmin (tws, 30.0)/30.0));
   return timeToRecupLow + fTws * (timeToRecupHigh - timeToRecupLow) / 2.0;
}

/*! Return JSON formatted subset of parameters into 'out'.
    Returns out on success, NULL on error or truncation. */
char *paramToStrJson (Par *par, char *out, size_t maxLen) {
   if (!par || !out || maxLen == 0) return NULL;

   char *fileName         = g_path_get_basename (par->gribFileName);
   char *fileNameCurrent  = g_path_get_basename (par->currentGribFileName);
   char *polarFileName    = g_path_get_basename (par->polarFileName);
   char *wavePolarFileName= g_path_get_basename (par->wavePolFileName);
   char *isSeaFileName    = g_path_get_basename (par->isSeaFileName);

   int n = snprintf(out, maxLen,
      "{\n"
      "  \"wd\": \"%s\",\n"
      "  \"grib\": \"%s\",\n"
      "  \"bottomLat\": %.2f, \"leftLon\": %.2f, \"topLat\": %.2f, \"rightLon\": %.2f,\n"
      "  \"currentGrib\": \"%s\",\n"
      "  \"polar\": \"%s\",\n"
      "  \"wavePolar\": \"%s\",\n"
      "  \"issea\": \"%s\"\n"
      "}\n",
      par->workingDir,
      fileName,
      zone.latMin, zone.lonLeft, zone.latMax, zone.lonRight,
      fileNameCurrent,
      polarFileName,
        wavePolarFileName,
      isSeaFileName
   );

   free(fileName);
   free(fileNameCurrent);
   free(polarFileName);
   free(wavePolarFileName);
   free(isSeaFileName);

   if (n < 0) return NULL;                
   if ((size_t)n >= maxLen) return NULL;  // Trunc
   return out;
}

/*! return id and name of nearest port found in file fileName from lat, lon. return empty string if not found */
int nearestPort (double lat, double lon, const char *fileName, char *res, size_t maxLen) {
   const double minShomLat = 42.0;
   const double maxShomLat = 52.0;
   const double minShomLon = -6.0;
   const double maxShomLon = 4.0;
   double minDist = DBL_MAX;
   FILE *f;
   char str [MAX_SIZE_LINE];
   double latPort, lonPort;
   char portName [MAX_SIZE_NAME];
   int id = 0, bestId = 0;
   res [0] = '\0';
   if ((lat < minShomLat) || (lat > maxShomLat) || (lon < minShomLon) || (lon > maxShomLon))
      return 0;

   if ((f = fopen (fileName, "r")) == NULL) {
      fprintf (stderr, "In nearestPort, Error cannot open: %s\n", fileName);
      return 0;
   }
   while (fgets (str, MAX_SIZE_LINE, f) != NULL ) {
      if (sscanf (str, "%lf,%lf,%63[^,],%d", &latPort, &lonPort, portName, &id) > 2) {
         double d = orthoDist (lat, lon, latPort, lonPort);
         if (d < minDist) {
            minDist = d;
            g_strstrip (portName);
            strlcpy (res, portName, maxLen);
            bestId = id;
            //printf ("%s %.2lf %.2lf %.2lf %.2lf %.2lf \n", portName, d, lat, lon, latPort, lonPort);
         }
      }
   }
   fclose (f);
   return bestId;
}

/*! return seconds with decimals */
double monotonic (void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

/*! read all text file in buffer. Allocate memory */
char *readTextFile (const char *fileName, char *errMessage, size_t maxLen) {
   FILE *f = fopen(fileName, "rb");
   if (!f) {
      snprintf(errMessage, maxLen, "In readTextFile: Error Cannot open: %s", fileName);
      return NULL;
   }
   struct stat st;
   if (fstat(fileno(f), &st) != 0) {
      snprintf(errMessage, maxLen, "In readTextFile: cannot stat %s", fileName);
      fclose(f);
      return NULL;
   }
   if (st.st_size < 0) {
      snprintf(errMessage, maxLen, "In readTextFile: negative file size?");
      fclose(f);
      return NULL;
   }
   size_t fileSize = (size_t)st.st_size;
   char *buffer = malloc(fileSize + 1);  // +1 pour '\0'
   if (!buffer) {
      snprintf(errMessage, maxLen, "In readTextFile: Malloc failed: %zu", fileSize + 1);
      fclose(f);
      return NULL;
   }
   size_t nread = fread(buffer, 1, fileSize, f);
   if (nread != fileSize && ferror(f)) {
      snprintf(errMessage, maxLen, "In readTextFile: fread error");
      free(buffer);
      fclose(f);
      return NULL;
   }
   buffer[nread] = '\0';
   fclose(f);
   return buffer;
}

/*! read CSV file marks (Virtual Regatta) */
bool readMarkCSVToJson (const char *fileName, char *out, size_t maxLen) {
   FILE *f = NULL;
   char buffer [MAX_SIZE_TEXT_FILE];
   char str [MAX_SIZE_TEXT];
   char *what, *name, *id, *coord0, *coord1, *status, *end, *ptLon;
   double lat0 = 0.0, lon0 = 0.0, lat1 = 0.0, lon1 = 0.0;
   char empty [] = "";

   if ((f = fopen (fileName, "r")) == NULL) {
      snprintf (out, maxLen,  "Error in readMarkCSVToJson: cannot open: %s\n", fileName);
      return false;
   }
   strlcpy (out, "[\n", maxLen);
   
   while (fgets(buffer, sizeof buffer, f)) {
      //buffer[strcspn(buffer, "\r\n")] = '\0';
      char **t = g_strsplit(buffer, ";", -1);
      if (t == NULL) continue;

      what   = t[0] ? g_strstrip(t[0]) : empty;
      name   = t[1] ? g_strstrip(t[1]) : empty;
      id     = t[2] ? g_strstrip(t[2]) : empty;
      coord0 = t[3] ? g_strstrip(t[3]) : empty;
      coord1 = t[4] ? g_strstrip(t[4]) : empty;
      status = t[5] ? g_strstrip(t[5]) : empty;

      ptLon = coord0 ? strchr (coord0, '-') : NULL;
      if (ptLon) *ptLon = '\0'; // cut coord end eliminate '-'
      lat0 = getCoord (coord0, MIN_LAT, MAX_LAT);
      lon0 = ptLon ? getCoord (ptLon + 1, MIN_LON, MAX_LON) : 0.0;

      ptLon = coord1 ? strchr (coord1, '-') : NULL;
      if (ptLon) *ptLon = '\0'; // cut coord end eliminate '-'
      lat1 = getCoord (coord1, MIN_LAT, MAX_LAT);
      lon1 = ptLon ? getCoord (ptLon + 1, MIN_LON, MAX_LON) : 0.0;

      snprintf(str, sizeof str,
         "  {\"what\": \"%s\", \"name\": \"%s\", \"id\": \"%s\", "
         "\"lat0\": %.4lf, \"lon0\": %.4lf, \"lat1\": %.4lf, \"lon1\": %.4lf, \"status\": \"%s\"},\n",
         what, name, id, lat0, lon0, lat1, lon1, status);

      strlcat (out, str, maxLen);
      g_strfreev(t);
   }
   end = strrchr (out, ',');
   *end = '\0'; // trunk from last comma
   strlcat (out, "\n]\n", maxLen);
   fclose (f);
   return true;
}

/**
 * @brief Reads a GeoJSON file and extracts forbidden zones as polygons.
 *
 * The function parses a GeoJSON FeatureCollection and reads all geometries
 * of type "Polygon". Only the first ring of each polygon is used (no holes).
 * GeoJSON coordinates are [lon, lat] but are stored as Point {lat, lon}.
 *
 * Memory for each polygon is dynamically allocated using realloc().
 *
 * @param fileName     Path to the GeoJSON file.
 * @param forbidZones  Array of polygons to fill.
 * @param maxZones     Maximum number of polygons that can be stored.
 * @param n            Output: number of polygons successfully read.
 *
 * @return true on success, false on error (file, format, or memory).
 */
bool readGeoJson(const char *fileName, MyPolygon forbidZones[], int maxZones, int *n) {
   char errMessage [MAX_SIZE_TEXT] = "";   
   if (!fileName || !forbidZones || !n || maxZones <= 0) return false;
   *n = 0;

   char *txt = readTextFile(fileName, errMessage, sizeof(errMessage));
   if (! txt) return false;
   wipeSpace(txt); // txt  contain all JSON specification without spaces
   if (txt[0] == '\0') { free (txt); return false; }

   // init output
   for (int i = 0; i < maxZones; i++) {
      forbidZones[i].n = 0;
      forbidZones[i].points = NULL;
   }

   const char *p = txt;

   while (true) {
      const char *cpos = strstr(p, "\"coordinates\"");
      if (!cpos) break;

      // Rough check that we're inside a Polygon feature (optional but helps)
      bool isPolygon = false; 
      {
         const char *back = cpos;
         for (int k = 0; k < 5000 && back > txt; k++, back--) {
            if (back[0] == '{') break;
         }
         if (strstr(back, "\"type\"") && strstr(back, "Polygon")) isPolygon = true;
      }

      p = cpos + strlen("\"coordinates\"");

      const char *b = strchr(p, '[');
      if (!b) { free(txt); return false; }
      p = b;

      if (!isPolygon) { p++; continue; }

      if (*n >= maxZones) { free(txt); return false; }

      // Expect: coordinates: [[[lon,lat],[lon,lat], ... ], ...]
      p = strstr (p, "[[["); //list of rings, ring0, first point in ring0
      p += 3;
      MyPolygon *poly = &forbidZones[*n];
      poly->n = 0;
      poly->points = NULL;

      int cap = 0;

      while (true) {
         double lon = 0.0, lat = 0.0;
         int consumed;
         if (sscanf(p, "%lf,%lf] %n", &lon, &lat, &consumed) != 2) return false;
         p += consumed;
         // GeoJSON point order: [lon, lat]
         if (poly->n >= MAX_SIZE_FORBID_ZONE) { free(txt); return false; }

         if (poly->n >= cap) {
            int newCap = (cap == 0) ? 64 : cap * 2;
            if (newCap > MAX_SIZE_FORBID_ZONE) newCap = MAX_SIZE_FORBID_ZONE;

            Point *temp = (Point *)realloc(poly->points, (size_t)newCap * sizeof(Point));
            if (!temp) {
               fprintf(stderr, "readGeoJson: realloc failed for zone %d\n", *n);
               free(txt);
               return false;
            }
            poly->points = temp;
            cap = newCap;
         }

         poly->points[poly->n].lat = lat;
         poly->points[poly->n].lon = lon;
         poly->n++;

         if (*p == ',') { // next point
            p++;
            if (*p++ != '[') { free (txt); return false; } 
            continue;
         }

         if (*p == ']') { // end ring0
            p++;
            break;
         }

         free(txt);
         return false;
      }

      // Skip remaining rings (holes) until end of rings list ']'
      if (*p == ',') {
         int depth = 0;
         while (*p) {
            if (*p == '[') depth++;
            else if (*p == ']') {
               if (depth == 0) { p++; break; }
               depth--;
            }
            p++;
         }
      } else {
         if (*p == ']') p++;
      }
      (*n)++;
   }
   free(txt);
   return true;
}


