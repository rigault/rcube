/*! this file contains small inlines functions
 to be included in source files */

#include <math.h>
#include <time.h>

/*! say if point is in sea */
static inline bool isSea (char * isSeaArray, double lat, double lon) {
   if (isSeaArray == NULL) return true;
   int iLon = round (lon * 10  + 1800);
   int iLat = round (-lat * 10  + 900);
   return isSeaArray [(iLat * 3601) + iLon];
}

/*! say if point is in sea */
static inline bool isSeaTolerant (char * isSeaArray, double lat, double lon) {
   if (isSeaArray == NULL) return true;
   int iLonInf = floor (lon * 10  + 1800);
   int iLonSup = ceil (lon * 10  + 1800);
   int iLatInf = floor (-lat * 10  + 900);
   int iLatSup = ceil (-lat * 10  + 900);
   return isSeaArray [(iLatInf * 3601) + iLonInf]
       || isSeaArray [(iLatInf * 3601) + iLonSup]
       || isSeaArray [(iLatSup * 3601) + iLonInf]
       || isSeaArray [(iLatSup * 3601) + iLonSup];
}

/*! return angle on [0, 360 ] interval */
static inline double norm360(double a) {
  a = fmod(a, 360.0);
  if (a < 0) a += 360.0;
  return a;
}

/*! return angle on ]-180, 180 ] interval 
 * Useful for for lon and twa */
static inline double norm180(double a) {
  return remainder(a, 360.0);   // (-180, 180]
}

/*! if antemeridian -180 < lon < 360. Normal case : -180 < lon <= 180 */
static inline double lonNormalize (double lon, bool anteMeridian) {
   norm180 (lon);
   if (anteMeridian && lon < 0) lon += 360;
   return lon;
}

/*! true if P (lat, lon) is within the zone */
static inline bool isInZone (double lat, double lon, Zone *zone) {
   return (lat >= zone->latMin) && (lat <= zone->latMax) && (lon >= zone->lonLeft) && (lon <= zone->lonRight);
}

/*! true wind direction */
static inline double fTwd (double u, double v) {
	double val = 180 + RAD_TO_DEG * atan2 (u, v);
   return (val > 180) ? val - 360 : val;
}

/*! true wind speed. cf Pythagore */
static inline double fTws (double u, double v) {
    return MS_TO_KN * hypot (u, v);
}

/*! return TWA between -180 and 180
   note : tribord amure if twa < 0 */
static inline double fTwa (double heading, double twd) {
   double val =  fmod ((twd - heading), 360.0);
   return (val > 180) ? val - 360 : (val < -180) ? val + 360 : val;
}

/*! return AWA and AWS Apparent Wind Angle and Speed */
static inline void fAwaAws (double twa, double tws, double sog, double *awa, double *aws) {
   double a = tws * sin (DEG_TO_RAD * twa);
   double b = tws * cos (DEG_TO_RAD * twa) + sog;
   *awa = RAD_TO_DEG * atan2 (a, b);
   *aws = hypot (a, b);
}

/*! return fx : linear interpolation */
static inline double interpolate (double x, double x0, double x1, double fx0, double fx1) {
   if (x1 == x0) return fx0;
   else return fx0 + (x-x0) * (fx1-fx0) / (x1-x0);
}

/*! return givry correction to apply to direct or loxodromic cap to get orthodromic cap  */
static inline double givry (double lat1, double lon1, double lat2, double lon2) {
   return (0.5 * (lon1 - lon2)) * sin (0.5 * (lat1 + lat2) * DEG_TO_RAD);
}

/*! return loxodromic cap from origin to destination */
static inline double directCap (double lat1, double lon1, double lat2, double lon2) {
   return RAD_TO_DEG * atan2 ((lon2 - lon1) * cos (DEG_TO_RAD * 0.5 * (lat1 + lat2)), lat2 - lat1);
}

