/*! compilation: gcc -c option.c */
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>  // uint64_t
#include "r3types.h"
#include "grib.h"
#include "readgriball.h"
#include "r3util.h"
#include "engine.h"
#include "polar.h"
#include "glibwrapper.h"
#include "inline.h"

/*! Manage command line option reduced to one character */
void optionManage (char option) {
	FILE *f = NULL;
   char *buffer = NULL;
   char footer [MAX_SIZE_LINE] = "";
   double w, twa, tws, lon, lat, lat2, lon2, cog, t, hours;
   char str [MAX_SIZE_LINE] = "";
   int sail, intRes, nTries;
   long dataDate;
   const long nIter = 1e9;

   if ((buffer = (char *) malloc (MAX_SIZE_BUFFER)) == NULL) {
      fprintf (stderr, "In optionManage, Error Malloc %d\n", MAX_SIZE_BUFFER); 
      return;
   }
   buffer [0] = '\0'; 
   printf ("\n");
   
   switch (option) {
   case 'c': // cap
      printf ("Lon1 = ");
      if (scanf ("%lf", &lon) < 1) break;
      printf ("Lat1 = ");
      if (scanf ("%lf", &lat) < 1) break;
      printf ("Lon2 = ");
      if (scanf ("%lf", &lon2) < 1) break;
      printf ("Lat2 = ");
      if (scanf ("%lf", &lat2) < 1) break;
      printf ("Ortho cap1: %.2lf°,   Ortho cap2: %.2lf°\n", orthoCap (lat, lon, lat2, lon2), orthoCap (lat2, lon2, lat, lon));
      printf ("Ortho2 cap1: %.2lf°,  Ortho2 cap2: %.2lf°\n", orthoCap2 (lat, lon, lat2, lon2), orthoCap2 (lat2, lon2, lat, lon));
      printf ("Orthodist1 : %.2lf,   Orthodist2: %.2lf\n", orthoDist (lat, lon, lat2, lon2), orthoDist (lat2, lon2, lat, lon));
      printf ("Orthodist1 : %.2lf,   Orthodist2: %.2lf\n", orthoDist2 (lat, lon, lat2, lon2), orthoDist (lat2, lon2, lat, lon));
      printf ("Loxodist1  : %.2lf,   Loxodist2 : %.2lf\n", loxoDist(lat, lon, lat2, lon2), loxoDist (lat2, lon2, lat, lon));
      break;
   case 'g': // grib
      gribToStr (&zone, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGrib (&zone, tGribData [WIND]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (true, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      break;
   case 'G': // grib current
      gribToStr (&currentZone, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      printf ("grib print...\n");
      printGrib (&currentZone, tGribData [CURRENT]);
      printf ("\n\nFollowing lines are suspects info...\n");
      checkGribToStr (true, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      break;
   case 'h': // help
      printf ("Size of size_t : %zu bytes\n", sizeof (size_t));
	   if ((f = fopen (par.cliHelpFileName, "r")) == NULL) {
		   fprintf (stderr, "In optionManage, Error help: Impossible to read: %s\n", par.cliHelpFileName);
		   break;
	   }
      while ((fgets (str, MAX_SIZE_LINE, f) != NULL ))
         printf ("%s", str);
      fclose (f);
      break;
   case 'p': // polar
      polToStr (&polMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      polToStr (&sailPolMat, buffer, MAX_SIZE_BUFFER);
      // printf ("%s\n", buffer);
      while (true) {
         printf ("twa true wind angle = ");
         if (scanf ("%lf", &twa) < 1) break;
         printf ("tws true wind speed = ");
         if (scanf ("%lf", &tws) < 1) break;
         printf ("- Speed over ground: %.2lf\n", findPolar  (twa, tws, &polMat, &sailPolMat, &sail));
         printf ("1 Speed over ground: %.2lf\n", findPolar1 (twa, tws, &polMat, &sailPolMat, &sail));
         printf ("2 Speed over ground: %.2lf\n", findPolar2 (twa, tws, &polMat, &sailPolMat, &sail));
         printf ("Sail: %d\n", sail); 

         double t0 = monotonic ();
         for (long i = 0; i < nIter; i += 1) findPolar (fmod (twa+=10.0, 180.0), fmod (tws+=13.0, 90.0), &polMat, &sailPolMat, &sail);

         double t1 = monotonic ();
         for (long i = 0; i < nIter; i += 1) findPolar1 (fmod (twa+=10.0, 180.0), fmod (tws+=13.0, 90.0), &polMat, &sailPolMat, &sail);
         
         double t2 = monotonic ();
         for (long i = 0; i < nIter; i += 1) findPolar2 (fmod (twa+=10.0, 180.0), fmod (tws+=13.0, 90.0), &polMat, &sailPolMat, &sail);

         double t3 = monotonic ();

         printf ("✅ findPolar.: %.2lf seconds\n", t1 - t0);
         printf ("✅ findPolar1: %.2lf seconds\n", t2 - t1);
         printf ("✅ findPolar2: %.2lf seconds\n", t3 - t2);
      }
    
      break;
   case 'P': // Wave polar
      polToStr (&wavePolMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      while (true) {
         printf ("angle = " );
         if (scanf ("%lf", &twa) < 1) break;
         printf ("w = ");
         if (scanf ("%lf", &w) < 1) break;
         printf ("coeff: %.2lf\n", findPolar (twa, w, &wavePolMat, NULL, &sail) / 100.0);
      }
      break;
   case 'q':
      polToStr (&polMat, buffer, MAX_SIZE_BUFFER);
      printf ("%s\n", buffer);
      while (true) {
         printf ("tws = " );
         if (scanf ("%lf", &tws) < 1) break;
         printf ("newMaxSpeedInPolarAt: %.4lf\n", maxSpeedInPolarAt (tws, &polMat));
      }
      break;
   case 'r': // routing
      routingLaunch ();
      routeToStr (&route, buffer, MAX_SIZE_BUFFER, footer, sizeof (footer));
      printf ("%s\n", buffer);
      printf ("%s\n", footer);
      break;
   case 'R': // routing
      printf ("nTries: ");
      if (scanf ("%d", &nTries) != 1) break;
      for (int i = 0; i < nTries; i += 1) routingLaunch ();
      routeToStr (&route, buffer, MAX_SIZE_BUFFER, footer, sizeof (footer));
      printf ("%s\n", buffer);
      printf ("%s\n", footer);
      break;
   case 's': // isIsea
      if (tIsSea == NULL) printf ("in readIsSea : bizarre\n");
      // dumpIsSea ();
      while (1) {
         printf ("Lat = ");
         if (scanf ("%lf", &lat) < 1) break;
         printf ("Lon = ");
         if (scanf ("%lf", &lon) < 1) break;
         if (isSea (tIsSea, lat, lon))
            printf ("Sea\n");
         else printf ("Earth\n");
      }
      break;
   case 't': // test
      readGribAll ("/home/rr/routing/grib/GFS_20251121_12Z_144.grb", &zone, WIND);
      printf ("GribTime: %s\n", gribDateTimeToStr (zone.dataDate[0], zone.dataTime[0], str, sizeof str));
      while (true) {
         printf ("Lat = ");
         if (scanf ("%lf", &lat) < 1) break;
         printf ("Lon = ");
         if (scanf ("%lf", &lon) < 1) break;
         printf ("t = ");
         if (scanf ("%lf", &t) < 1) break;
         intRes = isDay (t, zone.dataDate[0], zone.dataTime[0], lat, lon);;
         printf("res: %s\n", intRes ? "day" : "night");
      }
      break;
   case 'T': // time
      printf ("GribTime: %s\n", gribDateTimeToStr (zone.dataDate[0], zone.dataTime[0], str, sizeof str));
      printf ("offsetLocalUTC: %lf s\n", offsetLocalUTC ());
      time_t t = time (NULL);
      printf ("now epochToStr: %s\n", epochToStr (t, true, str, sizeof str)); 
      t = gribDateTimeToEpoch (zone.dataDate[0], zone.dataTime[0]);
      printf ("grib start epochToStr: %s\n", epochToStr (t, true, str, sizeof str)); 
      while (true) {
         printf ("date: ");
         if (scanf ("%ld", &dataDate) < 1) break;
         printf ("decimal hours: ");
         if (scanf ("%lf", &hours) < 1) break;
         printf ("DateUtc:    %s\n", newDate (dataDate, hours, str, sizeof str));
      }
 break;
   case 'v': // version
      printf ("Prog version: %s, %s, %s\n", PROG_NAME, PROG_VERSION, PROG_AUTHOR);
      printf ("Compilation-date: %s\n", __DATE__);
      break;
   case 'w': // twa
      while (true) {
         printf ("COG = ");
         if (scanf ("%lf", &cog) < 1) break;
         printf ("TWS = ");
         if (scanf ("%lf", &twa) < 1) break;
         printf ("fTwa = %.2lf\n",fTwa (cog, twa));
      }
   break;
   case 'z': //
      printf ("Password %s\n", par.mailPw);
      // printf ("Password %s\n", dollarSubstitute (par.mailPw, buffer, strlen (par.mailPw)));
      break;
   default:
      printf ("Option unknown: -%c\n", option);
      break;
   }
   free (buffer);
}
