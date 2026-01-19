/*! compilation: gcc -c polar.c */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <locale.h>
#include <math.h>
#include <ctype.h>
#include <sys/stat.h>
#include "glibwrapper.h"
#include "r3types.h"
#include "inline.h"
#include "r3util.h"

/*! str to double accepting both . or , as decimal separator */
static bool strtodNew(const char *str, double *v){
   if (str == NULL || v == NULL || (!isNumber(str))) return false;

   const size_t len = strlen(str);
   char *mutableStr = (char *)malloc(len + 1);
   if (!mutableStr) return false;

   // Replace comma with dots
   for (size_t i = 0; i <= len; ++i) {
      const char c = str[i];
      mutableStr[i] = (c == ',') ? '.' : c;  // copy also '\0'
   }

   char *endptr = NULL;
   *v = strtod(mutableStr, &endptr);

   bool ok = (endptr != mutableStr); // at least one character
   free(mutableStr);
   return ok;
}

/*! Check polar and return false and a report if something to say */
static bool polarCheck (PolMat *mat, char *report, size_t maxLen) {
   double maxInRow, maxInCol;
   char str [MAX_SIZE_LINE];
   int cMax, rowMax;
   report [0] ='\0';
   for (int c = 1; c < mat->nCol; c += 1) {  // check values in line 0 progress
      if (mat->t [0][c] < mat->t[0][c-1]) {
         snprintf (str, sizeof str, "Values in row 0 should progress, col: %d; ", c);
         g_strlcat (report, str, maxLen);
      }
   }
   for (int row = 1; row < mat->nLine; row += 1) {
      if ((mat->t [row][0] < mat->t[row-1] [0])) {
         snprintf (str, sizeof str, "Values in col 0 should progress, row: %d; ", row);
         g_strlcat (report, str, maxLen);
      }
   }
   for (int row = 1; row < mat->nLine; row += 1) {
      maxInRow = -1.0;
      cMax = -1;
      for (int c = 1; c < mat->nCol; c += 1) { // find maxInRow
         if (mat->t[row][c] > maxInRow) {
            maxInRow = mat->t[row][c];
            cMax = c;
         }
      }
      for (int c = 2; c <= cMax; c += 1) {
         if (mat->t [row][c] < mat->t[row][c-1]) {
            snprintf (str, sizeof str, "Values in row: %d should progress at col: %d up to maxInRow: %.2lf; ", row, c, maxInRow);
            g_strlcat (report, str, maxLen);
            break;
         }
      }
      for (int c = cMax + 1; c < mat->nCol; c += 1) {
         if ((mat->t [row][c] > mat->t[row][c-1])) {
            snprintf (str, sizeof str, "Values in row: %d should regress at col: %d after maxInRow: %.2lf; ", row, c, maxInRow);
            g_strlcat (report, str, maxLen);
            break;
         }
      }
   }

   for (int c = 1; c < mat->nCol; c += 1) {
      maxInCol = -1.0;
      rowMax = -1;
      for (int row = 1; row < mat->nLine; row += 1) { // find maxInRow
         if (mat->t[row][c] > maxInCol) {
            maxInCol = mat->t[row][c];
            rowMax = row;
         }
      }
      for (int row = 2; row <= rowMax; row += 1) {
         if (mat->t [row][c] < mat->t[row-1][c]) {
            snprintf (str, sizeof str, "Values in col: %d should progress at row: %d up to maxInCol: %.2lf; ", c, row, maxInCol);
            g_strlcat (report, str, maxLen);
            break;
         }
      }
      for (int row = rowMax + 1; row < mat->nLine; row += 1) {
         if ((mat->t [row][c] > mat->t[row-1][c])) {
            snprintf (str, sizeof str, "Values in col: %d should regress at row: %d after maxInCol: %.2lf; ", c, row, maxInCol);
            g_strlcat (report, str, maxLen);
            break;
         }
      }
   }
   return (report [0] == '\0'); // True if no error found
}