/*! return initial orthodromic cap from origin to destination
   equivalent to : return directCap (lat1, lon1, lat2, lon2) + givry (lat1, lon1, lat2, lon2);
*/
static inline double orthoCap (double lat1, double lon1, double lat2, double lon2) {
    const double avgLat = 0.5 * (lat1 + lat2);
    const double deltaLat = lat2 - lat1;
    const double deltaLon = lon2 - lon1;
    const double avgLatRad = avgLat * DEG_TO_RAD;
    const double cap = RAD_TO_DEG * atan2 (deltaLon * cos (avgLatRad), deltaLat);
    const double givryCorrection = -0.5 * deltaLon * sin  (avgLatRad);  // ou fast_sin(avgLatRad)

    return cap + givryCorrection;
}

/*! return initial orthodromic cap from origin to destination, no givry correction */
static inline double orthoCap2 (double lat1, double lon1, double lat2, double lon2) {
    lat1 *= DEG_TO_RAD;
    lat2 *= DEG_TO_RAD;
    double delta_lon = (lon2 - lon1) * DEG_TO_RAD;
    double y = sin(delta_lon) * cos(lat2);
    double x = cos(lat1) * sin(lat2) - sin(lat1) * cos(lat2) * cos(delta_lon);
    
    return fmod(RAD_TO_DEG * atan2(y, x) + 360.0, 360.0);
}

/*! return loxodromic distance in nautical miles from origin to destination */
static inline double loxoDist (double lat1, double lon1, double lat2, double lon2) {
   // Convert degrees to radians
   double lat1_rad = DEG_TO_RAD * lat1;
   double lon1_rad = DEG_TO_RAD * lon1;
   double lat2_rad = DEG_TO_RAD * lat2;
   double lon2_rad = DEG_TO_RAD * lon2;

   // Difference in longitude
   double delta_lon = lon2_rad - lon1_rad;

   // Calculate the change in latitude
   double delta_lat = lat2_rad - lat1_rad;

   // Calculate the mean latitude
   double mean_lat = (lat1_rad + lat2_rad) / 2.0;

   // Calculate the rhumb line distance
   double q = delta_lat / log(tan(G_PI / 4 + lat2_rad / 2) / tan (G_PI / 4 + lat1_rad / 2));
    
   // Correct for delta_lat being very small
   if (isnan (q)) q = cos (mean_lat);

   // Distance formula
   return hypot (delta_lat, q * delta_lon) * EARTH_RADIUS;
   // return sqrt (delta_lat * delta_lat + (q * delta_lon) * (q * delta_lon)) * EARTH_RADIUS;
}

/*! return orthodromic distance in nautical miles from origin to destination */
static inline double orthoDist(double lat1, double lon1, double lat2, double lon2) {
    lat1 *= DEG_TO_RAD;
    lat2 *= DEG_TO_RAD;
    double theta = (lon1 - lon2) * DEG_TO_RAD;

    double cosDist = sin(lat1) * sin(lat2) + cos(lat1) * cos(lat2) * cos(theta);
    cosDist = CLAMP (cosDist, -1.0, 1.0);  // to avoid NaN on acos()

    double distRad = acos(cosDist);
    return 60.0 * RAD_TO_DEG * distRad;
}

/*! return orthodomic distance in nautical miles from origin to destinationi, Haversine formula
   time consuming */
static inline double orthoDist2 (double lat1, double lon1, double lat2, double lon2) {
    lat1 *= DEG_TO_RAD;
    lat2 *= DEG_TO_RAD;
    double dLat = lat2 - lat1;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;

    double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1) * cos(lat2) * sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));

    return 60.0 * RAD_TO_DEG * c;
}

