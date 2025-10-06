/*! 
   Download grib files from ECMWF or NOAA, METEO FRANCE, METEO CONDULT
   see synopsys : <dir> mode> [<maxStep> <topLat> <leftLon> <bottomLat> <rightLon>]
   only <mode> for METEO CONSULT (mode 5xy and 6xy)
   Compilation: gcc -o r3gribget r3gribget.c -leccodes -lcurl `pkg-config --cflags --libs glib-2.0`
   calculate automatically the run depending on UTC time and estimated DELAY 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <dirent.h>      // DIR, opendir, readdir, closedir

#include "../csources/glibwrapper.h"

#define SYNOPSYS "<dir> <mode> [<maxStep> <topLat> <leftLon> <bottomLat> <rightLon>]\n\
dir: directory that will host grib file. Ex: /../../grib\n\
mode: 1 NOAA, 2 ECMWF, 3 ARPEGE, 4 AROME\n\
for METEO CONSULT WIND: mode = 50y with no other parameter,\n\
for METEO CONSULT CURRENT: mode = 60y with no other parameter.\n\
Example: ./r3getgrib grib 1 96 60 -20 10 1 # NOAA request up to 96 hours, and specified region\n\
Example: ./r3getgrib grib 1                # NOAA request with default values\n\
Example: ./r3getgrib grib 502              # METEO CONSULT Request for Wind Centre_Atlantique"

#define MAX_CMD                     512
#define MAX_STEP                    384   
#define MAX_SIZE_DIR                128
#define MAX_SIZE_LINE               1024
#define SHORTNAMES                  "10u/10v/gust"

// NOAA parameters
#define NOAA_ROOT                   "GFS_Inter"
#define NOAA_DELAY                  4 // hours
#define NOAA_BASE_URL               "https://nomads.ncep.noaa.gov/cgi-bin/filter_gfs_0p25.pl"
#define MAX_STEP_NOAA               384 // 16 days

// ECMWF parameters
#define ECMWF_ROOT                  "ECMWF_Inter"
#define ECMWF_DELAY                 10 // hours
#define ECMWF_BASE_URL              "https://data.ecmwf.int/forecasts"
#define MAX_STEP_ECMWF              240 // 10 days

// METEO FRANCE parameters
#define METEO_FRANCE_ROOT           "METEO_FRANCE_Inter"
#define METEO_FRANCE_DELAY          6
#define METEO_FRANCE_BASE_URL       "https://object.data.gouv.fr/meteofrance-pnt/pnt/"
#define MAX_STEP_AROME              48  // 2 days
#define MAX_STEP_ARPEGE             102 // 4 days and 12 hours

// METEO CONSULT parameters
#define N_METEO_CONSULT_WIND_URL    6
#define N_METEO_CONSULT_CURRENT_URL 6
#define METEO_CONSULT_WIND_DELAY    4  // nb hours after time run to get new grib
#define METEO_CONSULT_CURRENT_DELAY 4
#define METEO_CONSULT_ROOT_GRIB_URL "https://static1.mclcm.net/mc2020/int/cartes/marine/grib/"
#define MAX_N_TRY                   3  // max try for Meteoconsult download

enum {NOAA = 1, ECMWF = 2, ARPEGE = 3, AROME = 4, METEO_CONSULT_WIND = 5, METEO_CONSULT_CURRENT = 6};

const bool verbose = false;

const char *METEO_CONSULT_WIND_URL [N_METEO_CONSULT_WIND_URL * 2] = {
   "Atlantic North",   "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",  "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Centre_Atlantique.grb",
   "Antilles",         "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Antilles.grb",
   "Europe",           "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Europe.grb",
   "Manche",           "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Manche.grb",
   "Gascogne",         "%sMETEOCONSULT%02dZ_VENT_%02d%02d_Gascogne.grb"
};

const char *METEO_CONSULT_CURRENT_URL [N_METEO_CONSULT_CURRENT_URL * 2] = {
   "Atlantic North",    "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Nord_Atlantique.grb",
   "Atlantic Center",   "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Centre_Atlantique.grb",
   "Antilles",          "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Antilles.grb",
   "Europe",            "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Europe.grb",
   "Manche",            "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Manche.grb",
   "Gascogne",          "%sMETEOCONSULT%02dZ_COURANT_%02d%02d_Gascogne.grb"
};

char gribDir [MAX_SIZE_DIR];

/*! Check if a file exists and is non-empty */
static bool fileExists (const char *filename) {
   struct stat buffer;
   return (stat (filename, &buffer) == 0 && buffer.st_size > 0);
}