/*! read polar file and fill poLMat matrix */
static bool readPolarCsv (const char *fileName, PolMat *mat, char *errMessage, size_t maxLen) {
   FILE *f = NULL;
   char buffer [MAX_SIZE_TEXT];
   char *pLine = &buffer [0];
   char *strToken;
   double v = 0.0, max = 0.0;
   int c;

   errMessage [0] = '\0';
   strlcpy (mat->jsonHeader, "null", sizeof mat->jsonHeader); // important !
   
   if ((f = fopen (fileName, "r")) == NULL) {
      snprintf (errMessage, maxLen,  "Error in readPolarCsv: cannot open: %s", fileName);
      return false;
   }
   while (fgets (pLine, MAX_SIZE_TEXT, f) != NULL) {
      c = 0;
      if (pLine [0] == '#') continue;                       // ignore if comment
      if (strpbrk (pLine, CSV_SEP_POLAR) == NULL) continue; // ignore if no separator
         
      strToken = strtok (pLine, CSV_SEP_POLAR);
      while ((strToken != NULL) && (c < MAX_N_POL_MAT_COLS)) {
         if (strtodNew (strToken, &v)) {
            mat->t [mat->nLine][c] = v;
            if ((mat->nLine != 0) && (c != 0)) max = fmax (max, v);
            c += 1;
         }
         if ((mat->nLine == 0) && (c == 0))  // whatever accepted including not number at c == l == 0
            c += 1;
         strToken = strtok (NULL, CSV_SEP_POLAR);
      }
      if (c <= 2) continue; // line is not validated if not at least 2 columns
      if (c >= MAX_N_POL_MAT_COLS) {
         snprintf (errMessage, maxLen, "Error in readPolarCsv: max number of colums: %d", MAX_N_POL_MAT_COLS);
         fclose (f);
         return false;
      }
      mat->nLine += 1;
      
      if (mat->nLine >= MAX_N_POL_MAT_LINES) {
         snprintf (errMessage, maxLen, "Error in readPolarCsv: max number of line: %d", MAX_N_POL_MAT_LINES);
         fclose (f);
         return false;
      }
      if (mat->nLine == 1) mat->nCol = c; // max
   }
   mat->t [0][0] = -1;
   mat->maxAll = max;
   fclose (f);
   return true;
}

/*! return VMG: angle and speed at TWS: pres */
void bestVmg (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed) {
   *vmgSpeed = -1;
   int bidon;
   double vmg;
   for (int i = 1; i < mat->nLine; i++) {
      if (mat->t [i][0] > 90) break; // useless over 90°
      vmg = findPolar (mat->t [i][0], tws, mat, NULL, &bidon) * cos (DEG_TO_RAD * mat->t [i][0]);
      if (vmg > *vmgSpeed) {
         *vmgSpeed = vmg;
         *vmgAngle = mat->t [i][0];
      }
   }
}

/*! return VMG back: angle and speed at TWS: vent arriere */
void bestVmgBack (double tws, PolMat *mat, double *vmgAngle, double *vmgSpeed) {
   *vmgSpeed = -1;
   int bidon;
   double vmg;
   for (int i = 1; i < mat->nLine; i++) {
      if (mat->t [i][0] < 90) continue; // useless under  90° for Vmg Back
      if (mat->t [i][0] > 180) break;        
      vmg = fabs (findPolar (mat->t [i][0], tws, mat, NULL, &bidon) * cos (DEG_TO_RAD * mat->t [i][0]));
      if (vmg > *vmgSpeed) {
         *vmgSpeed = vmg;
         *vmgAngle = mat->t [i][0];
      }
   }
}

/*! write polar information in string */
char *polToStr (const PolMat *mat, char *str, size_t maxLen) {
   char line [MAX_SIZE_LINE] = "";
   str [0] = '\0';
   for (int i = 0; i < mat->nLine; i++) {
      for (int j = 0; j < mat->nCol; j++) {
         snprintf (line, MAX_SIZE_LINE, "%6.2f ", mat->t [i][j]);
         g_strlcat (str, line, maxLen);
      }
      g_strlcat (str, "\n", maxLen);
   }
   snprintf (line, sizeof line, 
             "Number of rows in polar : %d\n"
             "Number of lines in polar: %d\n"
             "Max                     : %.2lf\n"
             "nSail                   : %.zu\n",
             mat->nCol, mat->nLine, mat->maxAll, mat->nSail);
   g_strlcat (str, line, maxLen);
   return str;
}