/*! find in polar boat speed or wave coeff */
static inline double findPolar (double twa, double w, const PolMat *mat, const PolMat *sailMat, int *sail) {
   const int nLine = mat->nLine;   // local copy for perf
   const int nCol = mat->nCol; 
   int l, c, lInf, cInf, lSup, cSup;
   int bestL, bestC; // for sail

   if (twa > 180.0) twa = 360.0 - twa;
   else if (twa < 0.0) twa = -twa;

   for (l = 1; l < nLine; l++) {
      if (mat->t [l][0] > twa) break;
   }
   lSup = (l < nLine) ? l : nLine - 1;
   lInf = (l == 1) ? 1 : l - 1;

   for (c = 1; c < nCol; c++) {
      if (mat->t [0][c] > w) break;
   }
   cSup = (c < nCol - 1) ? c : nCol - 1;
   cInf = (c == 1) ? 1 : c - 1;

   // sail
   if (sailMat != NULL && sailMat->nLine == nLine && sailMat->nCol == nCol) {       // sail requested
      bestL = ((twa - mat->t [lInf][0]) < (mat->t [lSup][0] - twa)) ? lInf : lSup;  // for sail line
      bestC = ((w - mat->t [0][cInf]) < (mat->t [0][cSup] - w)) ? cInf : cSup;      // for sail col
      *sail = sailMat->t [bestL][bestC];                                            // sail found
   }
   else *sail = 0;

   const double lInf0 = mat->t [lInf][0];
   const double lSup0 = mat->t [lSup][0];
   double s0 = interpolate (twa, lInf0, lSup0, mat->t [lInf][cInf], mat->t [lSup][cInf]);
   double s1 = interpolate (twa, lInf0, lSup0, mat->t [lInf][cSup], mat->t [lSup][cSup]);
   return interpolate (w, mat->t [0][cInf], mat->t [0][cSup], s0, s1);
}

/*! dichotomic search on column 0 (TWA), using rows [1 .. nLine-1] */
static inline int binarySearchTwa (const PolMat *mat, double val) {
    const int nLine = mat->nLine;
    int low  = 1;
    int high = nLine;  // sentinel: just after last line

    while (low < high) {
        int mid = low + (high - low) / 2;
        double midVal = mat->t[mid][0];   // colonne 0
        if (midVal > val) high = mid;
        else low = mid + 1;
    }
    return low;
}

/*! dichotomic search on row 0 (wind speed), using columns [1 .. nCol-1] */
static inline int binarySearchW (const double *row0, int nCol, double val) {
    int low  = 1;
    int high = nCol; // sentinel: just after last col

    while (low < high) {
        int mid = low + (high - low) / 2;
        double midVal = row0[mid];
        if (midVal > val) high = mid;
        else low = mid + 1;
    }
    return low;   // in [1..nCol]
}

/*! find in polar boat speed or wave coeff and sail number if sailMat != NULL */
/*  Version = findPolar, with W dichotomic search */
static inline double findPolar1 (double twa, double w, const PolMat *mat, const PolMat *sailMat, int *sail) {
   const int nLine = mat->nLine;   // local copy for perf
   const int nCol = mat->nCol; 
   int l, c, lInf, cInf, lSup, cSup;
   int bestL, bestC; // for sail

   if (twa > 180.0) twa = 360.0 - twa;
   else if (twa < 0.0) twa = -twa;

   for (l = 1; l < nLine; l++) {
      if (mat->t [l][0] > twa) break;
   }
   lSup = (l < nLine - 1) ? l : nLine - 1;
   lInf = (l == 1) ? 1 : l - 1;
   
   c = binarySearchW (&mat->t [0][0], nCol, w);
   cSup = (c < nCol - 1) ? c : nCol - 1;
   cInf = (c == 1) ? 1 : c - 1;

   // sail
   if (sailMat != NULL && sailMat->nLine == nLine && sailMat->nCol == nCol) {       // sail requested
      bestL = ((twa - mat->t [lInf][0]) < (mat->t [lSup][0] - twa)) ? lInf : lSup;  // for sail line
      bestC = ((w - mat->t [0][cInf]) < (mat->t [0][cSup] - w)) ? cInf : cSup;      // for sail col
      *sail = sailMat->t [bestL][bestC];                                            // sail found
   }
   else *sail = 0;
   
   const double lInf0 = mat->t [lInf][0];
   const double lSup0 = mat->t [lSup][0];
   double s0 = interpolate (twa, lInf0, lSup0, mat->t [lInf][cInf], mat->t [lSup][cInf]);
   double s1 = interpolate (twa, lInf0, lSup0, mat->t [lInf][cSup], mat->t [lSup][cSup]);
   return interpolate (w, mat->t [0][cInf], mat->t [0][cSup], s0, s1);
}

