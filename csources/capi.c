#include <stdbool.h>
#include <stdarg.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <locale.h>
#include "glibwrapper.h"
#include "r3types.h"
#include "r3util.h"
#include "engine.h"
#include "polar.h"
#include "grib.h"
#include "common.h"
#include "readgriball.h"

#define SYNOPSYS               "[form] [<parameter file>]"
#define BIG_BUFFER_SIZE        (300*MILLION)
#define PATTERN                "GFS"

char parameterFileName [MAX_SIZE_FILE_NAME];

static int g_inited = 0;

bool rcube_init(const char *paramRelOrAbs) {
    if (g_inited) return true;
    if (!paramRelOrAbs) return false;
    bool ok = initContext(paramRelOrAbs, "");
    if (ok) g_inited = 1;
    return ok;
}

/*
 * ---------------------------------------------------------------------------
 *  Core-ish entrypoints for "routing" that are reusable by an offline client.
 *
 *  Motivation:
 *  - iPhone offline: you will call computeRoutingJsonFromForm("type=1&...", ...)
 *    from a Capacitor native plugin without going through sockets/HTTP.
 *  - Server: keep existing launchAction() flow unchanged.
 *
 *  IMPORTANT:
 *  - routingLaunch() currently reads globals (notably global clientReq).
 *    So these helpers temporarily copy the local request into the global.
 *  - This keeps changes minimal. Later we can refactor routingLaunch() to take
 *    explicit arguments (ClientRequest*, SailRoute*).
 * ---------------------------------------------------------------------------
 */

/*
 * Compute routing JSON from an already decoded ClientRequest.
 * Returns true if a JSON response (success or error) is written to outBuffer.
 */
bool computeRoutingJsonFromReq(const ClientRequest *reqIn, char *outBuffer, size_t maxLen) {
   char checkMessage [MAX_SIZE_TEXT];

   if (!reqIn || !outBuffer || maxLen == 0) return false;

   /* Work on a local copy first */
   ClientRequest req = *reqIn;

   if (!checkParamAndUpdate (&req, checkMessage, sizeof checkMessage)) {
      snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", checkMessage);
      return true;
   }

   /* routingLaunch() reads globals => copy local req into global */
   clientReq = req;
   // competitors.runIndex = 0;
   routingLaunch ();

   if (route.ret == ROUTING_ERROR) {
      snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "Routing failed");
   }
   else {
      routeToJson (&route, clientReq.isoc, clientReq.isoDesc, outBuffer, maxLen);
   }
   return true;
}

/*! Provide system information */
static void testToJson (char *out, size_t maxLen) {
   char strWind [MAX_SIZE_LINE] = "";
   char strCurrent [MAX_SIZE_LINE] = "";

   formatThousandSep (strWind, sizeof strWind, sizeof(FlowP) * (zone.nTimeStamp + 1) * zone.nbLat * zone.nbLon);
   formatThousandSep (strCurrent, sizeof strCurrent, sizeof(FlowP) * (currentZone.nTimeStamp + 1) * currentZone.nbLat * currentZone.nbLon);
   
   snprintf (out, maxLen,
      "{\n  \"Prog-version\": \"%s, %s, %s\",\n"
      "  \"Memory for Grib Wind\": \"%s\",\n"
      "  \"Memory for Grib Current\": \"%s\",\n"
      "  \"Compilation-date\": \"%s\"\n}\n",
      PROG_NAME, PROG_VERSION, PROG_AUTHOR, strWind, strCurrent, __DATE__
   );
}