/*! return secondes with decimals */
double monotonic (void) {
   struct timespec ts;
   clock_gettime(CLOCK_MONOTONIC, &ts);
   return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

/*! Equivalent to GLIB g_mkdir_with_parents.
   or bash  mkdir -p */
int mkdir_p(const char *path, mode_t mode) {
   if (!path || !*path) { errno = EINVAL; return -1; }

   char *tmp = strdup(path);
   if (!tmp) { errno = ENOMEM; return -1; }

   // delete filals //
   size_t len = strlen(tmp);
   while (len > 1 && tmp[len - 1] == '/') {
      tmp[--len] = '\0';
   }

   // create every level
   for (char *p = tmp + 1; *p; ++p) {
      if (*p == '/') {
         *p = '\0';
         if (mkdir(tmp, mode) != 0) {
            if (errno != EEXIST) { free(tmp); return -1; }
            struct stat st;
            if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
               free(tmp); errno = ENOTDIR; return -1;
            }
         }
         *p = '/';
      }
   }

   // create last level
   if (mkdir(tmp, mode) != 0) {
      if (errno != EEXIST) {
         free(tmp);
         return -1;
      }
      struct stat st;
      if (stat(tmp, &st) != 0 || !S_ISDIR(st.st_mode)) {
         free(tmp); errno = ENOTDIR; return -1;
      }
   }

   free(tmp);
   return 0;
}

/*! split "prefix" to (directory, base_prefix).
 *  Ex: "/tmp/abc" -> /tmp" and "abc"
 */
static void splitPrefix(const char *input, char **out_dir, char **out_base) {
   const char *slash = strrchr(input, '/');
   if (slash) {
      size_t dir_len = (size_t)(slash - input);
      if (dir_len == 0) dir_len = 1; /* garder "/" */
      *out_dir = (char *)malloc(dir_len + 1);
      if (*out_dir) {
         memcpy(*out_dir, input, dir_len);
         (*out_dir)[dir_len] = '\0';
      }
      *out_base = strdup(slash + 1);
   } else {
      *out_dir = strdup(".");
      *out_base = strdup(input);
   }
   if (!*out_dir || !*out_base) {
      free(*out_dir); free(*out_base);
      *out_dir = *out_base = NULL;
   }
}

/*! remove all .tmp file with prefix */
static void removeAllTmpFilesWithPrefix(const char *prefix) {
   char *directory = NULL;
   char *base_prefix = NULL;

   splitPrefix (prefix, &directory, &base_prefix);
   if (!directory || !base_prefix) {
      fprintf(stderr, "removeAllTmpFilesWithPrefix: out of memory\n");
      free(directory); free(base_prefix);
      return;
   }

   DIR *dir = opendir(directory);
   if (!dir) {
      fprintf(stderr, "In removeAllTmpFilesWithPrefix: Fail opening directory: %s: %s\n",
            directory, strerror(errno));
      free(directory);
      free(base_prefix);
      return;
   }

   struct dirent *ent;
   while ((ent = readdir(dir)) != NULL) {
      const char *filename = ent->d_name;

      /* Ignore "." et ".." */
      if (filename[0] == '.' && (filename[1] == '\0' || (filename[1] == '.' && filename[2] == '\0')))
         continue;

      if (g_str_has_prefix(filename, base_prefix) && g_str_has_suffix(filename, ".tmp")) {
         size_t dir_len = strlen(directory);
         int need_slash = (dir_len > 0 && directory[dir_len - 1] != '/');
         size_t path_len = dir_len + need_slash + strlen(filename) + 1;

         char *filepath = (char *)malloc(path_len);
         if (!filepath) {
            /* pas de mémoire : on saute */
            continue;
         }

         if (need_slash)
            snprintf(filepath, path_len, "%s/%s", directory, filename);
         else
            snprintf(filepath, path_len, "%s%s", directory, filename);

         if (remove(filepath) != 0) {
            fprintf(stderr, "removeAllTmpFilesWithPrefix: failed to remove %s: %s\n",
                  filepath, strerror(errno));
         }
         free(filepath);
      }
   }

   closedir(dir);
   free(directory);
   free(base_prefix);
}