/*! find in polar boat speed or wave coeff and sail number if sailMat != NULL */
/*  Version = findPolar, with both W and TWA dichotomic search */
static inline double findPolar2 (double twa, double w, const PolMat *mat, const PolMat *sailMat, int *sail) {
   const int nLine = mat->nLine;   // local copy for perf
   const int nCol  = mat->nCol; 
   int l, c, lInf, cInf, lSup, cSup;
   int bestL, bestC; // for sail

   if (twa > 180.0)      twa = 360.0 - twa;
   else if (twa < 0.0)   twa = -twa;

   l = binarySearchTwa(mat, twa);        // retourne [1 .. nLine]
   lSup = (l < nLine - 1) ? l : (nLine - 1);
   lInf = (l == 1) ? 1 : (l - 1);

   c = binarySearchW(&mat->t[0][0], nCol, w);
   cSup = (c < nCol - 1) ? c : nCol - 1;
   cInf = (c == 1) ? 1 : c - 1;

   if (sailMat != NULL && sailMat->nLine == nLine && sailMat->nCol == nCol) {       // sail requested
      bestL = ((twa - mat->t[lInf][0]) < (mat->t[lSup][0] - twa)) ? lInf : lSup;   // for sail line
      bestC = ((w   - mat->t[0][cInf]) < (mat->t[0][cSup] - w))   ? cInf : cSup;   // for sail col
      *sail = sailMat->t[bestL][bestC];                                            // sail found
   } else *sail = 0;

   double lInf0 = mat->t[lInf][0];
   double lSup0 = mat->t[lSup][0];
   double s0 = interpolate (twa, lInf0, lSup0, mat->t [lInf][cInf], mat->t [lSup][cInf]);
   double s1 = interpolate (twa, lInf0, lSup0, mat->t [lInf][cSup], mat->t [lSup][cSup]);
   return interpolate (w, mat->t [0][cInf], mat->t [0][cSup], s0, s1);
}

/*! return max speed of boat at tws for all twa */
static inline double maxSpeedInPolarAt (double tws, const PolMat *mat) {
   double max = 0.0;
   double s0, s1, speed;
   const int nCol = mat->nCol; 
   const int nLine = mat->nLine; // local storage
   const int c = binarySearchW (&mat->t [0][0], nCol, tws);
   const int cSup = (c < nCol - 1) ? c : nCol - 1;
   const int cInf = (c == 1) ? 1 : c - 1;

   for (int l = 1; l < nLine; l++) { // for every lines
      s0 = mat->t[l][cInf];   
      s1 = mat->t[l][cSup];
      speed = s0 + (tws - mat->t [0][cInf]) * (s1 - s0) / (mat->t [0][cSup] - mat->t [0][cInf]);
      if (speed > max) max = speed;
   }
   return max;
}

/*!
 * @brief Compute the intermediate point on the great circle from P1 to P2.
 *
 * This function returns the point Pr (latR, lonR) lying on the orthodromic
 * (great-circle) route from P1 (lat1, lon1) to P2 (lat2, lon2), at a distance
 * d_nm (nautical miles) from P1.
 *
 * Latitudes and longitudes are in degrees.
 * Distance d_nm is in nautical miles.
 *
 * The result (latR, lonR) is given in degrees.
 *
 * Edge cases:
 * - If P1 and P2 are (almost) identical, Pr = P1.
 * - If d_nm <= 0, Pr = P1.
 * - If d_nm >= orthodromic distance P1->P2, Pr = P2.
 */