/*! add a new sail in PoMat object */
static bool addSail (PolMat *polMat, int id, char *name, double max) {
   const int nSail = polMat->nSail;
   if (nSail >= MAX_N_SAIL - 1) return false;
   polMat->tSail [nSail].id = id;
   strlcpy (polMat->tSail [nSail].name, name, MAX_SIZE_NAME);
   polMat->tSail [nSail].max = max;
   polMat->nSail += 1;
   polMat->maxAll = fmax (max, polMat->maxAll);
   return true;
} 

/*! update polMat and sailPolMat with new sail found in Json str */
static char *findSailPol (char *str, PolMat *polMat, PolMat *sailPolMat, char *message, size_t maxLen) {
   double sailId, maxVal = 0.0;
   int nTws = 0, maxNTws= 0, nTwa = 0;
   char *sailName, *endPtr;
   char *pt = strstr (str, "\"id\":");
   if (pt == NULL) return NULL;
   pt += 5;
   sailId = strtod (pt, &endPtr);
   if (endPtr == pt) return NULL;
   pt = endPtr;

   pt = strstr (str, "\"name\":\"");
   if (pt == NULL) return NULL;
   sailName = pt + 8;
   pt = strchr (sailName, '"');
   if (pt == NULL) return NULL;
   *pt = '\0';
   // printf ("SailName: %s\n", sailName);
   pt = strstr (pt + 1, "\"speed\":[[");
   if (pt == NULL) return NULL;
   pt += 10;
   // printf ("%s\n", pt-1);
   while (pt) {
      nTws = 0;
      while ((*pt == ',') || (*pt == '[')) pt += 1; // important
      while (pt) {
         double val = strtod (pt, &endPtr);
         if (pt == endPtr) {
            fprintf (stderr, "In findSailPol, Error: No value at nTwa: %d, nTWs: %d\n", nTwa, nTws);
         }
         if ((val >= polMat->t [nTwa + 1][nTws + 1]) && (val > 0.0)) {
            polMat->t [nTwa + 1][nTws + 1] = val;
            sailPolMat->t [nTwa + 1][nTws + 1] = sailId;
            if (fabs (sailId - round (sailId)) > 0.2) {
               fprintf (stderr, "In findSail, Error: Strange Value for saiId: %.2lf\n", sailId);
            }
            maxVal = fmax (val, maxVal);
         }
         nTws += 1;
         if (*endPtr == ']') break;
         pt = endPtr + 1;
      }
      pt = endPtr + 1;
      maxNTws = fmax (nTws, maxNTws);
      nTwa += 1;
      if (*endPtr == ']') { // research of ]]
         endPtr += 1;
         if (*endPtr == ']') break;
      }
   }
   if (nTws != polMat->nCol -1) {
      snprintf (message, maxLen, "nTws = %d and polMat->nCol-1 = %d should be equal\n", nTws, polMat->nCol-1);
      return NULL;
   }
   if (nTwa != polMat->nLine -1) {
      snprintf (message, maxLen, "nTwa = %d and polMat->nLine-1 = %d should be equal\n", nTwa, polMat->nLine-1);
      return NULL;
   }
      
   if (! addSail (polMat, sailId, sailName, maxVal)) return NULL;
   return pt;
}

/*! update polMat and sailPolMat this tws ans twa lists found in Json str */
static char *findListPol (bool isTws, int maxN, char *str, const char *startWith, 
                          PolMat *polMat, PolMat *sailPolMat, char *message, size_t maxLen) {
   char *endPtr;
   int size = 0;
   char *pt = strstr (str, startWith);
   int n;
   if (pt == NULL)  {
      snprintf (message, maxLen, "In findListPol, Error: %s not found.\n", startWith);
      return NULL;
   }
   pt += strlen (startWith);
   for (n = 1; pt != NULL && n < maxN; n += 1) {
      const double val = strtod (pt, &endPtr);
      if (endPtr == pt) break;
      if (isTws) polMat->t [0][n] = sailPolMat->t [0][n] = val;
      else polMat->t [n][0] = sailPolMat->t [n][0] = val;
      size += 1;
      if (*endPtr == ']') break;
      pt = endPtr + 1;
   }
   if (n >= maxN) {
      snprintf (message, maxLen, "In findListPol, Error: maxN reached for %s\n", startWith);
      return NULL;
   }
   if (pt == NULL) {
      snprintf (message, maxLen, "In findListPol, Error: ] end not found for %s\n", startWith);
      return NULL;
   }
   if (isTws) polMat->nCol = sailPolMat->nCol = size + 1;
   else polMat->nLine = sailPolMat->nLine = size + 1;
   return pt;
}