/*! Treatment for ECMWF or ARPEGE or AROME files. DO NO USE FOR NOAA: USELESS because already reduced and may bug.
   Select shortnames and subregion defined by lon and lat values and produce output file
   return true if all OK */
static bool reduce (const char *dir, const char *inFile, const char *shortNames, double latMax, double lonLeft, double latMin, double lonRight, const char *outFile) {
   char toRemove [MAX_SIZE_LINE];
   const char* tempCompact = "compacted.tmp";
   char command [MAX_SIZE_LINE];
   char fullFileName [MAX_SIZE_LINE];
   snprintf (toRemove, sizeof (toRemove),"%s/%s", dir, tempCompact);
   snprintf (command, MAX_SIZE_LINE, "grib_copy -w shortName=%s %s %s/%s", shortNames, inFile, dir, tempCompact);
   if (system(command) != 0) {
      fprintf (stderr, "In reduce, Error call: %s\n", command);
      remove (toRemove);
      return false;
   }
   if (verbose) {
      printf ("command; %s\n", command);
      if (fileExists (toRemove))
         printf ("toRemove exist: %s\n", toRemove); 
      else
         printf ("toRemove does not exist: %s\n", toRemove); 
   }
   snprintf (fullFileName, MAX_SIZE_LINE, "%s/%s", dir, tempCompact);
   if (access (fullFileName, F_OK) != 0) {
      fprintf (stderr, "In reduce, Error: file does no exist: %s\n", fullFileName);
      remove (toRemove);
      return false;
   }

   snprintf (command, MAX_SIZE_LINE, "wgrib2 %s/%s -small_grib %.0lf:%.0lf %.0lf:%.0lf %s >/dev/null",
      dir, tempCompact, lonLeft, lonRight, latMin, latMax, outFile);
   
   if (verbose) printf ("wgrib2: %s\n", command);
   if (system (command) != 0) {
      fprintf (stderr, "In reduce, Error call: %s\n", command);
      remove (toRemove);
      return false;
   }
   if (verbose) printf ("remove: %s\n", toRemove);
   remove (toRemove);
   return true;
}

/*! Download a file using cURL */
static bool downloadFile (const char *url, const char *output) {
   CURL *curl = curl_easy_init();
   if (!curl) {
       fprintf(stderr, "❌ Error: Unable to initialize cURL.\n");
       return false;
   }

   if (verbose) printf("📥 Downloading: %s\n", url);

   FILE *fp = fopen(output, "wb");
   if (!fp) {
       fprintf(stderr, "❌ Error: Unable to create file %s\n", output);
       curl_easy_cleanup(curl);
       return false;
   }

   curl_easy_setopt (curl, CURLOPT_URL, url);
   curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, NULL);
   curl_easy_setopt (curl, CURLOPT_WRITEDATA, fp);
   curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt (curl, CURLOPT_FAILONERROR, 1L);

   CURLcode res = curl_easy_perform(curl);
   fclose (fp);
   curl_easy_cleanup (curl);

   if (res != CURLE_OK) {
      fprintf (stderr, "⚠️ Download failed: %s\n", url);
      remove (output); // Remove empty or broken file
      return false;
   }

   if (verbose) printf ("✅ Download successful: %s\n", output);
   return true;
}