static inline void orthoFindInterPoint(double lat1, double lon1,
                         double lat2, double lon2,
                         double d_nm,
                         double *latR, double *lonR) {
   if (!latR || !lonR) return;

   /* Total orthodromic distance P1 -> P2 (nautical miles) */
   double total_nm = orthoDist(lat1, lon1, lat2, lon2);

   /* Degenerate or almost zero distance: return origin */
   if (total_nm <= 1e-9) {
      *latR = lat1;
      *lonR = lon1;
      return;
   }

   /* Clamp requested distance into [0, total_nm] */
   if (d_nm <= 0.0) {
      *latR = lat1;
      *lonR = lon1;
      return;
   }
   if (d_nm >= total_nm) {
      *latR = lat2;
      *lonR = lon2;
      return;
   }

   /* Fraction of the great-circle path */
   double f = d_nm / total_nm;

   /* Convert endpoints to radians */
   double phi1 = lat1 * DEG_TO_RAD;
   double lambda1 = lon1 * DEG_TO_RAD;
   double phi2 = lat2 * DEG_TO_RAD;
   double lambda2 = lon2 * DEG_TO_RAD;

   /* Central angle delta between P1 and P2 (radians)
    * orthoDist uses: dist_nm = delta * (180 / pi) * 60
    * => delta = dist_nm / (60 * (180 / pi)) = dist_nm / (60 * RAD_TO_DEG)
    */
   double delta = total_nm / (60.0 * RAD_TO_DEG);
   double sin_delta = sin(delta);

   if (fabs(sin_delta) < 1e-15) {
      /* Numerically unstable (almost no separation) */
      *latR = lat1;
      *lonR = lon1;
      return;
   }

   /* Spherical linear interpolation on great-circle */
   double A = sin((1.0 - f) * delta) / sin_delta;
   double B = sin(f * delta) / sin_delta;

   double cos_phi1 = cos(phi1);
   double cos_phi2 = cos(phi2);

   double x = A * cos_phi1 * cos(lambda1) + B * cos_phi2 * cos(lambda2);
   double y = A * cos_phi1 * sin(lambda1) + B * cos_phi2 * sin(lambda2);
   double z = A * sin(phi1)               + B * sin(phi2);

   double phiR = atan2(z, sqrt(x * x + y * y));
   double lambdaR = atan2(y, x);

   *latR = phiR * RAD_TO_DEG;
   *lonR = norm180(lambdaR * RAD_TO_DEG);
}

/*! true if day light, false if night
 *  - t : hours since beginning of GRIB (UTC)
 *  - dataDate : YYYYMMDD (UTC)
 *  - dataTime : HHMM (UTC)
 *  Approximation:
 *    - local solar hour = GRIB_UTC_time + t + lon/15
 *    - day if localHour in [6, 18]
 *    - polar caps: rough month-based rule
 */
static inline bool isDay(double t, long dataDate, long dataTime, double lat, double lon) {
   lon = norm180(lon);

   // Polar case: only beginning og grib considered GRIB
   int month = (int)((dataDate % 10000) / 100);   // 1..12

   if (lat > 75.0) return (month > 4 && month < 10);          // 5..9 May to December: day 
   if (lat < -75.0) return !(month > 4 && month < 10);  

   // Start GRIB (UTC) – minutes allways 00
   int hour0 = (int)(dataTime / 100);    // ex: 1500 -> 15

   // Theorical local geographic hour: UTC + t + lon/15
   double localHours = hour0 + t + lon / 15.0;

   // convert to [0, 24)
   localHours = fmod(localHours, 24.0);
   if (localHours < 0.0) localHours += 24.0;

   // Say if local hour between 6:00 an 18:00
   return (localHours >= 6.0) && (localHours <= 18.0);
}