/*!
   read polar in json format (eg. VirtualRegatta) with several sails
*/
static bool readPolarJson (const char *fileName, PolMat *polMat, PolMat *sailPolMat, char *errMessage, size_t maxLen) {
   char start [9000];
   char *begin, *ptr;
   errMessage [0] = '\0';
   char *buffer = readTextFile (fileName, errMessage, maxLen); 
   if (buffer == NULL) return false;
   wipeSpace (buffer); // buffer contain all JSON specification without spaces
   if (buffer [0] == '\0') {
      free (buffer); 
      return false;
   }
   char *line = buffer;

   // extract the header
   begin = strstr (line, "\"_id\"");
   if (begin == NULL) {
      snprintf (errMessage, maxLen, "[ _id: not found\n");
      free (buffer);
      return false;
   }
   ptr = strstr (line, "\"tws\":");
   if (ptr == NULL) {
      snprintf (errMessage, maxLen, "\"tws\": not found\n");
      free (buffer);
      return false;
   }
   strlcpy (start, begin, (ptr-begin));
   const int len = strlen (start);
   if (start [len - 1] == ',') start [len - 1 ] = '\0';     // last comma elimination if exist 
   snprintf (polMat->jsonHeader, sizeof polMat->jsonHeader, "{%s}", start); // header extracted

   line = findListPol (true, MAX_N_POL_MAT_COLS, line, "\"tws\":[", polMat, sailPolMat, errMessage, maxLen);
   if (line == NULL) return false;
   line = findListPol (false, MAX_N_POL_MAT_LINES, line, "\"twa\":[", polMat, sailPolMat, errMessage, maxLen);
   if (line == NULL) return false;
   line = strstr (line, "\"sail\":");
   if (line == NULL) {
      snprintf (errMessage, maxLen, "\"sail\": not found\n");
      free (buffer);
      return false;
   }
   line += strlen ("sail") + 2;
   while (line) {
      line = findSailPol (line, polMat, sailPolMat, errMessage, maxLen);
   }
   polMat->fromJson = sailPolMat->fromJson = true;
   free (buffer);
   return errMessage [0] == '\0';
}

/*! Launch readPolarCsv or readPolarJson according to type */
bool readPolar (const char *fileName, PolMat *mat, PolMat *sailPolMat, char *errMessage, size_t maxLen) {
   char sailPolFileName [MAX_SIZE_NAME] = "";
   char bidon [MAX_SIZE_LINE];
   bool res = false;
   if (!mat) return false;
   *mat = (PolMat){0}; // set all to zero
   if (sailPolMat) *sailPolMat = (PolMat){0}; // set all to zero if not NULL

   if (g_str_has_suffix (fileName, ".json")) {
      res = readPolarJson (fileName, mat, sailPolMat, errMessage, maxLen);
   }
   else {
      res = readPolarCsv (fileName, mat, errMessage, maxLen);
      if (res && sailPolMat != NULL) {  // read additional polar for sail numbers
         newFileNameSuffix (fileName, "sailpol", sailPolFileName, sizeof (sailPolFileName));
         if (readPolarCsv (sailPolFileName, sailPolMat, bidon, sizeof bidon)) { // sailPolFile not mandatory 
            mat->nSail = sailPolMat->maxAll; // Number of sail max is the number of sails
            for (size_t i = 0; i < mat->nSail && i < sailNameSize; i += 1) 
               strlcpy (mat->tSail [i].name, sailName [i], MAX_SIZE_NAME);

            if (sailNameSize < mat->nSail) {
               fprintf (stderr, "in readPolar, Error: size of sailName: %zu not enough for nSail: %zu\n", sailNameSize, mat->nSail);
            }
         }
         else mat->nSail = 1;
      }
   }
   return res;
}