/*! Get the latest run datetime according to mode (NOAA, etc) */
static void getRunDatetime (int mode, int delay, char *yyyy, char *mm, char *dd, char *hh) {
   char strDate [64];
   time_t now = time (NULL);
   struct tm t = *gmtime(&now); // Copie locale de la structure tm
   strftime (strDate, sizeof (strDate), "%Y-%m-%d %H:%M:%S UTC", &t);
   printf ("%s\n", strDate);
    
   // Apply delay
   t.tm_hour -= delay;
    
   // Normalize timegm() to manage day month year change
   time_t adj_time = timegm(&t);
   t = *gmtime (&adj_time);
   
   // Calculate last run available
   if (mode == ECMWF || mode == METEO_CONSULT_CURRENT) 
      t.tm_hour = (t.tm_hour / 12) * 12; // ECMWF : 0 ou 12
   else 
      t.tm_hour = (t.tm_hour / 6) * 6;   // NOAA : 0, 6, 12 ou 18

   snprintf (yyyy, 6, "%4d", (t.tm_year % 9999) + 1900);
   snprintf (mm, 4, "%02d", (t.tm_mon % 12) + 1);
   snprintf (dd, 4, "%02d", t.tm_mday % 32);
   snprintf (hh, 4, "%02d", t.tm_hour % 24);
}

/*! Concatenate reduced GRIB files into a single file */
static void concatenateGrib (const char *root, const char *prefix, const char *yyyy, 
                             const char *mm, const char *dd, const char *hh, int lastStep) {
   char finalFile [MAX_SIZE_LINE];
   char toRemove [MAX_SIZE_LINE];
   struct stat st;
   snprintf (finalFile, sizeof(finalFile), "%s/%s_%s%s%s_%sZ_%03d.grb", gribDir, prefix, yyyy, mm, dd, hh, lastStep);

   FILE *fOut = fopen (finalFile, "wb");
   if (!fOut) {
      fprintf(stderr, "❌ Error: Unable to create final file %s\n", finalFile);
      return;
   }

   for (int step = 0; step <= lastStep; step++) {
      char inputFile [MAX_SIZE_LINE];
      snprintf (inputFile, sizeof (inputFile), "%s/%s_reduced_%03d.tmp", gribDir, root, step);
      if (verbose && stat (inputFile, &st) == 0)
         printf ("concat: %s, size: %ld\n", inputFile, st.st_size);

      FILE *fIn = fopen (inputFile, "rb");
      if (!fIn) {
         if (verbose) fprintf (stderr, "⚠️ In concantenateGrib Warning: Could not open %s\n", inputFile);
         continue;
      }
      char buffer [4096];
      size_t bytes;
      while ((bytes = fread (buffer, 1, sizeof (buffer), fIn)) > 0) {
         fwrite (buffer, 1, bytes, fOut);
      }
      fclose (fIn);
   }

   fclose (fOut);
   snprintf (toRemove, sizeof (toRemove),"%s/%s", gribDir, root);
   removeAllTmpFilesWithPrefix (toRemove);
   if (stat (finalFile, &st) == 0)
      printf ("✅ Concatenation completed: %s, size: %ld\n", finalFile, st.st_size);
   else 
      printf ("In concatenateGrib, Error: no finalFile: %s\n", finalFile);
}