/*! translate grib in a  binary file containing array of uv[gw] float */
static bool gribDump (ClientRequest *req, char *outBuffer, size_t maxLen) {
   char directory [MAX_SIZE_DIR_NAME];
   char checkMessage [MAX_SIZE_TEXT];
   char str [MAX_SIZE_NAME];
   const char* prefix = "gribdata_";
   size_t dataLen = 0;

   printf ("Dump try...\n");
   if (req->model [0] != '\0' && req->gribName [0] == '\0') { // there is a model specified but no grib file
      printf ("model: %s\n", req->model);
      snprintf (directory, sizeof directory, "%s%sgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
      mostRecentFile (directory, ".gr", req->model, req->gribName, sizeof req->gribName);
   }
   if (req->gribName [0] == '\0') {
      snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No Grib");
      return false;
   }
   updateWindGrib (req, checkMessage, sizeof checkMessage);
   buildInitialOfShortNameList(&zone, str, sizeof str);
   // strlcpy (str, "uvgw", 5); // ATT ecrase le précédent
   float *buf = buildUVGWarray(&zone, str, tGribData[WIND], &dataLen); // dataLen in number of float
   char fileName [MAX_SIZE_FILE_NAME]; 
   snprintf (fileName, sizeof fileName, "%s%s.bin", prefix, str);
   printf("✅ Available Binary float array with: Len=%zu, Bytes=%zu, Shortnames=%s, fileName=%s\n\n",
      dataLen, dataLen * sizeof(float), str, fileName);
   FILE *f = fopen(fileName, "wb");
   if (!f) {
      perror("fopen");
      return false;
   }
   size_t written = fwrite(buf, sizeof(float), dataLen, f);
   if (written != dataLen) {
      perror("fwrite");
      fclose(f);
      return false;
   }
   fclose(f);
   free(buf);
   return true;
}

/*!
 * Compute routing JSON from a form-urlencoded request body:
 * "type=1&boat=...&waypoints=...&model=GFS&..."
 * Returns true if a JSON response (success or error) is written to outBuffer.
 */
bool computeRoutingJsonFromForm(const char *formReq, char *outBuffer, size_t maxLen) {
   const char *date = getCurrentDate ();
   char errMessage [MAX_SIZE_TEXT] = "";
   bool resp = true;
   char directory [MAX_SIZE_DIR_NAME];
   ClientRequest req;
   char tempFileName [MAX_SIZE_FILE_NAME];
   char *strTemp = NULL;
   size_t len = 0;
   if (!formReq || !outBuffer || maxLen == 0) return false;
   if (!decodeFormReq (formReq, &req)) {
      snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "Bad request");
      return true;
   }
   switch (req.type) {
   case REQ_TEST:
      testToJson (outBuffer, maxLen);
      break;
   case REQ_ROUTING:
      resp = computeRoutingJsonFromReq(&req, outBuffer, maxLen);
      break;
   case REQ_COORD:
      if (req.nBoats > 0) {
         infoCoordToJson (req.boats [0].lat, req.boats [0].lon, &req, outBuffer, maxLen);
      }
      else {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"No Boat\"\n}\n");
      }
      break;
   case REQ_FORBID:
      forbidToJson (outBuffer, maxLen);
      break;
   case REQ_POLAR:
      if (req.polarName [0] != '\0') {
         if (strstr (req.polarName, "wavepol")) {
            polToStrJson (false, req.polarName, "wavePolarName", outBuffer, maxLen);
         }
         else {
            polToStrJson (true, req.polarName , "polarName", outBuffer, maxLen);
         }
         break;
      }
      polToStrJson (true, par.polarFileName, "polarName", outBuffer, maxLen);
      break;
   case REQ_GRIB:
      if (req.model [0] != '\0' && req.gribName [0] == '\0') { // there is a model specified but no grib file
         printf ("model: %s\n", req.model);
         snprintf (directory, sizeof directory, "%s%sgrib", par.workingDir, hasSlash (par.workingDir) ? "" : "/"); 
         if (!mostRecentFile (directory, ".gr", req.model, req.gribName, sizeof req.gribName)) {
            snprintf (outBuffer, maxLen, "{\"_Error\": \"No grib with model: %s\"}\n", req.model);
            break;
         }
      }
      if (req.gribName [0] != '\0') gribToStrJson (req.gribName, outBuffer, maxLen);
      else snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No Grib");
      break;
   case REQ_DIR:
      listDirToStrJson (par.workingDir, req.dirName, req.sortByName, NULL, filter, outBuffer, maxLen);
      break;
   case REQ_PAR_RAW:
      // yaml style if model = 1
      bool yaml = req.model [0] == 'y';
      buildRootName (TEMP_FILE_NAME, tempFileName, sizeof tempFileName);
      writeParam (tempFileName, false, false, yaml);
      strTemp = readTextFile (tempFileName, errMessage, sizeof errMessage);
      if (strTemp == NULL) {
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"%s\"\n}\n", errMessage);
         break;
      }
      strlcat (outBuffer, strTemp, maxLen);
      free (strTemp);
      if (yaml) normalizeSpaces (outBuffer);
      break;
   case REQ_PAR_JSON:
      paramToStrJson (&par, outBuffer, maxLen);
      break;
   case REQ_INIT:
      if (! initContext (parameterFileName, PATTERN))
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"Init failed\"\n}\n");
      else
         snprintf (outBuffer, maxLen, "{\n  \"message\": \"Init done\"\n}\n");
      break;
   case REQ_FEEDBACK:
      printf ("try feednack\n");
      handleFeedbackRequest (par.feedbackFileName, date, "no ip", req.feedback);
      snprintf (outBuffer, maxLen, "{\"_Feedback\": \"%s\"}\n", "OK");
      break;
   case REQ_DUMP_FILE:  // raw dump
      buildRootName (req.fileName, tempFileName, sizeof tempFileName);
      strTemp = readTextFile (tempFileName, errMessage, sizeof errMessage);
      if (strTemp == NULL) {
         snprintf (outBuffer, maxLen, "{\n  \"_Error\": \"%s\"\n}\n", errMessage);
         break;
      }
      strlcat (outBuffer, strTemp, maxLen);
      free (strTemp);
      break;
   case REQ_MARKS: 
      if (! readMarkCSVToJson (par.marksFileName, outBuffer, maxLen)) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"Reading Mark File %s\"}\n", par.marksFileName);
      }
      break;
   case REQ_NEAREST_PORT:
      if (req.nWp > 0)
         nearestPortToStrJson (req.wp[0].lat, req.wp[0].lon, outBuffer, maxLen);
      else snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "No coordinates found");
      break;
   
   case REQ_CHECK_GRIB: 
      len = strlen (req.gribName);
      bool hasGrib = len > 0 && req.gribName [len - 1] != '/';
      len = strlen (req.currentGribName);
      bool hasCurrentGrib = len > 0 && req.currentGribName [len - 1] != '/';
      if (!hasGrib  && !hasCurrentGrib) {
         snprintf (outBuffer, maxLen, "{\"_Error\": \"%s\"}\n", "Either wind grib or current grib required");
         break;
      }
      if (hasGrib && !(updateWindGrib (&req, outBuffer, maxLen))) break;
      if (hasCurrentGrib && !(updateCurrentGrib (&req, outBuffer, maxLen))) break;

      checkGribToStr (hasCurrentGrib, outBuffer, maxLen);
      if (outBuffer [0] == '\0') g_strlcpy (outBuffer, "All is OK\n", maxLen);
      break;
   case REQ_GRIB_DUMP:
      resp = gribDump (&req, outBuffer, maxLen);
      break;
   default:;
   }
   return resp;
}