/*! write polar information in string Json format */
char *polToStrJson (bool report, const char *fileName, const char *objName, char *out, size_t maxLen) {
   char polarName [MAX_SIZE_FILE_NAME];
   char errMessage [MAX_SIZE_LINE];
   char str [MAX_SIZE_TEXT] = "";;
   PolMat mat, sailMat;
   buildRootName (fileName, polarName, sizeof (polarName));
   out [0] = '\0';

   if (!readPolar (polarName, &mat, &sailMat, errMessage, sizeof (errMessage))) {
      fprintf (stderr, "%s", errMessage);
      snprintf (out, maxLen, "{\"_Error\": \"%s\"}\n", errMessage);
      return out;
   }
   if ((mat.nLine < 2) || (mat.nCol < 2)) {
      fprintf (stderr, "In polToStrJson Error: no value in: %s\n", polarName);
      snprintf (out, maxLen, "{\"_Error\": \"No value in polar\"}\n");
      return out;
   }
   
   snprintf (out, maxLen, 
      "{\n  \"header\": %s,\n  \"%s\": \"%s\",\n  \"nLine\": %d,\n  \"nCol\": %d,\n  \"nSail\": %zu,\n  \"max\": %.2lf,\n  \"fromJson\": %s,\n  \"array\": [\n", 
       mat.jsonHeader, objName, polarName, mat.nLine, mat.nCol, mat.nSail, mat.maxAll, mat.fromJson ? "true" : "false");

   // generate two dimensions array with the values 
   for (int i = 0; i < mat.nLine ; i++) {
      g_strlcat (out, "    [", maxLen);
      for (int j = 0; j < mat.nCol - 1; j++) {
         printFloat(str, sizeof str, mat.t[i][j]);
         g_strlcat(out, str, maxLen);
         g_strlcat(out, ", ", maxLen);
      }
      printFloat(str, sizeof str, mat.t[i][mat.nCol - 1]);
      g_strlcat(out, str, maxLen);
      snprintf(str, sizeof str, "]%s\n", (i < mat.nLine - 1) ? "," : "");
      g_strlcat(out, str, maxLen);
   }
   g_strlcat (out, "  ]", maxLen);

   // if several sails, generate two dimensions array with sail number
   if (mat.nSail > 1) {
      g_strlcat (out, ",\n  \"arraySail\": [\n", maxLen);
   
      for (int i = 0; i < mat.nLine ; i++) {
         g_strlcat (out, "    [", maxLen);
         for (int j = 0; j < mat.nCol - 1; j++) {
            snprintf (str, sizeof str, "%.0f, ", sailMat.t [i][j]);
            g_strlcat (out, str, maxLen);
         }
         snprintf (str, sizeof (str), "%.0f]%s\n", sailMat.t [i][mat.nCol -1], (i < mat.nLine - 1) ? "," : "");
         g_strlcat (out, str, maxLen);
      }
      g_strlcat (out, "  ],\n", maxLen);

      // generate one dimension array that list the name of sails, ordered NA = 0, Sail1, sail2 etc
      g_strlcat (out, "  \"legend\": [", maxLen);
      g_strlcat (out, "\"NA\",", maxLen); // first is NA (id 0)
      for (size_t i = 0; i < mat.nSail; i += 1) {
         snprintf (str, sizeof str, "\"%s\"%s", mat.tSail [i].name, (i < mat.nSail -1) ? ", " : "");
         g_strlcat (out, str, maxLen);
      }
      g_strlcat (out, "  ]", maxLen);   
   } // end if mat.nSail > 1
   
   // generate report
   if (report) {
      g_strlcat (out, ",\n  \"report\": \"", maxLen);  
      polarCheck (&mat, str, sizeof str);
      g_strlcat (out, str, maxLen);
      g_strlcat (out, "\"", maxLen);
   }
   g_strlcat (out, "\n}\n", maxLen);
   return out;
}