/*! Process NOAA GRIB downloads with maxStep limit */
static void fetchNoaa (int maxStep, double topLat, double leftLon, double bottomLat, double rightLon) {
   char yyyy [5], mm [3], dd [3], hh [3];
   getRunDatetime (NOAA, NOAA_DELAY, yyyy, mm, dd, hh);
   maxStep = MIN (maxStep, MAX_STEP_NOAA);

   printf("📅 NOAA Run selected: %s-%s-%s %sZ (Max Step: %d)\n", yyyy, mm, dd, hh, maxStep);

   for (int step = 0; step <= maxStep; step += (step >= 120 ? 3 : 1)) {
      char fileUrl [1024], outputFile [256], reducedFile [256];

      snprintf (fileUrl, sizeof (fileUrl),
             "%s?file=gfs.t%sz.pgrb2.0p25.f%03d&dir=/gfs.%s%s%s/%s/atmos"
             "&subregion=&toplat=%.2f&leftlon=%.2f&rightlon=%.2f&bottomlat=%.2f"
             "&var_GUST=on&var_UGRD=on&var_VGRD=on"
             "&lev_10_m_above_ground=on&lev_surface=on&lev_mean_sea_level=on",
             NOAA_BASE_URL, hh, step, yyyy, mm, dd, hh, topLat, leftLon, rightLon, bottomLat);

      snprintf (outputFile, sizeof (outputFile), "%s/%s_%03d.tmp", gribDir, NOAA_ROOT, step);
      snprintf (reducedFile, sizeof (reducedFile), "%s/%s_reduced_%03d.tmp", gribDir, NOAA_ROOT, step);

      if (!downloadFile (fileUrl, outputFile)) {
         printf("⚠️ Download failed at step %d, stopping further downloads.\n", step);
         maxStep = step;
         remove (outputFile);
         break;
      }
      rename (outputFile, reducedFile);
   }
   concatenateGrib (NOAA_ROOT, "GFS",  yyyy, mm, dd, hh, maxStep);
}

/*! Process ECMWF GRIB downloads with maxStep limit */
static void fetchEcmwf (int maxStep, double topLat, double leftLon, double bottomLat, double rightLon) {
   char yyyy [5], mm [3], dd [3], hh [3];
   getRunDatetime (ECMWF, ECMWF_DELAY, yyyy, mm, dd, hh);

   maxStep = MIN (maxStep, MAX_STEP_ECMWF);

   printf ("📅 ECMWF Run selected: %s-%s-%s %sZ (Max Step: %d)\n", yyyy, mm, dd, hh, maxStep);

   for (int step = 0; step <= maxStep; step += (step >= 144 ? 6 : 3)) {
      char fileUrl [1024], outputFile [256], reducedFile [256];

      snprintf(fileUrl, sizeof(fileUrl),
             "%s/%s%s%s/%sz/ifs/0p25/oper/%s%s%s%s0000-%dh-oper-fc.grib2",
             ECMWF_BASE_URL, yyyy, mm, dd, hh, yyyy, mm, dd, hh, step);

      snprintf(outputFile, sizeof (outputFile), "%s/%s_%03d.tmp", gribDir, ECMWF_ROOT, step);
      snprintf(reducedFile, sizeof (reducedFile), "%s/%s_reduced_%03d.tmp", gribDir, ECMWF_ROOT, step);

      if (!downloadFile(fileUrl, outputFile)) {
         printf("⚠️ Download failed at step %d, stopping further downloads.\n", step);
         maxStep = step;
         break;
      }
      reduce (gribDir, outputFile, SHORTNAMES, topLat, leftLon, bottomLat, rightLon, reducedFile);
      if (verbose && !fileExists (reducedFile))
         fprintf (stderr, "in fetchEcmf, Error: %s Does no exist !\n", reducedFile);
   }
   concatenateGrib (ECMWF_ROOT, "ECMWF", yyyy, mm, dd, hh, maxStep);
}