int main (int argc, char *argv[]) {
   char *bigBuffer = NULL;
   const double start = monotonic ();

   if (setlocale (LC_ALL, "C") == NULL) {                // very important for printf decimal numbers
      fprintf (stderr, "In main, Error: setlocale failed");
      return EXIT_FAILURE;
   }

   if (argc <= 1 || argc > 3) {
      fprintf (stderr, "Synopsys: %s %s\n", argv [0], SYNOPSYS);
      return EXIT_FAILURE;
   }
   
   if (argc > 2)
      g_strlcpy (parameterFileName, argv [2], sizeof parameterFileName);
   else 
      g_strlcpy (parameterFileName, PARAMETERS_FILE, sizeof parameterFileName);

   if (! initContext (parameterFileName, ""))
      return EXIT_FAILURE;
   // strlcpy (par.workingDir, "/home/rr/routing", sizeof (par.workingDir));

   if ((bigBuffer =malloc (BIG_BUFFER_SIZE)) == NULL) {
      fprintf (stderr, "In main, Error: Malloc: %d,", BIG_BUFFER_SIZE);
      return EXIT_FAILURE;
   }

   const double elapsed = monotonic () - start; 
   printf ("✅ Loaded in...: %.2lf seconds.\n\n", elapsed);
   
   const char *reqStr = "type=1&boat=banane,41.244772222222224,-16.787108333333336;&initialAmure=1&waypoints=41.068888,-17;41.707594,-17.953125&model=GFS&timeStep=7200&polar=pol/class40-2021.json&wavePolar=wavepol/polwave.csv&forbid=false&isoc=false&isodesc=false&withWaves=false&withCurrent=false&xWind=1&maxWind=100&penalty0=180&penalty1=180&penalty2=180&motorSpeed=0&threshold=0&dayEfficiency=1&nightEfficiency=1&cogStep=5&cogRange=90&jFactor=0&kFactor=1&nSectors=720&constWindTws=0&constWindTwd=0&constWave=0&constCurrentS=0&constCurrentD=0";

   if (argv[1][0] == '-') {
      if (computeRoutingJsonFromForm(reqStr, bigBuffer, BIG_BUFFER_SIZE)) {
         printf("%s\n", bigBuffer);
      }
   }
   else {
      if (computeRoutingJsonFromForm(argv [1], bigBuffer, BIG_BUFFER_SIZE)) {
         printf("%s\n", bigBuffer);
      }
   }
   free (bigBuffer);
   return EXIT_SUCCESS;
}
