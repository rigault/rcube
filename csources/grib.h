extern double  zoneTimeDiff (const Zone *zone1, const Zone *zone0);
extern void    findWindGrib (double lat, double lon, double t, double *u, double *v, double *gust, double *w, double *twd, double *tws );
extern double  findRainGrib (double lat, double lon, double t);
extern double  findPressureGrib (double lat, double lon, double t);
extern void    findCurrentGrib (double lat, double lon, double t, double *uCurr, double *vCurr, double *tcd, double *tcs);
extern char    *gribToStr (const Zone *zone, char *str, size_t maxLen);
extern void    printGrib (const Zone *zone, const FlowP *gribData);
extern bool    checkGribInfoToStr (int type, Zone *zone, char *buffer, size_t maxLen);
extern bool    checkGribToStr (bool hasCurrentGrib, char *buffer, size_t maxLen);
extern char    *gribToStrJson (const char *fileName, char *out, size_t maxLen);
extern float   *buildUVGWarray(const Zone *zone, const char *initialOfNames, const FlowP *gribData, size_t *outNValues);
extern bool    uvPresentGrib (const Zone *zone);
extern bool    isPresentGrib (const Zone *zone, const char *name);