/*! Process ARPEGE GRIB downloads with maxStep limit */
static void fetchArpege (int maxStep, double topLat, double leftLon, double bottomLat, double rightLon) {
   char yyyy [5], mm [3], dd [3], hh [3];
   const int arpegeStepMin [] = {0, 25, 49, 73};
   const int arpegeStepMax [] = {24, 48, 72, 102};
   const int maxI = (maxStep >= 73) ? 4 :
                    (maxStep >= 49) ? 3 : 
                    (maxStep >= 25) ? 2 : 1; 

   int resolution = 25; // 0.25 degree
   
   getRunDatetime (ARPEGE, METEO_FRANCE_DELAY, yyyy, mm, dd, hh);

   maxStep = arpegeStepMax [maxI - 1];

   printf ("📅 ARPEGE Run selected: %s-%s-%s %sZ (Max Step: %d)\n", yyyy, mm, dd, hh, maxStep);

   for (int i = 0; i < maxI; i += 1) {
      char fileName [512], fileUrl [1024], outputFile [256], reducedFile [256];
      int stepMin = arpegeStepMin [i];
      int stepMax = arpegeStepMax [i];
      snprintf (fileName, sizeof (fileName), "arpege__%03d__SP1__%03dH%03dH__%s-%s-%sT%s:00:00Z.grib2", resolution, stepMin, stepMax, yyyy, mm, dd, hh);
      snprintf (fileUrl, sizeof (fileUrl), "%s%s-%s-%sT%s:00:00Z/arpege/%03d/SP1/%s", METEO_FRANCE_BASE_URL, yyyy, mm, dd, hh, resolution, fileName);

      snprintf (outputFile, sizeof (outputFile), "%s/%s_%03d.tmp", gribDir, METEO_FRANCE_ROOT, i);
      snprintf (reducedFile, sizeof (reducedFile), "%s/%s_reduced_%03d.tmp", gribDir, METEO_FRANCE_ROOT, i);

      if (!downloadFile(fileUrl, outputFile)) {
         printf("⚠️ Download failed at index %d, stopping further downloads.\n", i);
         //maxI = i;
         break;
      }
      reduce (gribDir, outputFile, SHORTNAMES, topLat, leftLon, bottomLat, rightLon, reducedFile);
   }
   concatenateGrib (METEO_FRANCE_ROOT, "ARPEGE", yyyy, mm, dd, hh, maxStep);
}

/*! Process AROME GRIB downloads with maxStep limit */
static void fetchArome (int maxStep, double topLat, double leftLon, double bottomLat, double rightLon) {
   char yyyy [5], mm [3], dd [3], hh [3];
   int resolution = 1; // here this means 0.1 degree
   getRunDatetime (AROME, METEO_FRANCE_DELAY, yyyy, mm, dd, hh);

   maxStep = MIN (maxStep, MAX_STEP_AROME);

   printf ("📅 AROME Run selected: %s-%s-%s %sZ (Max Step: %d)\n", yyyy, mm, dd, hh, maxStep);

   for (int step = 0; step <= maxStep; step += 1) {
      char fileUrl [1024], outputFile [256], reducedFile [256];
      snprintf(fileUrl, sizeof(fileUrl),
             "%s%s-%s-%sT%s:00:00Z/arome/%03d/HP1/arome__%03d__HP1__%02dH__%s-%s-%sT%s:00:00Z.grib2",
             METEO_FRANCE_BASE_URL, yyyy, mm, dd, hh, resolution, 
             resolution, step, yyyy, mm, dd, hh);

      snprintf(outputFile, sizeof (outputFile), "%s/%s_%03d.tmp", gribDir, METEO_FRANCE_ROOT, step);
      snprintf(reducedFile, sizeof (reducedFile), "%s/%s_reduced_%03d.tmp", gribDir, METEO_FRANCE_ROOT, step);

      if (!downloadFile (fileUrl, outputFile)) {
         printf("⚠️ Download failed at step %d, stopping further downloads.\n", step);
         maxStep = step;
         break;
      }
      reduce (gribDir, outputFile, SHORTNAMES, topLat, leftLon, bottomLat, rightLon, reducedFile);
   }
   concatenateGrib (METEO_FRANCE_ROOT, "AROME", yyyy, mm, dd, hh, maxStep);
}

/*! Uncompress Grib file received from METOCONSULT to Template 5.0 */
static bool uncompress (const char *in, const char *out) {
   char command [MAX_SIZE_LINE * 4];
   snprintf (command, sizeof (command), "wgrib2 %s -set_scaling same same -set_grib_type simple -grib_out %s >/dev/null", in, out);
   return (system (command) == 0);
}

/*! Process METEOCONSULT Wind grib download */
static void fetchMeteoConsult (int type, int region) {
   char yyyy [5], mm [3], dd [3], hh [3], url [1024] = "";
   char finalFile [MAX_SIZE_LINE];
   char uncompressedFile [MAX_SIZE_LINE];

   int nTry = 0;
   int delay = (type == METEO_CONSULT_WIND) ? METEO_CONSULT_WIND_DELAY : METEO_CONSULT_CURRENT_DELAY;
   do {
      getRunDatetime (type, delay, yyyy, mm, dd, hh);
      int mmInt = atoi (mm);
      int ddInt = atoi (dd);      
      int hhInt = atoi (hh);
      printf ("📅 MC Run selected: %s-%s-%s %sZ Try: %d\n", yyyy, mm, dd, hh, nTry);

      if (type == METEO_CONSULT_WIND)
         snprintf (url, sizeof (url), METEO_CONSULT_WIND_URL [region *2 + 1], METEO_CONSULT_ROOT_GRIB_URL, hhInt, mmInt, ddInt);
      else
         snprintf (url, sizeof (url), METEO_CONSULT_CURRENT_URL [region *2 + 1], METEO_CONSULT_ROOT_GRIB_URL, hhInt, mmInt, ddInt);
      printf ("url: %s\n", url);

      char *baseName= g_path_get_basename (url);
      snprintf (finalFile, sizeof(finalFile), "%s/%s", gribDir, baseName);
      snprintf (uncompressedFile, sizeof(uncompressedFile), "%s/UC_%s", gribDir, baseName);
      free (baseName);

      if (downloadFile (url, finalFile))
         break; // success
      else {
         printf ("In fetchMeteoConsult Try Failed: %s\n", url); 
         delay += 6; // try another time with 6 hour older run
         nTry += 1;
      }
   } while (nTry < MAX_N_TRY);

   if (nTry >=  MAX_N_TRY)
      fprintf (stderr, "⚠️ Download failed after: %d try\n", nTry);
   else  {// creation of uncompressed file
      if (!uncompress(finalFile, uncompressedFile)) {
        fprintf(stderr, "⚠️ Uncompress failed for: %s\n", finalFile);
      }
      printf ("finalFile Uncompressed: %s\n", uncompressedFile);
      remove (finalFile);
   }
}

int main (int argc, char *argv[]) {
   double topLat = 60.0, leftLon = -80.0, bottomLat = -20.0, rightLon = 10.0; // default values
   int maxStep = MAX_STEP;
   double start = monotonic (); 

   if (argc != 8 && argc != 3) {
      fprintf (stderr, "Usage: %s %s\n", argv[0], SYNOPSYS);
      return EXIT_FAILURE;
   }
   g_strlcpy (gribDir, argv [1], sizeof (gribDir)); // Store  grib dir in global variable

   int mode = atoi (argv[2]);

   if (argc != 3 && mode > 0 && mode < 5) {
      maxStep = atoi (argv[3]);
      topLat = atof (argv[4]);
      leftLon = atof (argv[5]);
      bottomLat = atof (argv[6]);
      rightLon = atof (argv[7]);
   }

   if (mkdir_p (gribDir, 0755) != 0) {
      fprintf (stderr, "mkdir_p(%s) failed: %s\n", gribDir, strerror(errno));
   }

   switch (mode) {
   case 1:
      fetchNoaa (maxStep, topLat, leftLon, bottomLat, rightLon);
      break;
   case 2:
      fetchEcmwf (maxStep, topLat, leftLon, bottomLat, rightLon);
      break;
   case 3:
      fetchArpege (maxStep, topLat, leftLon, bottomLat, rightLon);
      break;
   case 4:;
      fetchArome (maxStep, topLat, leftLon, bottomLat, rightLon);
      break;
    case 500: case 501: case 502: case 503: case 504: case 505:
      fetchMeteoConsult (METEO_CONSULT_WIND, mode % 100);
      break;
   case 600: case 601: case 602: case 603: case 604: case 605:
      fetchMeteoConsult (METEO_CONSULT_CURRENT, mode % 100);
      break;
   default:
      fprintf (stderr, "Invalid mode: Use 1 for NOAA, 2 for ECMWF, 3 for ARPEGE, 4 for AROME, 5xy or 6xy for METEO_CONSULT.\n");
      return EXIT_FAILURE;
   }  
   double elapsed = monotonic () - start; 
   printf ("✅ Processing completed in: %.2lf seconds.\n\n", elapsed);
   return EXIT_SUCCESS;
}

